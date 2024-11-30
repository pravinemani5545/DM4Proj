/*
 * File  :      MCoreSimProjectXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 16, 2020
 */

#include "../header/CpuCoreGenerator.h"

namespace ns3 {

    // override ns3 type
    TypeId CpuCoreGenerator::GetTypeId(void) {
        static TypeId tid = TypeId("ns3::CpuCoreGenerator")
               .SetParent<Object > ();
        return tid;
    }

    // The only constructor
    CpuCoreGenerator::CpuCoreGenerator(CpuFIFO* associatedCpuFIFO) {
        // default
        m_coreId         = 1;
        m_cpuCycle       = 1;
        m_bmFileName     = "trace_C0.trc";
        m_dt             = 1;
        m_clkSkew        = 0;
        m_cpuMemReq      = CpuFIFO::ReqMsg();
        m_cpuMemResp     = CpuFIFO::RespMsg();
        m_cpuFIFO        = associatedCpuFIFO;
        m_cpuReqDone     = false;
        m_newSampleRdy   = false;
        m_cpuCoreSimDone = false;
        m_logFileGenEnable = false;
        m_prevReqFinish    = true;
        m_prevReqFinishCycle = 0;
        m_prevReqArriveCycle = 0;
        m_cpuReqCnt      = 0;
        m_cpuRespCnt     = 0;
        m_number_of_OoO_requests = 1;
        m_sent_requests = 0;

        // Initialize ROB and LSQ
        m_rob = CreateObject<ROB>(32);  // Size of 32 entries
        m_lsq = CreateObject<LSQ>(16);  // Size of 16 entries
    }

    // We don't do any dynamic allocations
    CpuCoreGenerator::~CpuCoreGenerator() {
    }

    // set Benchmark file name
    void CpuCoreGenerator::SetBmFileName (std::string bmFileName) {
        m_bmFileName = bmFileName;
    }

    void CpuCoreGenerator::SetCpuTraceFile (std::string fileName) {
        m_cpuTraceFileName = fileName; 
    }

    void CpuCoreGenerator::SetCtrlsTraceFile (std::string fileName) {
        m_CtrlsTraceFileName = fileName;
    }

    // set CoreId
    void CpuCoreGenerator::SetCoreId (int coreId) {
      m_coreId = coreId;
    }

    // get core id
    int CpuCoreGenerator::GetCoreId () {
      return m_coreId;
    }

    // set dt
    void CpuCoreGenerator::SetDt (double dt) {
      m_dt = dt;
    }

    // get dt
    int CpuCoreGenerator::GetDt () {
      return m_dt;
    }

    // set clk skew
    void CpuCoreGenerator::SetClkSkew (double clkSkew) {
       m_clkSkew = clkSkew;
    }

    // get simulation done flag
    bool CpuCoreGenerator::GetCpuSimDoneFlag() {
      return m_cpuCoreSimDone;
    }

    void CpuCoreGenerator::SetLogFileGenEnable (bool logFileGenEnable ) {
      m_logFileGenEnable = logFileGenEnable;
    }

    void CpuCoreGenerator::SetOutOfOrderStages(int stages)
    {
      m_number_of_OoO_requests = stages;
      //std::cout<<"stages="<<m_number_of_OoO_requests<<std::endl;
    }
    
    // The init function starts the generator calling once very m_dt NanoSeconds.
    void CpuCoreGenerator::init() {
        m_bmTrace.open(m_bmFileName.c_str());
        Simulator::Schedule(NanoSeconds(m_clkSkew), &CpuCoreGenerator::Step, Ptr<CpuCoreGenerator > (this));
    }

