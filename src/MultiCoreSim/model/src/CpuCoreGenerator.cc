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
    CpuCoreGenerator::CpuCoreGenerator(CpuFIFO* associatedCpuFIFO) 
        : m_coreId(0),
          m_cpuCycle(0),
          m_remaining_compute(0),
          m_newSampleRdy(false),
          m_cpuReqDone(false),
          m_sent_requests(0),
          m_number_of_OoO_requests(16),  // Default value
          m_cpuReqCnt(0),
          m_cpuRespCnt(0),
          m_prevReqFinish(true),
          m_prevReqFinishCycle(0),
          m_prevReqArriveCycle(0),
          m_dt(1),
          m_cpuFIFO(associatedCpuFIFO),
          m_rob(nullptr),
          m_lsq(nullptr),
          m_logFileGenEnable(false),
          m_cpuCoreSimDone(false) {
              
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
        std::cout << "[CPU] State: compute=" << m_remaining_compute 
                  << " in-flight=" << m_sent_requests << "/" << m_number_of_OoO_requests 
                  << std::endl;
        
        // First handle any remaining compute instructions
        if (m_remaining_compute > 0) {
            std::cout << "[CPU] 3.2: Processing compute instructions (" 
                      << m_remaining_compute << " remaining)" << std::endl;
                      
            while (m_remaining_compute > 0 && m_rob && m_rob->canAccept()) {
                CpuFIFO::ReqMsg compute_req;
                compute_req.msgId = m_cpuReqCnt++;
                compute_req.reqCoreId = m_coreId;
                compute_req.type = CpuFIFO::REQTYPE::COMPUTE;
                compute_req.addr = 0;  // Special value for compute
                compute_req.cycle = m_cpuCycle;
                compute_req.ready = true;  // 3.3.1: Compute ready at allocation
                
                if (m_rob->allocate(compute_req)) {
                    m_remaining_compute--;
                } else {
                    break;  // Try again next cycle if ROB full
                }
            }
            return;  // Must process all compute before moving to memory
        }
        
        // Try to process memory operation if no pending compute instructions
        if (m_newSampleRdy && m_rob && m_lsq) {
            if (m_rob->canAccept() && m_lsq->canAccept()) {
                // Try ROB first
                bool rob_ok = m_rob->allocate(m_cpuMemReq);
                bool lsq_ok = false;
                
                if (rob_ok) {
                    // Then try LSQ
                    lsq_ok = m_lsq->allocate(m_cpuMemReq);
                    if (!lsq_ok) {
                        m_rob->removeLastEntry();  // Rollback ROB allocation
                    } else {
                        m_sent_requests++;
                        m_newSampleRdy = false;
                    }
                }
            }
        }
        
        // Read new trace line if needed
        if (!m_newSampleRdy && m_remaining_compute == 0) {
            std::string line;
            if (std::getline(m_bmTrace, line)) {
                std::cout << "[CPU] Read trace line: " << line << std::endl;
                
                std::istringstream iss(line);
                uint32_t compute_count;
                uint64_t addr;
                std::string type;
                
                if (iss >> compute_count >> addr >> type) {
                    m_remaining_compute = compute_count;
                    std::cout << "[CPU] Found " << compute_count 
                              << " compute instructions" << std::endl;
                    
                    if (type == "R" || type == "W") {
                        m_cpuMemReq.msgId = m_cpuReqCnt;
                        m_cpuMemReq.reqCoreId = m_coreId;
                        m_cpuMemReq.addr = addr;
                        m_cpuMemReq.cycle = m_cpuCycle;
                        m_cpuMemReq.type = (type == "R") ? 
                            CpuFIFO::REQTYPE::READ : CpuFIFO::REQTYPE::WRITE;
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
        if (!m_cpuFIFO->m_rxFIFO.IsEmpty()) {
            m_cpuMemResp = m_cpuFIFO->m_rxFIFO.GetFrontElement();
            m_cpuFIFO->m_rxFIFO.PopElement();
            m_sent_requests--;
            
            if (m_rob) {
                m_rob->commit(m_cpuMemResp.msgId);
            }
            if (m_lsq) {
                m_lsq->commit(m_cpuMemResp.msgId);
            }
            
            m_prevReqFinish = true;
            m_prevReqFinishCycle = m_cpuCycle;
            m_prevReqArriveCycle = m_cpuMemResp.reqcycle;
            m_cpuRespCnt++;
        }
        
        if (m_cpuReqDone && m_cpuRespCnt >= m_cpuReqCnt) {
            m_cpuCoreSimDone = true;
            Logger::getLogger()->traceEnd(m_coreId);
            std::cout << "\n[CPU] Core " << m_coreId << " simulation complete at cycle " 
                      << m_cpuCycle << std::endl;
        } else {
            Simulator::Schedule(NanoSeconds(m_dt), &CpuCoreGenerator::Step, 
                              Ptr<CpuCoreGenerator>(this));
            m_cpuCycle++;
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
        // Update ROB cycle
        if (cpuCoreGenerator->m_rob) {
            cpuCoreGenerator->m_rob->setCycle(cpuCoreGenerator->m_cpuCycle);
            cpuCoreGenerator->m_rob->step();
        }
        
        // Update LSQ cycle
        if (cpuCoreGenerator->m_lsq) {
            cpuCoreGenerator->m_lsq->setCycle(cpuCoreGenerator->m_cpuCycle);
            cpuCoreGenerator->m_lsq->step();
        }
        
        // Process new instructions and responses
        cpuCoreGenerator->ProcessTxBuf();
        cpuCoreGenerator->ProcessRxBuf();
    }
}

