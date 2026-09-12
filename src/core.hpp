#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace pt {
constexpr std::size_t maxSamples = 500000;
constexpr std::size_t maxEvents = 50000;
struct Vec {
    double x{}, y{};
    Vec operator+(Vec p) const { return {x+p.x, y+p.y}; }
    Vec operator-(Vec p) const { return {x-p.x, y-p.y}; }
    Vec operator*(double s) const { return {x*s, y*s}; }
    bool operator==(const Vec&) const = default;
};
double length(Vec p);
bool finite(Vec p);
Vec mapHimetric(Vec reported,const std::array<std::int32_t,4>& device,const std::array<std::int32_t,4>& display);
Vec toCanvas(Vec screen,Vec clientOrigin,double dpi,Vec canvasOrigin);
enum class Kind : unsigned { Pen=1, Touch=2, Mouse=3 };
enum class Clock : unsigned { Qpc=1, Milliseconds=2, ReceiptFallback=3 };
enum class Mode : unsigned { Off=0, Gentle=1, Steady=2, Strong=3 };
const char* modeName(Mode mode);
const char* kindName(Kind kind);

// A Windows report plus its acquisition envelope. Original fields are retained.
// Coordinates p are canvas-relative DIPs (96 logical units/inch, NOT measured mm).
struct Sample {
    std::uint64_t sequence{}, device{}, qpc{};
    std::uint32_t pointer{}, frame{}, message{}, flags{}, buttonChange{}, milliseconds{};
    std::uint32_t historyCount{1}, advertisedHistoryCount{1}, batchIndex{}, mask{}, toolFlags{}, pressure{};
    std::int32_t tiltX{}, tiltY{};
    std::uint32_t rotation{}, orientation{};
    Kind kind{Kind::Pen};
    Clock clock{Clock::ReceiptFallback};
    double time{}, receipt{};
    Vec p{};
    // pixel, pixelRaw, himetric, himetricRaw; each pair is screen/device data.
    std::array<std::int32_t,8> coordinates{};
    // device RECT, display RECT, contact RECT, contactRaw RECT.
    std::array<std::int32_t,16> rectangles{};
    std::int32_t originX{}, originY{};
    std::uint32_t dpi{96};
    bool mapped{}, contact{}, down{}, up{}, canceled{}, eligible{true};
    // Capture loss is an application marker, not a fabricated device report.
    bool boundary{};
};
struct Event { double time{}; std::uint32_t code{}; std::string text; };
struct Session {
    std::string metadata{"Device: unknown; pen: unknown; test: free drawing"};
    std::vector<Sample> samples;
    std::vector<Event> events;
};
struct Stroke {
    Kind kind{Kind::Pen};
    std::uint32_t pointer{};
    std::uint64_t device{};
    std::vector<Sample> points; // in-contact measured samples only
    bool ended{}, canceled{}, recoveredStart{};
};
struct Diagnostics {
    std::size_t reports{}, duplicates{}, nonIncreasingTimes{}, gaps{}, invalid{}, boundaries{},limits{};
};

// Stateful chronological replay; timestamp equality does not imply duplication.
class Processor {
public:
    void consume(const Sample& s);
    void clear();
    const std::vector<Stroke>& strokes() const { return strokes_; }
    const Diagnostics& diagnostics() const { return diagnostics_; }
private:
    using Key = std::pair<std::uint64_t,std::uint32_t>;
    struct State { std::deque<Sample> recent; std::size_t active{static_cast<std::size_t>(-1)}; };
    std::map<Key,State> states_;
    std::vector<Stroke> strokes_;
    Diagnostics diagnostics_;
};

struct Metrics {
    std::size_t n{}, intervals{}, nonIncreasing{}, gaps{};
    double rms{}, p95{}, maximum{}, pathLength{}, duration{}, meanIntervalMs{}, p95IntervalMs{};
    double displacementRms{}, displacementMax{}, endpointDisplacement{};
    std::size_t speedIntervals{},invalidSpeedIntervals{};
    double meanSpeed{},p95Speed{},maxSpeed{},speedDuration{},lastSpeed{};
    bool lastSpeedValid{};
    bool lineDefined{};
};
enum class MotionStatus { First,Valid,NonIncreasingTime,Gap,ReceiptClock,ClockChanged,PointerChanged,CoordinateSpaceChanged,InvalidData };
struct Motion {
    double dt{};
    Vec velocity{}; // DIPs per second, interval ending at this sample
    double speed{};
    MotionStatus status{MotionStatus::First};
    bool valid() const { return status==MotionStatus::Valid; }
};
const char* motionStatusName(MotionStatus status);
std::vector<Motion> motion(const Stroke& stroke,const std::vector<Vec>& path);
std::vector<Vec> filter(const Stroke& stroke, Mode mode);
Metrics measure(const Stroke& stroke, const std::vector<Vec>& path);
// Experimental Catmull-Rom comparison only; never replaces the source samples.
std::vector<Vec> curve(const std::vector<Vec>& p, unsigned subdivisions=8);
double curveDeviation(const std::vector<Vec>& source, const std::vector<Vec>& fitted, unsigned subdivisions=8);
}
