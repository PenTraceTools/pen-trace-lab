#include "core.hpp"
#include "compare.hpp"
#include "trace_io.hpp"
#include <filesystem>
#include <fstream>
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
        pt::writeComparisonCsv(std::cout,processor,pt::recordedComparisonSettings(session),format=="--paths",format=="--sweep");
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    if(argc<2 || argc>3) { std::cerr<<"Usage: pentrace_analyze <recording.pentrace> [--compare|--paths|--sweep]\n"; return 2; }
    const auto format=argc==3?std::filesystem::path(argv[2]).string():"--compare";
    if(format!="--compare" && format!="--paths" && format!="--sweep") {
        std::cerr<<"Unknown output format\n"; return 2;
    }
    return analyze(std::filesystem::path(argv[1]),format);
}
