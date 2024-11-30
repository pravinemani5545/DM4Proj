#include "../header/LSQ.h"
#include "../header/ROB.h"
#include "../header/CpuCoreGenerator.h"

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
    std::cout << "[LSQ] Current entries: " << m_num_entries << "/" << MAX_ENTRIES << std::endl;
    
    // As per 3.3, handle memory operations every cycle
    pushToCache();  // Try to send stores to cache
    rxFromCache();  // Handle cache responses
    retire();       // Remove completed operations
}

bool LSQ::canAccept() {
    bool can_accept = m_num_entries < MAX_ENTRIES;
    std::cout << "[LSQ] Can accept new entry: " << (can_accept ? "yes" : "no") 
              << " (" << m_num_entries << "/" << MAX_ENTRIES << ")" << std::endl;
    return can_accept;
}

bool LSQ::allocate(const CpuFIFO::ReqMsg& request) {
    if (!canAccept()) {
        std::cout << "[LSQ] Allocation failed - LSQ full" << std::endl;
        return false;
    }
    
    LSQEntry entry;
    entry.request = request;
    entry.ready = false;
    entry.waitingForCache = false;
    entry.cache_ack = false;
    
    // As per 3.3.2: Store Instructions commit by the time you allocate them in the LSQ
    // Rationale: stores are not critical to CPU pipeline since CPU is not waiting for data
    if (request.type == CpuFIFO::REQTYPE::WRITE) {
        entry.ready = true;
        std::cout << "[LSQ] Store ready immediately (CPU not waiting for data)" << std::endl;
        if (m_rob) {
            m_rob->commit(request.msgId);
        }
    }
    // As per 3.3.3: For loads, check store forwarding first
    else if (request.type == CpuFIFO::REQTYPE::READ) {
        // Check for store-to-load forwarding
        if (ldFwd(request.addr)) {
            entry.ready = true;
            std::cout << "[LSQ] Load ready immediately due to store forwarding" << std::endl;
            if (m_rob) {
                m_rob->commit(request.msgId);
            }
        }
    }
    
    m_lsq_q.push_back(entry);
    m_num_entries++;
    
    std::cout << "[LSQ] Allocated " 
              << (request.type == CpuFIFO::REQTYPE::READ ? "load" : "store")
              << " request " << request.msgId 
              << " addr=0x" << std::hex << request.addr << std::dec 
              << " ready=" << entry.ready << std::endl;
    return true;
}

bool LSQ::ldFwd(uint64_t address) {
    std::cout << "[LSQ] Checking store-to-load forwarding for address 0x" 
              << std::hex << address << std::dec << std::endl;
              
    // As per 3.3.3 case 2: Check for store-to-load forwarding
    bool found_store = false;
    for (auto it = m_lsq_q.rbegin(); it != m_lsq_q.rend(); ++it) {
        if (it->request.type == CpuFIFO::REQTYPE::WRITE && 
            it->request.addr == address) {
            
            std::cout << "[LSQ] Found matching store (ID " << it->request.msgId 
                      << ") for forwarding" << std::endl;
            
            // Mark all subsequent loads to this address as ready
            for (auto& entry : m_lsq_q) {
                if (entry.request.type == CpuFIFO::REQTYPE::READ &&
                    entry.request.addr == address &&
                    !entry.ready) {
                    entry.ready = true;
                    std::cout << "[LSQ] Marking load " << entry.request.msgId 
                              << " ready through store forwarding" << std::endl;
                    if (m_rob) {
                        m_rob->commit(entry.request.msgId);
                    }
                }
            }
            found_store = true;
            break;  // Only need youngest matching store
        }
    }
    
    if (!found_store) {
        std::cout << "[LSQ] No matching store found for forwarding" << std::endl;
    }
    return found_store;
}

