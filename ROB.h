#ifndef _ROB_H
#define _ROB_H

#include "ns3/object.h"
#include "MemTemplate.h"
#include <queue>
#include <cstdint>

namespace ns3 {

class ROB : public ns3::Object {
private:
    struct ROBEntry {
        CpuFIFO::ReqMsg request;
        bool ready;
        uint64_t id;
    };

    std::queue<ROBEntry> buffer;
    uint32_t maxSize;
    uint64_t nextId;

public:
    static TypeId GetTypeId(void);
    
    ROB(uint32_t size);
    ~ROB() = default;

    // Core functionality
    bool isFull() const;
    bool isEmpty() const;
    bool allocate(const CpuFIFO::ReqMsg& request);
    bool retire();
    void commit(uint64_t requestId);
    
    // Getters
    uint32_t size() const;
    uint32_t getMaxSize() const;
    const ROBEntry& getHead() const;
};

} // namespace ns3

#endif // _ROB_H 