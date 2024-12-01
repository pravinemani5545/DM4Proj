#ifndef ROB_H
#define ROB_H

#include "MemTemplate.h"
#include <vector>
#include <iostream>
#include <iomanip>

namespace ns3 {

class LSQ; // Forward declaration

/**
 * @brief Reorder Buffer (ROB) implementation for Out-of-Order execution
 * 
 * Requirements from 3.2:
 * - Fixed size (32 entries)
 * - In-order retirement
 * - Multiple instruction retirement per cycle (IPC)
 * - Maintains program order
 */
class ROB {
private:
    static const uint32_t MAX_ENTRIES = 32;  // Maximum ROB entries (3.2)
    static const uint32_t IPC = 4;           // Instructions retired per cycle (3.2)
    
    struct ROBEntry {
        CpuFIFO::ReqMsg request;    // Instruction details
        bool ready;                 // True when instruction committed (3.3)
        uint64_t allocate_cycle;    // Cycle when instruction was allocated
    };
    
    uint32_t m_num_entries;         // Current number of entries
    std::vector<ROBEntry> m_rob_q;  // Queue storing ROB entries
    LSQ* m_lsq;                     // Pointer to LSQ for store commits
    uint64_t m_current_cycle;       // Current CPU cycle

public:
    ROB();
    ~ROB();
    
    // Core functionality (3.2, 3.3, 3.4)
    void step();                    // Called every cycle
    bool canAccept();              // Check if ROB can accept new entry
    bool allocate(const CpuFIFO::ReqMsg& request); // Allocate new instruction
    void retire();                 // Retire ready instructions in-order
    void commit(uint64_t requestId); // Mark instruction as ready
    
    // Utility functions
    bool isEmpty() const { return m_rob_q.empty(); }
    uint32_t size() const { return m_num_entries; }
    void setCycle(uint64_t cycle) { m_current_cycle = cycle; }
    
    void removeLastEntry() {
        if (!m_rob_q.empty()) {
            m_rob_q.pop_back();
            m_num_entries--;
            std::cout << "[ROB] Removed last entry, size now: " << m_num_entries << std::endl;
        }
    }

    void setLSQ(LSQ* lsq) { 
        m_lsq = lsq;
        std::cout << "[ROB] LSQ connection established" << std::endl;
    }
    
    // Debug support
    void printState() const {
        std::cout << "\n[ROB] Current State:" << std::endl;
        std::cout << "  Entries: " << m_num_entries << "/" << MAX_ENTRIES << std::endl;
        std::cout << "  Queue contents:" << std::endl;
        for (size_t i = 0; i < m_rob_q.size(); i++) {
            const auto& entry = m_rob_q[i];
            std::cout << "    [" << i << "] ID: " << entry.request.msgId
                      << " Type: " << (int)entry.request.type
                      << " Ready: " << (entry.ready ? "Yes" : "No")
                      << " Cycle: " << entry.allocate_cycle << std::endl;
        }
    }

    // Added getter for detailed logging
    uint32_t getNumEntries() const { return m_num_entries; }
};

} // namespace ns3

#endif // ROB_H