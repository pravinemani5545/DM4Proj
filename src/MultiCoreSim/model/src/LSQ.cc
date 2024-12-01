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
        std::cout << "[LSQ] 3.3.2: Store commits upon LSQ allocation" << std::endl;
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
            std::cout << "[LSQ] 3.3.3: Checking store-to-load forwarding" << std::endl;
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
    // Find youngest matching store first
    auto youngest_store = std::find_if(m_lsq_q.rbegin(), m_lsq_q.rend(),
        [address](const LSQEntry& entry) {
            return entry.request.type == CpuFIFO::REQTYPE::WRITE && 
                   entry.request.addr == address;
        });
    
    if (youngest_store == m_lsq_q.rend()) {
        return false;  // No matching store found
    }
    
    // Forward to loads that come after this store
    auto store_pos = youngest_store.base();
    for (auto it = store_pos; it != m_lsq_q.end(); ++it) {
        if (it->request.type == CpuFIFO::REQTYPE::READ && 
            it->request.addr == address && 
            !it->ready) {
            it->ready = true;
            if (m_rob) {
                m_rob->commit(it->request.msgId);
            }
        }
    }
    return true;
}

void LSQ::pushToCache() {
    if (m_lsq_q.empty() || !m_cpuFIFO || m_cpuFIFO->m_txFIFO.IsFull()) {
        return;
    }
    
    // Always check oldest entry first (front of queue)
    auto& oldest = m_lsq_q.front();
    if (!oldest.waitingForCache) {
        // For stores: only send if ready (committed)
        // For loads: only send if not satisfied by forwarding
        if ((oldest.request.type == CpuFIFO::REQTYPE::WRITE && oldest.ready) ||
            (oldest.request.type == CpuFIFO::REQTYPE::READ && !oldest.ready)) {
            
            oldest.waitingForCache = true;
            m_cpuFIFO->m_txFIFO.InsertElement(oldest.request);
            std::cout << "[LSQ] Pushed oldest " 
                      << (oldest.request.type == CpuFIFO::REQTYPE::READ ? "load " : "store ")
                      << oldest.request.msgId << " to cache" << std::endl;
        }
    }
}

void LSQ::rxFromCache() {
    std::cout << "[LSQ] rxFromCache check - FIFO exists=" << (m_cpuFIFO != nullptr) 
              << " isEmpty=" << (m_cpuFIFO ? m_cpuFIFO->m_rxFIFO.IsEmpty() : true) << std::endl;
              
    if (!m_cpuFIFO || m_cpuFIFO->m_rxFIFO.IsEmpty()) return;
    
    auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
    std::cout << "[LSQ] Processing cache response msgId=" << response.msgId 
              << " addr=0x" << std::hex << response.addr << std::dec << std::endl;
    
    m_cpuFIFO->m_rxFIFO.PopElement();
    
    // Find matching request in LSQ
    for (auto& entry : m_lsq_q) {
        if (entry.request.msgId == response.msgId) {
            if (entry.request.type == CpuFIFO::REQTYPE::READ) {
                entry.ready = true;
                if (m_rob) {
                    m_rob->commit(entry.request.msgId);
                }
            } else {
                entry.cache_ack = true;
            }
            break;
        }
    }
}

void LSQ::retire() {
    std::cout << "[LSQ] Attempting to retire entries..." << std::endl;
    auto it = m_lsq_q.begin();
    while (it != m_lsq_q.end()) {
        std::cout << "[LSQ] Checking entry " << it->request.msgId 
                  << " type=" << (it->request.type == CpuFIFO::REQTYPE::READ ? "LOAD" : "STORE")
                  << " ready=" << it->ready 
                  << " cache_ack=" << it->cache_ack
                  << " waitingForCache=" << it->waitingForCache << std::endl;
        
        bool can_remove = false;
        if (it->request.type == CpuFIFO::REQTYPE::READ) {
            can_remove = it->ready;
        } else {
            can_remove = it->cache_ack;
        }
        
        if (can_remove) {
            std::cout << "[LSQ] Removing entry " << it->request.msgId << std::endl;
            it = m_lsq_q.erase(it);
            m_num_entries--;
        } else {
            std::cout << "[LSQ] Cannot remove entry " << it->request.msgId 
                      << " - conditions not met" << std::endl;
            ++it;
        }
    }
}

} // namespace ns3