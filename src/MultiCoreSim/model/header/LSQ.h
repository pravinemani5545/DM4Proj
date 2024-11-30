#ifndef LSQ_H
#define LSQ_H

#include "MemTemplate.h"
#include <vector>

namespace ns3 {

/**
 * @brief Load Store Queue (LSQ) implementation for Out-of-Order execution
 * 
 * The LSQ handles memory operations (loads and stores) in an out-of-order processor.
 * It maintains program order for memory operations while allowing out-of-order execution
 * and implements store-to-load forwarding for memory disambiguation.
 */
class LSQ {
private:
    /**
     * @brief Entry in the LSQ containing a memory request and its status
     */
    struct LSQEntry {
        CpuFIFO::ReqMsg request;    // Memory request details
        bool ready;                  // True when operation is complete (data received for loads, store committed)
        bool waitingForCache;        // True when waiting for cache response
    };
    
    uint32_t MAX_ENTRIES;           // Maximum number of entries in LSQ
    uint32_t num_entries;           // Current number of entries
    std::vector<LSQEntry> lsq_q;    // Queue storing LSQ entries
    CpuFIFO* m_cpuFIFO;            // Interface to CPU FIFO for cache communication

public:
    LSQ();
    ~LSQ();
    
    /**
     * @brief Called every cycle to process LSQ operations
     * Handles pushing stores to cache and receiving load responses
     */
    void step();

    /**
     * @brief Checks if LSQ can accept a new entry
     * @return true if LSQ has space for new entry
     */
    bool canAccept();

    /**
     * @brief Allocates a new entry in LSQ
     * @param request Memory request to allocate
     * @return true if allocation successful
     */
    bool allocate(const CpuFIFO::ReqMsg& request);

    /**
     * @brief Retires completed memory operations
     * - Loads are retired when data is received or forwarded
     * - Stores are retired when cache confirms write complete
     */
    void retire();

    /**
     * @brief Implements store-to-load forwarding
     * @param address Memory address to check for forwarding
     * @return true if forwarding occurred
     */
    bool ldFwd(uint64_t address);

    /**
     * @brief Marks a request as complete/ready
     * @param requestId ID of request to commit
     */
    void commit(uint64_t requestId);

    /**
     * @brief Pushes oldest store operation to cache
     * Called during step() to handle store operations
     */
    void pushToCache();

    /**
     * @brief Processes responses from cache
     * Called during step() to handle load responses
     */
    void rxFromCache();

    /**
     * @brief Sets the CPU FIFO interface for cache communication
     * @param fifo Pointer to CPU FIFO
     */
    void setCpuFIFO(CpuFIFO* fifo) { m_cpuFIFO = fifo; }

    /**
     * @brief Checks if LSQ is empty
     * @return true if no entries in LSQ
     */
    bool isEmpty() const { return lsq_q.empty(); }

    /**
     * @brief Gets current number of entries in LSQ
     * @return Number of entries
     */
    uint32_t size() const { return num_entries; }
};

} // namespace ns3

#endif // LSQ_H