#pragma once
#include "core.hpp"
#include <iosfwd>

namespace pt {
enum class Algorithm { Raw,Local,OneEuro,Buffered,OfflineGaussian };
struct LocalOptions {
    double radius{12},window{.120},cap{4};
    bool operator==(const LocalOptions&) const = default;
};
struct Candidate {
    std::string id,name;
    Algorithm algorithm{};
    double radius{},window{},cap{},cutoff{},beta{},derivative{12};
};
constexpr unsigned candidateCount=6;
std::array<Candidate,candidateCount> candidates(LocalOptions local={});
std::vector<Candidate> sweepCandidates();
std::string comparisonSettings(LocalOptions options);
LocalOptions recordedComparisonSettings(const Session& session);
const char* executionKind(Algorithm algorithm);
struct Comparison {
    Candidate candidate;
    bool available{};
    std::vector<Vec> path;
    Metrics metrics;
    std::array<Variation,3> variation; // 5, 10 and 20 DIP spans
    double lagMeanMs{},lagP95Ms{},turnDisplacementMax{},loopAreaRatio{};
    std::size_t lagSamples{},turnSamples{};
    bool loopAreaAvailable{};
};
Comparison compare(const Stroke& stroke,const Candidate& candidate,bool score=true);
std::array<Comparison,candidateCount> compareAll(const Stroke& stroke,LocalOptions options={},bool score=true);
// Every candidate starts from original normalized samples, never another result.
void writeComparisonCsv(std::ostream& out,const Processor& processor,LocalOptions options={},bool paths=false,bool sweep=false);
}
