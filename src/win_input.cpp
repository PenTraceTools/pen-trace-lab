#include "win_input.hpp"
#include "history.hpp"
#include <algorithm>
#include <utility>
#include <type_traits>
#include <vector>

WindowsInput::WindowsInput(Emit emit,Log log):emit_(std::move(emit)),log_(std::move(log)) {
    QueryPerformanceFrequency(&frequency_); QueryPerformanceCounter(&start_);
    // QPC is available on supported Windows versions; keep a defined fallback.
    if(frequency_.QuadPart<=0) frequency_.QuadPart=1;
}
double WindowsInput::now() const {
    LARGE_INTEGER q{}; QueryPerformanceCounter(&q);
    return static_cast<double>(q.QuadPart-start_.QuadPart)/static_cast<double>(frequency_.QuadPart);
}
std::string WindowsInput::clockDescription() const {
    return "Clock mapping: QPC origin="+std::to_string(start_.QuadPart)+"; QPC frequency="+std::to_string(frequency_.QuadPart)+" Hz; time/receipt units=seconds.";
}
void WindowsInput::setCanvas(double l,double t,double r,double b) { left_=l; top_=t; right_=r; bottom_=b; }
pt::Sample WindowsInput::base(HWND window,UINT message,const POINTER_INFO& info,UINT32 index,UINT32 count) {
    pt::Sample s;
    s.sequence=++sequence_; s.device=reinterpret_cast<std::uintptr_t>(info.sourceDevice);
    s.pointer=info.pointerId; s.frame=info.frameId; s.message=message;
    s.qpc=info.PerformanceCount; s.milliseconds=info.dwTime; s.flags=info.pointerFlags;
    s.buttonChange=static_cast<std::uint32_t>(info.ButtonChangeType);
    s.historyCount=count; s.advertisedHistoryCount=info.historyCount; s.batchIndex=index; s.receipt=now();
    s.time=s.receipt; s.clock=pt::Clock::ReceiptFallback;
    if(const auto t=pt::reportTime(info.PerformanceCount,calibration(),s.receipt)) {
        s.time=*t;
        s.clock=pt::Clock::Qpc;
    } else if(info.dwTime) {
        const DWORD age=GetTickCount()-info.dwTime; // unsigned wraparound arithmetic
        if(age<60000) { s.time=s.receipt-static_cast<double>(age)*.001; s.clock=pt::Clock::Milliseconds; }
    }
    const unsigned warning=s.clock==pt::Clock::ReceiptFallback?2u:
        s.clock==pt::Clock::Qpc && s.time>s.receipt+.005?1u:0u;
    if(clockWarnings_.size()<1024 || clockWarnings_.contains(s.device)) {
        auto& previous=clockWarnings_[s.device];
        if(warning && !(warning&previous)) log_(s.receipt,0,warning==1?
            "Report clock ahead of receipt clock; report intervals retained, absolute latency unknown.":
            "No usable report clock; receipt time retained for replay only, filtering/speed disabled across it.");
        previous|=warning; // Once per condition/device, not once per report.
    }
    s.kind=info.pointerType==PT_PEN ? pt::Kind::Pen : info.pointerType==PT_TOUCH ? pt::Kind::Touch : pt::Kind::Mouse;
    s.coordinates={info.ptPixelLocation.x,info.ptPixelLocation.y,info.ptPixelLocationRaw.x,info.ptPixelLocationRaw.y,
        info.ptHimetricLocation.x,info.ptHimetricLocation.y,info.ptHimetricLocationRaw.x,info.ptHimetricLocationRaw.y};
    POINT origin{};
    if(!ClientToScreen(window,&origin)) log_(s.receipt,GetLastError(),"ClientToScreen failed.");
    s.originX=origin.x; s.originY=origin.y; s.dpi=GetDpiForWindow(window); if(!s.dpi) s.dpi=96;
    RECT device{},display{};
    double x=info.ptPixelLocationRaw.x, y=info.ptPixelLocationRaw.y;
    if(info.sourceDevice && GetPointerDeviceRects(info.sourceDevice,&device,&display) &&
        device.right>device.left && device.bottom>device.top && display.right>display.left && display.bottom>display.top) {
        s.mapped=true;
        s.rectangles[0]=device.left; s.rectangles[1]=device.top; s.rectangles[2]=device.right; s.rectangles[3]=device.bottom;
        s.rectangles[4]=display.left; s.rectangles[5]=display.top; s.rectangles[6]=display.right; s.rectangles[7]=display.bottom;
        const auto mapped=pt::mapHimetric({static_cast<double>(info.ptHimetricLocationRaw.x),static_cast<double>(info.ptHimetricLocationRaw.y)},
            {device.left,device.top,device.right,device.bottom},{display.left,display.top,display.right,display.bottom});
        x=mapped.x; y=mapped.y;
    }
    s.p=pt::toCanvas({x,y},{static_cast<double>(origin.x),static_cast<double>(origin.y)},s.dpi,{left_,top_});
    s.eligible=s.p.x>=0 && s.p.y>=0 && s.p.x<=right_-left_ && s.p.y<=bottom_-top_;
    s.contact=(info.pointerFlags&POINTER_FLAG_INCONTACT)!=0;
    s.down=(info.pointerFlags&POINTER_FLAG_DOWN)!=0;
    s.up=(info.pointerFlags&POINTER_FLAG_UP)!=0;
    s.canceled=(info.pointerFlags&POINTER_FLAG_CANCELED)!=0;
    return s;
}
void WindowsInput::deliver(pt::Sample s) {
    emit_(s);
    if(s.contact && !s.canceled && !s.up) active_[s.pointer]=s;
    else active_.erase(s.pointer);
}
template<class Info,class Getter,class Latest>
void WindowsInput::history(HWND window,UINT message,UINT32 id,Getter getter,Latest latest) {
    DWORD error=0;
    const auto batch=pt::readHistory<Info>([&](std::uint32_t* count,Info* data){
        const BOOL ok=getter(id,count,data); if(!ok) error=GetLastError(); return ok!=FALSE;
    },[&](Info* data){const BOOL ok=latest(id,data); if(!ok) error=GetLastError(); return ok!=FALSE;});
    if(batch.fallback) log_(now(),error,"History unavailable/incomplete; latest-only fallback (possible missing samples).");
    if(batch.retrievalFailed) { log_(now(),error,"Latest report unavailable; input gap."); return; }
    for(std::size_t i=0;i<batch.oldestFirst.size();++i) {
        const auto& info=batch.oldestFirst[i];
        auto s=base(window,message,info.pointerInfo,static_cast<UINT32>(i),static_cast<UINT32>(batch.oldestFirst.size()));
        if constexpr(std::is_same_v<Info,POINTER_PEN_INFO>) {
            s.mask=info.penMask; s.toolFlags=info.penFlags; s.pressure=info.pressure;
            s.tiltX=info.tiltX; s.tiltY=info.tiltY; s.rotation=info.rotation;
        } else {
            s.mask=info.touchMask; s.toolFlags=info.touchFlags; s.pressure=info.pressure; s.orientation=info.orientation;
            s.rectangles[8]=info.rcContact.left; s.rectangles[9]=info.rcContact.top;
            s.rectangles[10]=info.rcContact.right; s.rectangles[11]=info.rcContact.bottom;
            s.rectangles[12]=info.rcContactRaw.left; s.rectangles[13]=info.rcContactRaw.top;
            s.rectangles[14]=info.rcContactRaw.right; s.rectangles[15]=info.rcContactRaw.bottom;
        }
        deliver(s);
    }
}
void WindowsInput::cancelPointer(std::uint32_t id,const char* reason) {
    auto it=active_.find(id);
    if(it==active_.end()) return;
    auto s=it->second; s.sequence=++sequence_; s.boundary=true; s.contact=false; s.canceled=true;
    s.down=false; s.up=false; s.receipt=now(); s.time=s.receipt; s.clock=pt::Clock::ReceiptFallback;
    active_.erase(it); emit_(s); log_(s.receipt,0,reason);
}
void WindowsInput::cancelAll(const char* reason) {
    while(!active_.empty()) cancelPointer(active_.begin()->first,reason);
}
bool WindowsInput::handle(HWND window,UINT message,WPARAM wParam,LPARAM) {
    if(message==WM_POINTERCAPTURECHANGED) { cancelPointer(GET_POINTERID_WPARAM(wParam),"Pointer capture changed."); return true; }
    if(message==WM_CANCELMODE || message==WM_KILLFOCUS) { cancelAll("Window lost drawing focus."); return false; }
    if(message==WM_POINTERLEAVE) {
        // In-contact capture normally survives leaving the client; don't end it on hover leave.
        POINTER_INFO info{};
        if(GetPointerInfo(GET_POINTERID_WPARAM(wParam),&info) && !(info.pointerFlags&POINTER_FLAG_INCONTACT))
            cancelPointer(info.pointerId,"Pointer left detection range.");
        return true;
    }
    if(message==WM_POINTERENTER) return true;
    if(message!=WM_POINTERDOWN && message!=WM_POINTERUPDATE && message!=WM_POINTERUP) return false;
    const UINT32 id=GET_POINTERID_WPARAM(wParam); POINTER_INPUT_TYPE type{};
    if(!GetPointerType(id,&type)) { log_(now(),GetLastError(),"GetPointerType failed."); return false; }
    if(type==PT_PEN) history<POINTER_PEN_INFO>(window,message,id,GetPointerPenInfoHistory,GetPointerPenInfo);
    else if(type==PT_TOUCH) history<POINTER_TOUCH_INFO>(window,message,id,GetPointerTouchInfoHistory,GetPointerTouchInfo);
    else if(type==PT_MOUSE) {
        POINTER_INFO info{};
        if(GetPointerInfo(id,&info)) deliver(base(window,message,info,0,1));
        else log_(now(),GetLastError(),"Mouse pointer report failed.");
    } else return false;
    return true;
}
