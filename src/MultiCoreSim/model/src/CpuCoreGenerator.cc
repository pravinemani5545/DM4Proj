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
        std::cout << "[CPU][TX] Pipeline State:" << std::endl;
        std::cout << "[CPU][TX] - In-flight requests: " << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
        std::cout << "[CPU][TX] - Remaining compute: " << m_remaining_compute << std::endl;
        std::cout << "[CPU][TX] - Total requests: " << m_cpuReqCnt << std::endl;
        std::cout << "[CPU][TX] - Total responses: " << m_cpuRespCnt << std::endl;
        
        // First check LSQ for stores ready to commit to cache
        if (m_lsq && m_cpuFIFO && !m_cpuFIFO->m_txFIFO.IsFull()) {
            std::cout << "[CPU][TX] Checking LSQ for stores to push to cache" << std::endl;
            m_lsq->pushToCache();
        }
        
        // Handle remaining compute instructions
        if (m_remaining_compute > 0) {
            std::cout << "[CPU][TX] Processing compute instructions (" << m_remaining_compute << " remaining)" << std::endl;
            
            // Try to allocate as many compute instructions as possible this cycle
            while (m_remaining_compute > 0 && m_rob && m_rob->canAccept()) {
                std::cout << "[CPU][TX] Creating compute instruction:" << std::endl;
                
                CpuFIFO::ReqMsg compute_req;
                compute_req.msgId = m_cpuReqCnt++;
                compute_req.reqCoreId = m_coreId;
                compute_req.type = CpuFIFO::REQTYPE::COMPUTE;
                compute_req.addr = 0;
                compute_req.cycle = m_cpuCycle;
                compute_req.ready = true;
                
                std::cout << "[CPU][TX] - ID: " << compute_req.msgId << std::endl;
                std::cout << "[CPU][TX] - Cycle: " << compute_req.cycle << std::endl;
                
                if (m_rob->allocate(compute_req)) {
                    m_remaining_compute--;
                    std::cout << "[CPU][TX] Compute instruction allocated, " << m_remaining_compute << " remaining" << std::endl;
                } else {
                    std::cout << "[CPU][TX] ROB allocation failed, will retry next cycle" << std::endl;
                    break;
                }
            }
            
            if (m_remaining_compute > 0) {
                std::cout << "[CPU][TX] Still have compute instructions, continuing next cycle" << std::endl;
                return;
            }
        }
        
        // Process memory operations
        if (m_sent_requests < m_number_of_OoO_requests) {
            if (!m_newSampleRdy && !m_bmTrace.eof()) {
                std::string line;
                if (std::getline(m_bmTrace, line)) {
                    std::cout << "[CPU][TX] Read trace line: " << line << std::endl;
                    
                    std::istringstream iss(line);
                    uint32_t compute_count;
                    std::string type;
                    uint64_t addr;
                    
                    if (iss >> std::dec >> compute_count >> addr >> type) {
                        m_remaining_compute = compute_count;
                        std::cout << "[CPU][TX] Parsed trace line:" << std::endl;
                        std::cout << "[CPU][TX] - Compute count: " << compute_count << std::endl;
                        std::cout << "[CPU][TX] - Address: 0x" << std::hex << addr << std::dec << std::endl;
                        std::cout << "[CPU][TX] - Type: " << type << std::endl;
                        
                        if (compute_count > 0) {
                            std::cout << "[CPU][TX] Found compute instructions, will process next cycle" << std::endl;
                            return;
                        }
                        
                        if (type == "R" || type == "W") {
                            m_cpuMemReq.msgId = m_cpuReqCnt++;
                            m_cpuMemReq.reqCoreId = m_coreId;
                            m_cpuMemReq.addr = addr;
                            m_cpuMemReq.cycle = m_cpuCycle;
                            m_cpuMemReq.ready = false;
                            m_cpuMemReq.type = (type == "R" ? CpuFIFO::REQTYPE::READ : CpuFIFO::REQTYPE::WRITE);
                            
                            std::cout << "[CPU][TX] Created memory request:" << std::endl;
                            std::cout << "[CPU][TX] - ID: " << m_cpuMemReq.msgId << std::endl;
                            std::cout << "[CPU][TX] - Type: " << (type == "R" ? "READ" : "WRITE") << std::endl;
                            std::cout << "[CPU][TX] - Address: 0x" << std::hex << addr << std::dec << std::endl;
                            
                            m_newSampleRdy = true;
                        }
                    }
                } else {
                    m_cpuReqDone = true;
                    std::cout << "[CPU][TX] Reached end of trace file" << std::endl;
                }
            }
            
            if (m_newSampleRdy) {
                std::cout << "[CPU][TX] Attempting to allocate memory request " << m_cpuMemReq.msgId << std::endl;
                
                if (m_rob && m_lsq && m_rob->canAccept() && m_lsq->canAccept()) {
                    bool rob_ok = m_rob->allocate(m_cpuMemReq);
                    bool lsq_ok = false;
                    
                    if (rob_ok) {
                        lsq_ok = m_lsq->allocate(m_cpuMemReq);
                        if (!lsq_ok) {
                            m_rob->removeLastEntry();
                            std::cout << "[CPU][TX] LSQ allocation failed - rolled back ROB allocation" << std::endl;
                        } else {
                            m_sent_requests++;
                            m_newSampleRdy = false;
                            std::cout << "[CPU][TX] Memory request allocated to ROB and LSQ" << std::endl;
                            std::cout << "[CPU][TX] Updated in-flight requests: " << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
                        }
                    } else {
                        std::cout << "[CPU][TX] ROB allocation failed" << std::endl;
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
        std::cout << "\n[CPU][RX] ========== Core " << m_coreId << " Cycle " << m_cpuCycle << " ==========" << std::endl;
        
        // Check for memory responses
        while (!m_cpuFIFO->m_rxFIFO.IsEmpty()) {
            m_cpuMemResp = m_cpuFIFO->m_rxFIFO.GetFrontElement();
            m_cpuFIFO->m_rxFIFO.PopElement();
            
            std::cout << "[CPU][RX] Processing memory response:" << std::endl;
            std::cout << "[CPU][RX] - ID: " << m_cpuMemResp.msgId << std::endl;
            std::cout << "[CPU][RX] - Address: 0x" << std::hex << m_cpuMemResp.addr << std::dec << std::endl;
            std::cout << "[CPU][RX] - Request Cycle: " << m_cpuMemResp.reqcycle << std::endl;
            std::cout << "[CPU][RX] - Response Cycle: " << m_cpuMemResp.cycle << std::endl;
            
            if (m_sent_requests > 0) {
                m_sent_requests--;
                m_cpuRespCnt++;
                std::cout << "[CPU][RX] Updated in-flight requests: " << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
            }
            
            if (m_rob) {
                m_rob->commit(m_cpuMemResp.msgId);
                std::cout << "[CPU][RX] Notified ROB of completion" << std::endl;
            }
            if (m_lsq) {
                m_lsq->commit(m_cpuMemResp.msgId);
                std::cout << "[CPU][RX] Notified LSQ of completion" << std::endl;
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
    std::cout << "\n[CPU] ========== Cycle " << cpuCoreGenerator->m_cpuCycle << " ==========" << std::endl;

    // Update ROB cycle
    if (cpuCoreGenerator->m_rob) {
        cpuCoreGenerator->m_rob->setCycle(cpuCoreGenerator->m_cpuCycle);
        cpuCoreGenerator->m_rob->step();
    }

    // Update LSQ cycle
    if (cpuCoreGenerator->m_lsq) {
        cpuCoreGenerator->m_lsq->setCycle(cpuCoreGenerator->m_cpuCycle);
        cpuCoreGenerator->m_lsq->step();  // rxFromCache is handled in LSQ::step()

        // Try to send stores to cache
        if (cpuCoreGenerator->m_cpuFIFO && !cpuCoreGenerator->m_cpuFIFO->m_txFIFO.IsFull()) {
            cpuCoreGenerator->m_lsq->pushToCache();
        }
    }

    // Process new instructions
    cpuCoreGenerator->ProcessTxBuf();
    cpuCoreGenerator->ProcessRxBuf();
    cpuCoreGenerator->m_cpuCycle++;
}



    void CpuCoreGenerator::onInstructionRetired(const CpuFIFO::ReqMsg& request) {
        m_sent_requests--;  // Decrement in-flight count
        std::cout << "[CPU] Instruction " << request.msgId << " retired, in-flight: " 
                  << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
    }
}

