/*
 * File  :      MCoreSimProjectXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 16, 2020
 * Modified for Out-of-Order execution support
 */

#include "../header/CpuCoreGenerator.h"
#include "../header/Logger.h"
#include <sstream>
#include "../header/ROB.h"
#include "../header/LSQ.h"

namespace ns3 {

    TypeId CpuCoreGenerator::GetTypeId(void) {
        static TypeId tid = TypeId("ns3::CpuCoreGenerator")
               .SetParent<Object > ();
        return tid;
    }

    CpuCoreGenerator::CpuCoreGenerator(CpuFIFO* associatedCpuFIFO) 
        : m_coreId(0),
          m_dt(1.0),
          m_clkSkew(0.0),
          m_logFileGenEnable(false),
          m_cpuFIFO(associatedCpuFIFO),
          m_rob(nullptr),
          m_lsq(nullptr),
          m_cpuCycle(0),
          m_remaining_compute(0),
          m_newSampleRdy(false),
          m_cpuReqDone(false),
          m_cpuCoreSimDone(false),
          m_sent_requests(0),
          m_number_of_OoO_requests(16),
          m_cpuReqCnt(0),
          m_cpuRespCnt(0),
          m_prevReqFinish(true),
          m_prevReqFinishCycle(0),
          m_prevReqArriveCycle(0) {
              
        std::cout << "[CPU] Initializing Core " << m_coreId << std::endl;
        
        // Create and connect components
        m_rob = new ROB();
        m_lsq = new LSQ();
        
        // Set up connections
        m_lsq->setROB(m_rob);
        m_rob->setLSQ(m_lsq);
        m_lsq->setCpuFIFO(m_cpuFIFO);
        m_rob->setCpu(this);
    }

    CpuCoreGenerator::~CpuCoreGenerator() {
        if (m_bmTrace.is_open()) {
            m_bmTrace.close();
        }
        if (m_cpuTrace.is_open()) {
            m_cpuTrace.close();
        }
        if (m_ctrlsTrace.is_open()) {
            m_ctrlsTrace.close();
        }
    }

    // Configuration methods
    void CpuCoreGenerator::SetBmFileName(std::string bmFileName) {
        m_bmFileName = bmFileName;
    }

    void CpuCoreGenerator::SetCpuTraceFile(std::string fileName) {
        m_cpuTraceFileName = fileName; 
    }

    void CpuCoreGenerator::SetCtrlsTraceFile(std::string fileName) {
        m_ctrlsTraceFileName = fileName;
    }

    void CpuCoreGenerator::SetCoreId(int coreId) {
        m_coreId = coreId;
    }

    int CpuCoreGenerator::GetCoreId() {
        return m_coreId;
    }

    void CpuCoreGenerator::SetDt(double dt) {
        m_dt = dt;
    }

    double CpuCoreGenerator::GetDt() {
        return m_dt;
    }

    void CpuCoreGenerator::SetClkSkew(double clkSkew) {
        m_clkSkew = clkSkew;
    }

    bool CpuCoreGenerator::GetCpuSimDoneFlag() {
        return m_cpuCoreSimDone;
    }

    void CpuCoreGenerator::SetLogFileGenEnable(bool logFileGenEnable) {
        m_logFileGenEnable = logFileGenEnable;
    }

    /**
     * @brief Set maximum number of in-flight OoO requests
     * @param stages Number of OoO stages
     */
    void CpuCoreGenerator::SetOutOfOrderStages(int stages) {
        m_number_of_OoO_requests = stages;
    }
    
    /**
     * @brief Initialize CPU core and start simulation
     * 
     * Opens benchmark trace file and schedules first cycle
     */
    void CpuCoreGenerator::init() {
        m_bmTrace.open(m_bmFileName.c_str());
        if (!m_bmTrace.is_open()) {
            std::cerr << "[CPU] ERROR: Could not open trace file " << m_bmFileName << std::endl;
            throw std::runtime_error("Failed to open trace file");
        }
        
        if (m_logFileGenEnable) {
            m_cpuTrace.open(m_cpuTraceFileName.c_str());
            m_ctrlsTrace.open(m_ctrlsTraceFileName.c_str());
        }
        
        Simulator::Schedule(NanoSeconds(m_clkSkew), &CpuCoreGenerator::Step, Ptr<CpuCoreGenerator>(this));
    }

