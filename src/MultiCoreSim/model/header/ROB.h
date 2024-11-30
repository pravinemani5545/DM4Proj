#ifndef ROB_H
#define ROB_H

#include "MemTemplate.h"
#include <vector>

namespace ns3 {

/**
 * @brief Reorder Buffer (ROB) implementation for Out-of-Order execution
 * 
 * The ROB maintains program order while allowing out-of-order execution.
 * It ensures instructions commit in-order while allowing execution to complete
 * out-of-order. This preserves precise interrupts and correct program behavior.
 */
class ROB {
private:
    static const uint32_t MAX_ENTRIES = 32;  // Maximum ROB entries (default)
    static const uint32_t IPC = 4;           // Instructions retired per cycle (default)
    
    /**
     * @brief Entry in the ROB containing an instruction and its status
     */
    struct ROBEntry {
        CpuFIFO::ReqMsg request;    // Instruction details
        bool ready;                  // True when instruction has completed execution
    };
    
    uint32_t m_num_entries;         // Current number of entries
    std::vector<ROBEntry> m_rob_q;  // Queue storing ROB entries

public:
    ROB();
    ~ROB();
    
    /**
     * @brief Called every cycle to process ROB operations
     * Handles instruction retirement and updates ROB state
     */
    void step();

    /**
     * @brief Checks if ROB can accept a new entry
     * @return true if ROB has space for new entry
     */
    bool canAccept();

    /**
     * @brief Allocates a new entry in ROB
     * @param request Instruction to allocate
     * @return true if allocation successful
     * 
     * For compute instructions: marked ready immediately
     * For memory operations: marked ready when LSQ signals completion
     */
    bool allocate(const CpuFIFO::ReqMsg& request);

    /**
     * @brief Retires completed instructions in program order
     * 
     * - Can retire up to IPC instructions per cycle
     * - Only retires instructions from head of ROB
     * - Only retires instructions marked as ready
     * - Stops at first non-ready instruction
     */
    void retire();

    /**
     * @brief Marks an instruction as complete/ready
     * @param requestId ID of instruction to commit
     * 
     * Called when:
     * - Compute instruction allocated (immediate)
     * - Store instruction allocated (immediate)
     * - Load instruction receives data from cache or LSQ forwarding
     */
    void commit(uint64_t requestId);

    /**
     * @brief Checks if ROB is empty
     * @return true if no entries in ROB
     */
    bool isEmpty() const { return m_rob_q.empty(); }

    /**
     * @brief Gets current number of entries in ROB
     * @return Number of entries
     */
    uint32_t size() const { return m_num_entries; }

    /**
     * @brief Removes the most recently added entry
     * Used for cleanup if LSQ allocation fails
     */
    void removeLastEntry() {
        if (!m_rob_q.empty()) {
            m_rob_q.pop_back();
            m_num_entries--;
        }
    }
};

} // namespace ns3

#endif // ROB_H