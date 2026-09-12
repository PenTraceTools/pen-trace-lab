#include "core.hpp"
#include "history.hpp"
#include "trace_io.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
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
}
}
int main() {
    unsigned failures=0;
    for(const auto& test:std::vector<std::pair<const char*,std::function<void()>>>{
        {"lifecycle",lifecycle},{"pointer isolation",pointers},{"filters",filters},
        {"metrics and curves",metrics},{"recording validation",recordings},{"history retrieval",histories},
        {"coordinate mapping",coordinateMapping},{"speed measurements",speedMeasurements}}) {
        try {test.second(); std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& e) {++failures; std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    }
    return failures?1:0;
}
