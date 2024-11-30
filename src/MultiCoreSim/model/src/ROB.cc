#include "../header/ROB.h"

namespace ns3 {

/**
 * @brief Constructor initializes ROB with default parameters
 * 
 * Pre-allocates space for maximum entries to avoid reallocations
 */
ROB::ROB() : num_entries(0) {
    rob_q.reserve(MAX_ENTRIES);
}

ROB::~ROB() {}

/**
 * @brief Main processing step for ROB operations
 * 
 * Called every cycle to retire completed instructions
 * in program order up to IPC limit
 */
void ROB::step() {
    retire();
}

/**
 * @brief Check if ROB can accept new entries
 * @return true if space available
 * 
 * Ensures ROB doesn't exceed maximum capacity
 */
bool ROB::canAccept() {
    return num_entries < MAX_ENTRIES;
}

/**
 * @brief Allocate new instruction in ROB
 * @param request Instruction to allocate
 * @return true if allocation successful
 * 
 * Compute instructions marked ready immediately
 * Memory operations marked ready when completed:
 * - Stores: ready when allocated in LSQ
 * - Loads: ready when data available
 */
bool ROB::allocate(const CpuFIFO::ReqMsg& request) {
    if (!canAccept()) {
        std::cout << "[ROB] Cannot allocate - ROB full" << std::endl;
        return false;
    }

    ROBEntry entry;
    entry.request = request;
    entry.ready = (request.type == CpuFIFO::REQTYPE::COMPUTE || 
                  request.type == CpuFIFO::REQTYPE::WRITE);
    
    rob_q.push_back(entry);
    num_entries++;
    
    std::cout << "[ROB] Allocated instruction - Type: " 
              << (request.type == CpuFIFO::REQTYPE::COMPUTE ? "COMPUTE" :
                  request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE")
              << " Ready: " << entry.ready << std::endl;
    return true;
}

/**
 * @brief Retire completed instructions in program order
 * 
 * Retires up to IPC instructions per cycle:
 * 1. Check if head instruction is ready
 * 2. If ready, remove from ROB
 * 3. Stop at first not-ready instruction
 * 4. Stop after IPC instructions retired
 */
void ROB::retire() {
    uint32_t retired = 0;
    
    while (!rob_q.empty() && retired < IPC) {
        if (!rob_q.front().ready) {
            std::cout << "[ROB] Cannot retire - Head instruction not ready" << std::endl;
            break;  // Stop at first not-ready instruction
        }
        
        auto& front = rob_q.front();
        std::cout << "[ROB] Retiring instruction - Type: "
                  << (front.request.type == CpuFIFO::REQTYPE::COMPUTE ? "COMPUTE" :
                      front.request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE")
                  << " MsgId: " << front.request.msgId << std::endl;
        
        rob_q.erase(rob_q.begin());
        num_entries--;
        retired++;
    }
    
    if (retired > 0) {
        std::cout << "[ROB] Retired " << retired << " instructions this cycle" << std::endl;
    }
}

/**
 * @brief Mark instruction as complete
 * @param requestId ID of instruction to commit
 * 
 * Called when:
 * - Load receives data (from cache or forwarding)
 * - Store allocated in LSQ
 * - Compute instruction allocated (immediate)
 */
void ROB::commit(uint64_t requestId) {
    for (auto& entry : rob_q) {
        if (entry.request.msgId == requestId) {
            entry.ready = true;
            std::cout << "[ROB] Committed instruction MsgId: " << requestId << std::endl;
            break;
        }
    }
}

} // namespace ns3