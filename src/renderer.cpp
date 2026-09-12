#include "renderer.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

std::wstring widen(const std::string& s) {
    if(s.empty()) return {};
    const int count=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),result.data(),count); return result;
}
std::string narrow(const std::wstring& s) {
    if(s.empty()) return {};
    const int count=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string result(static_cast<std::size_t>(count),'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),result.data(),count,nullptr,nullptr); return result;
}
const wchar_t* testName(unsigned test) {
    static const wchar_t* names[]={L"Free drawing",L"Diagonal down",L"Diagonal up",L"Horizontal",L"Vertical",L"Curves and loops",L"Corners and tiny letters",L"Dots and pen lifts",L"Stationary pen hold"};
    return names[std::min(test,pt::testCount-1)];
}
const wchar_t* testInstruction(unsigned test) {
    static const wchar_t* instructions[]={L"Draw with your real pen. Choose Real-pen tests for tracing guides.",
        L"Follow the diagonal in both directions. Repeat slowly, normally, then quickly.",
        L"Follow the diagonal in both directions. Repeat slowly, normally, then quickly.",
        L"Draw left to right and back. Compare with diagonals at the same speed.",
        L"Draw top to bottom and back. Repeat at different screen positions.",
        L"Draw shallow arcs, circles, spirals and tiny loops. Watch for lost detail.",
        L"Draw V, W, L, small e and figure-eight strokes. Check corners and crossings.",
        L"Make dots, short flicks and light-pressure endings. Watch for tails or connections.",
        L"Hold the tip on a cross for 5 seconds. Repeat with light then comfortable pressure."};
    return instructions[std::min(test,pt::testCount-1)];
}
HRESULT Renderer::initialize(HWND window) {
    window_=window;
    HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf());
    if(FAILED(hr)) return hr;
    auto style=D2D1::StrokeStyleProperties(); style.dashStyle=D2D1_DASH_STYLE_DASH;
    hr=factory_->CreateStrokeStyle(style,nullptr,0,dashed_.GetAddressOf());
    if(FAILED(hr)) return hr;
    hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(textFactory_.GetAddressOf()));
    if(FAILED(hr)) return hr;
    hr=textFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13,L"en-us",normal_.GetAddressOf());
    if(FAILED(hr)) return hr;
    hr=textFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,22,L"en-us",large_.GetAddressOf());
    return hr;
}
HRESULT Renderer::target() {
    if(target_) return S_OK;
    RECT rect{}; GetClientRect(window_,&rect);
    auto properties=D2D1::RenderTargetProperties();
    const auto dpi=static_cast<float>(GetDpiForWindow(window_));
    properties.dpiX=properties.dpiY=dpi>0?dpi:96;
    HRESULT hr=factory_->CreateHwndRenderTarget(properties,D2D1::HwndRenderTargetProperties(window_,D2D1::SizeU(static_cast<UINT32>(rect.right),static_cast<UINT32>(rect.bottom))),target_.GetAddressOf());
    if(FAILED(hr)) return hr;
    hr=target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black),brush_.GetAddressOf());
    if(FAILED(hr)) target_.Reset();
    return hr;
}
void Renderer::resize() {
    if(!target_) return;
    RECT rect{}; GetClientRect(window_,&rect);
    const float dpi=static_cast<float>(GetDpiForWindow(window_)); target_->SetDpi(dpi,dpi);
    if(FAILED(target_->Resize(D2D1::SizeU(static_cast<UINT32>(rect.right),static_cast<UINT32>(rect.bottom))))) { brush_.Reset(); target_.Reset(); }
}
Layout Renderer::layout() const {
    RECT rect{}; GetClientRect(window_,&rect);
    double dpi=GetDpiForWindow(window_); if(dpi==0) dpi=96;
    return {20,90,std::max(200.0f,static_cast<float>(rect.right*96/dpi)-315),std::max(160.0f,static_cast<float>(rect.bottom*96/dpi)-30)};
}
void Renderer::invalidateCache() { cache_.clear(); comparisonStroke_=static_cast<std::size_t>(-1); }
void Renderer::prepareComparisons(const pt::Stroke& s,std::size_t index,const ViewOptions& view) {
    if(comparisonStroke_==index && comparisonPoints_==s.points.size() && comparisonEnded_==s.ended &&
        comparisonCanceled_==s.canceled && comparisonOptions_==view.local) return;
    // During contact compute paths only. Final statistics run at lift/inspection,
    // not in the Windows input callback. Offline candidate waits for a clean UP.
    comparisons_=pt::compareAll(s,view.local,s.ended);
    comparisonStroke_=index; comparisonPoints_=s.points.size(); comparisonEnded_=s.ended;
    comparisonCanceled_=s.canceled; comparisonOptions_=view.local;
}
static D2D1_COLOR_F candidateColor(unsigned i) {
    static const unsigned colors[]={0x009C88,0xB040A0,0x6843C2,0xB77B00,0xDB4252};
    return D2D1::ColorF(colors[std::min(i,pt::candidateCount-1)]);
}
void Renderer::comparisonGrid(const pt::Stroke& stroke,const ViewOptions& view,Layout l) {
    brush_->SetColor(D2D1::ColorF(0xFFFFFF)); target_->FillRectangle(D2D1::RectF(l.left,l.top,l.right,l.bottom),brush_.Get());
    if(stroke.points.empty()) return;
    const auto raw=pt::rawPath(stroke);
    const auto rawVariation=pt::localVariation(raw,10);
    double minX=raw[0].x,maxX=minX,minY=raw[0].y,maxY=minY;
    for(auto p:raw) { minX=std::min(minX,p.x); maxX=std::max(maxX,p.x); minY=std::min(minY,p.y); maxY=std::max(maxY,p.y); }
    // One scale and bounds for every panel; no misleading per-result auto-fit.
    minX-=12; maxX+=12; minY-=12; maxY+=12;
    const float width=(l.right-l.left-8)/2,height=(l.bottom-l.top-16)/3;
    for(unsigned i=0;i<pt::candidateCount;++i) {
        const float x=l.left+static_cast<float>(i%2)*(width+8),y=l.top+static_cast<float>(i/2)*(height+8);
        const auto& c=comparisons_[i];
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        brush_->SetColor(D2D1::ColorF(0xFFFFFF)); target_->FillRectangle(D2D1::RectF(x,y,x+width,y+height),brush_.Get());
        std::wstring title=std::to_wstring(i+1)+L". "+widen(c.candidate.name)+L" | "+widen(pt::executionKind(c.candidate.algorithm));
        text(title,D2D1::RectF(x+6,y+3,x+width-5,y+39),candidateColor(i));
        std::wostringstream info; info<<std::fixed<<std::setprecision(2);
        if(!c.available) info<<L"Unavailable until a completed, uncanceled stroke.";
        else if(stroke.ended) {
            info<<L"10-DIP variation raw -> result: ";
            if(c.variation[1].samples) info<<rawVariation.rms<<L" -> "<<c.variation[1].rms; else info<<L"N/A";
            info<<L" | end: "<<c.metrics.endpointDisplacement<<L" DIP";
        } else info<<L"Live / provisional; metrics finalize on lift.";
        text(info.str(),D2D1::RectF(x+6,y+height-35,x+width-5,y+height),D2D1::ColorF(0x526579));
        const auto plot=D2D1::RectF(x+6,y+40,x+width-6,y+height-38);
        if(plot.bottom<=plot.top) continue;
        target_->PushAxisAlignedClip(plot,D2D1_ANTIALIAS_MODE_ALIASED);
        const float scale=static_cast<float>(std::min((plot.right-plot.left)/(maxX-minX),(plot.bottom-plot.top)/(maxY-minY)))*view.zoom;
        target_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(-(minX+maxX)*.5),static_cast<float>(-(minY+maxY)*.5))*
            D2D1::Matrix3x2F::Scale(scale,scale)*D2D1::Matrix3x2F::Translation((plot.left+plot.right)*.5f,(plot.top+plot.bottom)*.5f));
        line(raw,D2D1::ColorF(0x2469D8),1/scale);
        if(c.available) line(c.path,candidateColor(i),1.5f/scale,true);
        target_->SetTransform(D2D1::Matrix3x2F::Identity()); target_->PopAxisAlignedClip();
    }
}
void Renderer::text(const std::wstring& s,D2D1_RECT_F rect,D2D1_COLOR_F color,bool large) {
    brush_->SetColor(color); target_->DrawText(s.data(),static_cast<UINT32>(s.size()),large?large_.Get():normal_.Get(),rect,brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Renderer::line(const std::vector<pt::Vec>& points,D2D1_COLOR_F color,float width,bool dashed) {
    if(points.empty()) return;
    brush_->SetColor(color);
    if(points.size()==1) {
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<float>(points[0].x),static_cast<float>(points[0].y)),width*.5f,width*.5f),brush_.Get()); return;
    }
    Ptr<ID2D1PathGeometry> path;
    if(FAILED(factory_->CreatePathGeometry(path.GetAddressOf()))) return;
    Ptr<ID2D1GeometrySink> sink;
    if(FAILED(path->Open(sink.GetAddressOf()))) return;
    sink->BeginFigure(D2D1::Point2F(static_cast<float>(points[0].x),static_cast<float>(points[0].y)),D2D1_FIGURE_BEGIN_HOLLOW);
    for(std::size_t i=1;i<points.size();++i) sink->AddLine(D2D1::Point2F(static_cast<float>(points[i].x),static_cast<float>(points[i].y)));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if(SUCCEEDED(sink->Close())) target_->DrawGeometry(path.Get(),brush_.Get(),width,dashed?dashed_.Get():nullptr);
}
HRESULT Renderer::paint(const pt::Processor& processor,const ViewOptions& view,const std::wstring& status,const std::wstring& metadata) {
    const auto started=std::chrono::steady_clock::now();
    HRESULT hr=target(); if(FAILED(hr)) return hr;
    const auto l=layout();
    const auto ink=D2D1::ColorF(0x17324D),muted=D2D1::ColorF(0x526579),blue=D2D1::ColorF(0x2469D8);
    target_->BeginDraw(); target_->SetTransform(D2D1::Matrix3x2F::Identity()); target_->Clear(D2D1::ColorF(0xF2F5FA));
    text(L"Pen Trace Lab 0.4",D2D1::RectF(20,10,280,42),ink,true);
    text(status,D2D1::RectF(280,15,l.right+295,44),muted);
    text(std::wstring(testName(view.test))+L"  |  "+testInstruction(view.test),D2D1::RectF(20,48,l.right+295,86),muted);
    brush_->SetColor(D2D1::ColorF(D2D1::ColorF::White)); target_->FillRectangle(D2D1::RectF(l.left,l.top,l.right,l.bottom),brush_.Get());
    target_->PushAxisAlignedClip(D2D1::RectF(l.left,l.top,l.right,l.bottom),D2D1_ANTIALIAS_MODE_ALIASED);
    target_->SetTransform(D2D1::Matrix3x2F::Translation(l.left,l.top));
    const float w=l.right-l.left,h=l.bottom-l.top;
    brush_->SetColor(D2D1::ColorF(0xE8EEF5));
    for(float x=0;x<w;x+=40) target_->DrawLine(D2D1::Point2F(x,0),D2D1::Point2F(x,h),brush_.Get());
    for(float y=0;y<h;y+=40) target_->DrawLine(D2D1::Point2F(0,y),D2D1::Point2F(w,y),brush_.Get());
    if(view.zoom==1 && view.test) {
        for(const auto& guide:pt::testGuides(view.test,w,h)) line(guide,D2D1::ColorF(0xBAC8DA),1,true);
        text(L"GREY = target only. Draw over it with your pen; nothing is generated or snapped.",
            D2D1::RectF(12,8,w-12,44),muted);
    }
    const auto& strokes=processor.strokes();
    const std::size_t selected=strokes.empty()?0:std::min(view.selected,strokes.size()-1);
    if(!strokes.empty()) prepareComparisons(strokes[selected],selected,view);
    if(cache_.size()!=strokes.size()) cache_.resize(strokes.size());
    // Inspect around the selected stroke center; captured coordinates are never zoomed.
    pt::Vec center{w*.5,h*.5};
    if(view.zoom!=1 && !strokes.empty() && !strokes[selected].points.empty()) {
        center={}; for(const auto& p:strokes[selected].points) center=center+p.p*(1.0/strokes[selected].points.size());
    }
    target_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(-center.x),static_cast<float>(-center.y))*
        D2D1::Matrix3x2F::Scale(view.zoom,view.zoom)*D2D1::Matrix3x2F::Translation(l.left+w*.5f,l.top+h*.5f));
    const std::size_t first=strokes.size()>100?strokes.size()-100:0;
    for(std::size_t i=0;i<strokes.size();++i) {
        if(view.selectedOnly && i!=selected) continue;
        if(i<first && i!=selected) continue;
        const auto& s=strokes[i];
        auto& c=cache_[i];
        if(c.count!=s.points.size()) {
            c.count=s.points.size(); c.raw=pt::rawPath(s); c.rawMetrics=pt::measure(s,c.raw);
        }
        if((s.kind==pt::Kind::Touch && !view.touch) || (s.kind==pt::Kind::Mouse && !view.mouse)) continue;
        const float width=(i==selected?1.5f:1.0f)/view.zoom;
        const auto rawColor=s.kind==pt::Kind::Touch?D2D1::ColorF(0x19846B):s.kind==pt::Kind::Mouse?muted:blue;
        if(view.raw) line(c.raw,rawColor,width);
        if(i==selected && view.filtered && view.comparisonView!=2) {
            for(unsigned j=0;j<pt::candidateCount;++j) {
                if(view.comparisonView!=1 && j!=view.candidate) continue;
                const auto& result=comparisons_[j]; if(!result.available) continue;
                if(view.comparisonView==3) {
                    auto exaggerated=result.path;
                    for(std::size_t k=0;k<exaggerated.size();++k) exaggerated[k]=c.raw[k]+(exaggerated[k]-c.raw[k])*8;
                    line(exaggerated,candidateColor(j),width,true);
                } else line(result.path,candidateColor(j),width,true);
            }
        }
        if(view.dots) {
            brush_->SetColor(rawColor);
            for(auto p:c.raw) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<float>(p.x),static_cast<float>(p.y)),1.8f/view.zoom,1.8f/view.zoom),brush_.Get());
        }
    }
    target_->SetTransform(D2D1::Matrix3x2F::Identity()); target_->PopAxisAlignedClip();
    if(view.comparisonView==2 && !strokes.empty()) comparisonGrid(strokes[selected],view,l);
    if(view.comparisonView==3) text(L"DIFFERENCE x8: exaggerated offsets, NOT actual filtered geometry. Inspection only.",
        D2D1::RectF(l.left+8,l.top+5,l.right-8,l.top+42),candidateColor(view.candidate));
    const float x=l.right+18;
    text(L"Stroke inspection",D2D1::RectF(x,92,x+280,122),ink,true);
    std::wostringstream stats; stats<<std::fixed<<std::setprecision(3);
    {
        stats<<L"Selected stroke: all 5 evaluated\n1-5: candidate | G: grid | D: difference\nO: all overlays | C: selected/live\nBlue = raw; dashed = SELECTED stroke only\n[ / ]: select an earlier stroke\n";
        const auto catalog=pt::candidates(view.local); const auto& candidate=catalog[view.candidate];
        stats<<L"Selected: "<<widen(candidate.name)<<L"\n"<<widen(pt::executionKind(candidate.algorithm))
            <<L" | window: "<<candidate.window*1000<<L" ms\nRadius: "<<candidate.radius<<L" | cap: "<<candidate.cap<<L" DIP\n";
        if(candidate.algorithm==pt::Algorithm::OneEuro) stats<<L"Cutoff: "<<candidate.cutoff<<L" Hz | beta: "<<candidate.beta<<L"\n";
        if(!strokes.empty()) {
            const auto& r=comparisons_[view.candidate];
            if(!r.available) stats<<L"OFFLINE: requires completed clean stroke\n";
            else if(!strokes[selected].ended) stats<<L"Paths updating; statistics finalize on lift.\n";
            else {
                stats<<L"\nCandidate variation at 5 / 10 / 20 DIP:\n";
                for(const auto& v:r.variation) { if(v.samples) stats<<v.rms; else stats<<L"N/A"; stats<<L" / "; }
                stats<<L"\nMax / endpoint displacement (DIP):\n"<<r.metrics.displacementMax<<L" / "<<r.metrics.endpointDisplacement<<L"\n";
                stats<<L"Nearest-path lag estimate mean / P95:\n";
                if(r.lagSamples) stats<<r.lagMeanMs<<L" / "<<r.lagP95Ms<<L" ms\n"; else stats<<L"N/A\n";
                stats<<L"Not physical pen-to-photon latency.\nRaw sharp-turn displacement: ";
                if(r.turnSamples) stats<<r.turnDisplacementMax<<L" DIP\n"; else stats<<L"N/A\n";
                stats<<L"Near-closed loop area ratio: ";
                if(r.loopAreaAvailable) stats<<r.loopAreaRatio; else stats<<L"N/A";
                stats<<L"\nShape proxies, not known intent.\n";
            }
        }
    }
    static const wchar_t* speeds[]={L"Slow",L"Normal",L"Fast"};
    stats<<L"\nReal-pen test: "<<testName(view.test)<<L"\nIntended pace: "<<speeds[std::min(view.speed,2u)]<<L" (a label, not measured)\nF2: next test | F3: next pace\nGrey guides are not recorded strokes.\n";
    stats<<L"\n";
    if(!strokes.empty()) {
        const auto& s=strokes[selected]; const auto& c=cache_[selected]; const auto& m=c.rawMetrics;
        stats<<L"Stroke "<<selected+1<<L" / "<<strokes.size()<<L" ("<<widen(pt::kindName(s.kind))<<L")\n";
        stats<<L"Contact samples: "<<s.points.size()<<L"\n";
        stats<<L"State: "<<(s.canceled?L"canceled":s.ended?L"ended":L"in progress")<<(s.recoveredStart?L" / recovered start":L"")<<L"\n\n";
        stats<<L"Straightness (only meaningful for lines)\n";
        if(m.lineDefined) stats<<L"Input RMS / P95 / max (DIP):\n"<<m.rms<<L" / "<<m.p95<<L" / "<<m.maximum<<L"\n";
        else stats<<L"N/A: insufficient movement\n";
        stats<<L"Reported speed (estimated DIP/s)\n";
        stats<<L"Last interval: "<<(m.lastSpeedValid?std::to_wstring(m.lastSpeed):L"N/A")<<L"\n";
        if(m.speedIntervals) stats<<L"Mean / P95 / max:\n"<<m.meanSpeed<<L" / "<<m.p95Speed<<L" / "<<m.maxSpeed<<L"\n";
        else stats<<L"N/A: no usable timed intervals\n";
        stats<<L"Speed intervals valid / rejected: "<<m.speedIntervals<<L" / "<<m.invalidSpeedIntervals<<L"\n\n";
        stats<<L"Report intervals mean / P95:\n"<<m.meanIntervalMs<<L" / "<<m.p95IntervalMs<<L" ms\nNon-increasing times: "<<m.nonIncreasing<<L"\nGaps >50 ms: "<<m.gaps<<L"\n";
        if(!s.points.empty()) {
            const auto& last=s.points.back();
            const bool hasPressure=(last.mask & (s.kind==pt::Kind::Pen?1u:4u))!=0;
            stats<<L"Last pressure: "<<(hasPressure?std::to_wstring(last.pressure):L"unavailable")<<L" / 1024\n";
            stats<<L"Clock: "<<(last.clock==pt::Clock::Qpc?L"QPC":last.clock==pt::Clock::Milliseconds?L"milliseconds":L"receipt fallback")
                 <<L" | "<<(last.mapped?L"HIMETRIC":L"pixel fallback")<<L"\n";
        }
    } else stats<<L"Draw with your pen to begin.\nMouse display: View menu.\n";
    const auto& d=processor.diagnostics();
    stats<<L"\nReports: "<<d.reports<<L" | duplicates: "<<d.duplicates<<L"\nTiming anomalies: "<<d.nonIncreasingTimes
        <<L"\nRecovered report clocks: "<<d.recoveredTiming<<L"\nReport/receipt offsets: "<<d.clockOffsetReports
        <<L" (not pen latency)\nInvalid: "<<d.invalid<<L" | boundaries: "<<d.boundaries<<L"\nNormalization limit hits: "<<d.limits;
    target_->PushAxisAlignedClip(D2D1::RectF(x,128,x+280,l.bottom),D2D1_ANTIALIAS_MODE_ALIASED);
    text(stats.str(),D2D1::RectF(x,132-view.sidebarScroll,x+280,3000-view.sidebarScroll),ink);
    target_->PopAxisAlignedClip();
    text(L"1-5: candidate   G: grid   D: difference x8   C: selected/live view   [ / ]: stroke   Space: pause/live",D2D1::RectF(20,l.bottom+5,l.right+295,l.bottom+29),muted);
    // Metadata stays in the file; a short line is visible without hiding the canvas.
    if(!metadata.empty()) SetWindowTextW(window_,(L"Pen Trace Lab — "+metadata.substr(0,100)).c_str());
    hr=target_->EndDraw();
    if(hr==D2DERR_RECREATE_TARGET) { brush_.Reset(); target_.Reset(); }
    lastPaintMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    return hr;
}
