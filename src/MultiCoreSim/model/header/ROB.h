#ifndef ROB_H
#define ROB_H

#include "MemTemplate.h"
#include <vector>

namespace ns3 {

class LSQ; // Forward declaration

/**
 * @brief Reorder Buffer (ROB) implementation for Out-of-Order execution
 * 
 * The ROB maintains program order while allowing out-of-order execution.
 * It ensures instructions commit in-order while allowing execution to complete
 * out-of-order. This preserves precise interrupts and correct program behavior.
 */
class ROB {
private:
    static const uint32_t MAX_ENTRIES = 32;  // Maximum ROB entries
    static const uint32_t IPC = 4;           // Instructions retired per cycle
    
    /**
     * @brief Entry in the ROB containing an instruction and its status
     */
    struct ROBEntry {
        CpuFIFO::ReqMsg request;    // Instruction details
        bool ready;                 // True when instruction has completed
    };
    
    uint32_t m_num_entries;         // Current number of entries
    std::vector<ROBEntry> m_rob_q;  // Queue storing ROB entries
    LSQ* m_lsq;                     // Pointer to LSQ for store commits

public:
    ROB();
    ~ROB();
    
    void step();
    bool canAccept();
    bool allocate(const CpuFIFO::ReqMsg& request);
    void retire();
    void commit(uint64_t requestId);
    
    bool isEmpty() const { return m_rob_q.empty(); }
    uint32_t size() const { return m_num_entries; }
    
    void removeLastEntry() {
        if (!m_rob_q.empty()) {
            m_rob_q.pop_back();
            m_num_entries--;
        }
    }

    void setLSQ(LSQ* lsq) { m_lsq = lsq; }
};

} // namespace ns3

#endif // ROB_H