void LSQ::pushToCache() {
    // Check FIFO availability first
    if (!m_cpuFIFO || m_cpuFIFO->m_txFIFO.IsFull()) {
        std::cout << "[LSQ] Cannot push to cache - FIFO full or not connected" << std::endl;
        return;
    }

    // Check for store operations ready to be sent to cache
    for (auto& entry : m_lsq_q) {
        if (entry.request.type == CpuFIFO::REQTYPE::WRITE && !entry.waitingForCache) {
            // Send store to cache
            entry.waitingForCache = true;
            m_cpuFIFO->m_txFIFO.InsertElement(entry.request);
            if (m_rob && m_rob->getCpu()) {
                m_rob->getCpu()->notifyRequestSentToCache();
            }
            std::cout << "[LSQ] Sent store request " << entry.request.msgId 
                      << " to cache (addr=0x" << std::hex << entry.request.addr 
                      << std::dec << ")" << std::endl;
            return;  // Only send one request per cycle
        }
    }

    // Check for load operations ready to be sent to cache
    for (auto& entry : m_lsq_q) {
        if (entry.request.type == CpuFIFO::REQTYPE::READ && !entry.waitingForCache) {
            // Send load to cache
            entry.waitingForCache = true;
            m_cpuFIFO->m_txFIFO.InsertElement(entry.request);
            if (m_rob && m_rob->getCpu()) {
                m_rob->getCpu()->notifyRequestSentToCache();
            }
            std::cout << "[LSQ] Sent load request " << entry.request.msgId 
                      << " to cache (addr=0x" << std::hex << entry.request.addr 
                      << std::dec << ")" << std::endl;
            return;  // Only send one request per cycle
        }
    }
}

void LSQ::rxFromCache() {
    // Check FIFO availability first
    if (!m_cpuFIFO || m_cpuFIFO->m_rxFIFO.IsEmpty()) {
        return;
    }
    
    auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
    m_cpuFIFO->m_rxFIFO.PopElement();
    
    std::cout << "[LSQ] Received cache response for request " << response.msgId 
              << " (addr=0x" << std::hex << response.addr << std::dec << ")" << std::endl;
    
    // Find matching request in LSQ
    for (auto& entry : m_lsq_q) {
        if (entry.request.msgId == response.msgId) {
            if (entry.request.type == CpuFIFO::REQTYPE::READ) {
                entry.ready = true;
                std::cout << "[LSQ] Load data received from memory, marking ready" << std::endl;
                if (m_rob) {
                    m_rob->commit(entry.request.msgId);
                }
                
                // After receiving load data, check if any other loads can be forwarded
                ldFwd(entry.request.addr);
            } else {
                entry.cache_ack = true;
                std::cout << "[LSQ] Store write acknowledged by cache" << std::endl;
            }
            break;
        }
    }
}

void LSQ::retire() {
    if (m_lsq_q.empty()) return;
    
    // As per 3.4.1: LSQ retire has different semantics - just removes entries
    auto it = m_lsq_q.begin();
    while (it != m_lsq_q.end()) {
        bool can_remove = false;
        
        if (it->request.type == CpuFIFO::REQTYPE::READ) {
            // For loads: remove when ready (either from cache or forwarding)
            can_remove = it->ready;
            if (can_remove) {
                std::cout << "[LSQ] Removing completed load " << it->request.msgId 
                          << " (addr=0x" << std::hex << it->request.addr 
                          << std::dec << ")" << std::endl;
            }
        } else {
            // For stores: remove only after cache acknowledges write completion
            can_remove = it->cache_ack;
            if (can_remove) {
                std::cout << "[LSQ] Removing completed store " << it->request.msgId 
                          << " (addr=0x" << std::hex << it->request.addr 
                          << std::dec << ") - cache write confirmed" << std::endl;
            }
        }
        
        if (can_remove) {
            it = m_lsq_q.erase(it);
            m_num_entries--;
        } else {
            ++it;
        }
    }
}

void LSQ::commit(uint64_t requestId) {
    std::cout << "[LSQ] Processing commit for request " << requestId << std::endl;
    
    for (auto& entry : m_lsq_q) {
        if (entry.request.msgId == requestId) {
            if (entry.request.type == CpuFIFO::REQTYPE::WRITE) {
                if (entry.cache_ack) {
                    // Store has been written to cache, can be removed
                    std::cout << "[LSQ] Store " << requestId 
                              << " committed and written to cache" << std::endl;
                } else {
                    // Need to write store to cache
                    std::cout << "[LSQ] Store " << requestId 
                              << " committed but waiting for cache write" << std::endl;
                }
            }
            return;
        }
    }
    
    std::cout << "[LSQ] Warning: Request " << requestId << " not found for commit" << std::endl;
}

} // namespace ns3