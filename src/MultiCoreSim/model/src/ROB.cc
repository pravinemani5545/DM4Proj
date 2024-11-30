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
    if (m_current_cycle >= 60) {
        std::cout << "\n[ROB] ========== Final state at cycle limit (60) ==========" << std::endl;
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
    
    // Print current ROB state
    if (!m_rob_q.empty()) {
        std::cout << "[ROB] Current ROB queue state:" << std::endl;
        for (size_t i = 0; i < m_rob_q.size(); i++) {
            const ROBEntry& entry = m_rob_q[i];
            std::cout << "[ROB]   [" << i << "] msgId=" << entry.request.msgId 
                      << " type=" << (int)entry.request.type
                      << " ready=" << entry.ready 
                      << " cycle=" << entry.allocate_cycle << std::endl;
        }
    }
    
    // As per 3.4, retire instructions every cycle
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
    return true;
}

void ROB::retire() {
    if (m_rob_q.empty()) {
        std::cout << "[ROB] No entries to retire" << std::endl;
        return;
    }
    
    std::cout << "[ROB] Starting retirement at cycle " << m_current_cycle << std::endl;
    
    // As per 3.4: ROB is the only class architecturally retiring instructions
    // Must maintain program order, so check from top of ROB queue
    uint32_t retired = 0;
    
    // Keep retiring until we hit IPC limit or a non-ready instruction
    while (!m_rob_q.empty() && retired < IPC) {
        ROBEntry& head = m_rob_q.front();
        
        std::cout << "[ROB] Examining head entry: msgId=" << head.request.msgId 
                  << " type=" << (int)head.request.type
                  << " ready=" << head.ready 
                  << " allocated at cycle " << head.allocate_cycle << std::endl;
        
        // Stop at first non-ready instruction to maintain program order
        if (!head.ready) {
            std::cout << "[ROB] Head entry not ready (request " << head.request.msgId 
                      << "), stopping retirement to maintain program order" << std::endl;
            break;
        }
        
        std::cout << "[ROB] Architecturally retiring request " << head.request.msgId 
                  << " type=" << (int)head.request.type 
                  << " allocated at cycle " << head.allocate_cycle 
                  << " retired at cycle " << m_current_cycle << std::endl;
        
        // For stores, notify LSQ that store can be written to cache
        if (head.request.type == CpuFIFO::REQTYPE::WRITE && m_lsq) {
            std::cout << "[ROB] Notifying LSQ to commit store " << head.request.msgId << std::endl;
            m_lsq->commit(head.request.msgId);
        }
        
        // Remove from ROB after architectural retirement
        m_rob_q.erase(m_rob_q.begin());
        m_num_entries--;
        retired++;
        
        std::cout << "[ROB] Successfully retired instruction " << head.request.msgId 
                  << " (" << retired << "/" << IPC << " this cycle)" << std::endl;
    }
    
    if (retired > 0) {
        std::cout << "[ROB] Architecturally retired " << retired << " instructions this cycle" 
                  << ", remaining entries: " << m_num_entries << std::endl;
    }
    
    std::cout << "[ROB] Retirement complete for cycle " << m_current_cycle << std::endl;
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