#include "core.hpp"
#include "history.hpp"
#include "trace_io.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
bool near(double a,double b,double tolerance=1e-8) { return std::abs(a-b)<tolerance; }
pt::Sample point(unsigned i,double x,double y) {
    pt::Sample s; s.sequence=i+1; s.device=10; s.pointer=2; s.qpc=i+1; s.frame=i;
    s.time=s.receipt=i/120.0; s.clock=pt::Clock::Qpc; s.p={x,y}; s.contact=true; s.down=i==0;
    return s;
}
void expectFailure(const std::function<void()>& f) {
    bool failed=false; try { f(); } catch(const std::exception&) {failed=true;}
    require(failed,"Expected a rejected invalid operation");
}
void lifecycle() {
    pt::Processor p;
    auto a=point(0,1,2); p.consume(a); p.consume(a);
    auto b=point(1,2,3); b.qpc=a.qpc; b.time=a.time; p.consume(b);
    auto up=b; up.contact=false; up.up=true; up.p={100,100}; p.consume(up);
    require(p.strokes().size()==1,"One stroke expected");
    require(p.strokes()[0].points.size()==2,"Equal timestamps with different state/data must survive");
    require(p.strokes()[0].ended,"Pen-up must finish stroke");
    require(p.strokes()[0].points.back().p==b.p,"Hover/up coordinate must not make an invented tail");
    require(p.diagnostics().duplicates==1,"Exact duplicate must be counted");
    require(p.diagnostics().nonIncreasingTimes==1,"Equal sample times must be diagnosed");
    auto next=point(3,9,9); next.down=true; p.consume(next);
    auto cancel=next; cancel.boundary=true; cancel.contact=false; p.consume(cancel);
    require(p.strokes().size()==2 && p.strokes()[1].canceled,"Capture-loss boundary must cancel active stroke");
}
void pointers() {
    pt::Processor p; auto pen=point(0,0,0),finger=pen;
    finger.pointer=3; finger.kind=pt::Kind::Touch;
    p.consume(pen); p.consume(finger);
    pen=point(1,4,5); p.consume(pen);
    require(p.strokes().size()==2,"Simultaneous inputs need distinct strokes");
    require(p.strokes()[0].points.size()==2 && p.strokes()[1].points.size()==1,"Pointer streams mixed");
    pt::Processor recovered; auto r=point(7,5,5); recovered.consume(r);
    require(recovered.strokes()[0].recoveredStart,"Mid-contact starts must be labeled");
    pt::Processor ignored; r.eligible=false; ignored.consume(r);
    require(ignored.strokes().empty(),"Off-canvas contact must not start stroke");
}
pt::Stroke diagonal(unsigned hz) {
    pt::Stroke s;
    for(unsigned i=0;i<hz*3;++i) {
        const double t=static_cast<double>(i)/hz,n=std::sin(2*std::numbers::pi*12*t);
        auto p=point(i,30*t-n,30*t+n); p.time=t; s.points.push_back(p);
    }
    return s;
}
void filters() {
    for(unsigned hz:{60u,120u,240u}) {
        auto s=diagonal(hz); const auto raw=pt::filter(s,pt::Mode::Off);
        for(std::size_t i=0;i<raw.size();++i) require(raw[i]==s.points[i].p,"Off must preserve points exactly");
        for(auto mode:{pt::Mode::Gentle,pt::Mode::Steady,pt::Mode::Strong}) {
            const auto f=pt::filter(s,mode); auto m=pt::measure(s,f);
            require(m.rms<pt::measure(s,raw).rms,"Filter should reduce synthetic 12 Hz diagonal noise");
            if(mode==pt::Mode::Gentle) require(m.displacementMax<=1.50000001,"Gentle exceeded displacement budget");
            auto rotated=s;
            for(auto& p:rotated.points) p.p={-p.p.y,p.p.x};
            const auto r=pt::filter(rotated,mode);
            for(std::size_t i=0;i<f.size();++i)
                require(near(r[i].x,-f[i].y) && near(r[i].y,f[i].x),"Filter not rotation symmetric");
        }
    }
    pt::Stroke equal; equal.points={point(0,0,0),point(1,5,5)}; equal.points[1].time=0;
    require(pt::filter(equal,pt::Mode::Strong).back()==equal.points.back().p,"Bad timing fallback loses sample");
    equal.points[1].time=1;
    require(pt::filter(equal,pt::Mode::Strong).back()==equal.points.back().p,"Long gap should reset filter");
}
void metrics() {
    pt::Stroke s;
    for(unsigned i=0;i<100;++i) s.points.push_back(point(i,i,2*i+3));
    const auto path=pt::filter(s,pt::Mode::Off); const auto m=pt::measure(s,path);
    require(m.lineDefined && m.rms<1e-9,"Orthogonal line fit must recognize a straight diagonal");
    require(near(m.meanIntervalMs,1000.0/120),"Interval units incorrect");
    require(m.displacementMax==0,"Off displacement must be zero");
    pt::Stroke dot; dot.points.push_back(point(0,1,1));
    require(!pt::measure(dot,pt::filter(dot,pt::Mode::Off)).lineDefined,"Dot does not define a line");
    const std::vector<pt::Vec> corner={{0,0},{10,0},{10,10}};
    const auto curve=pt::curve(corner);
    require(curve.size()==17 && curve.front()==corner.front() && curve.back()==corner.back(),"Curve endpoints/subdivisions wrong");
    require(pt::curveDeviation(corner,curve)>0,"Corner curve experiment should expose deviation");
}
void recordings() {
    pt::Session session; session.metadata="Pen \"example\"; unicode: \xC3\xA9";
    auto s=point(0,1.123456789012345,2); s.coordinates={1,2,3,4,5,6,7,8};
    for(std::size_t i=0;i<s.rectangles.size();++i) s.rectangles[i]=static_cast<int>(i)-5;
    s.pressure=432; s.mask=7; s.advertisedHistoryCount=11; s.tiltX=-20; s.tiltY=35;
    session.samples.push_back(s); session.events.push_back({.1,123,"History \"fallback\""});
    std::ostringstream out; pt::writeTrace(out,session);
    std::istringstream in(out.str()); const auto copy=pt::readTrace(in);
    std::ostringstream again; pt::writeTrace(again,copy);
    require(out.str()==again.str(),"All serialized fields must round-trip exactly");
    require(copy.samples[0].p==s.p,"Double precision lost on save");
    expectFailure([&]{std::istringstream bad("PENTRACE 2\n");pt::readTrace(bad);});
    expectFailure([&]{std::istringstream bad(out.str().substr(0,out.str().find("END")));pt::readTrace(bad);});
    expectFailure([&]{std::istringstream bad(out.str()+"garbage\n");pt::readTrace(bad);});
    expectFailure([&]{std::istringstream bad("PENTRACE 1\nM \"notes\"\nS -1\n");pt::readTrace(bad);});
    expectFailure([&]{std::istringstream bad("PENTRACE 1\nM \"notes\"\n"+std::string(32769,'x'));pt::readTrace(bad);});
    auto altered=out.str(); altered.replace(altered.find("END 1 1"),7,"END 2 1");
    expectFailure([&]{std::istringstream bad(altered);pt::readTrace(bad);});
    std::ostringstream csv; pt::writeSamplesCsv(csv,session); require(csv.str().find("pressure,validity_mask")!=std::string::npos,"CSV missing validity context");
}
void histories() {
    auto normal=pt::readHistory<int>([](std::uint32_t* count,int* data){
        if(!data) {*count=3;return true;} data[0]=3; data[1]=2; data[2]=1; *count=3; return true;
    },[](int*){return false;});
    require(normal.oldestFirst==std::vector<int>({1,2,3}) && !normal.fallback,"History order incorrect");
    int calls=0;
    auto growing=pt::readHistory<int>([&](std::uint32_t* count,int* data){
        ++calls; if(!data) {*count=1; return true;}
        if(*count<2) {data[0]=9;*count=2;return true;} data[0]=2;data[1]=1;*count=2;return true;
    },[](int*){return false;});
    require(calls==3 && growing.oldestFirst==std::vector<int>({1,2}),"Insufficient successful buffer must be retried");
    auto fallback=pt::readHistory<int>([](std::uint32_t*,int*){return false;},[](int* p){*p=7;return true;});
    require(fallback.fallback && fallback.oldestFirst==std::vector<int>({7}),"Latest fallback missing");
    auto missing=pt::readHistory<int>([](std::uint32_t* n,int*){*n=1000000;return true;},[](int*){return false;});
    require(missing.fallback && missing.retrievalFailed && missing.oldestFirst.empty(),"Oversized history must fail safely");
    auto zero=pt::readHistory<int>([](std::uint32_t* n,int*){*n=0;return true;},[](int* p){*p=4;return true;});
    require(zero.fallback && zero.oldestFirst.size()==1,"Empty history should use latest");
}
void coordinateMapping() {
    const auto screen=pt::mapHimetric({600,1200},{100,200,1100,2200},{-1920,0,0,1080});
    require(near(screen.x,-960) && near(screen.y,540),"Nonzero/negative device/display origins mapped incorrectly");
    const auto dip=pt::toCanvas(screen,{-1200,240},144,{20,90});
    require(near(dip.x,140) && near(dip.y,110),"144 DPI client/canvas conversion incorrect");
    expectFailure([]{pt::mapHimetric({0,0},{0,0,0,1},{0,0,1,1});});
    expectFailure([]{pt::toCanvas({0,0},{0,0},0,{0,0});});
}
void speedMeasurements() {
    pt::Stroke s;
    for(unsigned i=0;i<3;++i) {
        auto p=point(i,3.0*i,4.0*i); p.time=i*.01; s.points.push_back(p);
    }
    const auto raw=pt::filter(s,pt::Mode::Off);
    const auto v=pt::motion(s,raw);
    require(!v[0].valid() && v[1].valid(),"First sample must not invent speed");
    require(near(v[1].velocity.x,300) && near(v[1].velocity.y,400) && near(v[1].speed,500),"Speed/velocity units incorrect");
    const auto m=pt::measure(s,raw);
    require(m.speedIntervals==2 && m.invalidSpeedIntervals==0 && near(m.meanSpeed,500) && near(m.p95Speed,500),"Constant speed statistics incorrect");
    require(m.lastSpeedValid && near(m.lastSpeed,500),"Last-interval speed incorrect");
    auto changed=s;
    changed.points[1].time=0;
    require(pt::motion(changed,raw)[1].status==pt::MotionStatus::NonIncreasingTime,"Equal timestamps need N/A speed");
    changed=s; changed.points[1].time=.1;
    require(pt::motion(changed,raw)[1].status==pt::MotionStatus::Gap,"Large gap must not imply reliable speed");
    changed=s; changed.points[1].clock=pt::Clock::ReceiptFallback;
    require(pt::motion(changed,raw)[1].status==pt::MotionStatus::ReceiptClock,"Receipt-time batching cannot measure pen speed");
    changed=s; changed.points[1].clock=pt::Clock::Milliseconds;
    require(pt::motion(changed,raw)[1].status==pt::MotionStatus::ClockChanged,"Clock-source transition must not create speed");
    changed=s; changed.points[1].pointer=99;
    require(pt::motion(changed,raw)[1].status==pt::MotionStatus::PointerChanged,"Pointer streams must not share speed intervals");
    changed=s; changed.points[1].dpi=144;
    require(pt::motion(changed,raw)[1].status==pt::MotionStatus::CoordinateSpaceChanged,"DPI changes must not create speed spikes");
    changed=s;
    for(auto& p:changed.points) p.p={7,7};
    require(pt::measure(changed,pt::filter(changed,pt::Mode::Off)).maxSpeed==0,"Stationary valid samples should have zero speed");
    changed=s; changed.points[2].time=.03;
    const auto weighted=pt::measure(changed,raw);
    require(near(weighted.meanSpeed,10/.03),"Mean speed must be distance / usable time, not sample-averaged speed");
    pt::Processor p; for(const auto& sample:s.points) p.consume(sample);
    std::ostringstream csv; pt::writeMotionCsv(csv,p,pt::Mode::Off);
    std::istringstream rows(csv.str()); std::string header,row;
    std::getline(rows,header); std::getline(rows,row);
    require(header.find("raw_speed_dip_per_s")!=std::string::npos,"Motion CSV missing speed column");
    require(row.find("first_sample,,,,,,")!=std::string::npos,"Unknown speed must be blank, not zero");
    const auto columns=[](const std::string& text){return std::count(text.begin(),text.end(),',');};
    require(columns(row)==columns(header),"First motion CSV row has incorrect column count");
    while(std::getline(rows,row)) require(columns(row)==columns(header),"Motion CSV column count differs between valid/invalid rows");
    std::ostringstream summary; pt::writeMetricsCsv(summary,p,pt::Mode::Steady);
    std::istringstream summaries(summary.str()); std::getline(summaries,header);
    require(header.find("local_variation_rms_dip")!=std::string::npos && header.find("filter_version")!=std::string::npos,"Metric provenance missing");
    while(std::getline(summaries,row)) require(columns(row)==columns(header),"Metric CSV column mismatch");
}
void clockRecovery() {
    const pt::ClockCalibration c{1000000000,10000000};
    require(near(*pt::reportTime(c.origin+230000,c,0),.023),"23 ms clock offset must retain QPC");
    require(near(*pt::reportTime(c.origin-10000,c,0),-.001),"Signed clock subtraction failed");
    require(!pt::reportTime(0,c,0) && !pt::reportTime(c.origin,{},0),"Missing calibration must not invent timestamps");
    require(!pt::reportTime(c.origin+1000000000,c,0),"Gross clock mismatch should be unavailable");
    pt::Session session;
    session.events.push_back({0,0,"Clock mapping: QPC origin=1000000000; QPC frequency=10000000 Hz; time/receipt units=seconds."});
    const auto calibration=pt::clockCalibration(session);
    require(calibration.origin==c.origin && calibration.frequency==c.frequency,"Saved calibration not parsed");
    pt::Processor p(calibration);
    for(unsigned i=0;i<50;++i) {
        auto s=point(i,i,0); s.qpc=c.origin+230000+i*40000;
        s.clock=pt::Clock::ReceiptFallback; s.receipt=s.time=i*.004;
        session.samples.push_back(s); p.consume(s);
    }
    require(p.diagnostics().recoveredTiming==50 && p.diagnostics().nonIncreasingTimes==0,"Legacy fallback recovery failed");
    require(session.samples[0].clock==pt::Clock::ReceiptFallback && session.samples[0].time==0,"Recovery mutated raw report");
    const auto& stroke=p.strokes()[0];
    require(stroke.points[0].timingRecovered,"Recovery provenance missing");
    require(near(pt::measure(stroke,pt::filter(stroke,pt::Mode::Off)).meanSpeed,250),"Recovered cadence corrupted speed");
    auto marker=session.samples.back(); marker.boundary=true; marker.contact=false; p.consume(marker);
    require(p.diagnostics().recoveredTiming==50 && p.strokes()[0].canceled,"Boundary must not recover a fabricated clock");
    session.events.push_back({0,0,"Clock mapping: QPC origin=2; QPC frequency=3 Hz;"});
    require(!pt::clockCalibration(session).frequency,"Conflicting calibrations must not be guessed");
    session.events={{0,0,"Clock mapping: QPC origin=bad; QPC frequency=0 Hz;"}};
    require(!pt::clockCalibration(session).frequency,"Invalid calibration accepted");
}
void boundedFiltering() {
    auto s=diagonal(240);
    for(auto mode:{pt::Mode::Gentle,pt::Mode::Steady,pt::Mode::Strong}) {
        const auto f=pt::filter(s,mode);
        require(f.front()==s.points.front().p && f.back()==s.points.back().p,"Filter endpoint moved");
        const double cap=mode==pt::Mode::Gentle?1.5:mode==pt::Mode::Steady?2.5:4;
        require(pt::measure(s,f).displacementMax<=cap+1e-8,"Displacement budget exceeded");
        auto prefix=s; prefix.points.resize(400); const auto early=pt::filter(prefix,mode);
        for(std::size_t i=0;i<prefix.points.size();++i)
            if(prefix.points[i].time<prefix.points.back().time-.041)
                require(pt::length(early[i]-f[i])<1e-8,"Filter revised points older than 40 ms");
        auto shifted=s; for(auto& p:shifted.points) p.p=p.p+pt::Vec{1100,-900};
        const auto moved=pt::filter(shifted,mode);
        for(std::size_t i=0;i<f.size();++i)
            require(pt::length(moved[i]-f[i]-pt::Vec{1100,-900})<1e-7,"Translation changed filter shape");
    }
    pt::Stroke line;
    for(unsigned i=0;i<120;++i) line.points.push_back(point(i,i*i*.01,i*i*.02));
    auto straight=pt::filter(line,pt::Mode::Strong);
    for(std::size_t i=0;i<straight.size();++i)
        require(pt::length(straight[i]-line.points[i].p)<1e-8,"Straight-line variable speed acquired lag");
    pt::Stroke corner;
    for(unsigned i=0;i<=40;++i) corner.points.push_back(point(i,i<=20?i:20,i<=20?0:i-20));
    require(pt::filter(corner,pt::Mode::Strong)[20]==corner.points[20].p,"Right-angle vertex must survive");
    auto unknown=s; for(auto& p:unknown.points) p.clock=pt::Clock::ReceiptFallback;
    const auto untouched=pt::filter(unknown,pt::Mode::Strong);
    for(std::size_t i=0;i<untouched.size();++i) require(untouched[i]==unknown.points[i].p,"Receipt batches must not drive smoothing");
    auto gap=s; gap.points[100].dpi=144;
    const auto g=pt::filter(gap,pt::Mode::Strong);
    require(g[99]==gap.points[99].p && g[100]==gap.points[100].p && g[101]==gap.points[101].p,"Filter crossed coordinate-space boundary");
    pt::Stroke stationary; for(unsigned i=0;i<100;++i) stationary.points.push_back(point(i,7,8));
    for(auto p:pt::filter(stationary,pt::Mode::Strong)) require(p==pt::Vec{7,8},"Stationary point changed");
    pt::Stroke loop;
    for(unsigned i=0;i<=240;++i) {
        const double angle=2*std::numbers::pi*i/240;
        auto p=point(i,2*std::cos(angle),2*std::sin(angle)); p.time=i/240.0; loop.points.push_back(p);
    }
    for(auto p:pt::filter(loop,pt::Mode::Strong)) require(pt::length(p)>1.9,"Small deliberate loop collapsed");
    auto slowCurve=s;
    for(auto& p:slowCurve.points) p.p={100*p.time,std::sin(2*std::numbers::pi*p.time)};
    require(pt::measure(slowCurve,pt::filter(slowCurve,pt::Mode::Strong)).displacementMax<.1,"Gentle deliberate curvature overcorrected");
    require(pt::measure(line,straight).localVariationRms<1e-8,"Local variation must be zero for a straight line");
    pt::Processor terminal; terminal.consume(point(0,0,0));
    auto bad=point(1,1,1); bad.up=true; bad.contact=false; bad.p.x=std::numeric_limits<double>::quiet_NaN();
    terminal.consume(bad); terminal.consume(point(2,3,3));
    require(terminal.strokes().size()==2 && terminal.strokes()[0].canceled,"Invalid terminal report joined two strokes");
    pt::Processor conflictingUp; conflictingUp.consume(point(0,0,0));
    auto up=point(1,100,100); up.up=true; conflictingUp.consume(up);
    require(conflictingUp.strokes()[0].points.size()==1 && conflictingUp.strokes()[0].ended,"UP with contact flag created a tail");
}
void realPenGuides() {
    pt::Processor p;
    require(pt::testGuides(0,800,600).empty(),"Free drawing should not contain targets");
    for(unsigned test=1;test<pt::testCount;++test) {
        const auto guides=pt::testGuides(test,800,600);
        require(!guides.empty(),"Real-pen test missing its visual target");
        for(const auto& guide:guides) for(auto v:guide)
            require(pt::finite(v) && v.x>=0 && v.y>=0 && v.x<=800 && v.y<=600,"Guide outside canvas");
    }
    require(p.strokes().empty() && p.diagnostics().reports==0,"Guides fabricated recorded samples");
    require(pt::testGuides(1,-1,0).empty(),"Invalid canvas should not generate guides");
}
}
int main() {
    unsigned failures=0;
    for(const auto& test:std::vector<std::pair<const char*,std::function<void()>>>{
        {"lifecycle",lifecycle},{"pointer isolation",pointers},{"filters",filters},
        {"metrics and curves",metrics},{"recording validation",recordings},{"history retrieval",histories},
        {"coordinate mapping",coordinateMapping},{"speed measurements",speedMeasurements},
        {"clock recovery",clockRecovery},{"bounded filtering",boundedFiltering},{"real-pen guides",realPenGuides}}) {
        try {test.second(); std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& e) {++failures; std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    }
    return failures?1:0;
}
