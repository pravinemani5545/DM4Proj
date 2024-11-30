#include "LSQ.h"
#include "ns3/type-id.h"

namespace ns3 {

TypeId LSQ::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::LSQ")
        .SetParent<Object>()
        .AddConstructor<LSQ>();
    return tid;
}

LSQ::LSQ(uint32_t size) : maxSize(size) {}

LSQ::~LSQ() {
    addressMap.clear();
}

bool LSQ::isFull() const {
    return queue.size() >= maxSize;
}

bool LSQ::isEmpty() const {
    return queue.empty();
}

bool LSQ::allocate(const CpuFIFO::ReqMsg& request) {
    if (isFull()) {
        return false;
    }

    LSQEntry entry;
    entry.request = request;
    entry.ready = false;
    entry.id = request.msgId;

    queue.push(entry);
    
    // If it's a store instruction, add it to the address map
    if (request.type == CpuFIFO::REQTYPE::WRITE) {
        addressMap[request.addr] = &queue.back();
    }
    
    return true;
}

bool LSQ::retire(const CpuFIFO::ReqMsg& request) {
    if (isEmpty() || !queue.front().ready) {
        return false;
    }

    // If it's a store, remove it from the address map
    if (queue.front().request.type == CpuFIFO::REQTYPE::WRITE) {
        addressMap.erase(queue.front().request.addr);
    }

    queue.pop();
    return true;
}

bool LSQ::hasStore(uint64_t address) const {
    return addressMap.find(address) != addressMap.end();
}

void LSQ::pushToCache() {
    // Implementation would interact with the cache controller
    // This is a placeholder for the actual implementation
}

void LSQ::rxFromCache() {
    // Implementation would handle receiving data from cache
    // This is a placeholder for the actual implementation
}

bool LSQ::lsqJitFwd(uint64_t address) const {
    auto it = addressMap.find(address);
    if (it != addressMap.end()) {
        return it->second->ready;  // Return true if the store is ready
    }
    return false;
}

void LSQ::commit(uint64_t requestId) {
    std::queue<LSQEntry> temp;
    while (!queue.empty()) {
        LSQEntry entry = queue.front();
        queue.pop();
        
        if (entry.id == requestId) {
            entry.ready = true;
            // Update the entry in addressMap if it's a store
            if (entry.request.type == CpuFIFO::REQTYPE::WRITE) {
                addressMap[entry.request.addr]->ready = true;
            }
        }
        temp.push(entry);
    }
    
    queue = temp;
}

uint32_t LSQ::size() const {
    return queue.size();
}

uint32_t LSQ::getMaxSize() const {
    return maxSize;
}

} // namespace ns3 