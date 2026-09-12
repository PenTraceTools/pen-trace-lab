#include "core.hpp"
#include "compare.hpp"
#include "trace_io.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

// Read-only console analysis using exactly the same core as the GUI.
int analyze(const std::filesystem::path& path,const std::string& format) {
    try {
        std::ifstream input(path,std::ios::binary);
        if(!input) throw std::runtime_error("Cannot open recording");
        const auto session=pt::readTrace(input);
        pt::Processor processor(pt::clockCalibration(session));
        for(const auto& s:session.samples) processor.consume(s);
        if(format!="--legacy") {
            pt::writeComparisonCsv(std::cout,processor,pt::recordedComparisonSettings(session),format=="--paths",format=="--sweep");
            return 0;
        }
        const auto& d=processor.diagnostics();
        std::cout<<"Pen Trace Lab 0.3.0; legacy presets; source file unchanged\nReports: "<<d.reports
            <<"; strokes: "<<processor.strokes().size()<<"; recovered clocks: "<<d.recoveredTiming
            <<"; nonincreasing contact times: "<<d.nonIncreasingTimes<<"; gaps: "<<d.gaps<<'\n';
        std::cout<<"stroke\tmode\tpoints\tlocal_rms_dip\tmax_displacement_dip\tendpoint_dip\tmean_speed_dip_s\tinvalid_speed_intervals\n";
        std::cout<<std::fixed<<std::setprecision(5);
        unsigned index=0;
        for(const auto& stroke:processor.strokes()) {
            ++index;
            for(auto mode:{pt::Mode::Off,pt::Mode::Gentle,pt::Mode::Steady,pt::Mode::Strong}) {
                const auto m=pt::measure(stroke,pt::filter(stroke,mode));
                std::cout<<index<<'\t'<<pt::modeName(mode)<<'\t'<<m.n<<'\t';
                if(m.localVariationSamples) std::cout<<m.localVariationRms; else std::cout<<"N/A";
                std::cout<<'\t'<<m.displacementMax<<'\t'<<m.endpointDisplacement<<'\t';
                if(m.speedIntervals) std::cout<<m.meanSpeed; else std::cout<<"N/A";
                std::cout<<'\t'<<m.invalidSpeedIntervals<<'\n';
            }
        }
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    if(argc<2 || argc>3) { std::cerr<<"Usage: pentrace_analyze <recording.pentrace> [--compare|--paths|--sweep|--legacy]\n"; return 2; }
    const auto format=argc==3?std::filesystem::path(argv[2]).string():"--compare";
    if(format!="--compare" && format!="--paths" && format!="--sweep" && format!="--legacy") {
        std::cerr<<"Unknown output format\n"; return 2;
    }
    return analyze(std::filesystem::path(argv[1]),format);
}
