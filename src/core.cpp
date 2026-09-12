#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace pt {
double length(Vec p) { return std::hypot(p.x,p.y); }
bool finite(Vec p) { return std::isfinite(p.x) && std::isfinite(p.y); }
std::optional<double> reportTime(std::uint64_t qpc,ClockCalibration c,double receipt) {
    if(!qpc || !c.frequency || !std::isfinite(receipt)) return std::nullopt;
    const double ticks=qpc>=c.origin ? static_cast<double>(qpc-c.origin) : -static_cast<double>(c.origin-qpc);
    const double t=ticks/static_cast<double>(c.frequency);
    if(!std::isfinite(t) || std::abs(t-receipt)>60) return std::nullopt;
    return t;
}
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
void Processor::consume(const Sample& original) {
    auto s=original; // Derived view only; never rewrite the source Session.
    if(!s.boundary && s.clock==Clock::ReceiptFallback) {
        if(const auto t=reportTime(s.qpc,calibration_,s.receipt)) {
            s.time=*t; s.clock=Clock::Qpc; s.timingRecovered=true; ++diagnostics_.recoveredTiming;
        }
    }
    if(s.clock==Clock::Qpc && s.time>s.receipt+.005) ++diagnostics_.clockOffsetReports;
    ++diagnostics_.reports;
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
    if (!finite(s.p) || !std::isfinite(s.time)) {
        ++diagnostics_.invalid;
        if(state.active!=none) { strokes_[state.active].ended=true; strokes_[state.active].canceled=true; }
        state.active=none; return;
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
    if(s.contact && !s.up && state.active==none && s.eligible) {
        if(strokes_.size()>=20000) { ++diagnostics_.limits; return; }
        Stroke stroke; stroke.kind=s.kind; stroke.pointer=s.pointer; stroke.device=s.device;
        stroke.recoveredStart=!s.down;
        strokes_.push_back(std::move(stroke)); state.active=strokes_.size()-1;
    }
    if(state.active!=none) {
        auto& stroke=strokes_[state.active];
        if(s.contact && !s.up) {
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
std::vector<Vec> rawPath(const Stroke& stroke) {
    std::vector<Vec> out; out.reserve(stroke.points.size());
    for(const auto& s:stroke.points) out.push_back(s.p);
    return out;
}
std::vector<Vec> localFilter(const Stroke& stroke,double radius,double cap,double window) {
    if(!std::isfinite(radius) || !std::isfinite(cap) || !std::isfinite(window) ||
        radius<=0 || radius>100 || cap<=0 || cap>50 || window<=0 || window>1)
        throw std::invalid_argument("Invalid local filter settings");
    std::vector<Vec> out; out.reserve(stroke.points.size());
    for(const auto& s:stroke.points) out.push_back(s.p);
    if(out.size()<3) return out;
    const auto raw=out;
    const auto movement=motion(stroke,raw);
    // Symmetric spatial integration avoids causal along-stroke lag and sample-
    // density weighting. Only local-normal displacement is applied. This is an
    // experimental comparison, not the independently measured true trajectory.
    for(std::size_t first=0;first<raw.size();) {
        std::size_t end=first+1;
        while(end<raw.size() && movement[end].valid()) ++end;
        const auto n=end-first;
        std::vector<double> arc(n),times(n);
        std::vector<Vec> area(n);
        for(std::size_t k=0;k<n;++k) {
            times[k]=stroke.points[first+k].time;
            if(k) {
                const double ds=length(raw[first+k]-raw[first+k-1]);
                arc[k]=arc[k-1]+ds;
                area[k]=area[k-1]+(raw[first+k]+raw[first+k-1])*(ds*.5);
            }
        }
        const auto at=[&](double distance) {
            auto hi=static_cast<std::size_t>(std::upper_bound(arc.begin(),arc.end(),distance)-arc.begin());
            hi=std::clamp(hi,std::size_t{1},n-1);
            const double ds=arc[hi]-arc[hi-1];
            const double fraction=ds>0?(distance-arc[hi-1])/ds:0;
            const auto p=raw[first+hi-1]+(raw[first+hi]-raw[first+hi-1])*fraction;
            return std::pair{p,area[hi-1]+(raw[first+hi-1]+p)*((distance-arc[hi-1])*.5)};
        };
        for(std::size_t k=1;k+1<n;++k) {
            const auto left=static_cast<std::size_t>(std::lower_bound(times.begin(),times.end(),times[k]-window)-times.begin());
            const auto right=static_cast<std::size_t>(std::upper_bound(times.begin(),times.end(),times[k]+window)-times.begin()-1);
            const double r=std::min({radius,arc[k]-arc[left],arc[right]-arc[k]});
            if(r<1e-6) continue;
            const auto [a,ia]=at(arc[k]-r); const auto [b,ib]=at(arc[k]+r);
            const auto p=raw[first+k],u=p-a,v=b-p,tangent=b-a;
            const double lu=length(u),lv=length(v),lt=length(tangent);
            if(lu<1e-6 || lv<1e-6 || lt<1e-6) continue;
            // Taper at turns between 30 and 60 degrees; preserve sharper corners.
            const double cosine=std::clamp((u.x*v.x+u.y*v.y)/(lu*lv),-1.0,1.0);
            const double corner=std::clamp((cosine-.5)/(.8660254037844386-.5),0.0,1.0);
            const double edge=std::min({1.0,(times[k]-times.front())/window,(times.back()-times[k])/window});
            const double gain=corner*edge*edge*(3-2*edge);
            const Vec normal{-tangent.y/lt,tangent.x/lt},delta=(ib-ia)*(1/(2*r))-p;
            const double offset=std::clamp(delta.x*normal.x+delta.y*normal.y,-cap,cap)*gain;
            out[first+k]=p+normal*offset;
        }
        first=end;
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
    if(m.nonIncreasing==0 && m.gaps==0) {
        const auto local=localVariation(path,10);
        m.localVariationRms=local.rms; m.localVariationSamples=local.samples;
    }
    return m;
}
Variation localVariation(const std::vector<Vec>& path,double span) {
    Variation result;
    if(path.size()<2 || !std::isfinite(span) || span<=0) return result;
    for(auto p:path) if(!finite(p)) return result;
    std::vector<double> arc(path.size());
    for(std::size_t i=1;i<path.size();++i) arc[i]=arc[i-1]+length(path[i]-path[i-1]);
    if(arc.back()>span && arc.back()<=100000) {
        const auto at=[&](double s) {
            auto hi=static_cast<std::size_t>(std::upper_bound(arc.begin(),arc.end(),s)-arc.begin());
            hi=std::clamp(hi,std::size_t{1},path.size()-1);
            const double ds=arc[hi]-arc[hi-1];
            return path[hi-1]+(path[hi]-path[hi-1])*(ds>0?(s-arc[hi-1])/ds:0);
        };
        double squares=0;
        for(double s=span*.5;s<arc.back()-span*.5;s+=.5) {
            const auto a=at(s-span*.5),b=at(s+span*.5),p=at(s),v=b-a;
            const double size=length(v); if(size<1e-6) continue;
            const double d=(v.x*(p.y-a.y)-v.y*(p.x-a.x))/size;
            squares+=d*d; ++result.samples;
        }
        if(result.samples) result.rms=std::sqrt(squares/result.samples);
    }
    return result;
}
std::vector<std::vector<Vec>> testGuides(unsigned test,double w,double h) {
    std::vector<std::vector<Vec>> guides;
    if(!std::isfinite(w) || !std::isfinite(h) || w<=0 || h<=0) return guides;
    if(test>=1 && test<=4) {
        for(double offset:{-.12,0.0,.12}) {
            Vec a{w*.18,h*(.24+offset)},b{w*.82,h*(.76+offset)};
            if(test==2) { a.y=h*(.76+offset); b.y=h*(.24+offset); }
            if(test==3) { a.x=w*.12; b.x=w*.88; a.y=b.y=h*(.5+offset*2); }
            if(test==4) { a.x=b.x=w*(.5+offset*2); a.y=h*.12; b.y=h*.88; }
            guides.push_back({a,b});
        }
    } else if(test==5 || test==6) {
        for(unsigned j=0;j<3;++j) {
            const double radius=std::min(w,h)*(.06+.04*j);
            const Vec center{w*(.22+.28*j),h*.5};
            std::vector<Vec> shape;
            if(test==5) {
                for(unsigned i=0;i<=96;++i) {
                    const double a=2*std::numbers::pi*i/96;
                    shape.push_back(center+Vec{std::cos(a),std::sin(a)}*radius);
                }
            } else {
                shape={center+Vec{-radius,-radius},center+Vec{-radius*.5,radius},center,
                    center+Vec{radius*.5,radius},center+Vec{radius,-radius}};
            }
            guides.push_back(std::move(shape));
        }
    } else if(test==7 || test==8) {
        for(unsigned i=0;i<5;++i) {
            const Vec center{w*(.15+.175*i),h*.5};
            guides.push_back({center+Vec{-5,0},center+Vec{5,0}});
            guides.push_back({center+Vec{0,-5},center+Vec{0,5}});
        }
    }
    return guides;
}
}
