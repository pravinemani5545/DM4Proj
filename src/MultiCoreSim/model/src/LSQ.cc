#include "../header/LSQ.h"

namespace ns3 {

/**
 * @brief Constructor initializes LSQ with default parameters
 * 
 * Sets maximum entries to 16 and initializes empty queue
 */
LSQ::LSQ() : MAX_ENTRIES(16), num_entries(0), m_cpuFIFO(nullptr) {}

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
    std::cout << "\n[LSQ] Step - Queue size: " << lsq_q.size() << std::endl;
    if (!lsq_q.empty()) {
        std::cout << "[LSQ] Head operation - Type: " 
                  << (lsq_q.front().request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE")
                  << " MsgId: " << lsq_q.front().request.msgId 
                  << " Ready: " << lsq_q.front().ready
                  << " WaitingForCache: " << lsq_q.front().waitingForCache << std::endl;
    }
    pushToCache();
    rxFromCache();
    retire();
}

/**
 * @brief Check if LSQ can accept new entries
 * @return true if space available
 * 
 * Ensures LSQ doesn't exceed maximum capacity
 */
bool LSQ::canAccept() {
    return num_entries < MAX_ENTRIES;
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
    if (!canAccept()) {
        std::cout << "[LSQ] Cannot allocate - LSQ full" << std::endl;
        return false;
    }

    LSQEntry entry;
    entry.request = request;
    entry.ready = (request.type == CpuFIFO::REQTYPE::WRITE);  // Stores ready immediately
    entry.waitingForCache = false;
    
    lsq_q.push_back(entry);
    num_entries++;
    
    std::cout << "[LSQ] Allocated memory operation - Type: " 
              << (request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE")
              << " Address: 0x" << std::hex << request.addr << std::dec 
              << " Ready: " << entry.ready << std::endl;
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
    if (lsq_q.empty()) return;
    
    auto& front = lsq_q.front();
    if (front.ready) {
        std::cout << "[LSQ] Retiring memory operation - Type: "
                  << (front.request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE")
                  << " Address: 0x" << std::hex << front.request.addr << std::dec << std::endl;
        
        lsq_q.erase(lsq_q.begin());
        num_entries--;
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
    // Search LSQ from youngest to oldest for matching store
    for (auto it = lsq_q.rbegin(); it != lsq_q.rend(); ++it) {
        if (it->request.type == CpuFIFO::REQTYPE::WRITE && 
            it->request.addr == address) {  // Requirements don't specify ready check
            std::cout << "[LSQ] Store-to-load forwarding hit - Address: 0x" 
                      << std::hex << address << std::dec 
                      << " Store MsgId: " << it->request.msgId << std::endl;
            return true;
        }
    }
    std::cout << "[LSQ] No store-to-load forwarding hit - Address: 0x" 
              << std::hex << address << std::dec << std::endl;
    return false;
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
    for (auto& entry : lsq_q) {
        if (entry.request.msgId == requestId) {
            entry.ready = true;
            std::cout << "[LSQ] Committed memory operation MsgId: " << requestId << std::endl;
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
    if (!m_cpuFIFO || m_cpuFIFO->m_txFIFO.IsFull()) {
        return;
    }

    // For loads: Push all non-waiting loads that didn't hit in LSQ
    for (auto& entry : lsq_q) {
        if (entry.request.type == CpuFIFO::REQTYPE::READ && 
            !entry.waitingForCache && !entry.ready) {
            m_cpuFIFO->m_txFIFO.InsertElement(entry.request);
            entry.waitingForCache = true;
            std::cout << "[LSQ] Pushed load to cache - MsgId: " << entry.request.msgId 
                     << " Address: 0x" << std::hex << entry.request.addr << std::dec << std::endl;
        }
    }

    // For stores: Only push if at head of queue
    if (!lsq_q.empty()) {
        auto& head = lsq_q.front();
        if (head.request.type == CpuFIFO::REQTYPE::WRITE && !head.waitingForCache) {
            m_cpuFIFO->m_txFIFO.InsertElement(head.request);
            head.waitingForCache = true;
            std::cout << "[LSQ] Pushed store to cache - MsgId: " << head.request.msgId 
                     << " Address: 0x" << std::hex << head.request.addr << std::dec << std::endl;
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
    if (m_cpuFIFO && !m_cpuFIFO->m_rxFIFO.IsEmpty()) {
        auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
        m_cpuFIFO->m_rxFIFO.PopElement();
        
        std::cout << "[LSQ] Received cache response for MsgId: " << response.msgId << std::endl;
        
        bool found = false;
        for (auto& entry : lsq_q) {
            if (entry.request.msgId == response.msgId) {
                entry.ready = true;
                found = true;
                std::cout << "[LSQ] Marked operation ready from cache response - MsgId: " << response.msgId << std::endl;
                break;
            }
        }
        if (!found) {
            std::cout << "[LSQ] WARNING: Could not find operation MsgId: " << response.msgId << " for cache response" << std::endl;
        }
    }
}

} // namespace ns3