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


    /**
     * @brief Constructor initializes CPU core with OoO execution support
     * @param associatedCpuFIFO Interface to cache system
     * 
     * Initializes:
     * - Core parameters (ID, clock, skew)
     * - Statistics counters
     * - Out-of-Order components (ROB, LSQ)
     * - Simulation state flags
     */
    
    /**
     * @brief Constructor initializes CPU core with OoO execution support
     * @param associatedCpuFIFO Interface to cache system
     * 
     * Initializes:
     * - Core parameters (ID, clock, skew)
     * - Statistics counters
     * - Out-of-Order components (ROB, LSQ)
     * - Simulation state flags
     */
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
        
        delete m_rob;
        delete m_lsq;
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
        std::cout << "\n[CPU][TX] ========== Core " << m_coreId << " Cycle " << m_cpuCycle << " ==========" << std::endl;
        
        if (!m_rob || !m_lsq) {
            std::cout << "[CPU][TX] ERROR: ROB or LSQ not initialized" << std::endl;
            return;
        }

        // First handle any pending compute instructions
        if (m_remaining_compute > 0) {
            uint32_t allocated_this_cycle = 0;
            const uint32_t max_per_cycle = 4;  // IPC value from ROB
            
            while (allocated_this_cycle < max_per_cycle && m_remaining_compute > 0 && m_rob->canAccept()) {
                CpuFIFO::ReqMsg compute_req;
                compute_req.msgId = m_cpuReqCnt++;
                compute_req.reqCoreId = m_coreId;
                compute_req.addr = 0;  // Special value for compute instructions
                compute_req.cycle = m_cpuCycle;
                compute_req.type = CpuFIFO::REQTYPE::COMPUTE;
                compute_req.ready = true;  // 3.3.1: Compute commits (ready) on allocation
                
                std::cout << "[CPU][TX] Allocating compute instruction " << compute_req.msgId 
                         << " (" << allocated_this_cycle + 1 << "/" << max_per_cycle << " this cycle)" << std::endl;
                
                if (m_rob->allocate(compute_req)) {
                    m_remaining_compute--;
                    allocated_this_cycle++;
                    std::cout << "[CPU][TX] Compute instruction allocated and committed, " 
                             << m_remaining_compute << " remaining" << std::endl;
                } else {
                    std::cout << "[CPU][TX] ROB full - cannot allocate more this cycle" << std::endl;
                    break;
                }
            }
            
            if (m_remaining_compute > 0) {
                std::cout << "[CPU][TX] Still have " << m_remaining_compute 
                         << " compute instructions - will continue next cycle" << std::endl;
                return;  // Must finish compute instructions before memory op
            }
        }

        // Then handle memory operation if one is ready
        if (m_newSampleRdy && m_sent_requests < m_number_of_OoO_requests) {
            // Check both ROB and LSQ can accept before allocating to either
            if (m_rob->canAccept() && m_lsq->canAccept()) {
                bool should_allocate = true;
                
                // For loads, check store forwarding first
                if (m_cpuMemReq.type == CpuFIFO::REQTYPE::READ) {
                    // 3.3.3: Load commits if it hits in LSQ
                    if (m_lsq->ldFwd(m_cpuMemReq.addr)) {
                        m_cpuMemReq.ready = true;  // Load commits due to forwarding
                    }
                } else if (m_cpuMemReq.type == CpuFIFO::REQTYPE::WRITE) {
                    // 3.3.2: Store commits on allocation to LSQ
                    m_cpuMemReq.ready = true;
                }
                
                // Try to allocate to both ROB and LSQ
                if (should_allocate) {
                    bool rob_ok = m_rob->allocate(m_cpuMemReq);
                    if (rob_ok) {
                        bool lsq_ok = m_lsq->allocate(m_cpuMemReq);
                        if (!lsq_ok) {
                            m_rob->removeLastEntry();
                        } else {
                            m_sent_requests++;
                            m_newSampleRdy = false;
                        }
                    }
                }
            }
            return;  // Wait for memory op to complete before next instruction group
        }

        // Read next instruction group if current one is complete
        if (!m_newSampleRdy && !m_bmTrace.eof()) {
            std::string line;
            if (std::getline(m_bmTrace, line)) {
                std::istringstream iss(line);
                uint32_t compute_count;
                std::string type;
                uint64_t addr;
                
                if (iss >> std::dec >> compute_count >> addr >> type) {
                    // Set up next instruction group
                    m_remaining_compute = compute_count;
                    
                    if (type == "R" || type == "W") {
                        m_cpuMemReq.msgId = m_cpuReqCnt++;
                        m_cpuMemReq.reqCoreId = m_coreId;
                        m_cpuMemReq.addr = addr;
                        m_cpuMemReq.cycle = m_cpuCycle;
                        m_cpuMemReq.ready = false;  // Will be set according to 3.3.1-3.3.3
                        m_cpuMemReq.type = (type == "R" ? CpuFIFO::REQTYPE::READ : CpuFIFO::REQTYPE::WRITE);
                        m_newSampleRdy = true;
                    }
                }
            } else {
                m_cpuReqDone = true;
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
        std::cout << "\n[CPU][RX] ========== Core " << m_coreId << " Cycle " << m_cpuCycle << " ==========" << std::endl;
        
        if (!m_cpuFIFO) {
            return;
        }

        // Process all available responses
        while (!m_cpuFIFO->m_rxFIFO.IsEmpty()) {
            m_cpuMemResp = m_cpuFIFO->m_rxFIFO.GetFrontElement();
            m_cpuFIFO->m_rxFIFO.PopElement();
            
            // Update response tracking
            m_cpuRespCnt++;
            if (m_sent_requests > 0) {
                m_sent_requests--;
            }
            
            // Notify ROB of completion
            if (m_rob) {
                m_rob->commit(m_cpuMemResp.msgId);
            }
            
            m_prevReqFinishCycle = m_cpuCycle;
        }
        
        // Check for simulation completion
        if (m_cpuReqDone && m_sent_requests == 0 && 
            m_rob && m_rob->isEmpty() && 
            m_lsq && m_lsq->isEmpty()) {
            m_cpuCoreSimDone = true;
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
    if (!cpuCoreGenerator->GetCpuSimDoneFlag()) {
        // Update components
        if (cpuCoreGenerator->m_rob) {
            cpuCoreGenerator->m_rob->setCycle(cpuCoreGenerator->m_cpuCycle);
            cpuCoreGenerator->m_rob->step();
        }
        
        if (cpuCoreGenerator->m_lsq) {
            cpuCoreGenerator->m_lsq->setCycle(cpuCoreGenerator->m_cpuCycle);
            cpuCoreGenerator->m_lsq->step();
        }
        
        // Process buffers
        cpuCoreGenerator->ProcessRxBuf();  // Handle responses first
        cpuCoreGenerator->ProcessTxBuf();  // Then try to send new requests
        
        // Increment cycle
        cpuCoreGenerator->m_cpuCycle++;
        
        // Schedule next step
        if (!cpuCoreGenerator->GetCpuSimDoneFlag()) {
            Simulator::Schedule(Seconds(cpuCoreGenerator->GetDt()), 
                              &CpuCoreGenerator::Step, cpuCoreGenerator);
        }
    }
}



    void CpuCoreGenerator::onInstructionRetired(const CpuFIFO::ReqMsg& request) {
        m_sent_requests--;  // Decrement in-flight count
        std::cout << "[CPU] Instruction " << request.msgId << " retired, in-flight: " 
                  << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
    }
}

