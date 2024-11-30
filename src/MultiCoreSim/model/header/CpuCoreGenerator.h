/*
 * File  :      MCoreSimProjectXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 16, 2020
 * Modified for Out-of-Order execution support
 */
#ifndef CPU_CORE_GENERATOR_H
#define CPU_CORE_GENERATOR_H

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/core-module.h"
#include "MemTemplate.h"
#include <string>

namespace ns3 {

class CpuFIFO;
class ROB;
class LSQ;

/**
 * @brief CPU Core Generator with Out-of-Order execution support
 */
class CpuCoreGenerator : public ns3::Object {
private:
    // Core configuration
    uint32_t m_coreId;              // Core identifier
    double m_dt;                    // Clock period
    double m_clkSkew;               // Clock skew
    bool m_logFileGenEnable;        // Enable log file generation
    
    // Pipeline components
    CpuFIFO* m_cpuFIFO;            // Interface to memory system
    ROB* m_rob;                     // Reorder buffer
    LSQ* m_lsq;                     // Load-store queue
    
    // Trace file handling
    std::string m_bmFileName;       // Benchmark trace filename
    std::string m_cpuTraceFileName; // CPU trace output filename
    std::string m_ctrlsTraceFileName; // Controllers trace filename
    std::ifstream m_bmTrace;        // Trace file stream
    std::ofstream m_cpuTrace;       // CPU trace output stream
    std::ofstream m_ctrlsTrace;     // Controllers trace stream
    
    // Execution state
    uint64_t m_cpuCycle;            // Current CPU cycle
    uint32_t m_remaining_compute;    // Remaining compute instructions
    bool m_newSampleRdy;            // New trace line ready
    bool m_cpuReqDone;              // Trace processing complete
    bool m_cpuCoreSimDone;          // Simulation complete
    
    // Request tracking
    uint32_t m_sent_requests;       // In-flight memory requests
    uint32_t m_number_of_OoO_requests; // Maximum in-flight requests
    CpuFIFO::ReqMsg m_cpuMemReq;    // Current memory request
    CpuFIFO::RespMsg m_cpuMemResp;  // Current memory response
    uint32_t m_cpuReqCnt;           // Total requests processed
    uint32_t m_cpuRespCnt;          // Total responses received
    
    // Request timing
    bool m_prevReqFinish;           // Previous request complete
    uint64_t m_prevReqFinishCycle;  // Cycle previous request finished
    uint64_t m_prevReqArriveCycle;  // Cycle previous request arrived

public:
    static TypeId GetTypeId(void);  // Required by NS3
    
    // Constructor/Destructor
    CpuCoreGenerator(CpuFIFO* associatedCpuFIFO);
    virtual ~CpuCoreGenerator();
    
    // Configuration methods
    void SetBmFileName(std::string bmFileName);
    void SetCpuTraceFile(std::string fileName);
    void SetCtrlsTraceFile(std::string fileName);
    void SetCoreId(int coreId);
    void SetDt(double dt);
    void SetClkSkew(double clkSkew);
    void SetLogFileGenEnable(bool logFileGenEnable);
    void SetOutOfOrderStages(int stages);
    
    // Getters
    int GetCoreId();
    double GetDt();
    bool GetCpuSimDoneFlag();
    
    // Core functionality
    void init();
    void ProcessTxBuf();
    void ProcessRxBuf();
    static void Step(Ptr<CpuCoreGenerator> cpuCoreGenerator);
    
    // Pipeline component setters
    void setROB(ROB* rob) { m_rob = rob; }
    void setLSQ(LSQ* lsq) { m_lsq = lsq; }
    void setCpuFIFO(CpuFIFO* fifo) { m_cpuFIFO = fifo; }
    
    // Called by ROB when an instruction is retired
    void onInstructionRetired(const CpuFIFO::ReqMsg& request) {
        m_sent_requests--;  // Decrement in-flight count
        std::cout << "[CPU] Instruction " << request.msgId << " retired, in-flight: " 
                  << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
    }
    
    void notifyRequestSentToCache() { 
        m_sent_requests++;  // Track when request is actually sent to cache
        std::cout << "[CPU] Request sent to cache, in-flight: " 
                  << m_sent_requests << "/" << m_number_of_OoO_requests << std::endl;
    }
};

} // namespace ns3

#endif // CPU_CORE_GENERATOR_H
