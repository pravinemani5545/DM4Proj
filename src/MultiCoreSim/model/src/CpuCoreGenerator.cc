/*
 * File  :      MCoreSimProjectXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 16, 2020
 * Modified for Out-of-Order execution support
 */

#include "../header/CpuCoreGenerator.h"

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
    CpuCoreGenerator::CpuCoreGenerator(CpuFIFO* associatedCpuFIFO) {
        m_cpuFIFO = associatedCpuFIFO;
        m_coreId = 0;
        m_dt = 1;
        m_clkSkew = 0;
        m_cpuCycle = 0;
        m_cpuReqCnt = 0;
        m_cpuRespCnt = 0;
        m_prevReqFinish = true;
        m_prevReqFinishCycle = 0;
        m_prevReqArriveCycle = 0;
        m_cpuReqDone = false;
        m_newSampleRdy = false;
        m_cpuCoreSimDone = false;
        m_logFileGenEnable = false;
        m_number_of_OoO_requests = 0;
        m_sent_requests = 0;
        m_remainingComputeInst = 0;
        
        // Initialize Out-of-Order execution components
        m_rob = new ROB();
        m_lsq = new LSQ();
        m_lsq->setCpuFIFO(m_cpuFIFO);
    }

    CpuCoreGenerator::~CpuCoreGenerator() {
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
        m_CtrlsTraceFileName = fileName;
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

    int CpuCoreGenerator::GetDt() {
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
        // Handle compute instructions first
        if (m_remainingComputeInst > 0) {
            std::cout << "\n[ProcessTxBuf] Processing " << m_remainingComputeInst << " compute instructions" << std::endl;
            while (m_remainingComputeInst > 0 && m_rob->canAccept()) {
                CpuFIFO::ReqMsg computeReq;
                computeReq.type = CpuFIFO::REQTYPE::COMPUTE;
                computeReq.addr = 0;  // Special value for compute
                computeReq.msgId = IdGenerator::nextReqId();
                computeReq.reqCoreId = m_coreId;
                
                if (m_rob->allocate(computeReq)) {
                    m_rob->commit(computeReq.msgId);  // Mark ready immediately per 3.3.1
                    m_remainingComputeInst--;
                    std::cout << "[ProcessTxBuf] Allocated compute instruction to ROB, " << m_remainingComputeInst << " remaining" << std::endl;
                }
            }
            return;  // Process remaining compute next cycle
        }

        // Process memory instructions
        if (!m_cpuFIFO->m_txFIFO.IsFull() && !m_cpuReqDone && 
            m_sent_requests < m_number_of_OoO_requests) {
            if (m_rob->canAccept() && m_lsq->canAccept()) {
                if (m_newSampleRdy) {
                    std::cout << "[ProcessTxBuf] Processing memory instruction - Type: " 
                             << (m_cpuMemReq.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE")
                             << " Address: 0x" << std::hex << m_cpuMemReq.addr << std::dec << std::endl;
                    // Try LSQ allocation first
                    if (m_lsq->allocate(m_cpuMemReq)) {
                        // If LSQ allocation succeeds, then allocate to ROB
                        if (m_rob->allocate(m_cpuMemReq)) {
                            if (m_cpuMemReq.type == CpuFIFO::REQTYPE::READ) {
                                // Check store forwarding for loads
                                if (m_lsq->ldFwd(m_cpuMemReq.addr)) {
                                    std::cout << "[ProcessTxBuf] Load forwarding hit" << std::endl;
                                    m_rob->commit(m_cpuMemReq.msgId);
                                    m_lsq->commit(m_cpuMemReq.msgId);
                                }
                            } else {
                                // Mark stores ready in ROB when allocated
                                m_rob->commit(m_cpuMemReq.msgId);
                            }
                            m_newSampleRdy = false;
                            m_sent_requests++;
                            std::cout << "[ProcessTxBuf] Successfully allocated to both ROB and LSQ" << std::endl;
                        } else {
                            // If ROB allocation fails, we need to undo LSQ allocation
                            std::cout << "[ProcessTxBuf] ROB allocation failed, undoing LSQ allocation" << std::endl;
                            m_lsq->removeLastEntry();
                        }
                    }
                }
            }
        }

        // Read next trace line
        if (!m_newSampleRdy) {
            std::string fline;
            if (getline(m_bmTrace, fline)) {
                std::istringstream iss(fline);
                uint32_t computeCount;
                std::string addr, type;
                
                // Parse: computeCount addr type
                iss >> computeCount >> addr >> type;
                
                m_remainingComputeInst = computeCount;
                m_cpuMemReq.addr = (uint64_t)strtol(addr.c_str(), NULL, 16);
                m_cpuMemReq.type = (type == "R") ? CpuFIFO::REQTYPE::READ : 
                                              CpuFIFO::REQTYPE::WRITE;
                
                m_cpuMemReq.msgId = IdGenerator::nextReqId();
                m_cpuMemReq.reqCoreId = m_coreId;
                m_newSampleRdy = true;
                
                std::cout << "\n[ProcessTxBuf] Read trace line - Compute Count: " << computeCount 
                         << " Address: 0x" << addr 
                         << " Type: " << type << std::endl;
            } else {
                m_cpuReqDone = true;
                std::cout << "[ProcessTxBuf] Reached end of trace file" << std::endl;
            }
        }
    }

    /**
     * @brief Process receive buffer operations
     * 
     * Handles:
     * 1. Cache responses
     *    - Update LSQ and ROB status
     *    - Track statistics
     * 2. ROB and LSQ retirement
     * 3. Simulation completion check
     * 4. Next cycle scheduling
     */
    void CpuCoreGenerator::ProcessRxBuf() {
        // Process cache responses
        if (!m_cpuFIFO->m_rxFIFO.IsEmpty()) {
            auto response = m_cpuFIFO->m_rxFIFO.GetFrontElement();
            m_cpuFIFO->m_rxFIFO.PopElement();
            
            // Update LSQ and ROB
            m_lsq->commit(response.msgId);
            m_rob->commit(response.msgId);
            
            m_sent_requests--;
            m_cpuRespCnt++;
        }
        
        // Call retire on ROB and LSQ every cycle
        m_rob->retire();
        m_lsq->retire();
        
        // Check if simulation is complete
        if (m_cpuReqDone && m_cpuRespCnt >= m_cpuReqCnt && 
            m_rob->isEmpty() && m_lsq->isEmpty()) {
            m_cpuCoreSimDone = true;
            Logger::getLogger()->traceEnd(m_coreId);
            std::cout << "Cpu " << m_coreId << " Simulation End @ processor cycle # " 
                     << m_cpuCycle << std::endl;
        } else {
            // Schedule next cycle
            Simulator::Schedule(NanoSeconds(m_dt), &CpuCoreGenerator::Step,
                              Ptr<CpuCoreGenerator>(this));
            m_cpuCycle++;
        }
    }

    /**
     * @brief Main processing step called each clock cycle
     * @param cpuCoreGenerator Pointer to CPU core
     * 
     * Sequence of operations:
     * 1. Process ROB and LSQ state
     * 2. Handle instruction processing (TX)
     * 3. Handle responses and retirement (RX)
     * 4. Schedule next cycle if needed
     */
    void CpuCoreGenerator::Step(Ptr<CpuCoreGenerator> cpuCoreGenerator) {
        // Process ROB and LSQ every cycle
        cpuCoreGenerator->m_rob->step();
        cpuCoreGenerator->m_lsq->step();
        
        // Process TX and RX buffers
        cpuCoreGenerator->ProcessTxBuf();
        cpuCoreGenerator->ProcessRxBuf();
    }
}
