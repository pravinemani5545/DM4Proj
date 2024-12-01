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
    if (m_current_cycle >= 200) {
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
    if (!canAccept()) {
        std::cout << "[ROB] Allocation failed - ROB full" << std::endl;
        return false;
    }
    
    ROBEntry entry;
    entry.request = request;
    entry.allocate_cycle = m_current_cycle;
    
    // As per 3.3.1, compute instructions are ready immediately
    entry.ready = (request.type == CpuFIFO::REQTYPE::COMPUTE);
    
    m_rob_q.push_back(entry);
    m_num_entries++;
    
    std::cout << "[ROB] Allocated entry for request " << request.msgId 
              << " type=" << (int)request.type 
              << " ready=" << entry.ready 
              << " at cycle " << m_current_cycle << std::endl;
    
    std::cout << "[ROB] Instruction Flow:" << std::endl;
    std::cout << "[ROB] - Type: " << (request.type == CpuFIFO::REQTYPE::COMPUTE ? "Compute" :
                                     request.type == CpuFIFO::REQTYPE::READ ? "Load" : "Store") << std::endl;
    std::cout << "[ROB] - Ready Status: " << (entry.ready ? "Ready" : "Waiting") << std::endl;
    std::cout << "[ROB] - Queue Position: " << m_num_entries << std::endl;
    
    return true;
}

void ROB::retire() {
    std::cout << "[ROB] Retire check - Queue contents:" << std::endl;
    for (size_t i = 0; i < m_rob_q.size(); i++) {
        const ROBEntry& entry = m_rob_q[i];
        std::cout << "[ROB] Position " << i 
                  << " msgId=" << entry.request.msgId
                  << " type=" << (int)entry.request.type
                  << " ready=" << entry.ready
                  << " cycle=" << entry.allocate_cycle << std::endl;
    }
    
    if (m_rob_q.empty()) {
        std::cout << "[ROB] Cannot retire - queue empty" << std::endl;
        return;
    }
    
    uint32_t retired = 0;
    while (!m_rob_q.empty() && retired < IPC) {
        std::cout << "[ROB] Retire conditions: !empty=" << (!m_rob_q.empty()) 
                  << " retired<IPC=" << (retired < IPC) 
                  << " head.ready=" << m_rob_q.front().ready
                  << " head.type=" << (int)m_rob_q.front().request.type
                  << " head.msgId=" << m_rob_q.front().request.msgId
                  << std::endl;
                  
        ROBEntry& head = m_rob_q.front();
        if (!head.ready) {
            std::cout << "[ROB] Stopping - head not ready" << std::endl;
            break;
        }
        
        m_rob_q.erase(m_rob_q.begin());
        m_num_entries--;
        retired++;
    }
    
    if (retired > 0) {
        std::cout << "[ROB] Retired " << retired << " instructions" << std::endl;
    }
}

void ROB::commit(uint64_t requestId) {
    std::cout << "[ROB] Attempting to commit request " << requestId << std::endl;
    
    for (auto& entry : m_rob_q) {
        if (entry.request.msgId == requestId) {
            entry.ready = true;
            std::cout << "[ROB] Marked request " << requestId << " as ready" << std::endl;
            return;
        }
    }
    
    std::cout << "[ROB] Warning: Request " << requestId << " not found for commit" << std::endl;
}

} // namespace ns3