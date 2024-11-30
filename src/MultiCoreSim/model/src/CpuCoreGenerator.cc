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
        m_lsq->setROB(m_rob);
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
        // First handle compute instructions if any remaining
        if (m_remaining_compute > 0) {
            if (m_rob->canAccept()) {
                // Create compute instruction request
                CpuFIFO::ReqMsg compute_req;
                compute_req.type = CpuFIFO::REQTYPE::COMPUTE;
                compute_req.addr = 0;  // Special value for compute
                compute_req.msgId = IdGenerator::nextReqId();
                compute_req.reqCoreId = m_coreId;
                compute_req.cycle = m_cpuCycle;
                compute_req.fifoInserionCycle = m_cpuCycle;
                
                std::cout << "Processing compute instruction " << m_remaining_compute 
                         << " remaining, msgId: " << compute_req.msgId << std::endl;
                
                // Allocate in ROB (will be marked ready immediately)
                if (m_rob->allocate(compute_req)) {
                    m_remaining_compute--;
                    m_cpuReqCnt++;
                }
                return;  // Process one instruction per cycle
            }
            return;  // Can't proceed to memory ops until compute instructions done
        }

        // Then handle memory operations
        if (!m_cpuFIFO->m_txFIFO.IsFull() && !m_cpuReqDone && m_sent_requests < m_number_of_OoO_requests) {
            if (!m_newSampleRdy) {
                std::string fline;
                if (getline(m_bmTrace, fline)) {
                    // Parse new trace format: [compute_count] [addr] [type]
                    std::istringstream iss(fline);
                    std::string compute_count, addr, type;
                    
                    if (!(iss >> compute_count >> addr >> type)) {
                        return;  // Invalid line format
                    }
                    
                    std::cout << "\nParsed trace line:" << std::endl;
                    std::cout << "  Compute count: " << compute_count << std::endl;
                    std::cout << "  Address: 0x" << addr << std::endl;
                    std::cout << "  Type: " << type << std::endl;
                    
                    // Set compute instructions to process before this memory op
                    m_remaining_compute = std::stoi(compute_count);
                    
                    // Setup memory request
                    m_cpuMemReq.addr = (uint64_t)strtol(addr.c_str(), NULL, 16);
                    m_cpuMemReq.type = (type == "R") ? 
                        CpuFIFO::REQTYPE::READ : 
                        CpuFIFO::REQTYPE::WRITE;
                    m_cpuMemReq.msgId = IdGenerator::nextReqId();
                    m_cpuMemReq.reqCoreId = m_coreId;
                    m_cpuMemReq.cycle = m_cpuCycle;
                    m_cpuMemReq.fifoInserionCycle = m_cpuCycle;
                    m_newSampleRdy = true;
                    
                    std::cout << "Created memory request:" << std::endl;
                    std::cout << "  Type: " << (m_cpuMemReq.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE") << std::endl;
                    std::cout << "  Address: 0x" << std::hex << m_cpuMemReq.addr << std::dec << std::endl;
                    std::cout << "  MsgId: " << m_cpuMemReq.msgId << std::endl;
                }
            }

            // Try to allocate memory operation if ROB and LSQ have space
            if (m_newSampleRdy && m_rob->canAccept() && m_lsq->canAccept()) {
                // First allocate in ROB
                if (m_rob->allocate(m_cpuMemReq)) {
                    // Then try LSQ
                    if (m_lsq->allocate(m_cpuMemReq)) {
                        std::cout << "Successfully allocated memory request in both ROB and LSQ" << std::endl;
                        m_newSampleRdy = false;
                        m_sent_requests++;
                        m_cpuReqCnt++;
                    } else {
                        // LSQ allocation failed, rollback ROB
                        std::cout << "LSQ allocation failed, rolling back ROB" << std::endl;
                        m_rob->removeLastEntry();
                    }
                } else {
                    std::cout << "ROB allocation failed" << std::endl;
                }
            }
        }

        if (m_bmTrace.eof()) {
            m_bmTrace.close();
            m_cpuReqDone = true;
            std::cout << "Reached end of trace file" << std::endl;
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
            m_sent_requests--;
            
            // LSQ will handle marking load as ready and notifying ROB
            m_lsq->rxFromCache();
            
            // Update stats
            m_prevReqFinish = true;
            m_prevReqFinishCycle = m_cpuCycle;
            m_prevReqArriveCycle = response.reqcycle;
            m_cpuRespCnt++;
        }

        // Check for simulation completion
        if (m_cpuReqDone && m_cpuRespCnt >= m_cpuReqCnt) {
            m_cpuCoreSimDone = true;
            Logger::getLogger()->traceEnd(this->m_coreId);
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
        // First process ROB and LSQ steps
        cpuCoreGenerator->m_rob->step();
        cpuCoreGenerator->m_lsq->step();
        
        // Then process TX and RX buffers
        cpuCoreGenerator->ProcessTxBuf();
        cpuCoreGenerator->ProcessRxBuf();
    }
}
