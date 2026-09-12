#include "trace_io.hpp"
#include <algorithm>
#include <cmath>
#include <charconv>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace pt {
namespace {
template<class S,class F> void fields(S& s,F f) {
    f(s.sequence); f(s.device); f(s.qpc); f(s.pointer); f(s.frame); f(s.message);
    f(s.flags); f(s.buttonChange); f(s.milliseconds); f(s.historyCount); f(s.advertisedHistoryCount); f(s.batchIndex);
    f(s.mask); f(s.toolFlags); f(s.pressure); f(s.tiltX); f(s.tiltY); f(s.rotation);
    f(s.orientation); f(s.kind); f(s.clock); f(s.time); f(s.receipt); f(s.p.x); f(s.p.y);
    for(auto& v:s.coordinates) f(v);
    for(auto& v:s.rectangles) f(v);
    f(s.originX); f(s.originY); f(s.dpi); f(s.mapped); f(s.contact); f(s.down);
    f(s.up); f(s.canceled); f(s.eligible); f(s.boundary);
}
std::string oneLine(std::string s) {
    for(auto& c:s) if(c=='\r' || c=='\n') c=' ';
    return s;
}
void endLine(std::istringstream& line) {
    line>>std::ws;
    if(!line.eof()) throw std::runtime_error("Unexpected trailing data in recording.");
}
bool getLine(std::istream& in,std::string& line) {
    line.clear(); char c;
    while(in.get(c)) {
        if(c=='\n') return true;
        if(line.size()>=32768) throw std::runtime_error("Recording line exceeds safety limit.");
        if(c!='\r') line+=c;
    }
    if(in.bad()) throw std::runtime_error("Could not read recording.");
    return !line.empty();
}
}
void writeTrace(std::ostream& out,const Session& session) {
    out.imbue(std::locale::classic()); out<<std::setprecision(17);
    out<<"PENTRACE 1\nM "<<std::quoted(oneLine(session.metadata))<<'\n';
    for(const auto& sample:session.samples) {
        out<<'S';
        fields(sample,[&](const auto& v){
            using T=std::remove_cvref_t<decltype(v)>;
            if constexpr(std::is_enum_v<T>) out<<' '<<static_cast<unsigned>(v);
            else out<<' '<<v;
        });
        out<<'\n';
    }
    for(const auto& event:session.events)
        out<<"E "<<event.time<<' '<<event.code<<' '<<std::quoted(oneLine(event.text))<<'\n';
    out<<"END "<<session.samples.size()<<' '<<session.events.size()<<'\n';
    if(!out) throw std::runtime_error("Failed writing recording.");
}
Session readTrace(std::istream& in) {
    Session session; std::string text;
    if(!getLine(in,text) || text!="PENTRACE 1") throw std::runtime_error("Not a supported Pen Trace Lab recording (version 1).");
    if(!getLine(in,text)) throw std::runtime_error("Missing metadata.");
    std::istringstream meta(text); char tag{};
    if(!(meta>>tag) || tag!='M' || !(meta>>std::quoted(session.metadata))) throw std::runtime_error("Invalid metadata.");
    endLine(meta);
    while(getLine(in,text)) {
        std::istringstream line(text); line.imbue(std::locale::classic()); std::string type; line>>type;
        if(type=="END") {
            std::size_t samples{},events{};
            if(!(line>>samples>>events) || samples!=session.samples.size() || events!=session.events.size())
                throw std::runtime_error("Recording count mismatch.");
            endLine(line);
            if(getLine(in,text)) throw std::runtime_error("Data after recording end.");
            return session;
        }
        if(type=="S") {
            if(session.samples.size()>=maxSamples) throw std::runtime_error("Recording sample limit exceeded.");
            Sample s;
            fields(s,[&](auto& v){
                using T=std::remove_cvref_t<decltype(v)>;
                if constexpr(std::is_enum_v<T>) {
                    unsigned n{}; if(!(line>>n)) throw std::runtime_error("Invalid enum field."); v=static_cast<T>(n);
                } else if constexpr(std::is_same_v<T,bool>) {
                    int n{}; if(!(line>>n) || (n!=0 && n!=1)) throw std::runtime_error("Invalid boolean field."); v=n!=0;
                } else if constexpr(std::is_integral_v<T> && std::is_unsigned_v<T>) {
                    line>>std::ws;
                    if(line.peek()=='-' || !(line>>v)) throw std::runtime_error("Invalid unsigned field.");
                } else if(!(line>>v)) throw std::runtime_error("Invalid sample field.");
            });
            endLine(line);
            const auto kind=static_cast<unsigned>(s.kind),clock=static_cast<unsigned>(s.clock);
            if(kind<1 || kind>3 || clock<1 || clock>3 || !finite(s.p) ||
                !std::isfinite(s.time) || !std::isfinite(s.receipt) ||
                std::abs(s.time)>1e12 || std::abs(s.receipt)>1e12 ||
                std::abs(s.p.x)>1e8 || std::abs(s.p.y)>1e8 || s.dpi<48 || s.dpi>960 ||
                s.historyCount>65536 || s.batchIndex>=s.historyCount)
                throw std::runtime_error("Sample fields outside recording limits.");
            session.samples.push_back(s);
        } else if(type=="E") {
            if(session.events.size()>=maxEvents) throw std::runtime_error("Recording event limit exceeded.");
            Event e;
            if(!(line>>e.time>>e.code>>std::quoted(e.text)) || !std::isfinite(e.time)) throw std::runtime_error("Invalid event.");
            endLine(line); session.events.push_back(std::move(e));
        } else throw std::runtime_error("Unknown recording record.");
    }
    throw std::runtime_error("Incomplete recording: END marker missing.");
}
ClockCalibration clockCalibration(const Session& session) {
    ClockCalibration result{};
    for(const auto& event:session.events) {
        const std::string prefix="Clock mapping: QPC origin=",separator="; QPC frequency=";
        if(!event.text.starts_with(prefix)) continue;
        const auto middle=event.text.find(separator,prefix.size());
        if(middle==std::string::npos) return {};
        const auto end=event.text.find(" Hz;",middle+separator.size());
        if(end==std::string::npos) return {};
        ClockCalibration candidate;
        const auto* start=event.text.data();
        const auto a=std::from_chars(start+prefix.size(),start+middle,candidate.origin);
        const auto b=std::from_chars(start+middle+separator.size(),start+end,candidate.frequency);
        if(a.ec!=std::errc{} || b.ec!=std::errc{} || a.ptr!=start+middle || b.ptr!=start+end || !candidate.frequency) return {};
        if(result.frequency && (result.origin!=candidate.origin || result.frequency!=candidate.frequency)) return {};
        result=candidate;
    }
    return result;
}
void writeSamplesCsv(std::ostream& out,const Session& session) {
    out.imbue(std::locale::classic()); out<<std::setprecision(17);
    out<<"sequence,device_session_id,pointer,kind,time_seconds,receipt_seconds,clock_source,x_dip,y_dip,contact,down,up,canceled,boundary,pressure,validity_mask,tilt_x,tilt_y,rotation,frame,history_count,batch_index,qpc,dwTime,dpi,mapped,flags\n";
    for(const auto& s:session.samples)
        out<<s.sequence<<','<<s.device<<','<<s.pointer<<','<<kindName(s.kind)<<','<<s.time<<','<<s.receipt<<','<<static_cast<unsigned>(s.clock)<<','
        <<s.p.x<<','<<s.p.y<<','<<s.contact<<','<<s.down<<','<<s.up<<','<<s.canceled<<','<<s.boundary<<','<<s.pressure<<','<<s.mask<<','
        <<s.tiltX<<','<<s.tiltY<<','<<s.rotation<<','<<s.frame<<','<<s.historyCount<<','<<s.batchIndex<<','<<s.qpc<<','<<s.milliseconds<<','
        <<s.dpi<<','<<s.mapped<<','<<s.flags<<'\n';
    if(!out) throw std::runtime_error("Failed writing sample CSV.");
}
void writeMetricsCsv(std::ostream& out,const Processor& processor,Mode mode) {
    out.imbue(std::locale::classic()); out<<std::setprecision(17);
    out<<"stroke,kind,mode,points,ended,canceled,recovered_start,line_defined,straightness_rms_dip,straightness_p95_dip,straightness_max_dip,path_length_dip,duration_seconds,mean_interval_ms,p95_interval_ms,nonincreasing_times,gaps_over_50ms,displacement_rms_dip,displacement_max_dip,endpoint_displacement_dip,sampled_curve_deviation_dip,speed_valid_intervals,speed_invalid_intervals,speed_covered_seconds,mean_speed_dip_per_s,p95_speed_dip_per_s,max_speed_dip_per_s,local_variation_rms_dip,local_variation_samples,analysis_recovered_points,filter_version\n";
    std::size_t index=0;
    for(const auto& s:processor.strokes()) {
        const auto path=filter(s,mode); const auto m=measure(s,path);
        out<<++index<<','<<kindName(s.kind)<<','<<modeName(mode)<<','<<m.n<<','<<s.ended<<','<<s.canceled<<','<<s.recoveredStart<<','<<m.lineDefined<<','
        <<m.rms<<','<<m.p95<<','<<m.maximum<<','<<m.pathLength<<','<<m.duration<<','<<m.meanIntervalMs<<','<<m.p95IntervalMs<<','<<m.nonIncreasing<<','<<m.gaps<<','
        <<m.displacementRms<<','<<m.displacementMax<<','<<m.endpointDisplacement<<','<<curveDeviation(path,curve(path))<<','
        <<m.speedIntervals<<','<<m.invalidSpeedIntervals<<','<<m.speedDuration<<',';
        if(m.speedIntervals) out<<m.meanSpeed<<','<<m.p95Speed<<','<<m.maxSpeed;
        else out<<",,"; // unavailable is not zero speed
        out<<',';
        if(m.localVariationSamples) out<<m.localVariationRms;
        out<<','<<m.localVariationSamples<<','
            <<std::count_if(s.points.begin(),s.points.end(),[](const auto& p){return p.timingRecovered;})<<",0.2.0\n";
    }
    if(!out) throw std::runtime_error("Failed writing metrics CSV.");
}
void writeMotionCsv(std::ostream& out,const Processor& processor,Mode mode) {
    out.imbue(std::locale::classic()); out<<std::setprecision(17);
    out<<"stroke,sequence,device_session_id,pointer,kind,mode,time_seconds,clock_source,raw_x_dip,raw_y_dip,filtered_x_dip,filtered_y_dip,dt_seconds,speed_status,raw_vx_dip_per_s,raw_vy_dip_per_s,raw_speed_dip_per_s,filtered_vx_dip_per_s,filtered_vy_dip_per_s,filtered_speed_dip_per_s,analysis_clock_recovered,filter_version\n";
    std::size_t index=0;
    for(const auto& stroke:processor.strokes()) {
        ++index;
        const auto raw=filter(stroke,Mode::Off),fitted=filter(stroke,mode);
        const auto rawMotion=motion(stroke,raw),filteredMotion=motion(stroke,fitted);
        for(std::size_t i=0;i<stroke.points.size();++i) {
            const auto& s=stroke.points[i]; const auto& r=rawMotion[i]; const auto& f=filteredMotion[i];
            out<<index<<','<<s.sequence<<','<<s.device<<','<<s.pointer<<','<<kindName(s.kind)<<','<<modeName(mode)<<','<<s.time<<','
                <<static_cast<unsigned>(s.clock)<<','<<raw[i].x<<','<<raw[i].y<<','<<fitted[i].x<<','<<fitted[i].y<<',';
            if(i) out<<r.dt;
            out<<','<<motionStatusName(r.status)<<',';
            if(r.valid()) out<<r.velocity.x<<','<<r.velocity.y<<','<<r.speed;
            else out<<",,";
            out<<',';
            if(f.valid()) out<<f.velocity.x<<','<<f.velocity.y<<','<<f.speed;
            else out<<",,";
            out<<','<<s.timingRecovered<<",0.2.0\n";
        }
    }
    if(!out) throw std::runtime_error("Failed writing motion CSV.");
}
}