    // This function does most of the functionality.
    void CpuCoreGenerator::ProcessTxBuf() {
        std::string fline;
        uint64_t newArrivalCycle;

        Logger::getLogger()->setClkCount(this->m_coreId, this->m_cpuCycle);
        
        if ((m_cpuFIFO->m_txFIFO.IsFull() == false) && 
            (m_cpuReqDone == false) &&
            (!m_rob->isFull()))  // Check ROB space
        {   
           if(m_sent_requests < m_number_of_OoO_requests)
           {
            if (m_newSampleRdy == true) {
                newArrivalCycle = m_prevReqFinishCycle + m_cpuMemReq.cycle - m_prevReqArriveCycle;

                if (m_cpuCycle >= newArrivalCycle) {
                    m_newSampleRdy = false;
                    m_prevReqFinish = false;
                    m_cpuMemReq.fifoInserionCycle = m_cpuCycle;

                    // For loads, check LSQ for store forwarding before allocation
                    if (m_cpuMemReq.type == CpuFIFO::REQTYPE::READ && m_lsq->hasStore(m_cpuMemReq.addr)) {
                        if (m_lsq->ldFwd(m_cpuMemReq.addr)) {
                            // Store is ready, can forward data
                            m_cpuMemReq.ready = true;
                        }
                    }

                    // Allocate to ROB first
                    if (m_rob->allocate(m_cpuMemReq)) {
                        // For memory operations, also allocate to LSQ
                        if (m_cpuMemReq.type != CpuFIFO::REQTYPE::COMPUTE) {
                            if (!m_lsq->allocate(m_cpuMemReq)) {
                                // LSQ allocation failed, don't proceed
                                return;
                            }
                        }

                        m_cpuFIFO->m_txFIFO.InsertElement(m_cpuMemReq);
                        Logger::getLogger()->addRequest(this->m_coreId, m_cpuMemReq);
                        m_sent_requests++;

                        if (m_logFileGenEnable) {
                            std::cout << "Cpu " << m_coreId << " MemReq: ReqId = " << m_cpuMemReq.msgId 
                                    << ", CpuRefCycle = " << m_cpuMemReq.cycle 
                                    << ", Type = " << (m_cpuMemReq.type == CpuFIFO::REQTYPE::COMPUTE ? "COMPUTE" :
                                                     m_cpuMemReq.type == CpuFIFO::REQTYPE::READ ? "READ" : "WRITE")
                                    << ", CpuClkTic = " <<  m_cpuCycle << std::endl;
                        }
                    }
                }
            }
           }

           if (m_newSampleRdy == false) {
                if (getline(m_bmTrace,fline)) {
                    m_newSampleRdy = true;
                    
                    // Find positions of both spaces
                    size_t first_space = fline.find(" ");
                    size_t second_space = fline.find(" ", first_space + 1);
                    
                    // Parse the three components
                    std::string first_num = fline.substr(0, first_space);
                    std::string addr = fline.substr(first_space + 1, second_space - first_space - 1);
                    std::string type = fline.substr(second_space + 1, 1);

                    m_cpuMemReq.addr = (uint64_t) strtol(addr.c_str(), NULL, 16);
                    // Use first number as cycle
                    m_cpuMemReq.cycle = (uint64_t) strtol(first_num.c_str(), NULL, 10);
                    
                    if (type == "C") {
                        m_cpuMemReq.type = CpuFIFO::REQTYPE::COMPUTE;
                        m_cpuMemReq.numComputeInst = 1;  // Default to 1 compute instruction
                    } else {
                        m_cpuMemReq.type = (type == "R") ? CpuFIFO::REQTYPE::READ : CpuFIFO::REQTYPE::WRITE;
                        m_cpuMemReq.numComputeInst = 0;
                    }

                    m_cpuMemReq.msgId = IdGenerator::nextReqId();
                    m_cpuMemReq.reqCoreId = m_coreId;
                    m_cpuReqCnt++;
                }
           } 
        }

        if (m_bmTrace.eof()) {
            m_bmTrace.close();
            m_cpuReqDone = true;
        }           
    }

