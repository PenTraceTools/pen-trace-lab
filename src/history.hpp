#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace pt {
template<class T> struct HistoryBatch {
    std::vector<T> oldestFirst;
    bool fallback{},retrievalFailed{};
};
// Getter(count*, buffer*) follows Windows history count semantics, including
// success with count > capacity. No callbacks here may pump the message queue.
template<class T,class Getter,class Latest>
HistoryBatch<T> readHistory(Getter get,Latest latest) {
    HistoryBatch<T> result;
    std::uint32_t count=0;
    if(get(&count,nullptr) && count>0 && count<=65536) {
        for(unsigned attempt=0;attempt<3;++attempt) {
            result.oldestFirst.resize(count);
            const auto capacity=count;
            if(!get(&count,result.oldestFirst.data())) { result.oldestFirst.clear(); break; }
            if(count>0 && count<=capacity) {
                result.oldestFirst.resize(count);
                std::reverse(result.oldestFirst.begin(),result.oldestFirst.end()); return result;
            }
            result.oldestFirst.clear();
            if(count==0 || count>65536) break;
        }
    }
    result.fallback=true;
    T last{};
    if(latest(&last)) result.oldestFirst.push_back(last);
    else result.retrievalFailed=true;
    return result;
}
}
