#ifndef _LSQ_H
#define _LSQ_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/type-id.h"
#include "MemTemplate.h"
#include <queue>
#include <unordered_map>
#include <cstdint>

namespace ns3 {

class LSQ : public ns3::Object {
private:
    struct LSQEntry {
        CpuFIFO::ReqMsg request;
        bool ready;
        uint64_t id;
    };

    std::queue<LSQEntry> queue;
    std::unordered_map<uint64_t, LSQEntry*> addressMap;  // Maps addresses to store entries
    uint32_t maxSize;

public:
    static TypeId GetTypeId(void);
    
    LSQ();
    LSQ(uint32_t size);
    ~LSQ();

    // Core functionality
    bool isFull() const;
    bool isEmpty() const;
    bool allocate(const CpuFIFO::ReqMsg& request);
    bool retire(const CpuFIFO::ReqMsg& request);
    
    // Load-Store Queue specific
    bool hasStore(uint64_t address) const;
    void pushToCache();
    void rxFromCache();
    bool ldFwd(uint64_t address) const;  // Load forwarding from youngest matching store
    
    // Commit handling
    void commit(uint64_t requestId);
    
    // Getters
    uint32_t size() const;
    uint32_t getMaxSize() const;
};

} // namespace ns3

#endif // _LSQ_H