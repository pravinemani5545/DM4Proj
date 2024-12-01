#include "../header/ROB.h"
#include "../header/LSQ.h"

namespace ns3 {

ROB::ROB() 
    : m_num_entries(0), 
      m_lsq(nullptr),
      m_current_cycle(0) {
    m_rob_q.reserve(MAX_ENTRIES);
    std::cout << "[ROB] Initialized with " << MAX_ENTRIES << " entries capacity" << std::endl;
}

ROB::~ROB() {}

void ROB::step() {
    // Add cycle limit check
    if (m_current_cycle >= 25) {
        std::cout << "\n[ROB] ========== Final state at cycle limit (200) ==========" << std::endl;
        std::cout << "[ROB] Final ROB state:" << std::endl;
        std::cout << "[ROB] Current entries: " << m_num_entries << "/" << MAX_ENTRIES << std::endl;
        
        if (!m_rob_q.empty()) {
            std::cout << "[ROB] Final ROB queue contents:" << std::endl;
            for (size_t i = 0; i < m_rob_q.size(); i++) {
                const ROBEntry& entry = m_rob_q[i];
                std::cout << "[ROB]   [" << i << "] msgId=" << entry.request.msgId 
                          << " type=" << (int)entry.request.type
                          << " ready=" << entry.ready 
                          << " cycle=" << entry.allocate_cycle << std::endl;
            }
        }
        return;
    }

    std::cout << "\n[ROB] ========== Step at cycle " << m_current_cycle << " ==========" << std::endl;
    std::cout << "[ROB] Current entries: " << m_num_entries << "/" << MAX_ENTRIES << std::endl;
    
    retire();
    m_current_cycle++;
}

bool ROB::canAccept() {
    bool can_accept = m_num_entries < MAX_ENTRIES;
    std::cout << "[ROB] Can accept new entry: " << (can_accept ? "yes" : "no") 
              << " (" << m_num_entries << "/" << MAX_ENTRIES << ")" << std::endl;
    return can_accept;
}

bool ROB::allocate(const CpuFIFO::ReqMsg& request) {
    std::cout << "\n[ROB][ALLOC] ========== Allocation Request ==========" << std::endl;
    std::cout << "[ROB][ALLOC] Current State:" << std::endl;
    std::cout << "[ROB][ALLOC] - Entries: " << m_num_entries << "/" << MAX_ENTRIES << std::endl;
    std::cout << "[ROB][ALLOC] - Request Details:" << std::endl;
    std::cout << "[ROB][ALLOC]   * ID: " << request.msgId << std::endl;
    std::cout << "[ROB][ALLOC]   * Type: " << (request.type == CpuFIFO::REQTYPE::COMPUTE ? "COMPUTE" :
                                               request.type == CpuFIFO::REQTYPE::READ ? "READ" :
                                               request.type == CpuFIFO::REQTYPE::WRITE ? "WRITE" : "OTHER") << std::endl;
    std::cout << "[ROB][ALLOC]   * Address: 0x" << std::hex << request.addr << std::dec << std::endl;
    std::cout << "[ROB][ALLOC]   * Cycle: " << request.cycle << std::endl;

    if (!canAccept()) {
        std::cout << "[ROB][ALLOC] FAILED - ROB full" << std::endl;
        return false;
    }

    ROBEntry entry;
    entry.request = request;
    entry.allocate_cycle = m_current_cycle;
    entry.ready = (request.type == CpuFIFO::REQTYPE::COMPUTE);

    m_rob_q.push_back(entry);
    m_num_entries++;

    std::cout << "[ROB][ALLOC] SUCCESS - Entry allocated" << std::endl;
    std::cout << "[ROB][ALLOC] - Ready: " << (entry.ready ? "Yes" : "No") << std::endl;
    std::cout << "[ROB][ALLOC] - New Size: " << m_num_entries << "/" << MAX_ENTRIES << std::endl;
    return true;
}

void ROB::retire() {
    std::cout << "\n[ROB][RETIRE] ========== Retirement Cycle " << m_current_cycle << " ==========" << std::endl;
    std::cout << "[ROB][RETIRE] Current Queue State:" << std::endl;
    
    if (m_rob_q.empty()) {
        std::cout << "[ROB][RETIRE] Queue empty, nothing to retire" << std::endl;
        return;
    }

    for (size_t i = 0; i < m_rob_q.size(); i++) {
        const ROBEntry& entry = m_rob_q[i];
        std::cout << "[ROB][RETIRE] [" << i << "] ID=" << entry.request.msgId 
                  << " Type=" << (entry.request.type == CpuFIFO::REQTYPE::COMPUTE ? "COMPUTE" :
                                 entry.request.type == CpuFIFO::REQTYPE::READ ? "READ" :
                                 entry.request.type == CpuFIFO::REQTYPE::WRITE ? "WRITE" : "OTHER")
                  << " Ready=" << (entry.ready ? "Yes" : "No")
                  << " Cycle=" << entry.allocate_cycle << std::endl;
    }

    uint32_t retired = 0;
    while (!m_rob_q.empty() && retired < IPC) {
        const ROBEntry& head = m_rob_q.front();
        
        std::cout << "[ROB][RETIRE] Checking head instruction:" << std::endl;
        std::cout << "[ROB][RETIRE] - ID: " << head.request.msgId << std::endl;
        std::cout << "[ROB][RETIRE] - Type: " << (head.request.type == CpuFIFO::REQTYPE::COMPUTE ? "COMPUTE" :
                                                 head.request.type == CpuFIFO::REQTYPE::READ ? "READ" :
                                                 head.request.type == CpuFIFO::REQTYPE::WRITE ? "WRITE" : "OTHER") << std::endl;
        std::cout << "[ROB][RETIRE] - Ready: " << (head.ready ? "Yes" : "No") << std::endl;

        if (!head.ready) {
            std::cout << "[ROB][RETIRE] Cannot retire - head not ready" << std::endl;
            break;
        }

        m_rob_q.erase(m_rob_q.begin());
        m_num_entries--;
        retired++;

        std::cout << "[ROB][RETIRE] Retired instruction " << head.request.msgId << std::endl;
        std::cout << "[ROB][RETIRE] - Retired this cycle: " << retired << "/" << IPC << std::endl;
        std::cout << "[ROB][RETIRE] - Remaining entries: " << m_num_entries << std::endl;
    }

    std::cout << "[ROB][RETIRE] Cycle complete - retired " << retired << " instructions" << std::endl;
}

void ROB::commit(uint64_t requestId) {
    std::cout << "\n[ROB][COMMIT] ========== Commit Request ==========" << std::endl;
    std::cout << "[ROB][COMMIT] Looking for instruction " << requestId << std::endl;
    
    for (auto& entry : m_rob_q) {
        if (entry.request.msgId == requestId) {
            entry.ready = true;
            std::cout << "[ROB][COMMIT] Found and committed instruction:" << std::endl;
            std::cout << "[ROB][COMMIT] - ID: " << requestId << std::endl;
            std::cout << "[ROB][COMMIT] - Type: " << (entry.request.type == CpuFIFO::REQTYPE::COMPUTE ? "COMPUTE" :
                                                     entry.request.type == CpuFIFO::REQTYPE::READ ? "READ" :
                                                     entry.request.type == CpuFIFO::REQTYPE::WRITE ? "WRITE" : "OTHER") << std::endl;
            std::cout << "[ROB][COMMIT] - Allocation Cycle: " << entry.allocate_cycle << std::endl;
            return;
        }
    }
    std::cout << "[ROB][COMMIT] WARNING: Instruction " << requestId << " not found in ROB" << std::endl;
}

} // namespace ns3