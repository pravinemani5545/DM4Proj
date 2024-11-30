#include "../header/LSQ.h"

namespace ns3 {

/**
 * @brief Constructor initializes LSQ with default parameters
 * 
 * Sets maximum entries to 16 and initializes empty queue
 */
LSQ::LSQ() : m_num_entries(0), m_cpuFIFO(nullptr), m_rob(nullptr) {
    m_lsq_q.reserve(MAX_ENTRIES);
}

LSQ::~LSQ() {}

/**
 * @brief Main processing step for LSQ operations
 * 
 * Called every cycle to:
 * 1. Push pending stores to cache
 * 2. Process responses from cache
 * 3. Retire completed operations
 */
void LSQ::step() {
    std::cout << "\n[LSQ] Step - Queue size: " << m_lsq_q.size() << "/" << MAX_ENTRIES << std::endl;
    if (!m_lsq_q.empty()) {
        auto& head = m_lsq_q.front();
        std::cout << "[LSQ] Head operation - Type: " << 
            (head.request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") <<
            " MsgId: " << head.request.msgId << 
            " Ready: " << head.ready << 
            " WaitingForCache: " << head.waitingForCache <<
            " CacheAck: " << head.cache_ack << std::endl;
    }
    
    pushToCache();  // Push memory operations to cache
    rxFromCache();  // Process cache responses
    retire();       // Remove completed operations
}

/**
 * @brief Check if LSQ can accept new entries
 * @return true if space available
 * 
 * Ensures LSQ doesn't exceed maximum capacity
 */
bool LSQ::canAccept() {
    return m_num_entries < MAX_ENTRIES;
}

/**
 * @brief Allocate new memory operation in LSQ
 * @param request Memory request to allocate
 * @return true if allocation successful
 * 
 * Stores are marked ready immediately since CPU doesn't wait
 * Loads are marked not ready until data received
 */
bool LSQ::allocate(const CpuFIFO::ReqMsg& request) {
    std::cout << "\n[LSQ] Attempting to allocate " << 
        (request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") <<
        " MsgId: " << request.msgId << 
        " Address: 0x" << std::hex << request.addr << std::dec << std::endl;
    
    if (!canAccept()) {
        std::cout << "[LSQ] Cannot allocate - Queue full" << std::endl;
        return false;
    }
    
    LSQEntry entry;
    entry.request = request;
    entry.waitingForCache = false;
    entry.cache_ack = false;
    
    // For loads, check store forwarding before allocation
    if (request.type == CpuFIFO::REQTYPE::READ) {
        // Search from youngest to oldest for matching store
        for (auto it = m_lsq_q.rbegin(); it != m_lsq_q.rend(); ++it) {
            if (it->request.type == CpuFIFO::REQTYPE::WRITE && 
                it->request.addr == request.addr) {
                std::cout << "[LSQ] Found forwarding opportunity from store MsgId: " << 
                    it->request.msgId << std::endl;
                entry.ready = true;
                break;
            }
        }
    } else if (request.type == CpuFIFO::REQTYPE::WRITE) {
        entry.ready = true;
        std::cout << "[LSQ] Store marked ready immediately" << std::endl;
    }
    
    m_lsq_q.push_back(entry);
    m_num_entries++;
    
    // If load was forwarded or if it's a store, notify ROB
    if (entry.ready) {
        std::cout << "[LSQ] Notifying ROB - Operation ready MsgId: " << request.msgId << std::endl;
        m_rob->commit(request.msgId);
    }
    
    return true;
}

/**
 * @brief Retire completed memory operations
 * 
 * Removes oldest operation if ready:
 * - Loads: ready when data received
 * - Stores: ready when allocated (removed after cache confirms)
 */
void LSQ::retire() {
    if (m_lsq_q.empty()) return;
    
    auto& front = m_lsq_q.front();
    bool can_remove = false;
    
    std::cout << "[LSQ] Checking retirement for head operation:" << std::endl;
    std::cout << "  - Type: " << (front.request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") << std::endl;
    std::cout << "  - MsgId: " << front.request.msgId << std::endl;
    std::cout << "  - Address: 0x" << std::hex << front.request.addr << std::dec << std::endl;
    std::cout << "  - Ready: " << front.ready << std::endl;
    std::cout << "  - WaitingForCache: " << front.waitingForCache << std::endl;
    
    if (front.request.type == CpuFIFO::REQTYPE::READ) {
        can_remove = front.ready;  // Loads removed when ready (data received or forwarded)
        std::cout << "  - Load can retire: " << can_remove << " (ready)" << std::endl;
    } else if (front.request.type == CpuFIFO::REQTYPE::WRITE) {
        can_remove = front.cache_ack;  // Stores removed when cache confirms write
        std::cout << "  - Store can retire: " << can_remove << " (cache confirmed)" << std::endl;
    }
    
    if (can_remove) {
        std::cout << "[LSQ] Removing operation MsgId: " << front.request.msgId << std::endl;
        m_lsq_q.erase(m_lsq_q.begin());
        m_num_entries--;
    }
}

/**
 * @brief Check for store-to-load forwarding
 * @param address Memory address to check
 * @return true if forwarding possible
 * 
 * Searches LSQ from youngest to oldest for matching store
 * Returns true if ready store to same address found
 */
bool LSQ::ldFwd(uint64_t address) {
    std::cout << "\n[LSQ] Checking store forwarding for address: 0x" << std::hex << address << std::dec << std::endl;
    
    // Search from youngest to oldest for matching store
    for (auto it = m_lsq_q.rbegin(); it != m_lsq_q.rend(); ++it) {
        if (it->request.type == CpuFIFO::REQTYPE::WRITE && 
            it->request.addr == address) {
            std::cout << "[LSQ] Found matching store MsgId: " << it->request.msgId << std::endl;
            
            // Found youngest matching store - mark load as ready
            for (auto& entry : m_lsq_q) {
                if (entry.request.type == CpuFIFO::REQTYPE::READ && 
                    entry.request.addr == address &&
                    !entry.ready) {
                    std::cout << "[LSQ] Forwarding to load MsgId: " << entry.request.msgId << std::endl;
                    entry.ready = true;
                    m_rob->commit(entry.request.msgId);
                }
            }
            return true;  // Found and forwarded from youngest store
        }
    }
    
    std::cout << "[LSQ] No matching store found for forwarding" << std::endl;
    return false;  // No matching store found
}

/**
 * @brief Mark operation as complete
 * @param requestId ID of operation to commit
 * 
 * Called when:
 * - Load receives data from cache
 * - Load hits in store buffer (forwarding)
 */
void LSQ::commit(uint64_t requestId) {
    for (auto& entry : m_lsq_q) {
        if (entry.request.msgId == requestId) {
            entry.ready = true;
            break;
        }
    }
}

/**
 * @brief Push oldest store to cache
 * 
 * Sends store operation to cache if:
 * 1. LSQ not empty
 * 2. CPU FIFO available
 * 3. Operation not already waiting
 */
void LSQ::pushToCache() {
    if (!m_cpuFIFO || m_cpuFIFO->m_txFIFO.IsFull() || m_lsq_q.empty()) {
        if (m_cpuFIFO && m_cpuFIFO->m_txFIFO.IsFull()) {
            std::cout << "[LSQ] Cannot push to cache - txFIFO full" << std::endl;
        }
        return;
    }
    
    auto& oldest = m_lsq_q.front();
    if (!oldest.waitingForCache) {
        if (oldest.request.type == CpuFIFO::REQTYPE::WRITE) {
            if (oldest.ready && !oldest.cache_ack) {
                std::cout << "[LSQ] Pushing store to cache - MsgId: " << oldest.request.msgId << 
                    " Address: 0x" << std::hex << oldest.request.addr << std::dec << std::endl;
                m_cpuFIFO->m_txFIFO.InsertElement(oldest.request);
                oldest.waitingForCache = true;
            } else {
                std::cout << "[LSQ] Store not ready for cache - MsgId: " << oldest.request.msgId << 
                    " Ready: " << oldest.ready << " CacheAck: " << oldest.cache_ack << std::endl;
            }
        } else if (oldest.request.type == CpuFIFO::REQTYPE::READ && !oldest.ready) {
            std::cout << "[LSQ] Pushing load to cache - MsgId: " << oldest.request.msgId << 
                " Address: 0x" << std::hex << oldest.request.addr << std::dec << std::endl;
            m_cpuFIFO->m_txFIFO.InsertElement(oldest.request);
            oldest.waitingForCache = true;
        }
    }
}

/**
 * @brief Process responses from cache
 * 
 * Handles cache responses:
 * 1. Get response from CPU FIFO
 * 2. Find matching LSQ entry
 * 3. Mark entry as ready
 */
void LSQ::rxFromCache() {
    if (!m_cpuFIFO || m_cpuFIFO->m_rxFIFO.IsEmpty()) return;
    
    auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
    m_cpuFIFO->m_rxFIFO.PopElement();
    
    std::cout << "[LSQ] Received cache response for MsgId: " << response.msgId << std::endl;
    
    bool found = false;
    for (auto& entry : m_lsq_q) {
        if (entry.request.msgId == response.msgId) {
            found = true;
            if (entry.request.type == CpuFIFO::REQTYPE::READ) {
                std::cout << "[LSQ] Marking load as ready and notifying ROB - MsgId: " << response.msgId << std::endl;
                entry.ready = true;
                m_rob->commit(entry.request.msgId);
            } else if (entry.request.type == CpuFIFO::REQTYPE::WRITE) {
                std::cout << "[LSQ] Marking store as acknowledged by cache - MsgId: " << response.msgId << std::endl;
                entry.cache_ack = true;
            }
            entry.waitingForCache = false;
            break;
        }
    }
    
    if (!found) {
        std::cout << "[LSQ] WARNING: Received cache response for unknown MsgId: " << response.msgId << std::endl;
    }
}

} // namespace ns3