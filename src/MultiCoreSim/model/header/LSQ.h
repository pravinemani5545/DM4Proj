#ifndef LSQ_H
#define LSQ_H

#include "MemTemplate.h"
#include <vector>
#include <iostream>
#include <iomanip>

namespace ns3 {

class ROB;
class CpuFIFO;

/**
 * @brief Load Store Queue (LSQ) implementation for Out-of-Order execution
 * 
 * Requirements from 3.3:
 * - Fixed size (8 entries)
 * - Store-to-load forwarding
 * - Memory ordering
 * - Coordination with ROB
 */
class LSQ {
private:
    static const uint32_t MAX_ENTRIES = 8;  // Maximum LSQ entries (3.3)
    
    struct LSQEntry {
        CpuFIFO::ReqMsg request;    // Memory request details
        bool ready;                  // True when operation complete (3.3)
        bool waitingForCache;        // True when waiting for cache response
        bool cache_ack;             // True when cache confirms write complete
        uint64_t allocate_cycle;    // Cycle when instruction was allocated
    };
    
    uint32_t m_num_entries;         // Current number of entries
    std::vector<LSQEntry> m_lsq_q;  // Queue storing LSQ entries
    CpuFIFO* m_cpuFIFO;            // Interface to CPU FIFO
    ROB* m_rob;                     // Pointer to ROB for coordination
    uint64_t m_current_cycle;       // Current CPU cycle

public:
    LSQ();
    ~LSQ();
    
    // Core functionality (3.3)
    void step();                    // Called every cycle
    bool canAccept();              // Check if LSQ can accept new entry
    bool allocate(const CpuFIFO::ReqMsg& request); // Allocate new memory operation
    void retire();                 // Remove completed operations
    bool ldFwd(uint64_t address); // Check store-to-load forwarding (3.3)
    void commit(uint64_t requestId); // Handle operation completion
    
    // Memory system interface (3.3)
    void pushToCache();           // Send requests to cache
    void rxFromCache();           // Handle cache responses
    
    // Configuration
    void setCycle(uint64_t cycle) { m_current_cycle = cycle; }
    void setROB(ROB* rob) { m_rob = rob; }
    void setCpuFIFO(CpuFIFO* fifo) { m_cpuFIFO = fifo; }
    
    // Utility functions
    bool isEmpty() const { return m_lsq_q.empty(); }
    uint32_t size() const { return m_num_entries; }
    
    void removeLastEntry() {
        if (!m_lsq_q.empty()) {
            m_lsq_q.pop_back();
            m_num_entries--;
        }
    }
    
    void printState() const;
};

} // namespace ns3

#endif // LSQ_H