    void CpuCoreGenerator::ProcessRxBuf() {
        // Process received buffer
        if (!m_cpuFIFO->m_rxFIFO.IsEmpty()) {
            m_cpuMemResp = m_cpuFIFO->m_rxFIFO.GetFrontElement();
            m_cpuFIFO->m_rxFIFO.PopElement();
            
            // Commit the instruction in ROB and LSQ
            m_rob->commit(m_cpuMemResp.msgId);
            m_lsq->commit(m_cpuMemResp.msgId);

            Logger::getLogger()->updateRequest(m_cpuMemResp.msgId, Logger::EntryId::CPU_RX_CHECKPOINT);
            m_sent_requests--;
            
            if (m_logFileGenEnable) {
                std::cout << "Cpu " << m_coreId << " new response is received at cycle " << m_cpuCycle << std::endl;
            }
            
            m_prevReqFinish = true;
            m_prevReqFinishCycle = m_cpuCycle;
            m_prevReqArriveCycle = m_cpuMemResp.reqcycle;
            m_cpuRespCnt++;
            
            // Try retiring instructions after receiving response
            ProcessROB();
        }

        // Process ROB and LSQ
        ProcessLSQ();  // Do LSQ first to enable forwarding
        ProcessROB();  // Then try retiring
 
        // Schedule next run or finish simulation
        if (m_cpuReqDone == true && m_sent_requests == 0 && 
            m_rob->isEmpty() && m_lsq->isEmpty()) {
            m_cpuCoreSimDone = true;
            Logger::getLogger()->traceEnd(this->m_coreId);
            std::cout << "Cpu " << m_coreId << " Simulation End @ processor cycle # " << m_cpuCycle << std::endl;
        }
        else {
            Simulator::Schedule(NanoSeconds(m_dt), &CpuCoreGenerator::Step, Ptr<CpuCoreGenerator>(this));
            m_cpuCycle++;
        }
    }

    void CpuCoreGenerator::ProcessROB() {
        // Only try to retire if we have instructions and the head is ready
        if (!m_rob->isEmpty()) {
            if (!m_rob->isHeadReady()) return;

            // For memory operations, need LSQ coordination
            const CpuFIFO::ReqMsg& headReq = m_rob->getHeadRequest();
            if (headReq.type != CpuFIFO::REQTYPE::COMPUTE) {
                if (!m_lsq->isEmpty()) {
                    // Ensure LSQ is ready and memory ordering is maintained
                    if (!m_lsq->canExecute(headReq) || !m_lsq->retire(headReq)) {
                        return;
                    }
                }
            }
            
            // Now safe to retire from ROB
            m_rob->retire();
            if (m_logFileGenEnable) {
                std::cout << "Cpu " << m_coreId << " retired instruction at cycle " << m_cpuCycle << std::endl;
            }
        }
    }

    void CpuCoreGenerator::ProcessLSQ() {
        if (m_rob->isEmpty()) return;

        // Check for store-load forwarding opportunities
        if (!m_rob->isHeadReady()) {
            const CpuFIFO::ReqMsg& headReq = m_rob->getHeadRequest();
            if (headReq.type == CpuFIFO::REQTYPE::READ) {
                if (m_lsq->hasStore(headReq.addr)) {
                    // Check if we can forward from a ready store
                    if (m_lsq->ldFwd(headReq.addr)) {
                        // Mark as ready in both ROB and LSQ
                        m_rob->commit(headReq.msgId);
                        m_lsq->commit(headReq.msgId);
                        
                        if (m_logFileGenEnable) {
                            std::cout << "Cpu " << m_coreId << " forwarded store to load at cycle " << m_cpuCycle << std::endl;
                        }
                    }
                }
            }
        }
    }

    /**
     * Runs one mobility Step for the given vehicle generator.
     * This function is called each interval dt
     */
    void CpuCoreGenerator::Step(Ptr<CpuCoreGenerator> cpuCoreGenerator) {
        cpuCoreGenerator->ProcessTxBuf();
        cpuCoreGenerator->ProcessRxBuf();
    }
}
