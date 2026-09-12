#include "core.hpp"
#include "trace_io.hpp"
#include "win_input.hpp"
#include "renderer.hpp"
#include <commdlg.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
enum Command : UINT {
    New=100,Save,Open,Samples,Notes,Exit,Pause,Replay,ReplayFast,Stop,ShowLog,MotionCsv,
    Raw=200,Filtered,Dots,Touch,Mouse,Zoom,Previous,Next,SelectedOnly,
    Test0=400,Help=500,NextTest,NextPace,Slow=600,Normal,Fast,
    LabSelected=700,LabOverlay,LabGrid,LabDifference,LabSummary,LabPaths,LabSweep,
    Candidate0=720,Radius0=740,Window0=750,Cap0=760
};
struct NotesDialog { HWND edit{}; bool accepted{}; std::wstring value; };
LRESULT CALLBACK notesProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* dialog=reinterpret_cast<NotesDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        dialog=static_cast<NotesDialog*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(dialog));
    }
    if(!dialog) return DefWindowProcW(window,message,wParam,lParam);
    if(message==WM_CREATE) {
        CreateWindowW(L"STATIC",L"Device / pen model, test speed, guide used, firmware/driver notes:",WS_CHILD|WS_VISIBLE,12,12,540,24,window,nullptr,nullptr,nullptr);
        dialog->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",dialog->value.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,12,42,540,155,window,nullptr,nullptr,nullptr);
        SendMessageW(dialog->edit,EM_SETLIMITTEXT,8000,0);
        CreateWindowW(L"BUTTON",L"Save notes",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,342,210,100,30,window,reinterpret_cast<HMENU>(IDOK),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,452,210,100,30,window,reinterpret_cast<HMENU>(IDCANCEL),nullptr,nullptr);
        SetFocus(dialog->edit); return 0;
    }
    if(message==WM_COMMAND && (LOWORD(wParam)==IDOK || LOWORD(wParam)==IDCANCEL)) {
        if(LOWORD(wParam)==IDOK) {
            const int length=GetWindowTextLengthW(dialog->edit);
            std::wstring text(static_cast<std::size_t>(length)+1,L'\0');
            GetWindowTextW(dialog->edit,text.data(),length+1); text.resize(static_cast<std::size_t>(length));
            dialog->value=std::move(text); dialog->accepted=true;
        }
        DestroyWindow(window); return 0;
    }
    if(message==WM_CLOSE) { DestroyWindow(window); return 0; }
    return DefWindowProcW(window,message,wParam,lParam);
}
bool editNotes(HWND owner,std::string& value) {
    WNDCLASSW wc{}; wc.lpfnWndProc=notesProc; wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"PenTraceNotes"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    RegisterClassW(&wc);
    NotesDialog dialog; dialog.value=widen(value);
    RECT parent{}; GetWindowRect(owner,&parent);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"Recording notes",WS_CAPTION|WS_SYSMENU|WS_POPUP,
        parent.left+60,parent.top+60,580,290,owner,nullptr,wc.hInstance,&dialog);
    if(!window) return false;
    EnableWindow(owner,FALSE); ShowWindow(window,SW_SHOW);
    MSG msg{};
    while(IsWindow(window)) {
        const BOOL result=GetMessageW(&msg,nullptr,0,0);
        if(result<=0) { if(result==0) PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        if(!IsDialogMessageW(window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if(IsWindow(window)) DestroyWindow(window);
    EnableWindow(owner,TRUE); SetForegroundWindow(owner);
    if(dialog.accepted) value=narrow(dialog.value);
    return dialog.accepted;
}
std::filesystem::path chooseFile(HWND window,bool save,bool csv) {
    std::vector<wchar_t> path(32768);
    OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window;
    dialog.lpstrFile=path.data(); dialog.nMaxFile=static_cast<DWORD>(path.size());
    dialog.lpstrFilter=csv?L"CSV data (*.csv)\0*.csv\0\0":L"Pen Trace recording (*.pentrace)\0*.pentrace\0\0";
    dialog.lpstrDefExt=csv?L"csv":L"pentrace";
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    const BOOL result=save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);
    if(!result) {
        if(const DWORD error=CommDlgExtendedError()) throw std::runtime_error("File dialog failed: "+std::to_string(error));
        return {};
    }
    return path.data();
}
void atomicWrite(const std::filesystem::path& path,const std::function<void(std::ostream&)>& writer) {
    // Same-directory temporary file. Existing destination remains intact on write failure.
    wchar_t temporary[MAX_PATH]{};
    if(!GetTempFileNameW(path.parent_path().c_str(),L"ptl",0,temporary)) throw std::runtime_error("Cannot create temporary output file (check directory/path length).");
    try {
        std::ofstream out(std::filesystem::path(temporary),std::ios::binary|std::ios::trunc);
        if(!out) throw std::runtime_error("Cannot open temporary output file.");
        writer(out); out.flush();
        if(!out) throw std::runtime_error("Output flush failed; destination was not replaced.");
        out.close();
        if(!out) throw std::runtime_error("Output close failed; destination was not replaced.");
        if(!MoveFileExW(temporary,path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace output file; destination was not changed.");
    } catch(...) { DeleteFileW(temporary); throw; } // only the exact file created above
}
HMENU menus() {
    HMENU bar=CreateMenu(),session=CreatePopupMenu(),capture=CreatePopupMenu(),view=CreatePopupMenu(),test=CreatePopupMenu(),lab=CreatePopupMenu();
    auto add=[](HMENU menu,UINT id,const wchar_t* label){AppendMenuW(menu,MF_STRING,id,label);};
    add(session,New,L"New recording\tCtrl+N"); add(session,Notes,L"Device and test notes...");
    add(session,Save,L"Save recording...\tCtrl+S"); add(session,Open,L"Open recording...\tCtrl+O");
    add(session,Samples,L"Export reported samples CSV...");
    add(session,MotionCsv,L"Export reported motion / speed CSV...");
    add(session,ShowLog,L"Recent diagnostic events...");
    add(session,Exit,L"Exit");
    add(capture,Pause,L"Pause / resume live capture\tSpace"); add(capture,Replay,L"Replay recording at 1x");
    add(capture,ReplayFast,L"Replay recording at 4x"); add(capture,Stop,L"Show full recording / stop replay");
    add(view,Raw,L"Reported polyline (solid)"); add(view,Filtered,L"Filtered polyline (dashed)"); add(view,Dots,L"Reported sample dots");
    add(view,Touch,L"Show finger strokes");
    add(view,Mouse,L"Capture/show mouse (not a pen test)"); add(view,Zoom,L"Inspection zoom: 1x / 2x / 4x\tZ");
    add(view,Previous,L"Previous stroke\t["); add(view,Next,L"Next stroke\t]");
    add(view,SelectedOnly,L"Show only selected stroke (keeps all data)");
    for(unsigned i=0;i<pt::testCount;++i) add(test,Test0+i,testName(i));
    AppendMenuW(test,MF_SEPARATOR,0,nullptr);
    add(test,NextTest,L"Next real-pen test\tF2");
    add(test,Slow,L"Intended pace: slow"); add(test,Normal,L"Intended pace: normal"); add(test,Fast,L"Intended pace: fast");
    add(test,NextPace,L"Next intended pace\tF3");
    add(lab,LabSelected,L"Selected candidate (live-capable)\tC"); add(lab,LabOverlay,L"Overlay all candidates\tO");
    add(lab,LabGrid,L"Side-by-side grid (pauses capture)\tG"); add(lab,LabDifference,L"Difference x8 (exaggerated; pauses)\tD");
    AppendMenuW(lab,MF_SEPARATOR,0,nullptr);
    const auto catalog=pt::candidates();
    for(unsigned i=0;i<pt::candidateCount;++i) add(lab,Candidate0+i,(widen(catalog[i].name)+L"\t"+std::to_wstring(i+1)).c_str());
    const auto settings=[&](const wchar_t* name,UINT base,const std::vector<const wchar_t*>& labels) {
        HMENU submenu=CreatePopupMenu();
        for(unsigned i=0;i<labels.size();++i) add(submenu,base+i,labels[i]);
        AppendMenuW(lab,MF_POPUP,reinterpret_cast<UINT_PTR>(submenu),name);
    };
    settings(L"Adjustable local: radius",Radius0,{L"4 DIP",L"8 DIP",L"12 DIP",L"20 DIP"});
    settings(L"Adjustable local: revision window",Window0,{L"40 ms",L"80 ms",L"120 ms",L"200 ms"});
    settings(L"Adjustable local: displacement cap",Cap0,{L"1.5 DIP",L"2.5 DIP",L"4 DIP",L"6 DIP"});
    AppendMenuW(lab,MF_SEPARATOR,0,nullptr);
    add(lab,LabSummary,L"Export all-algorithm summary CSV...");
    add(lab,LabPaths,L"Export all-algorithm paths CSV...");
    add(lab,LabSweep,L"Export parameter sweep CSV (slower)...");
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(session),L"Session");
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(capture),L"Capture / replay");
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(view),L"View");
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(lab),L"Compare");
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(test),L"Real-pen tests"); add(bar,Help,L"Help");
    return bar;
}
class App {
public:
    HWND window{}; Renderer renderer; WindowsInput input;
    pt::Session session; pt::Processor processor; ViewOptions view;
    bool live{true},dirty{},loaded{},replaying{},ready{};
    std::size_t cursor{}; double replayStart{},replayOrigin{},speed{1};
    std::wstring notice;
    App():input([this](const pt::Sample& s){sample(s);},[this](double t,DWORD code,const std::string& text){log(t,code,text);}) {}
    void log(double t,DWORD code,const std::string& text) {
        if(!live) return;
        if(session.events.size()<pt::maxEvents) session.events.push_back({t,code,text});
        notice=widen(text); dirty=true;
    }
    void sample(const pt::Sample& s) {
        if(!live || loaded || view.zoom!=1 || view.comparisonView==2 || view.comparisonView==3 || (s.kind==pt::Kind::Mouse && !view.mouse)) return;
        if(session.samples.size()>=pt::maxSamples) { live=false; notice=L"Recording limit reached. Save, then start a new session."; return; }
        session.samples.push_back(s); processor.consume(s); dirty=true;
        if(s.contact && s.eligible) view.selected=static_cast<std::size_t>(-1);
    }
    void pause() { input.cancelAll("Capture paused; active strokes ended as canceled."); live=false; }
    void rebuild() {
        processor.setClockCalibration(pt::clockCalibration(session)); for(const auto& s:session.samples) processor.consume(s);
        renderer.invalidateCache(); view.selected=static_cast<std::size_t>(-1);
    }
    bool save() {
        pause(); const auto path=chooseFile(window,true,false); if(path.empty()) return false;
        if(session.events.size()<pt::maxEvents) session.events.push_back({input.now(),0,pt::comparisonSettings(view.local)});
        dirty=true;
        atomicWrite(path,[&](std::ostream& out){pt::writeTrace(out,session);});
        dirty=false; notice=L"Recording saved: "+path.filename().wstring(); return true;
    }
    bool mayDiscard() {
        pause(); if(!dirty) return true;
        const int answer=MessageBoxW(window,L"Save this recording before continuing?",L"Unsaved recording",MB_YESNOCANCEL|MB_ICONQUESTION);
        if(answer==IDCANCEL) return false;
        return answer==IDNO || save();
    }
    void updateLayout() {
        const auto l=renderer.layout(); input.setCanvas(l.left,l.top,l.right,l.bottom);
    }
    void recordEnvironment() {
        SYSTEM_INFO info{}; GetNativeSystemInfo(&info);
        const auto l=renderer.layout();
        log(input.now(),0,input.clockDescription());
        log(input.now(),0,"Environment: native processor architecture="+std::to_string(info.wProcessorArchitecture)+
            "; window DPI="+std::to_string(GetDpiForWindow(window))+"; canvas DIP width="+std::to_string(l.right-l.left)+
            "; height="+std::to_string(l.bottom-l.top)+"; app version=0.4.0; test="+narrow(testName(view.test)));
        log(input.now(),0,"Intended pace: "+std::to_string(view.speed)+" (0 slow, 1 normal, 2 fast; label only)");
        log(input.now(),0,pt::comparisonSettings(view.local));
    }
    void refreshMenus() {
        const HMENU menu=GetMenu(window);
        auto check=[&](UINT id,bool on){CheckMenuItem(menu,id,MF_BYCOMMAND|(on?MF_CHECKED:MF_UNCHECKED));};
        check(Raw,view.raw); check(Filtered,view.filtered); check(Dots,view.dots);
        check(Touch,view.touch); check(Mouse,view.mouse); check(Pause,!live);
        check(SelectedOnly,view.selectedOnly);
        CheckMenuRadioItem(menu,Test0,Test0+pt::testCount-1,Test0+view.test,MF_BYCOMMAND);
        CheckMenuRadioItem(menu,Slow,Fast,Slow+view.speed,MF_BYCOMMAND);
        CheckMenuRadioItem(menu,LabSelected,LabDifference,LabSelected+view.comparisonView,MF_BYCOMMAND);
        CheckMenuRadioItem(menu,Candidate0,Candidate0+pt::candidateCount-1,Candidate0+view.candidate,MF_BYCOMMAND);
        const double radii[]={4,8,12,20},windows[]={.040,.080,.120,.200},caps[]={1.5,2.5,4,6};
        for(unsigned i=0;i<4;++i) { check(Radius0+i,view.local.radius==radii[i]); check(Window0+i,view.local.window==windows[i]); check(Cap0+i,view.local.cap==caps[i]); }
    }
    void command(UINT id) {
        if(id==NextTest) id=Test0+(view.test+1)%pt::testCount;
        if(id==NextPace) id=Slow+(view.speed+1)%3;
        if(id>=LabSelected && id<=LabDifference) {
            if(id==LabGrid || id==LabDifference) pause();
            view.comparisonView=id-LabSelected;
        } else if(id>=Candidate0 && id<Candidate0+pt::candidateCount) {
            view.candidate=id-Candidate0;
        } else if((id>=Radius0 && id<Radius0+4) || (id>=Window0 && id<Window0+4) || (id>=Cap0 && id<Cap0+4)) {
            const double radii[]={4,8,12,20},windows[]={.040,.080,.120,.200},caps[]={1.5,2.5,4,6};
            if(id>=Cap0) view.local.cap=caps[id-Cap0]; else if(id>=Window0) view.local.window=windows[id-Window0]; else view.local.radius=radii[id-Radius0];
            log(input.now(),0,pt::comparisonSettings(view.local)); view.candidate=0;
            notice=L"Adjusted candidate 1 only; other algorithms still use the same original input.";
        } else if(id>=Slow && id<=Fast) {
            input.cancelAll("Intended pace changed."); view.speed=id-Slow;
            log(input.now(),0,"Intended pace: "+std::to_string(view.speed)+" (0 slow, 1 normal, 2 fast; label only)");
        } else if(id>=Test0 && id<Test0+pt::testCount) {
            input.cancelAll("Test changed."); view.test=id-Test0;
            log(input.now(),0,"Test: "+narrow(testName(view.test)));
            notice=loaded?L"Guide only. Ctrl+N starts a real-pen recording; loaded data is unchanged.":
                L"Trace the grey targets with your real pen. Space resumes if paused; F3 changes intended pace.";
        } else switch(id) {
        case New:
            if(!mayDiscard()) break;
            session={}; processor.clear(); renderer.invalidateCache(); loaded=false; replaying=false;
            view.zoom=1; view.comparisonView=0; view.selected=static_cast<std::size_t>(-1); live=true; dirty=false; notice=L"New recording. All candidates use original samples; G opens the comparison grid.";
            session.metadata="Pen Trace Lab 0.4.0; Windows native pointer API; coordinates: canvas DIPs. Device: unknown; pen: unknown.";
            processor.setClockCalibration(input.calibration());
            recordEnvironment();
            break;
        case Save: save(); break;
        case Open: {
            pause(); const auto path=chooseFile(window,false,false); if(path.empty()) break;
            std::ifstream in(path,std::ios::binary); if(!in) throw std::runtime_error("Cannot open recording.");
            auto candidate=pt::readTrace(in); // validate fully before replacing current work
            if(!mayDiscard()) break;
            session=std::move(candidate); loaded=true; dirty=false; replaying=false; view.zoom=1;
            view.local=pt::recordedComparisonSettings(session); view.comparisonView=2;
            rebuild(); view.selected=0;
            notice=L"Loaded: all-algorithm grid. [ / ] choose stroke; 1-5 choose candidate; D exaggerates differences."; break;
        }
        case Samples: case MotionCsv: {
            pause(); const auto path=chooseFile(window,true,true); if(path.empty()) break;
            if(id==Samples) atomicWrite(path,[&](std::ostream& out){pt::writeSamplesCsv(out,session);});
            else {
                pt::Processor full(pt::clockCalibration(session)); for(const auto& s:session.samples) full.consume(s);
                atomicWrite(path,[&](std::ostream& out){pt::writeRawMotionCsv(out,full);});
            }
            notice=L"CSV exported. Full original recording remains available."; break;
        }
        case LabSummary: case LabPaths: case LabSweep: {
            pause(); const auto path=chooseFile(window,true,true); if(path.empty()) break;
            pt::Processor full(pt::clockCalibration(session)); for(const auto& s:session.samples) full.consume(s);
            atomicWrite(path,[&](std::ostream& out){pt::writeComparisonCsv(out,full,view.local,id==LabPaths,id==LabSweep);});
            notice=L"All candidates exported independently with settings/provenance. Source recording unchanged."; break;
        }
        case Notes:
            pause(); if(editNotes(window,session.metadata)) dirty=true; break;
        case ShowLog: {
            pause(); std::wostringstream text;
            text<<L"Last 20 events. The complete log is retained in .pentrace files.\n\n";
            const auto first=session.events.size()>20?session.events.size()-20:0;
            for(std::size_t i=first;i<session.events.size();++i) {
                const auto& e=session.events[i]; text<<std::fixed<<std::setprecision(3)<<e.time<<L"s ["<<e.code<<L"] "<<widen(e.text)<<L"\n";
            }
            MessageBoxW(window,text.str().c_str(),L"Diagnostic events",MB_OK|MB_ICONINFORMATION); break;
        }
        case Exit: SendMessageW(window,WM_CLOSE,0,0); break;
        case Pause:
            if(live) pause();
            else if(loaded) notice=L"Loaded data is read-only. Start a new recording to draw.";
            else if(view.zoom!=1) notice=L"Return inspection zoom to 1x before resuming capture.";
            else {
                if(replaying) { replaying=false; rebuild(); }
                if(view.comparisonView==2 || view.comparisonView==3) view.comparisonView=0;
                live=true;
                log(input.now(),0,"Test: "+narrow(testName(view.test)));
                log(input.now(),0,"Intended pace: "+std::to_string(view.speed)+" (0 slow, 1 normal, 2 fast; label only)");
                log(input.now(),0,pt::comparisonSettings(view.local));
                notice=L"Live capture resumed. Draw with your real pen.";
            }
            break;
        case Replay: case ReplayFast:
            pause(); if(session.samples.empty()) break;
            cursor=0; processor.setClockCalibration(pt::clockCalibration(session)); renderer.invalidateCache(); replaying=true;
            speed=id==Replay?1:4; replayStart=input.now(); replayOrigin=session.samples.front().receipt; break;
        case Stop:
            pause(); replaying=false; rebuild(); break;
        case Raw: view.raw=!view.raw; break;
        case Filtered: view.filtered=!view.filtered; break;
        case Dots: view.dots=!view.dots; break;
        case SelectedOnly: view.selectedOnly=!view.selectedOnly; break;
        case Touch: view.touch=!view.touch; break;
        case Mouse: input.cancelAll("Mouse capture setting changed."); view.mouse=!view.mouse; break;
        case Zoom: pause(); view.zoom=view.zoom==1?2.0f:view.zoom==2?4.0f:1.0f; break;
        case Previous: case Next: {
            const auto n=processor.strokes().size(); if(!n) break;
            const auto selected=std::min(view.selected,n-1);
            view.selected=id==Previous?(selected?selected-1:0):std::min(selected+1,n-1); break;
        }
        case Help:
            pause();
            MessageBoxW(window,L"Draw the same real-pen test slowly, normally, and quickly. Raw input is always retained.\n\n"
                L"Session > Notes: enter device, pen, speed and guide details.\n"
                L"Save .pentrace to preserve all recorded reports, including hover/up and duplicates.\n"
                L"Replay and change filters to compare identical strokes. CSV exports are for analysis.\n\n"
                L"Solid blue = reported pen; coloured dashed = selected comparison candidate.\n"
                L"Real-pen tests / F2: grey tracing guides only; no strokes are generated. F3: intended pace.\n"
                L"Compare: all five candidates run independently, never stacked. 1-5 selects a candidate.\n"
                L"G: side-by-side grid. D: difference offsets exaggerated x8 (not actual geometry).\n"
                L"C: selected/live view. O: all overlays. Grid/difference pause capture.\n"
                L"Local filters revise a bounded tail; One Euro/buffered filters can lag/end short.\n"
                L"Offline Gaussian waits for a clean pen-up and is not a live-ink solution.\n"
                L"Compare exports all settings, multi-scale metrics and paths; sweep tries more settings.\n"
                L"Old recordings with clock calibration are reanalyzed without rewriting their source data.\n"
                L"Motion CSV includes speed and X/Y velocity, derived from valid report intervals.\n"
                L"Mouse capture is opt-in. Touch remains recorded when hidden.\n"
                L"Zoom is inspection-only and pauses capture. Space resumes at 1x.\n\n"
                L"Mouse wheel / Page Up / Page Down scroll the statistics sidebar.\n\n"
                L"Straightness is meaningful only for intended straight lines. These are Windows reports,\n"
                L"not electrical sensor signals. This app cannot separate hand movement from device error.\n"
                L"No prediction, no auto-straightening, no pressure-shaped brush.\n\n"
                L"All data stays local. No drivers, system settings or hardware are modified.\n"
                L"See docs/TESTING.md and docs/DESIGN.md for limits and acceptance checks.",L"Pen Trace Lab",MB_OK|MB_ICONINFORMATION); break;
        default: break;
        }
        refreshMenus(); InvalidateRect(window,nullptr,FALSE);
    }
    LRESULT handle(UINT message,WPARAM wParam,LPARAM lParam) {
        if(ready && input.handle(window,message,wParam,lParam)) return 0;
        switch(message) {
        case WM_CREATE:
            if(FAILED(renderer.initialize(window))) return -1;
            ready=true; SetMenu(window,menus()); updateLayout();
            if(!EnableMouseInPointer(TRUE)) notice=L"Mouse-as-pointer unavailable; pen/touch capture still enabled.";
            session.metadata="Pen Trace Lab 0.4.0; Windows native pointer API; coordinates: canvas DIPs. Device: unknown; pen: unknown.";
            processor.setClockCalibration(input.calibration());
            recordEnvironment();
            SetTimer(window,1,16,nullptr); refreshMenus(); return 0;
        case WM_TIMER:
        {
            const bool advancing=replaying;
            if(replaying) {
                const double until=replayOrigin+(input.now()-replayStart)*speed;
                unsigned budget=10000;
                while(cursor<session.samples.size() && session.samples[cursor].receipt<=until && budget--) processor.consume(session.samples[cursor++]);
                if(cursor==session.samples.size()) { replaying=false; notice=L"Replay complete. Original reports unchanged."; }
            }
            if(live || advancing) InvalidateRect(window,nullptr,FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{}; BeginPaint(window,&ps);
            HRESULT result=S_OK;
            try {
                std::wostringstream status;
                status<<(replaying?L"REPLAY":live?L"LIVE":L"PAUSED")<<L" | "<<session.samples.size()<<L" reports | "<<view.zoom<<L"x | "<<session.events.size()<<L" log events";
                status<<L" | paint call "<<std::fixed<<std::setprecision(1)<<renderer.lastPaintMs()<<L" ms (not pen latency)";
                if(!notice.empty()) status<<L" | "<<notice;
                result=renderer.paint(processor,view,status.str(),widen(session.metadata));
            } catch(...) { EndPaint(window,&ps); throw; }
            EndPaint(window,&ps);
            if(result==D2DERR_RECREATE_TARGET) InvalidateRect(window,nullptr,FALSE);
            if(FAILED(result) && result!=D2DERR_RECREATE_TARGET) notice=L"Renderer failed; recording can still be saved.";
            return 0;
        }
        case WM_SIZE:
            if(ready) { input.cancelAll("Window resized; active strokes canceled to avoid coordinate jumps."); renderer.resize(); updateLayout(); }
            InvalidateRect(window,nullptr,FALSE); return 0;
        case WM_ENTERSIZEMOVE: input.cancelAll("Window move/resize began."); return 0;
        case WM_DPICHANGED: {
            input.cancelAll("Display DPI changed.");
            const auto* r=reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
            renderer.resize(); updateLayout(); return 0;
        }
        case WM_DISPLAYCHANGE: input.cancelAll("Display configuration changed."); updateLayout(); return 0;
        case WM_DEVICECHANGE: input.cancelAll("Device configuration changed."); return 0;
        case WM_GETMINMAXINFO: {
            auto* info=reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi=GetDpiForWindow(window);
            info->ptMinTrackSize={MulDiv(900,static_cast<int>(dpi?dpi:96),96),MulDiv(600,static_cast<int>(dpi?dpi:96),96)};
            return 0;
        }
        case WM_MOUSEWHEEL: case WM_POINTERWHEEL:
            view.sidebarScroll=std::clamp(view.sidebarScroll-GET_WHEEL_DELTA_WPARAM(wParam)/120.0f*48,0.0f,2000.0f);
            InvalidateRect(window,nullptr,FALSE); return 0;
        case WM_COMMAND: command(LOWORD(wParam)); return 0;
        case WM_KEYDOWN: {
            const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
            if(ctrl && wParam=='S') command(Save);
            else if(ctrl && wParam=='O') command(Open);
            else if(ctrl && wParam=='N') command(New);
            else if(wParam==VK_SPACE) command(Pause);
            else if(wParam>='1' && wParam<='5') command(Candidate0+static_cast<UINT>(wParam-'1'));
            else if(wParam=='G') command(LabGrid);
            else if(wParam=='D') command(LabDifference);
            else if(wParam=='C') command(LabSelected);
            else if(wParam=='O') command(LabOverlay);
            else if(wParam=='Z') command(Zoom);
            else if(wParam==VK_OEM_4) command(Previous);
            else if(wParam==VK_OEM_6) command(Next);
            else if(wParam==VK_F1) command(Help);
            else if(wParam==VK_F2) command(NextTest);
            else if(wParam==VK_F3) command(NextPace);
            else if(wParam==VK_NEXT || wParam==VK_PRIOR) {
                view.sidebarScroll=std::clamp(view.sidebarScroll+(wParam==VK_NEXT?100.0f:-100.0f),0.0f,2000.0f);
                InvalidateRect(window,nullptr,FALSE);
            }
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_CLOSE: if(mayDiscard()) DestroyWindow(window); return 0;
        case WM_DESTROY: KillTimer(window,1); PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window,message,wParam,lParam);
        }
    }
};
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if(!app) return DefWindowProcW(window,message,wParam,lParam);
    try { return app->handle(message,wParam,lParam); }
    catch(const std::exception& e) {
        app->live=false;
        MessageBoxW(window,widen(e.what()).c_str(),L"Pen Trace Lab — operation failed",MB_OK|MB_ICONERROR);
        return message==WM_CREATE?-1:0;
    }
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    App app;
    WNDCLASSW wc{}; wc.lpfnWndProc=windowProc; wc.hInstance=instance;
    wc.lpszClassName=L"PenTraceLabMain"; wc.hCursor=LoadCursorW(nullptr,IDC_CROSS);
    if(!RegisterClassW(&wc)) { if(SUCCEEDED(com)) CoUninitialize(); return 1; }
    RECT work{0,0,1280,900}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const auto dpi=static_cast<int>(GetDpiForSystem());
    const int width=std::min(MulDiv(1200,dpi,96),static_cast<int>(work.right-work.left)-32);
    const int height=std::min(MulDiv(850,dpi,96),static_cast<int>(work.bottom-work.top)-32);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Pen Trace Lab",WS_OVERLAPPEDWINDOW,work.left+16,work.top+16,width,height,nullptr,nullptr,instance,&app);
    if(!window) { MessageBoxW(nullptr,L"Could not initialize Pen Trace Lab.",L"Startup error",MB_OK|MB_ICONERROR); if(SUCCEEDED(com)) CoUninitialize(); return 1; }
    ShowWindow(window,show); UpdateWindow(window);
    MSG msg{}; BOOL result;
    while((result=GetMessageW(&msg,nullptr,0,0))>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if(SUCCEEDED(com)) CoUninitialize();
    return result<0?1:static_cast<int>(msg.wParam);
}
