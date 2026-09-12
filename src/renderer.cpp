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
    static const wchar_t* names[]={L"Free drawing",L"Diagonal down",L"Diagonal up",L"Horizontal",L"Vertical",L"Curves and loops",L"Corners and tiny letters",L"Dots and pen lifts"};
    return names[std::min(test,7u)];
}
const wchar_t* testInstruction(unsigned test) {
    static const wchar_t* instructions[]={L"Draw freely. Record your device and pen under Session > Notes.",
        L"Follow the diagonal in both directions. Repeat slowly, normally, then quickly.",
        L"Follow the diagonal in both directions. Repeat slowly, normally, then quickly.",
        L"Draw left to right and back. Compare with diagonals at the same speed.",
        L"Draw top to bottom and back. Repeat at different screen positions.",
        L"Draw shallow arcs, circles, spirals and tiny loops. Watch for lost detail.",
        L"Draw V, W, L, small e and figure-eight strokes. Check corners and crossings.",
        L"Make dots, short flicks and light-pressure endings. Watch for tails or connections."};
    return instructions[std::min(test,7u)];
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
void Renderer::invalidateCache() { cache_.clear(); }
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
    const auto ink=D2D1::ColorF(0x17324D),muted=D2D1::ColorF(0x526579),blue=D2D1::ColorF(0x2469D8),orange=D2D1::ColorF(0xD16817),purple=D2D1::ColorF(0x9148BA);
    target_->BeginDraw(); target_->SetTransform(D2D1::Matrix3x2F::Identity()); target_->Clear(D2D1::ColorF(0xF2F5FA));
    text(L"Pen Trace Lab",D2D1::RectF(20,10,280,42),ink,true);
    text(status,D2D1::RectF(280,15,l.right+295,44),muted);
    text(std::wstring(testName(view.test))+L"  |  "+testInstruction(view.test),D2D1::RectF(20,48,l.right+295,86),muted);
    brush_->SetColor(D2D1::ColorF(D2D1::ColorF::White)); target_->FillRectangle(D2D1::RectF(l.left,l.top,l.right,l.bottom),brush_.Get());
    target_->PushAxisAlignedClip(D2D1::RectF(l.left,l.top,l.right,l.bottom),D2D1_ANTIALIAS_MODE_ALIASED);
    target_->SetTransform(D2D1::Matrix3x2F::Translation(l.left,l.top));
    const float w=l.right-l.left,h=l.bottom-l.top;
    brush_->SetColor(D2D1::ColorF(0xE8EEF5));
    for(float x=0;x<w;x+=40) target_->DrawLine(D2D1::Point2F(x,0),D2D1::Point2F(x,h),brush_.Get());
    for(float y=0;y<h;y+=40) target_->DrawLine(D2D1::Point2F(0,y),D2D1::Point2F(w,y),brush_.Get());
    if(view.zoom==1 && view.test>=1 && view.test<=4) {
        pt::Vec a{w*.15,h*.2},b{w*.85,h*.8};
        if(view.test==2) { a.y=h*.8; b.y=h*.2; }
        if(view.test==3) a.y=b.y=h*.5;
        if(view.test==4) a.x=b.x=w*.5;
        line({a,b},D2D1::ColorF(0xBAC8DA),1);
    }
    const auto& strokes=processor.strokes();
    const std::size_t selected=strokes.empty()?0:std::min(view.selected,strokes.size()-1);
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
        if(i<first && i!=selected) continue;
        const auto& s=strokes[i];
        auto& c=cache_[i];
        if(c.count!=s.points.size() || c.mode!=view.mode) {
            c.count=s.points.size(); c.mode=view.mode; c.raw=pt::filter(s,pt::Mode::Off); c.filtered=pt::filter(s,view.mode);
            c.curve=pt::curve(c.filtered); c.rawMetrics=pt::measure(s,c.raw); c.filteredMetrics=pt::measure(s,c.filtered);
            c.deviation=pt::curveDeviation(c.filtered,c.curve);
        }
        if((s.kind==pt::Kind::Touch && !view.touch) || (s.kind==pt::Kind::Mouse && !view.mouse)) continue;
        const float width=(i==selected?1.5f:1.0f)/view.zoom;
        const auto rawColor=s.kind==pt::Kind::Touch?D2D1::ColorF(0x19846B):s.kind==pt::Kind::Mouse?muted:blue;
        if(view.raw) line(c.raw,rawColor,width);
        if(view.filtered && (view.mode!=pt::Mode::Off || !view.raw)) line(c.filtered,orange,width,true);
        if(view.fitted) line(c.curve,purple,width);
        if(view.dots) {
            brush_->SetColor(rawColor);
            for(auto p:c.raw) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<float>(p.x),static_cast<float>(p.y)),1.8f/view.zoom,1.8f/view.zoom),brush_.Get());
        }
    }
    target_->SetTransform(D2D1::Matrix3x2F::Identity()); target_->PopAxisAlignedClip();
    const float x=l.right+18;
    text(L"Stroke inspection",D2D1::RectF(x,92,x+280,122),ink,true);
    std::wostringstream stats; stats<<std::fixed<<std::setprecision(3);
    stats<<L"Filter: "<<widen(pt::modeName(view.mode))<<L"\nSolid blue: reported pen\nDashed orange: filtered comparison\nGreen: touch | Purple: curve experiment\n";
    if(view.mode==pt::Mode::Off) stats<<L"Off: paths coincide; shown once.\n";
    stats<<L"\n";
    if(!strokes.empty()) {
        const auto& s=strokes[selected]; const auto& c=cache_[selected]; const auto& m=c.rawMetrics; const auto& f=c.filteredMetrics;
        stats<<L"Stroke "<<selected+1<<L" / "<<strokes.size()<<L" ("<<widen(pt::kindName(s.kind))<<L")\n";
        stats<<L"Contact samples: "<<s.points.size()<<L"\n";
        stats<<L"State: "<<(s.canceled?L"canceled":s.ended?L"ended":L"in progress")<<(s.recoveredStart?L" / recovered start":L"")<<L"\n\n";
        stats<<L"Straightness (only meaningful for lines)\n";
        if(m.lineDefined) stats<<L"Input RMS / P95 / max (DIP):\n"<<m.rms<<L" / "<<m.p95<<L" / "<<m.maximum<<L"\nFilter RMS: "<<f.rms<<L" DIP\n";
        else stats<<L"N/A: insufficient movement\n";
        stats<<L"\nFilter displacement RMS / max:\n"<<f.displacementRms<<L" / "<<f.displacementMax<<L" DIP\nEndpoint displacement: "<<f.endpointDisplacement<<L" DIP\n";
        stats<<L"Sampled curve deviation: "<<c.deviation<<L" DIP\n\n";
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
    stats<<L"\nReports: "<<d.reports<<L" | duplicates: "<<d.duplicates<<L"\nTiming anomalies: "<<d.nonIncreasingTimes<<L"\nInvalid: "<<d.invalid<<L" | boundaries: "<<d.boundaries<<L"\nNormalization limit hits: "<<d.limits;
    target_->PushAxisAlignedClip(D2D1::RectF(x,128,x+280,l.bottom),D2D1_ANTIALIAS_MODE_ALIASED);
    text(stats.str(),D2D1::RectF(x,132-view.sidebarScroll,x+280,1400-view.sidebarScroll),ink);
    target_->PopAxisAlignedClip();
    text(L"Space: pause/live   0-3: filter   [ / ]: stroke   Z: zoom   Ctrl+S: save   F1: help",D2D1::RectF(20,l.bottom+5,l.right+295,l.bottom+29),muted);
    // Metadata stays in the file; a short line is visible without hiding the canvas.
    if(!metadata.empty()) SetWindowTextW(window_,(L"Pen Trace Lab — "+metadata.substr(0,100)).c_str());
    hr=target_->EndDraw();
    if(hr==D2DERR_RECREATE_TARGET) { brush_.Reset(); target_.Reset(); }
    lastPaintMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    return hr;
}
