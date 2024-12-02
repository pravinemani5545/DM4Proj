#include "../header/LSQ.h"
#include "../header/ROB.h"
#include <algorithm>
#include <iostream>

namespace ns3 {

LSQ::LSQ() 
    : m_num_entries(0), 
      m_cpuFIFO(nullptr), 
      m_rob(nullptr),
      m_current_cycle(0) {
    m_lsq_q.reserve(MAX_ENTRIES);
    std::cout << "[LSQ] Initialized with " << MAX_ENTRIES << " entries capacity" << std::endl;
}

LSQ::~LSQ() {}

void LSQ::step() {
    std::cout << "\n[LSQ][STEP] ========== Begin Cycle " << m_current_cycle << " ==========" << std::endl;
    
    // 1. Process cache responses first
    std::cout << "[LSQ][STEP] Processing cache responses" << std::endl;
    rxFromCache();
    
    // 2. Remove completed entries
    std::cout << "[LSQ][STEP] Removing completed entries" << std::endl;
    retire();
    
    // 3. Send new requests to cache
    std::cout << "[LSQ][STEP] Sending new requests to cache" << std::endl;
    pushToCache();
    
    m_current_cycle++;
    std::cout << "[LSQ][STEP] ========== End Cycle " << m_current_cycle << " ==========\n" << std::endl;
}

bool LSQ::canAccept() {
    bool can_accept = m_num_entries < MAX_ENTRIES;
    std::cout << "[LSQ] Can accept new entry: " << (can_accept ? "yes" : "no") 
              << " (" << m_num_entries << "/" << MAX_ENTRIES << ")" << std::endl;
    return can_accept;
}

bool LSQ::allocate(const CpuFIFO::ReqMsg& request) {
    std::cout << "\n[LSQ][ALLOC] ========== Allocation Request ==========" << std::endl;
    std::cout << "[LSQ][ALLOC] Current State:" << std::endl;
    std::cout << "[LSQ][ALLOC] - Entries: " << m_num_entries << "/" << MAX_ENTRIES << std::endl;
    std::cout << "[LSQ][ALLOC] - Request Details:" << std::endl;
    std::cout << "[LSQ][ALLOC]   * ID: " << request.msgId << std::endl;
    std::cout << "[LSQ][ALLOC]   * Type: " << (request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") << std::endl;
    std::cout << "[LSQ][ALLOC]   * Address: 0x" << std::hex << request.addr << std::dec << std::endl;
    std::cout << "[LSQ][ALLOC]   * Cycle: " << request.cycle << std::endl;

    if (!canAccept()) {
        std::cout << "[LSQ][ALLOC] FAILED - LSQ full" << std::endl;
        return false;
    }
    
    LSQEntry entry;
    entry.request = request;
    entry.ready = false;
    entry.waitingForCache = false;
    entry.cache_ack = false;
    
    // Store Instructions commit upon LSQ allocation
    if (request.type == CpuFIFO::REQTYPE::WRITE) {
        entry.ready = true;
        std::cout << "[LSQ][ALLOC] Store commits upon allocation (3.3.2)" << std::endl;
        if (m_rob) {
            m_rob->commit(request.msgId);
        }
    }
    // For loads, check store forwarding
    else if (request.type == CpuFIFO::REQTYPE::READ) {
        if (ldFwd(request.addr)) {
            entry.ready = true;
            std::cout << "[LSQ][ALLOC] Load satisfied by store forwarding" << std::endl;
            if (m_rob) {
                m_rob->commit(request.msgId);
            }
        } else {
            std::cout << "[LSQ][ALLOC] Load needs memory access" << std::endl;
        }
    }
    
    m_lsq_q.push_back(entry);
    m_num_entries++;
    
    std::cout << "[LSQ][ALLOC] SUCCESS - Entry allocated" << std::endl;
    std::cout << "[LSQ][ALLOC] - Ready: " << (entry.ready ? "Yes" : "No") << std::endl;
    std::cout << "[LSQ][ALLOC] - New Size: " << m_num_entries << "/" << MAX_ENTRIES << std::endl;
    return true;
}

void LSQ::commit(uint64_t requestId) {
    auto it = std::find_if(m_lsq_q.begin(), m_lsq_q.end(),
                           [requestId](const LSQEntry& entry) {
                               return entry.request.msgId == requestId;
                           });

    if (it != m_lsq_q.end()) {
        std::cout << "[LSQ] Committed request msgId=" << requestId << std::endl;
        m_lsq_q.erase(it);
        m_num_entries--;
    }
}

bool LSQ::ldFwd(uint64_t address) {
    std::cout << "\n[LSQ][FORWARD] ========== Store-to-Load Forward Check ==========" << std::endl;
    std::cout << "[LSQ][FORWARD] Checking for store to address 0x" << std::hex << address << std::dec << std::endl;
    
    auto youngest_store = std::find_if(m_lsq_q.rbegin(), m_lsq_q.rend(),
        [address](const LSQEntry& entry) {
            return entry.request.type == CpuFIFO::REQTYPE::WRITE && 
                   entry.request.addr == address;
        });
    
    if (youngest_store == m_lsq_q.rend()) {
        std::cout << "[LSQ][FORWARD] No matching store found" << std::endl;
        return false;
    }
    
    std::cout << "[LSQ][FORWARD] Found matching store:" << std::endl;
    std::cout << "[LSQ][FORWARD] - ID: " << youngest_store->request.msgId << std::endl;
    std::cout << "[LSQ][FORWARD] - Address: 0x" << std::hex << youngest_store->request.addr << std::dec << std::endl;
    std::cout << "[LSQ][FORWARD] - Cycle: " << youngest_store->request.cycle << std::endl;
    
    auto store_pos = youngest_store.base();
    for (auto it = store_pos; it != m_lsq_q.end(); ++it) {
        if (it->request.type == CpuFIFO::REQTYPE::READ && 
            it->request.addr == address && 
            !it->ready) {
            it->ready = true;
            std::cout << "[LSQ][FORWARD] Forwarding to load:" << std::endl;
            std::cout << "[LSQ][FORWARD] - ID: " << it->request.msgId << std::endl;
            if (m_rob) {
                m_rob->commit(it->request.msgId);
            }
        }
    }
    return true;
}

void LSQ::pushToCache() {
    std::cout << "\n[LSQ][PUSH] ========== Push To Cache Check ==========" << std::endl;
    
    if (m_lsq_q.empty()) {
        std::cout << "[LSQ][PUSH] LSQ empty - nothing to push" << std::endl;
        return;
    }
    
    if (!m_cpuFIFO) {
        std::cout << "[LSQ][PUSH] No CPU FIFO connected" << std::endl;
        return;
    }
    
    if (m_cpuFIFO->m_txFIFO.IsFull()) {
        std::cout << "[LSQ][PUSH] TX FIFO full - cannot push" << std::endl;
        return;
    }

    auto& oldest = m_lsq_q.front();
    std::cout << "[LSQ][PUSH] Checking oldest entry:" << std::endl;
    std::cout << "[LSQ][PUSH] - ID: " << oldest.request.msgId << std::endl;
    std::cout << "[LSQ][PUSH] - Type: " << (oldest.request.type == CpuFIFO::REQTYPE::READ ? "LOAD" : "STORE") << std::endl;
    std::cout << "[LSQ][PUSH] - Address: 0x" << std::hex << oldest.request.addr << std::dec << std::endl;
    std::cout << "[LSQ][PUSH] - Ready: " << (oldest.ready ? "Yes" : "No") << std::endl;
    std::cout << "[LSQ][PUSH] - Waiting for Cache: " << (oldest.waitingForCache ? "Yes" : "No") << std::endl;
    
    if (oldest.waitingForCache) {
        std::cout << "[LSQ][PUSH] Entry already waiting for cache" << std::endl;
        return;
    }

    bool should_push = false;
    if (oldest.request.type == CpuFIFO::REQTYPE::WRITE) {
        // Stores: always push to cache regardless of ready state
        should_push = true;
        std::cout << "[LSQ][PUSH] Store operation - will push to cache" << std::endl;
    } else {
        // Loads: only push if not ready (needs memory access)
        should_push = !oldest.ready;
        std::cout << "[LSQ][PUSH] Load operation - " << (should_push ? "needs cache access" : "already ready (forwarded)") << std::endl;
    }
    
    if (should_push) {
        std::cout << "[LSQ][PUSH] Pushing request to cache:" << std::endl;
        std::cout << "[LSQ][PUSH] - ID: " << oldest.request.msgId << std::endl;
        std::cout << "[LSQ][PUSH] - Type: " << (oldest.request.type == CpuFIFO::REQTYPE::READ ? "LOAD" : "STORE") << std::endl;
        std::cout << "[LSQ][PUSH] - Address: 0x" << std::hex << oldest.request.addr << std::dec << std::endl;
        
        m_cpuFIFO->m_txFIFO.InsertElement(oldest.request);
        oldest.waitingForCache = true;
        std::cout << "[LSQ][PUSH] Request sent to cache, marked as waiting" << std::endl;
    } else {
        std::cout << "[LSQ][PUSH] No need to push to cache" << std::endl;
    }
}

void LSQ::rxFromCache() {
    std::cout << "\n[LSQ][RX] ========== Processing Cache Response ==========" << std::endl;
    
    if (!m_cpuFIFO || m_cpuFIFO->m_rxFIFO.IsEmpty()) {
        std::cout << "[LSQ][RX] No cache response available" << std::endl;
        return;
    }

    auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
    m_cpuFIFO->m_rxFIFO.PopElement();
    
    std::cout << "[LSQ][RX] Processing response for msgId=" << response.msgId 
              << ", addr=0x" << std::hex << response.addr << std::dec << std::endl;

    for (auto& entry : m_lsq_q) {
        if (entry.request.msgId == response.msgId) {
            std::cout << "[LSQ][RX] Found matching entry" << std::endl;
            entry.waitingForCache = false;  // Clear waiting state
            
            if (entry.request.type == CpuFIFO::REQTYPE::READ) {
                entry.ready = true;  // Mark load as ready
                std::cout << "[LSQ][RX] Load marked ready, committing to ROB" << std::endl;
                if (m_rob) {
                    m_rob->commit(entry.request.msgId);
                }
            } else {
                entry.cache_ack = true;  // Mark store as acknowledged
                std::cout << "[LSQ][RX] Store marked as acknowledged by cache" << std::endl;
            }
            break;
        }
    }
    
    std::cout << "[LSQ][RX] ========== Cache Response Processing Complete ==========\n" << std::endl;
}

void LSQ::retire() {
    std::cout << "\n[LSQ][RETIRE] ========== LSQ Entry Removal Check ==========" << std::endl;
    std::cout << "[LSQ][RETIRE] Current LSQ state: " << m_num_entries << " entries" << std::endl;
    
    auto it = m_lsq_q.begin();
    while (it != m_lsq_q.end()) {
        std::cout << "[LSQ][RETIRE] Checking entry:" << std::endl;
        std::cout << "[LSQ][RETIRE] - ID: " << it->request.msgId << std::endl;
        std::cout << "[LSQ][RETIRE] - Type: " << (it->request.type == CpuFIFO::REQTYPE::READ ? "LOAD" : "STORE") << std::endl;
        std::cout << "[LSQ][RETIRE] - Address: 0x" << std::hex << it->request.addr << std::dec << std::endl;
        std::cout << "[LSQ][RETIRE] - Ready: " << (it->ready ? "Yes" : "No") << std::endl;
        std::cout << "[LSQ][RETIRE] - Cache Ack: " << (it->cache_ack ? "Yes" : "No") << std::endl;
        std::cout << "[LSQ][RETIRE] - Waiting for Cache: " << (it->waitingForCache ? "Yes" : "No") << std::endl;
        
        bool can_remove = false;
        
        if (it->request.type == CpuFIFO::REQTYPE::READ) {
            can_remove = it->ready;  // Loads remove when ready (Section 3.4.1)
            std::cout << "[LSQ][RETIRE] Load removal check - ready=" << (it->ready ? "Yes" : "No") << std::endl;
        } else {
            can_remove = it->cache_ack;  // Stores remove when cache acknowledges (Section 3.4.1)
            std::cout << "[LSQ][RETIRE] Store removal check - cache_ack=" << (it->cache_ack ? "Yes" : "No") << std::endl;
        }
        
        if (can_remove) {
            std::cout << "[LSQ][RETIRE] Removing entry " << it->request.msgId << std::endl;
            it = m_lsq_q.erase(it);
            m_num_entries--;
            std::cout << "[LSQ][RETIRE] LSQ now has " << m_num_entries << " entries" << std::endl;
        } else {
            std::cout << "[LSQ][RETIRE] Keeping entry " << it->request.msgId << " - conditions not met" << std::endl;
            ++it;
        }
    }
    
    std::cout << "[LSQ][RETIRE] ========== Entry Removal Complete ==========" << std::endl;
    std::cout << "[LSQ][RETIRE] Final LSQ state: " << m_num_entries << " entries" << std::endl;
}

} // namespace ns3