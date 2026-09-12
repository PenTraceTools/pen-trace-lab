#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace pt {
double length(Vec p) { return std::hypot(p.x,p.y); }
bool finite(Vec p) { return std::isfinite(p.x) && std::isfinite(p.y); }
Vec mapHimetric(Vec p,const std::array<std::int32_t,4>& device,const std::array<std::int32_t,4>& display) {
    const double dw=static_cast<double>(device[2])-device[0],dh=static_cast<double>(device[3])-device[1];
    if(dw<=0 || dh<=0) throw std::invalid_argument("Invalid device mapping rectangle.");
    return {display[0]+(p.x-device[0])*(static_cast<double>(display[2])-display[0])/dw,
        display[1]+(p.y-device[1])*(static_cast<double>(display[3])-display[1])/dh};
}
Vec toCanvas(Vec screen,Vec origin,double dpi,Vec offset) {
    if(!std::isfinite(dpi) || dpi<=0) throw std::invalid_argument("Invalid window DPI.");
    return (screen-origin)*(96/dpi)-offset;
}
const char* modeName(Mode m) {
    switch(m) { case Mode::Gentle:return "Gentle"; case Mode::Steady:return "Steady";
    case Mode::Strong:return "Strong"; default:return "Off"; }
}
const char* kindName(Kind k) {
    return k==Kind::Pen ? "Pen" : k==Kind::Touch ? "Touch" : "Mouse";
}
static bool sameReport(const Sample& a, const Sample& b) {
    // Do not compare arrival/batch/message envelopes: histories can overlap.
    // If neither clock exists, identical positions may be new reports; retain them.
    if (!(a.qpc || a.milliseconds) || !(b.qpc || b.milliseconds)) return false;
    return a.qpc==b.qpc && a.milliseconds==b.milliseconds && a.frame==b.frame &&
        a.device==b.device && a.pointer==b.pointer && a.kind==b.kind &&
        a.flags==b.flags && a.buttonChange==b.buttonChange && a.mask==b.mask &&
        a.toolFlags==b.toolFlags && a.pressure==b.pressure && a.tiltX==b.tiltX &&
        a.tiltY==b.tiltY && a.rotation==b.rotation && a.orientation==b.orientation &&
        a.coordinates==b.coordinates && a.rectangles==b.rectangles &&
        a.contact==b.contact && a.down==b.down && a.up==b.up && a.canceled==b.canceled;
}
void Processor::clear() { states_.clear(); strokes_.clear(); diagnostics_={}; }
void Processor::consume(const Sample& s) {
    ++diagnostics_.reports;
    if (!finite(s.p) || !std::isfinite(s.time)) { ++diagnostics_.invalid; return; }
    const Key key{s.device,s.pointer};
    if(states_.find(key)==states_.end() && states_.size()>=1024) { ++diagnostics_.limits; return; }
    auto& state=states_[key];
    constexpr auto none=static_cast<std::size_t>(-1);
    if (s.boundary) {
        ++diagnostics_.boundaries;
        if(state.active!=none) { strokes_[state.active].ended=true; strokes_[state.active].canceled=true; }
        state.active=none;
        state.recent.clear();
        return;
    }
    if(std::any_of(state.recent.begin(),state.recent.end(),[&](const Sample& p){return sameReport(p,s);})) {
        ++diagnostics_.duplicates; return;
    }
    state.recent.push_back(s);
    if(state.recent.size()>256) state.recent.pop_front();
    if(s.canceled) {
        if(state.active!=none) { strokes_[state.active].ended=true; strokes_[state.active].canceled=true; }
        state.active=none; return;
    }
    if(s.down && state.active!=none) {
        // Unexpected new contact: split instead of drawing a connecting segment.
        strokes_[state.active].ended=true;
        strokes_[state.active].canceled=true;
        state.active=none;
    }
    if(s.contact && state.active==none && s.eligible) {
        if(strokes_.size()>=20000) { ++diagnostics_.limits; return; }
        Stroke stroke; stroke.kind=s.kind; stroke.pointer=s.pointer; stroke.device=s.device;
        stroke.recoveredStart=!s.down;
        strokes_.push_back(std::move(stroke)); state.active=strokes_.size()-1;
    }
    if(state.active!=none) {
        auto& stroke=strokes_[state.active];
        if(s.contact) {
            if(!stroke.points.empty()) {
                const auto dt=s.time-stroke.points.back().time;
                if(dt<=0) ++diagnostics_.nonIncreasingTimes;
                if(dt>0.05) ++diagnostics_.gaps;
            }
            stroke.points.push_back(s);
        }
        if(s.up || !s.contact) { stroke.ended=true; state.active=none; }
    }
}
static double alpha(double cutoff,double dt) {
    const double r=2*std::numbers::pi*cutoff*dt; return r/(1+r);
}
std::vector<Vec> filter(const Stroke& stroke,Mode mode) {
    std::vector<Vec> out; out.reserve(stroke.points.size());
    if(stroke.points.empty()) return out;
    Vec previous=stroke.points.front().p, result=previous, velocity{};
    double time=stroke.points.front().time;
    for(std::size_t i=0;i<stroke.points.size();++i) {
        const auto& s=stroke.points[i]; const double dt=s.time-time;
        if(mode==Mode::Off || i==0) result=s.p;
        else if(dt<=0 || dt>0.25) { result=s.p; velocity={}; }
        else {
            const auto v=(s.p-previous)*(1/dt);
            velocity=velocity+(v-velocity)*alpha(12,dt);
            const double cutoff=mode==Mode::Gentle ? 6 : mode==Mode::Steady ? 3 : 1.5;
            const double beta=mode==Mode::Gentle ? .035 : mode==Mode::Steady ? .025 : .018;
            result=result+(s.p-result)*alpha(cutoff+beta*length(velocity),dt);
            if(mode==Mode::Gentle) {
                // Explicit displacement budget, not an estimate of true pen error.
                const auto delta=result-s.p; const double d=length(delta);
                if(d>1.5) result=s.p+delta*(1.5/d);
            }
        }
        out.push_back(result); previous=s.p; time=s.time;
    }
    return out;
}
static double percentile(std::vector<double> v,double q) {
    if(v.empty()) return 0;
    std::sort(v.begin(),v.end());
    return v[std::min(v.size()-1,static_cast<std::size_t>(std::ceil(q*v.size())-1))];
}
const char* motionStatusName(MotionStatus status) {
    switch(status) {
    case MotionStatus::First:return "first_sample";
    case MotionStatus::Valid:return "valid";
    case MotionStatus::NonIncreasingTime:return "nonincreasing_time";
    case MotionStatus::Gap:return "gap_over_50ms";
    case MotionStatus::ReceiptClock:return "receipt_clock";
    case MotionStatus::ClockChanged:return "clock_changed";
    case MotionStatus::PointerChanged:return "pointer_changed";
    case MotionStatus::CoordinateSpaceChanged:return "coordinate_space_changed";
    default:return "invalid_data";
    }
}
std::vector<Motion> motion(const Stroke& stroke,const std::vector<Vec>& path) {
    if(path.size()!=stroke.points.size()) throw std::invalid_argument("Motion path/sample count mismatch.");
    std::vector<Motion> out(path.size());
    for(std::size_t i=1;i<path.size();++i) {
        auto& m=out[i]; const auto& a=stroke.points[i-1]; const auto& b=stroke.points[i];
        m.dt=b.time-a.time;
        if(!std::isfinite(m.dt) || !finite(path[i-1]) || !finite(path[i])) m.status=MotionStatus::InvalidData;
        else if(a.device!=b.device || a.pointer!=b.pointer || a.kind!=b.kind) m.status=MotionStatus::PointerChanged;
        else if(a.dpi!=b.dpi || a.originX!=b.originX || a.originY!=b.originY || a.mapped!=b.mapped ||
            !std::equal(a.rectangles.begin(),a.rectangles.begin()+8,b.rectangles.begin())) m.status=MotionStatus::CoordinateSpaceChanged;
        else if(a.clock==Clock::ReceiptFallback || b.clock==Clock::ReceiptFallback) m.status=MotionStatus::ReceiptClock;
        else if(a.clock!=b.clock) m.status=MotionStatus::ClockChanged;
        else if(m.dt<=0) m.status=MotionStatus::NonIncreasingTime;
        else if(m.dt>0.05) m.status=MotionStatus::Gap;
        else {
            m.velocity=(path[i]-path[i-1])*(1/m.dt); m.speed=length(m.velocity);
            if(!finite(m.velocity) || !std::isfinite(m.speed)) { m.velocity={}; m.speed=0; m.status=MotionStatus::InvalidData; }
            else m.status=MotionStatus::Valid;
        }
    }
    return out;
}
Metrics measure(const Stroke& stroke,const std::vector<Vec>& path) {
    Metrics m;
    if(path.size()!=stroke.points.size() || path.empty()) return m;
    m.n=path.size(); Vec mean{};
    for(auto p:path) mean=mean+p*(1.0/path.size());
    double xx=0,xy=0,yy=0;
    for(auto p:path) { p=p-mean; xx+=p.x*p.x; xy+=p.x*p.y; yy+=p.y*p.y; }
    const double theta=.5*std::atan2(2*xy,xx-yy);
    const Vec normal{-std::sin(theta),std::cos(theta)};
    m.lineDefined=xx+yy>1e-12;
    std::vector<double> deviations,intervals;
    double square=0,displacements=0;
    for(std::size_t i=0;i<path.size();++i) {
        const Vec d=path[i]-mean;
        const double e=std::abs(d.x*normal.x+d.y*normal.y);
        deviations.push_back(e); square+=e*e; m.maximum=std::max(m.maximum,e);
        const double movement=length(path[i]-stroke.points[i].p);
        displacements+=movement*movement; m.displacementMax=std::max(m.displacementMax,movement);
        if(i) {
            m.pathLength+=length(path[i]-path[i-1]);
            const double dt=(stroke.points[i].time-stroke.points[i-1].time)*1000;
            if(dt<=0) ++m.nonIncreasing;
            else { intervals.push_back(dt); m.meanIntervalMs+=dt; if(dt>50) ++m.gaps; }
        }
    }
    m.rms=std::sqrt(square/path.size()); m.p95=percentile(deviations,.95);
    m.displacementRms=std::sqrt(displacements/path.size());
    m.endpointDisplacement=length(path.back()-stroke.points.back().p);
    m.duration=std::max(0.0,stroke.points.back().time-stroke.points.front().time);
    m.intervals=intervals.size();
    if(m.intervals) m.meanIntervalMs/=m.intervals;
    m.p95IntervalMs=percentile(intervals,.95);
    std::vector<double> speeds;
    const auto movement=motion(stroke,path);
    for(std::size_t i=1;i<movement.size();++i) {
        const auto& v=movement[i];
        if(!v.valid()) { ++m.invalidSpeedIntervals; continue; }
        ++m.speedIntervals; m.speedDuration+=v.dt;
        m.meanSpeed+=v.speed*v.dt; m.maxSpeed=std::max(m.maxSpeed,v.speed); speeds.push_back(v.speed);
    }
    if(m.speedDuration>0) m.meanSpeed/=m.speedDuration;
    m.p95Speed=percentile(speeds,.95);
    if(!movement.empty()) { m.lastSpeedValid=movement.back().valid(); m.lastSpeed=movement.back().speed; }
    return m;
}
std::vector<Vec> curve(const std::vector<Vec>& p,unsigned subdivisions) {
    if(p.size()<2 || subdivisions==0 || subdivisions>64) return p;
    std::vector<Vec> out; out.reserve((p.size()-1)*subdivisions+1); out.push_back(p.front());
    for(std::size_t i=0;i+1<p.size();++i) {
        Vec a=p[i?i-1:i], b=p[i], c=p[i+1], d=p[std::min(i+2,p.size()-1)];
        for(unsigned j=1;j<=subdivisions;++j) {
            const double t=static_cast<double>(j)/subdivisions;
            out.push_back((b*2+(c-a)*t+(a*2-b*5+c*4-d)*(t*t)+
                (a*(-1)+b*3-c*3+d)*(t*t*t))*.5);
        }
    }
    return out;
}
static double segmentDistance(Vec p,Vec a,Vec b) {
    const Vec d=b-a; const double sq=d.x*d.x+d.y*d.y;
    const Vec v=p-a; const double t=sq>0 ? std::clamp((v.x*d.x+v.y*d.y)/sq,0.0,1.0) : 0;
    return length(p-(a+d*t));
}
double curveDeviation(const std::vector<Vec>& source,const std::vector<Vec>& fitted,unsigned subdivisions) {
    if(source.size()<2 || subdivisions==0 || fitted.size()!=(source.size()-1)*subdivisions+1) return 0;
    double maximum=0;
    for(std::size_t i=1;i<fitted.size();++i) {
        const auto segment=(i-1)/subdivisions;
        maximum=std::max(maximum,segmentDistance(fitted[i],source[segment],source[segment+1]));
    }
    return maximum;
}
}