    /**
     * @brief Process transmit buffer operations
     * 
     * Main instruction processing loop:
     * 1. Handle pending compute instructions
     * 2. Process memory operations
     * 3. Read next trace line when ready
     * 
     * For each instruction:
     * - Allocate in ROB (all instructions)
     * - Allocate in LSQ (memory operations)
     * - Handle store-to-load forwarding
     * - Mark ready status appropriately
     */
    void CpuCoreGenerator::ProcessTxBuf() {
        std::cout << "\n[CPU] ========== Core " << m_coreId << " Cycle " << m_cpuCycle << " ==========" << std::endl;
        std::cout << "[CPU] Pipeline state:" << std::endl;
        std::cout << "[CPU] - In-flight requests: " << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
        std::cout << "[CPU] - Remaining compute: " << m_remaining_compute << std::endl;
        std::cout << "[CPU] - Request count: " << m_cpuReqCnt << std::endl;
        std::cout << "[CPU] - Response count: " << m_cpuRespCnt << std::endl;
        
        // First check LSQ for stores ready to commit to cache
        if (m_lsq && m_cpuFIFO && !m_cpuFIFO->m_txFIFO.IsFull()) {
            m_lsq->pushToCache();
        }
        
        // First handle any remaining compute instructions from previous line
        if (m_remaining_compute > 0) {
            std::cout << "[CPU] Processing compute instructions (" 
                      << m_remaining_compute << " remaining)" << std::endl;
              
            // Try to allocate ONE compute instruction this cycle
            if (m_rob && m_rob->canAccept() && m_sent_requests < m_number_of_OoO_requests) {
                std::cout << "[CPU] Attempting to allocate compute instruction:" << std::endl;
                
                // Create compute instruction request
                CpuFIFO::ReqMsg compute_req;
                compute_req.msgId = m_cpuReqCnt++;
                compute_req.reqCoreId = m_coreId;
                compute_req.type = CpuFIFO::REQTYPE::COMPUTE;
                compute_req.addr = 0;  // Special value for compute
                compute_req.cycle = m_cpuCycle;
                compute_req.ready = true;  // Compute instructions are ready immediately
                
                std::cout << "[CPU] Created compute request " << compute_req.msgId 
                          << " at cycle " << m_cpuCycle << std::endl;
                
                // Try to allocate in ROB
                if (m_rob->allocate(compute_req)) {
                    m_remaining_compute--;
                    m_sent_requests++;  // Track compute instruction as in-flight
                    std::cout << "[CPU] Successfully allocated compute instruction " 
                              << compute_req.msgId << " (ready immediately)" << std::endl;
                } else {
                    std::cout << "[CPU] ROB allocation failed, will retry next cycle" << std::endl;
                }
            }
            
            // If we still have compute instructions, return and try again next cycle
            if (m_remaining_compute > 0) {
                std::cout << "[CPU] Still have " << m_remaining_compute 
                          << " compute instructions remaining, will continue next cycle" << std::endl;
                return;  // Let Step() handle cycle advancement
            }
        }
        
        // Only proceed to memory instructions if all compute instructions are allocated
        // Check if we can accept new instructions
        if (m_sent_requests >= m_number_of_OoO_requests) {
            std::cout << "[CPU] Maximum in-flight requests reached" << std::endl;
            return;
        }
        
        // Read new trace line if needed and no pending compute instructions
        if (!m_newSampleRdy && !m_bmTrace.eof() && m_remaining_compute == 0) {
            std::string line;
            if (std::getline(m_bmTrace, line)) {
                std::cout << "[CPU] Read trace line: " << line << std::endl;
                
                std::istringstream iss(line);
                uint32_t compute_count;
                std::string type;
                uint64_t addr;
                
                // Parse compute count and type as before, but address as decimal
                if (iss >> std::dec >> compute_count >> addr >> type) {
                    m_remaining_compute = compute_count;
                    std::cout << "[CPU] Found " << compute_count << " compute instructions" << std::endl;
                    
                    // If we have compute instructions, handle them first
                    if (compute_count > 0) {
                        return;  // Process compute instructions next cycle
                    }
                    
                    // Otherwise setup memory request if present
                    if (type == "R" || type == "W") {
                        m_cpuMemReq.msgId = m_cpuReqCnt++;
                        m_cpuMemReq.reqCoreId = m_coreId;
                        m_cpuMemReq.addr = addr;
                        m_cpuMemReq.cycle = m_cpuCycle;
                        m_cpuMemReq.ready = false;
                        
                        if (type == "R") {
                            m_cpuMemReq.type = CpuFIFO::REQTYPE::READ;
                            std::cout << "[CPU] Parsed LOAD: addr=" << addr 
                                      << " msgId=" << m_cpuMemReq.msgId << std::endl;
                        }
                        else {  // type == "W"
                            m_cpuMemReq.type = CpuFIFO::REQTYPE::WRITE;
                            m_cpuMemReq.ready = true;  // Stores ready immediately
                            std::cout << "[CPU] Store instruction " << m_cpuMemReq.msgId 
                                      << " will commit upon LSQ allocation" << std::endl;
                        }
                        m_newSampleRdy = true;
                    }
                }
                else {
                    std::cout << "[CPU] Error: Invalid trace format" << std::endl;
                }
            }
            else {
                m_cpuReqDone = true;
                std::cout << "[CPU] Reached end of trace file" << std::endl;
            }
        }
        
        // Try to allocate memory instruction if we have one
        if (m_newSampleRdy) {
            if (m_rob && m_lsq && m_rob->canAccept() && m_lsq->canAccept() && 
                m_sent_requests < m_number_of_OoO_requests) {
                
                // For loads, check store-to-load forwarding first
                if (m_cpuMemReq.type == CpuFIFO::REQTYPE::READ) {
                    bool forwarded = m_lsq->ldFwd(m_cpuMemReq.addr);
                    if (forwarded) {
                        m_cpuMemReq.ready = true;  // Load got data from LSQ
                        std::cout << "[CPU] Load " << m_cpuMemReq.msgId 
                                  << " committed via store-to-load forwarding" << std::endl;
                    } else {
                        std::cout << "[CPU] No matching store found in LSQ for forwarding" << std::endl;
                    }
                }
                
                // Try ROB first
                bool rob_ok = m_rob->allocate(m_cpuMemReq);
                bool lsq_ok = false;
                
                if (rob_ok) {
                    // Then try LSQ
                    lsq_ok = m_lsq->allocate(m_cpuMemReq);
                    if (!lsq_ok) {
                        m_rob->removeLastEntry();  // Rollback ROB allocation
                        std::cout << "[CPU] LSQ allocation failed - rolled back ROB allocation" << std::endl;
                    } else {
                        m_sent_requests++;  // Track memory request as in-flight
                        std::cout << "[CPU] Successfully allocated " 
                                  << (m_cpuMemReq.type == CpuFIFO::REQTYPE::READ ? "LOAD" : "STORE")
                                  << " to ROB and LSQ" << std::endl;
                        std::cout << "[CPU] Updated in-flight requests: " << m_sent_requests 
                                  << "/" << m_number_of_OoO_requests << std::endl;
                        m_newSampleRdy = false;
                    }
                }
            }
        }
    }

