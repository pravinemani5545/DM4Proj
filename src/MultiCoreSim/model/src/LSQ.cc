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
    std::cout << "\n[LSQ] Step at cycle " << m_current_cycle << std::endl;

    // Push to cache
    pushToCache();

    // Handle cache responses
    rxFromCache();

    // Retire completed operations
    retire();

    // Increment cycle tracking
    m_current_cycle++;
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
    std::cout << "\n[LSQ][PUSH] ========== Push To Cache ==========" << std::endl;
    
    if (m_lsq_q.empty() || !m_cpuFIFO || m_cpuFIFO->m_txFIFO.IsFull()) {
        return;
    }

    // Check if any older entries are waiting for cache
    for (const auto& entry : m_lsq_q) {
        if (entry.waitingForCache) {
            return;  // Maintain memory ordering
        }
        if (&entry == &m_lsq_q.front()) break;
    }

    auto& oldest = m_lsq_q.front();
    if (!oldest.waitingForCache) {
        // For writes: send if ready (committed to ROB)
        // For reads: send if not ready (needs memory access)
        if ((oldest.request.type == CpuFIFO::REQTYPE::WRITE && oldest.ready) ||
            (oldest.request.type == CpuFIFO::REQTYPE::READ && !oldest.ready)) {
                
            if (!m_cpuFIFO->m_txFIFO.IsFull()) {
                m_cpuFIFO->m_txFIFO.InsertElement(oldest.request);
                oldest.waitingForCache = true;
            }
        }
    }
}




void LSQ::rxFromCache() {
    if (!m_cpuFIFO || m_cpuFIFO->m_rxFIFO.IsEmpty()) {
        return;
    }

    auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
    m_cpuFIFO->m_rxFIFO.PopElement();

    // First pass: Handle the specific response
    for (auto& entry : m_lsq_q) {
        if (entry.request.msgId == response.msgId) {
            entry.waitingForCache = false;  // Clear waiting state
            
            if (entry.request.type == CpuFIFO::REQTYPE::READ) {
                entry.ready = true;  // Mark load as ready
                if (m_rob) {
                    m_rob->commit(entry.request.msgId);
                }
            } else {
                entry.cache_ack = true;  // Mark store as acknowledged
            }
            break;
        }
    }
}



// void LSQ::rxFromCache() {
//     std::cout << "[LSQ] rxFromCache check - FIFO exists=" << (m_cpuFIFO != nullptr) 
//               << ", isEmpty=" << (m_cpuFIFO ? m_cpuFIFO->m_rxFIFO.IsEmpty() : true) << std::endl;

//     if (!m_cpuFIFO || m_cpuFIFO->m_rxFIFO.IsEmpty()) {
//         std::cout << "[LSQ] No cache response available" << std::endl;
//         return;
//     }

//     // Fetch the cache response
//     auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
//     std::cout << "[LSQ] Processing cache response msgId=" << response.msgId 
//               << ", addr=0x" << std::hex << response.addr << std::dec 
//               << std::endl;

//     // Remove the response from the FIFO
//     m_cpuFIFO->m_rxFIFO.PopElement();

//     // Find matching request in LSQ
//     bool found = false;
//     for (auto& entry : m_lsq_q) {
//         if (entry.request.msgId == response.msgId) {
//             found = true;
//             std::cout << "[LSQ] Found matching request in LSQ for msgId=" << response.msgId << std::endl;

//             if (entry.request.type == CpuFIFO::REQTYPE::READ) {
//                 entry.ready = true;
//                 std::cout << "[LSQ] Marking load request " << response.msgId << " as ready" << std::endl;
//                 if (m_rob) {
//                     m_rob->commit(entry.request.msgId);
//                 }
//             } else if (entry.request.type == CpuFIFO::REQTYPE::WRITE) {
//                 entry.cache_ack = true;
//                 std::cout << "[LSQ] Marking store request " << response.msgId << " as acknowledged" << std::endl;
//             }
//             break;
//         }
//     }

//     if (!found) {
//         std::cout << "[LSQ] No matching request in LSQ for response msgId=" << response.msgId << std::endl;
//     }
// }


// void LSQ::rxFromCache() {
//     std::cout << "[LSQ] rxFromCache check - FIFO exists=" << (m_cpuFIFO != nullptr) 
//               << ", isEmpty=" << (m_cpuFIFO ? m_cpuFIFO->m_rxFIFO.IsEmpty() : true) << std::endl;

//     if (!m_cpuFIFO || m_cpuFIFO->m_rxFIFO.IsEmpty()) {
//         std::cout << "[LSQ] No cache response available" << std::endl;
//         return;
//     }

//     // Fetch the cache response
//     auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
//     std::cout << "[LSQ] Processing cache response msgId=" << response.msgId 
//               << ", addr=0x" << std::hex << response.addr << std::dec 
//               << std::endl;

//     // Remove the response from the FIFO
//     m_cpuFIFO->m_rxFIFO.PopElement();

//     // Find matching request in LSQ
//     for (auto& entry : m_lsq_q) {
//         if (entry.request.msgId == response.msgId) {
//             std::cout << "[LSQ] Found matching request in LSQ for msgId=" << response.msgId << std::endl;

//             if (entry.request.type == CpuFIFO::REQTYPE::READ) {
//                 entry.ready = true;
//                 std::cout << "[LSQ] Marking load request " << response.msgId << " as ready" << std::endl;
//                 if (m_rob) {
//                     m_rob->commit(entry.request.msgId);
//                 }
//             } else if (entry.request.type == CpuFIFO::REQTYPE::WRITE) {
//                 entry.cache_ack = true;
//                 std::cout << "[LSQ] Marking store request " << response.msgId << " as acknowledged" << std::endl;
//             }
//             break;
//         }
//     }
// }


void LSQ::retire() {
    std::cout << "\n[LSQ][RETIRE] ========== Retirement Check ==========" << std::endl;
    
    auto it = m_lsq_q.begin();
    while (it != m_lsq_q.end()) {
        std::cout << "[LSQ][RETIRE] Checking entry:" << std::endl;
        std::cout << "[LSQ][RETIRE] - ID: " << it->request.msgId << std::endl;
        std::cout << "[LSQ][RETIRE] - Type: " << (it->request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") << std::endl;
        std::cout << "[LSQ][RETIRE] - Ready: " << (it->ready ? "Yes" : "No") << std::endl;
        std::cout << "[LSQ][RETIRE] - Cache Ack: " << (it->cache_ack ? "Yes" : "No") << std::endl;
        
        bool should_remove = false;
        
        if (it->request.type == CpuFIFO::REQTYPE::READ) {
            // For loads: remove when ready (either forwarded or got cache response)
            if (it->ready) {
                should_remove = true;
                std::cout << "[LSQ][RETIRE] Load is ready - removing" << std::endl;
            }
        } else {
            // For stores: remove ONLY when cache acknowledges write completion
            if (it->cache_ack) {
                should_remove = true;
                std::cout << "[LSQ][RETIRE] Store acknowledged by cache - removing" << std::endl;
            }
        }
        
        if (should_remove) {
            std::cout << "[LSQ][RETIRE] Removing entry " << it->request.msgId << std::endl;
            it = m_lsq_q.erase(it);
            m_num_entries--;
        } else {
            ++it;
        }
    }
    
    std::cout << "[LSQ][RETIRE] LSQ now has " << m_num_entries << " entries" << std::endl;
}

} // namespace ns3