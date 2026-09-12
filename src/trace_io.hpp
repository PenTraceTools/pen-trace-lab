#pragma once
#include "core.hpp"
#include <iosfwd>

namespace pt {
void writeTrace(std::ostream& out,const Session& session);
Session readTrace(std::istream& in); // throws on malformed/oversized input; never partially loads
ClockCalibration clockCalibration(const Session& session);
void writeSamplesCsv(std::ostream& out,const Session& session);
void writeRawMotionCsv(std::ostream& out,const Processor& processor);
}
