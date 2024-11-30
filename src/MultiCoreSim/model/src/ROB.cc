#include "../header/ROB.h"

namespace ns3 {

/**
 * @brief Constructor initializes ROB with default parameters
 * 
 * Pre-allocates space for maximum entries to avoid reallocations
 */
ROB::ROB() : m_num_entries(0) {
    m_rob_q.reserve(MAX_ENTRIES);
}

ROB::~ROB() {}

/**
 * @brief Main processing step for ROB operations
 * 
 * Called every cycle to retire completed instructions
 * in program order up to IPC limit
 */
void ROB::step() {
    std::cout << "\n[ROB] Step - Queue size: " << m_rob_q.size() << "/" << MAX_ENTRIES << std::endl;
    if (!m_rob_q.empty()) {
        auto& head = m_rob_q.front();
        std::cout << "[ROB] Head entry - Type: " << 
            (head.type == COMPUTE ? "COMPUTE" : (head.type == LOAD ? "LOAD" : "STORE")) <<
            " MsgId: " << head.msgId << 
            " Ready: " << head.ready << std::endl;
    }
    
    retire();  // Try to retire instructions
}

/**
 * @brief Check if ROB can accept new entries
 * @return true if space available
 * 
 * Ensures ROB doesn't exceed maximum capacity
 */
bool ROB::canAccept() {
    return m_num_entries < MAX_ENTRIES;
}

/**
 * @brief Allocate new instruction in ROB
 * @param request Instruction to allocate
 * @return true if allocation successful
 * 
 * Compute instructions are ready immediately
 * Stores and loads must be marked ready by LSQ
 */
bool ROB::allocate(uint64_t msgId, ROBEntryType type) {
    std::cout << "\n[ROB] Attempting to allocate " << 
        (type == COMPUTE ? "COMPUTE" : (type == LOAD ? "LOAD" : "STORE")) <<
        " MsgId: " << msgId << std::endl;
    
    if (!canAccept()) {
        std::cout << "[ROB] Cannot allocate - Queue full" << std::endl;
        return false;
    }
    
    ROBEntry entry;
    entry.msgId = msgId;
    entry.type = type;
    entry.ready = (type == COMPUTE);  // Compute instructions are ready immediately
    
    if (entry.ready) {
        std::cout << "[ROB] Compute instruction marked ready immediately" << std::endl;
    }
    
    m_rob_q.push_back(entry);
    m_num_entries++;
    
    return true;
}

/**
 * @brief Retire completed instructions in program order
 * 
 * Retires up to IPC instructions per cycle
 */
void ROB::retire() {
    while (!m_rob_q.empty()) {
        auto& oldest = m_rob_q.front();
        
        if (!oldest.ready) {
            std::cout << "[ROB] Cannot retire - Head entry not ready MsgId: " << oldest.msgId << std::endl;
            break;
        }
        
        std::cout << "[ROB] Retiring " << 
            (oldest.type == COMPUTE ? "COMPUTE" : (oldest.type == LOAD ? "LOAD" : "STORE")) <<
            " MsgId: " << oldest.msgId << std::endl;
        
        // For stores, notify LSQ that store can be sent to cache
        if (oldest.type == STORE) {
            std::cout << "[ROB] Notifying LSQ that store is committed" << std::endl;
            m_lsq->commit(oldest.msgId);
        }
        
        m_rob_q.pop_front();
        m_num_entries--;
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
void ROB::commit(uint64_t msgId) {
    std::cout << "\n[ROB] Committing MsgId: " << msgId << std::endl;
    bool found = false;
    
    for (auto& entry : m_rob_q) {
        if (entry.msgId == msgId) {
            found = true;
            if (!entry.ready) {
                entry.ready = true;
                std::cout << "[ROB] Marked entry as ready" << std::endl;
            } else {
                std::cout << "[ROB] Entry was already ready" << std::endl;
            }
            break;
        }
    }
    
    if (!found) {
        std::cout << "[ROB] WARNING: Commit request for unknown MsgId: " << msgId << std::endl;
    }
}

} // namespace ns3