    /**
     * @brief Process receive buffer operations
     * 
     * Handles:
     * 1. Data returning from memory system
     * 2. Committing loads when data arrives
     * 3. Simulation completion check
     */
    void CpuCoreGenerator::ProcessRxBuf() {
        // First check for any responses in the FIFO
        while (!m_cpuFIFO->m_rxFIFO.IsEmpty()) {
            m_cpuMemResp = m_cpuFIFO->m_rxFIFO.GetFrontElement();
            m_cpuFIFO->m_rxFIFO.PopElement();
            
            // Protect against underflow
            if (m_sent_requests > 0) {
                m_sent_requests--;
                m_cpuRespCnt++;  // Use existing response counter
                std::cout << "[CPU] Decremented in-flight requests to " << m_sent_requests 
                          << "/" << m_number_of_OoO_requests << std::endl;
            }
            
            // For loads: mark as ready in ROB and LSQ
            // For stores: remove from LSQ (write confirmed)
            if (m_rob) {
                m_rob->commit(m_cpuMemResp.msgId);
                std::cout << "[CPU] Request " << m_cpuMemResp.msgId 
                          << " committed upon memory system response" << std::endl;
            }
            if (m_lsq) {
                m_lsq->commit(m_cpuMemResp.msgId);  // LSQ will handle based on instruction type
                std::cout << "[CPU] LSQ notified of memory system response for request " 
                          << m_cpuMemResp.msgId << std::endl;
            }
            
            // Track request completion
            m_prevReqFinish = true;
            m_prevReqFinishCycle = m_cpuCycle;
        }
        
        // Check if simulation is complete
        if (m_cpuReqDone && m_cpuRespCnt >= m_cpuReqCnt) {
            m_cpuCoreSimDone = true;
            std::cout << "\n[CPU] Core " << m_coreId << " simulation complete at cycle " 
                      << m_cpuCycle << std::endl;
            std::cout << "[CPU] Processed " << m_cpuReqCnt << " requests with " 
                      << m_cpuRespCnt << " responses" << std::endl;
        }
    }

    /**
     * @brief Main step function called each cycle
     * 
     * Handles:
     * 1. ROB retirement
     * 2. LSQ operations
     * 3. Processing TX and RX buffers
     */
    void CpuCoreGenerator::Step(Ptr<CpuCoreGenerator> cpuCoreGenerator) {
        std::cout << "\n[CPU] ========== Cycle " << cpuCoreGenerator->m_cpuCycle << " ==========" << std::endl;
        
        // Update ROB cycle
        if (cpuCoreGenerator->m_rob) {
            cpuCoreGenerator->m_rob->setCycle(cpuCoreGenerator->m_cpuCycle);
            cpuCoreGenerator->m_rob->step();
        }
        
        // Update LSQ cycle
        if (cpuCoreGenerator->m_lsq) {
            cpuCoreGenerator->m_lsq->setCycle(cpuCoreGenerator->m_cpuCycle);
            cpuCoreGenerator->m_lsq->step();
            
            // First handle any cache responses
            if (cpuCoreGenerator->m_cpuFIFO && !cpuCoreGenerator->m_cpuFIFO->m_rxFIFO.IsEmpty()) {
                cpuCoreGenerator->m_lsq->rxFromCache();
            }
            
            // Then try to send stores to cache
            if (cpuCoreGenerator->m_cpuFIFO && !cpuCoreGenerator->m_cpuFIFO->m_txFIFO.IsFull()) {
                cpuCoreGenerator->m_lsq->pushToCache();
            }
        }
        
        // Process new instructions
        cpuCoreGenerator->ProcessTxBuf();
        cpuCoreGenerator->ProcessRxBuf();
    }
}

