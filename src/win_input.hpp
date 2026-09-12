#pragma once
#include "core.hpp"
#include <windows.h>
#include <functional>
#include <map>

class WindowsInput {
public:
    using Emit=std::function<void(const pt::Sample&)>;
    using Log=std::function<void(double,DWORD,const std::string&)>;
    WindowsInput(Emit emit,Log log);
    bool handle(HWND window,UINT message,WPARAM wParam,LPARAM lParam);
    void cancelAll(const char* reason);
    double now() const;
    pt::ClockCalibration calibration() const { return {static_cast<std::uint64_t>(start_.QuadPart),static_cast<std::uint64_t>(frequency_.QuadPart)}; }
    std::string clockDescription() const;
    // Canvas origin is in client DIPs; acquisition is independent of inspection zoom.
    void setCanvas(double left,double top,double right,double bottom);
private:
    Emit emit_; Log log_;
    LARGE_INTEGER start_{},frequency_{};
    std::uint64_t sequence_{};
    std::map<std::uint32_t,pt::Sample> active_;
    std::map<std::uintptr_t,unsigned> clockWarnings_;
    double left_{20},top_{90},right_{800},bottom_{600};
    template<class Info,class Getter,class Latest>
    void history(HWND window,UINT message,UINT32 id,Getter getter,Latest latest);
    pt::Sample base(HWND window,UINT message,const POINTER_INFO& info,UINT32 batchIndex,UINT32 count);
    void deliver(pt::Sample sample);
    void cancelPointer(std::uint32_t id,const char* reason);
};
