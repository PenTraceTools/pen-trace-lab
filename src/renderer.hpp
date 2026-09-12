#pragma once
#include "core.hpp"
#include "compare.hpp"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

struct ViewOptions {
    bool raw{true},filtered{true},dots{},touch{true},mouse{false},selectedOnly{};
    float zoom{1},sidebarScroll{};
    unsigned test{};
    unsigned speed{};
    std::size_t selected{static_cast<std::size_t>(-1)};
    // 0 selected, 1 all overlays, 2 inspection grid, 3 exaggerated difference.
    unsigned comparisonView{},candidate{};
    pt::LocalOptions local;
};
struct Layout { float left{20},top{90},right{800},bottom{600}; };
class Renderer {
public:
    HRESULT initialize(HWND window);
    void resize();
    Layout layout() const;
    HRESULT paint(const pt::Processor& processor,const ViewOptions& view,const std::wstring& status,const std::wstring& metadata);
    void invalidateCache();
    double lastPaintMs() const { return lastPaintMs_; }
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    HWND window_{};
    Ptr<ID2D1Factory> factory_;
    Ptr<ID2D1HwndRenderTarget> target_;
    Ptr<ID2D1SolidColorBrush> brush_;
    Ptr<ID2D1StrokeStyle> dashed_;
    Ptr<IDWriteFactory> textFactory_;
    Ptr<IDWriteTextFormat> normal_,large_;
    struct Cache {
        std::size_t count{};
        std::vector<pt::Vec> raw;
        pt::Metrics rawMetrics;
    };
    std::vector<Cache> cache_;
    std::array<pt::Comparison,pt::candidateCount> comparisons_;
    std::size_t comparisonStroke_{static_cast<std::size_t>(-1)},comparisonPoints_{};
    bool comparisonEnded_{},comparisonCanceled_{};
    pt::LocalOptions comparisonOptions_;
    void prepareComparisons(const pt::Stroke& stroke,std::size_t index,const ViewOptions& view);
    void comparisonGrid(const pt::Stroke& stroke,const ViewOptions& view,Layout layout);
    double lastPaintMs_{};
    HRESULT target();
    void text(const std::wstring& s,D2D1_RECT_F rect,D2D1_COLOR_F color,bool large=false);
    void line(const std::vector<pt::Vec>& points,D2D1_COLOR_F color,float width,bool dashed=false);
};
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
const wchar_t* testName(unsigned test);
const wchar_t* testInstruction(unsigned test);
