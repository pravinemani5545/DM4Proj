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
        std::cout << "[LSQ][PUSH] Cannot push:" << std::endl;
        std::cout << "[LSQ][PUSH] - Queue empty: " << (m_lsq_q.empty() ? "Yes" : "No") << std::endl;
        std::cout << "[LSQ][PUSH] - FIFO exists: " << (m_cpuFIFO ? "Yes" : "No") << std::endl;
        std::cout << "[LSQ][PUSH] - FIFO full: " << (m_cpuFIFO && m_cpuFIFO->m_txFIFO.IsFull() ? "Yes" : "No") << std::endl;
        return;
    }

    auto& oldest = m_lsq_q.front();
    std::cout << "[LSQ][PUSH] Checking oldest entry:" << std::endl;
    std::cout << "[LSQ][PUSH] - ID: " << oldest.request.msgId << std::endl;
    std::cout << "[LSQ][PUSH] - Type: " << (oldest.request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") << std::endl;
    std::cout << "[LSQ][PUSH] - Ready: " << (oldest.ready ? "Yes" : "No") << std::endl;
    std::cout << "[LSQ][PUSH] - Waiting: " << (oldest.waitingForCache ? "Yes" : "No") << std::endl;

    if (!oldest.waitingForCache) {
        if ((oldest.request.type == CpuFIFO::REQTYPE::WRITE && oldest.ready) ||
            (oldest.request.type == CpuFIFO::REQTYPE::READ && !oldest.ready)) {
            
            oldest.waitingForCache = true;
            m_cpuFIFO->m_txFIFO.InsertElement(oldest.request);
            
            std::cout << "[LSQ][PUSH] Successfully pushed to cache:" << std::endl;
            std::cout << "[LSQ][PUSH] - ID: " << oldest.request.msgId << std::endl;
            std::cout << "[LSQ][PUSH] - Type: " << (oldest.request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") << std::endl;
            std::cout << "[LSQ][PUSH] - Address: 0x" << std::hex << oldest.request.addr << std::dec << std::endl;
        }
    }
}




void LSQ::rxFromCache() {
    std::cout << "\n[LSQ][RX] ========== Receive From Cache ==========" << std::endl;
    
    if (!m_cpuFIFO || m_cpuFIFO->m_rxFIFO.IsEmpty()) {
        std::cout << "[LSQ][RX] No response available" << std::endl;
        return;
    }

    auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
    m_cpuFIFO->m_rxFIFO.PopElement();
    
    std::cout << "[LSQ][RX] Processing response:" << std::endl;
    std::cout << "[LSQ][RX] - ID: " << response.msgId << std::endl;
    std::cout << "[LSQ][RX] - Address: 0x" << std::hex << response.addr << std::dec << std::endl;
    std::cout << "[LSQ][RX] - Request Cycle: " << response.reqcycle << std::endl;
    std::cout << "[LSQ][RX] - Response Cycle: " << response.cycle << std::endl;

    for (auto& entry : m_lsq_q) {
        if (entry.request.msgId == response.msgId) {
            std::cout << "[LSQ][RX] Found matching entry:" << std::endl;
            std::cout << "[LSQ][RX] - Type: " << (entry.request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") << std::endl;
            
            if (entry.request.type == CpuFIFO::REQTYPE::READ) {
                entry.ready = true;
                std::cout << "[LSQ][RX] Load marked ready" << std::endl;
                if (m_rob) {
                    m_rob->commit(entry.request.msgId);
                }
            } else if (entry.request.type == CpuFIFO::REQTYPE::WRITE) {
                entry.cache_ack = true;
                std::cout << "[LSQ][RX] Store acknowledged by cache" << std::endl;
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
    std::cout << "[LSQ][RETIRE] Current Queue State:" << std::endl;
    
    for (auto it = m_lsq_q.begin(); it != m_lsq_q.end(); ++it) {
        std::cout << "[LSQ][RETIRE] Entry:" << std::endl;
        std::cout << "[LSQ][RETIRE] - ID: " << it->request.msgId << std::endl;
        std::cout << "[LSQ][RETIRE] - Type: " << (it->request.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") << std::endl;
        std::cout << "[LSQ][RETIRE] - Ready: " << (it->ready ? "Yes" : "No") << std::endl;
        std::cout << "[LSQ][RETIRE] - Cache Ack: " << (it->cache_ack ? "Yes" : "No") << std::endl;
        
        bool can_remove = false;
        if (it->request.type == CpuFIFO::REQTYPE::READ) {
            can_remove = it->ready;
            if (can_remove) {
                std::cout << "[LSQ][RETIRE] Load can be removed - ready" << std::endl;
            }
        } else {
            can_remove = it->cache_ack;
            if (can_remove) {
                std::cout << "[LSQ][RETIRE] Store can be removed - acknowledged" << std::endl;
            }
        }
        
        if (can_remove) {
            std::cout << "[LSQ][RETIRE] Removing entry " << it->request.msgId << std::endl;
            it = m_lsq_q.erase(it);
            m_num_entries--;
            if (it == m_lsq_q.end()) break;
        }
    }
    
    std::cout << "[LSQ][RETIRE] Final state: " << m_num_entries << " entries" << std::endl;
}

} // namespace ns3