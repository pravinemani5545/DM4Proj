#include "ROB.h"
#include "ns3/type-id.h"

namespace ns3 {

TypeId ROB::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::ROB")
        .SetParent<Object>()
        .AddConstructor<ROB>();
    return tid;
}

ROB::ROB(uint32_t size) : maxSize(size), nextId(0) {}

bool ROB::isFull() const {
    return buffer.size() >= maxSize;
}

bool ROB::isEmpty() const {
    return buffer.empty();
}

bool ROB::allocate(const CpuFIFO::ReqMsg& request) {
    if (isFull()) {
        return false;
    }

    ROBEntry entry;
    entry.request = request;
    entry.ready = (request.type == CpuFIFO::REQTYPE::COMPUTE);
    entry.id = nextId++;
    
    buffer.push(entry);
    return true;
}

bool ROB::retire() {
    if (isEmpty() || !buffer.front().ready) {
        return false;
    }

    buffer.pop();
    return true;
}

void ROB::commit(uint64_t requestId) {
    std::queue<ROBEntry> temp;
    while (!buffer.empty()) {
        ROBEntry entry = buffer.front();
        buffer.pop();
        
        if (entry.id == requestId) {
            entry.ready = true;
        }
        temp.push(entry);
    }
    
    buffer = temp;
}

uint32_t ROB::size() const {
    return buffer.size();
}

uint32_t ROB::getMaxSize() const {
    return maxSize;
}

const ROB::ROBEntry& ROB::getHead() const {
    return buffer.front();
}

} // namespace ns3 