/*
 * File  :      MCoreSimProjectXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 16, 2020
 * Modified for Out-of-Order execution support
 */
#ifndef _CpuCoreGenerator_H
#define _CpuCoreGenerator_H

#include "ns3/ptr.h"
#include "ns3/object.h"
#include "ns3/core-module.h"
#include "IdGenerator.h"
#include "MemTemplate.h"
#include "Logger.h"
#include "ROB.h"
#include "LSQ.h"
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

namespace ns3 { 
  /**
   * @brief CPU Core Generator with Out-of-Order execution support
   * 
   * Implements processor instruction execution according to a benchmark trace.
   * Instructions are processed out-of-order using ROB and LSQ while maintaining
   * program order for commits. The generator interfaces with the Cache Controller
   * through request/response buffers.
   * 
   * Key features:
   * - Reads benchmark trace with compute and memory instructions
   * - Manages ROB and LSQ for out-of-order execution
   * - Handles memory operations through cache interface
   * - Maintains program order for instruction retirement
   */
  class CpuCoreGenerator : public ns3::Object {
  private:
    int         m_coreId;              // Core identifier
    double      m_dt;                  // CPU clock period
    double      m_clkSkew;             // CPU clock skew
    uint64_t    m_cpuCycle;            // Current CPU cycle

    uint64_t    m_cpuReqCnt;           // Total requests processed
    uint64_t    m_cpuRespCnt;          // Total responses received

    uint64_t    m_prevReqFinishCycle,  // Cycle when previous request finished
                m_prevReqArriveCycle;   // Cycle when previous request arrived

    bool        m_prevReqFinish;       // Previous request completion status
   
    std::string m_bmFileName;          // Benchmark trace filename
    std::string m_cpuTraceFileName;    // CPU trace output filename
    std::string m_CtrlsTraceFileName;  // Controllers trace filename

    CpuFIFO::ReqMsg  m_cpuMemReq;     // Current memory request
    CpuFIFO::RespMsg m_cpuMemResp;    // Current memory response
 
    std::ifstream m_bmTrace;           // Benchmark trace input stream
    std::ofstream m_cpuTrace;          // CPU trace output stream
    std::ofstream m_ctrlsTrace;        // Controllers trace output stream

    CpuFIFO* m_cpuFIFO;               // Interface to CPU FIFO

    // Out-of-Order execution components
    ROB* m_rob;                        // Reorder Buffer
    LSQ* m_lsq;                        // Load Store Queue

    bool m_cpuReqDone;                // All requests processed flag
    bool m_newSampleRdy;              // New instruction ready flag
    bool m_cpuCoreSimDone;            // Simulation complete flag
    bool m_logFileGenEnable;          // Log file generation enabled

    int m_number_of_OoO_requests;     // Maximum in-flight requests
    int m_sent_requests;              // Current in-flight requests

    uint32_t m_remainingComputeInst;  // Remaining compute instructions

    /**
     * @brief Processes transmit buffer operations
     * 
     * - Reads new instructions from trace
     * - Allocates instructions to ROB/LSQ
     * - Handles compute instruction processing
     * - Manages memory request transmission
     */
    void ProcessTxBuf();

    /**
     * @brief Processes receive buffer operations
     * 
     * - Handles memory responses
     * - Updates ROB/LSQ status
     * - Manages instruction completion
     * - Tracks simulation progress
     */
    void ProcessRxBuf();

  public:
    static TypeId GetTypeId(void);

    /**
     * @brief Constructor
     * @param associatedCpuFIFO CPU FIFO interface for cache communication
     */
    CpuCoreGenerator(CpuFIFO* associatedCpuFIFO);

    ~CpuCoreGenerator();

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
    int GetDt();
    bool GetCpuSimDoneFlag();

    /**
     * @brief Initializes the CPU core generator
     * 
     * - Opens trace files
     * - Initializes ROB and LSQ
     * - Sets up simulation parameters
     */
    void init();
  
    /**
     * @brief Main processing step called each clock cycle
     * 
     * - Processes new instructions
     * - Handles ROB/LSQ operations
     * - Manages memory operations
     * - Updates simulation state
     */
    static void Step(Ptr<CpuCoreGenerator> cpuCoreGenerator);

    // Out-of-Order component setters
    void setROB(ROB* rob) { m_rob = rob; }
    void setLSQ(LSQ* lsq) { m_lsq = lsq; }
  };

}

#endif /* _CpuCoreGenerator_H */
