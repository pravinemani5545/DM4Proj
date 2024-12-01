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
        std::cout << "[CPU][TX] STATE:" << std::endl;
        std::cout << "[CPU][TX] - Remaining compute: " << m_remaining_compute << std::endl;
        std::cout << "[CPU][TX] - New sample ready: " << (m_newSampleRdy ? "Yes" : "No") << std::endl;
        std::cout << "[CPU][TX] - In-flight requests: " << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
        std::cout << "[CPU][TX] - Request count: " << m_cpuReqCnt << std::endl;
        std::cout << "[CPU][TX] - Response count: " << m_cpuRespCnt << std::endl;
        
        // Handle remaining compute instructions from current instruction group
        if (m_remaining_compute > 0) {
            std::cout << "[CPU][TX] DECISION: Processing compute instructions (" << m_remaining_compute << " remaining)" << std::endl;
            
            // Try to allocate as many compute instructions as possible this cycle
            while (m_remaining_compute > 0 && m_rob && m_rob->canAccept()) {
                CpuFIFO::ReqMsg compute_req;
                compute_req.msgId = m_cpuReqCnt++;
                compute_req.reqCoreId = m_coreId;
                compute_req.type = CpuFIFO::REQTYPE::COMPUTE;
                compute_req.addr = 0;
                compute_req.cycle = m_cpuCycle;
                compute_req.ready = true;
                
                std::cout << "[CPU][TX] ATTEMPT: Allocating compute instruction " << compute_req.msgId << std::endl;
                
                if (m_rob->allocate(compute_req)) {
                    m_remaining_compute--;
                    std::cout << "[CPU][TX] SUCCESS: Compute instruction " << compute_req.msgId << " allocated, " << m_remaining_compute << " remaining" << std::endl;
                } else {
                    std::cout << "[CPU][TX] BLOCKED: ROB allocation failed for compute instruction " << compute_req.msgId << std::endl;
                    break;
                }
            }
            
            if (m_remaining_compute > 0) {
                std::cout << "[CPU][TX] EARLY RETURN: Still have " << m_remaining_compute << " compute instructions to process" << std::endl;
                return;  // Must finish compute instructions before memory op
            }
        }
        
        // Process memory operation from current instruction group
        if (m_newSampleRdy) {
            std::cout << "[CPU][TX] DECISION: Attempting to allocate memory request " << m_cpuMemReq.msgId << std::endl;
            
            if (m_rob && m_lsq && m_rob->canAccept() && m_lsq->canAccept()) {
                bool rob_ok = m_rob->allocate(m_cpuMemReq);
                bool lsq_ok = false;
                
                if (rob_ok) {
                    lsq_ok = m_lsq->allocate(m_cpuMemReq);
                    if (!lsq_ok) {
                        m_rob->removeLastEntry();
                        std::cout << "[CPU][TX] FAILED: LSQ allocation failed - rolled back ROB allocation" << std::endl;
                    } else {
                        m_sent_requests++;
                        m_newSampleRdy = false;  // Memory op from current group processed
                        std::cout << "[CPU][TX] SUCCESS: Memory request " << m_cpuMemReq.msgId << " allocated to ROB and LSQ" << std::endl;
                    }
                } else {
                    std::cout << "[CPU][TX] FAILED: ROB allocation failed for memory request " << m_cpuMemReq.msgId << std::endl;
                }
            } else {
                std::cout << "[CPU][TX] BLOCKED: Cannot allocate - ROB/LSQ state: "
                          << "ROB=" << (m_rob ? "present" : "missing") 
                          << " LSQ=" << (m_lsq ? "present" : "missing")
                          << " ROB_accept=" << (m_rob && m_rob->canAccept() ? "yes" : "no")
                          << " LSQ_accept=" << (m_lsq && m_lsq->canAccept() ? "yes" : "no") << std::endl;
            }
            return;  // Wait for memory op to complete before next instruction group
        }
        
        // Read next instruction group if we've completed the current one
        if (!m_newSampleRdy && !m_bmTrace.eof()) {
            std::string line;
            if (std::getline(m_bmTrace, line)) {
                std::cout << "[CPU][TX] READ TRACE: " << line << std::endl;
                
                std::istringstream iss(line);
                uint32_t compute_count;
                std::string type;
                uint64_t addr;
                
                if (iss >> std::dec >> compute_count >> addr >> type) {
                    std::cout << "[CPU][TX] PARSED: compute=" << compute_count << " addr=0x" << std::hex << addr << std::dec << " type=" << type << std::endl;
                    
                    // Set up next instruction group
                    m_remaining_compute = compute_count;
                    
                    if (type == "R" || type == "W") {
                        m_cpuMemReq.msgId = m_cpuReqCnt++;
                        m_cpuMemReq.reqCoreId = m_coreId;
                        m_cpuMemReq.addr = addr;
                        m_cpuMemReq.cycle = m_cpuCycle;
                        m_cpuMemReq.ready = false;
                        m_cpuMemReq.type = (type == "R" ? CpuFIFO::REQTYPE::READ : CpuFIFO::REQTYPE::WRITE);
                        
                        std::cout << "[CPU][TX] CREATED: Memory request " << m_cpuMemReq.msgId << " (" << type << ")" << std::endl;
                        m_newSampleRdy = true;  // Mark that we have a memory op pending
                    }
                    
                    if (m_remaining_compute > 0) {
                        std::cout << "[CPU][TX] EARLY RETURN: Found " << m_remaining_compute << " compute instructions to process first" << std::endl;
                        return;
                    }
                }
            } else {
                m_cpuReqDone = true;
                std::cout << "[CPU][TX] COMPLETE: Reached end of trace file" << std::endl;
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
        std::cout << "[CPU][RX] DETAILED STATE:" << std::endl;
        std::cout << "[CPU][RX] - In-flight requests: " << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
        std::cout << "[CPU][RX] - Total requests: " << m_cpuReqCnt << std::endl;
        std::cout << "[CPU][RX] - Total responses: " << m_cpuRespCnt << std::endl;
        std::cout << "[CPU][RX] - FIFO State: " << (m_cpuFIFO ? "Present" : "Missing") << std::endl;
        if (m_cpuFIFO) {
            std::cout << "[CPU][RX] - RX FIFO Empty: " << (m_cpuFIFO->m_rxFIFO.IsEmpty() ? "Yes" : "No") << std::endl;
        }
        
        // Check for memory responses
        while (!m_cpuFIFO->m_rxFIFO.IsEmpty()) {
            m_cpuMemResp = m_cpuFIFO->m_rxFIFO.GetFrontElement();
            std::cout << "[CPU][RX] Processing memory response:" << std::endl;
            std::cout << "[CPU][RX] - ID: " << m_cpuMemResp.msgId << std::endl;
            std::cout << "[CPU][RX] - Address: 0x" << std::hex << m_cpuMemResp.addr << std::dec << std::endl;
            std::cout << "[CPU][RX] - Request Cycle: " << m_cpuMemResp.reqcycle << std::endl;
            std::cout << "[CPU][RX] - Response Cycle: " << m_cpuMemResp.cycle << std::endl;
            std::cout << "[CPU][RX] - Latency: " << (m_cpuMemResp.cycle - m_cpuMemResp.reqcycle) << " cycles" << std::endl;
            
            m_cpuFIFO->m_rxFIFO.PopElement();
            
            if (m_sent_requests > 0) {
                m_sent_requests--;
                m_cpuRespCnt++;
                std::cout << "[CPU][RX] Updated counters:" << std::endl;
                std::cout << "[CPU][RX] - In-flight: " << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
                std::cout << "[CPU][RX] - Total responses: " << m_cpuRespCnt << std::endl;
            }
            
            if (m_rob) {
                std::cout << "[CPU][RX] Notifying ROB of completion for ID " << m_cpuMemResp.msgId << std::endl;
                m_rob->commit(m_cpuMemResp.msgId);
            }
            if (m_lsq) {
                std::cout << "[CPU][RX] Notifying LSQ of completion for ID " << m_cpuMemResp.msgId << std::endl;
                m_lsq->commit(m_cpuMemResp.msgId);
            }
            
            m_prevReqFinish = true;
            m_prevReqFinishCycle = m_cpuCycle;
        }
        
        // Check simulation completion
        if (m_cpuReqDone && m_cpuRespCnt >= m_cpuReqCnt) {
            m_cpuCoreSimDone = true;
            std::cout << "\n[CPU][RX] ========== Simulation Complete ==========" << std::endl;
            std::cout << "[CPU][RX] Core " << m_coreId << " finished at cycle " << m_cpuCycle << std::endl;
            std::cout << "[CPU][RX] Total requests: " << m_cpuReqCnt << std::endl;
            std::cout << "[CPU][RX] Total responses: " << m_cpuRespCnt << std::endl;
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
    std::cout << "\n[CPU][STEP] ==================== BEGIN CYCLE " << cpuCoreGenerator->m_cpuCycle << " ====================" << std::endl;
    
    // Add 30-cycle limit check
    if (cpuCoreGenerator->m_cpuCycle >= 30) {
        std::cout << "[CPU][STEP] Reached 30-cycle limit, ending simulation" << std::endl;
        cpuCoreGenerator->m_cpuCoreSimDone = true;
        return;
    }

    std::cout << "[CPU][STEP] SimDone=" << (cpuCoreGenerator->m_cpuCoreSimDone ? "true" : "false")
              << " ReqDone=" << (cpuCoreGenerator->m_cpuReqDone ? "true" : "false")
              << " ReqCount=" << cpuCoreGenerator->m_cpuReqCnt 
              << " RespCount=" << cpuCoreGenerator->m_cpuRespCnt << std::endl;

    // Update ROB cycle
    if (cpuCoreGenerator->m_rob) {
        std::cout << "[CPU][STEP] Updating ROB cycle to " << cpuCoreGenerator->m_cpuCycle << std::endl;
        cpuCoreGenerator->m_rob->setCycle(cpuCoreGenerator->m_cpuCycle);
        cpuCoreGenerator->m_rob->step();
    }

    // Update LSQ cycle
    if (cpuCoreGenerator->m_lsq) {
        std::cout << "[CPU][STEP] Updating LSQ cycle to " << cpuCoreGenerator->m_cpuCycle << std::endl;
        cpuCoreGenerator->m_lsq->setCycle(cpuCoreGenerator->m_cpuCycle);
        cpuCoreGenerator->m_lsq->step();
    }

    // Process new instructions and responses
    std::cout << "[CPU][STEP] Starting ProcessTxBuf" << std::endl;
    cpuCoreGenerator->ProcessTxBuf();
    std::cout << "[CPU][STEP] Starting ProcessRxBuf" << std::endl;
    cpuCoreGenerator->ProcessRxBuf();
    
    // Check simulation state before incrementing cycle
    std::cout << "[CPU][STEP] Pre-increment state:" << std::endl;
    std::cout << "[CPU][STEP] - Current cycle: " << cpuCoreGenerator->m_cpuCycle << std::endl;
    std::cout << "[CPU][STEP] - Remaining compute: " << cpuCoreGenerator->m_remaining_compute << std::endl;
    std::cout << "[CPU][STEP] - In-flight requests: " << cpuCoreGenerator->m_sent_requests << "/" 
              << cpuCoreGenerator->m_number_of_OoO_requests << std::endl;
    std::cout << "[CPU][STEP] - New sample ready: " << (cpuCoreGenerator->m_newSampleRdy ? "Yes" : "No") << std::endl;
    std::cout << "[CPU][STEP] - Simulation done: " << (cpuCoreGenerator->m_cpuCoreSimDone ? "Yes" : "No") << std::endl;
    
    // Always increment cycle at end of step
    cpuCoreGenerator->m_cpuCycle++;
    std::cout << "[CPU][STEP] Incremented cycle to " << cpuCoreGenerator->m_cpuCycle << std::endl;
    
    // Schedule next step if not done
    if (!cpuCoreGenerator->m_cpuCoreSimDone) {
        std::cout << "[CPU][STEP] Scheduling next cycle" << std::endl;
        Simulator::Schedule(NanoSeconds(cpuCoreGenerator->m_dt), &CpuCoreGenerator::Step, 
                          Ptr<CpuCoreGenerator>(cpuCoreGenerator));
    } else {
        std::cout << "[CPU][STEP] Not scheduling next cycle - simulation complete" << std::endl;
    }
    
    std::cout << "[CPU][STEP] ==================== END CYCLE " << cpuCoreGenerator->m_cpuCycle << " ====================" << std::endl;
}



    void CpuCoreGenerator::onInstructionRetired(const CpuFIFO::ReqMsg& request) {
        m_sent_requests--;  // Decrement in-flight count
        std::cout << "[CPU] Instruction " << request.msgId << " retired, in-flight: " 
                  << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
    }
}

