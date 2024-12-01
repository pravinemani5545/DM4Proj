/*
 * File  :      MemTemplate.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 16, 2020
 * Modified for Out-of-Order execution support
 */

#ifndef _MemTemplate_H
#define _MemTemplate_H

#include "CommunicationInterface.h"
#include "ns3/object.h"
#include <queue>
#include <string>

namespace ns3
{
  /**
   * @brief Generic FIFO template class for communication interfaces
   * 
   * Implements a fixed-depth FIFO queue with standard operations.
   * Used as base for specialized communication interfaces like CPU FIFO.
   */
  template <typename T>
  class GenericFIFO : public ns3::Object
  {
  private:
    std::queue<T> m_FIFO;      // Underlying queue structure
    uint16_t m_fifoDepth;      // Maximum FIFO depth

  public:
    // FIFO configuration
    void SetFifoDepth(int fifoDepth) { m_fifoDepth = fifoDepth; }
    int GetFifoDepth() { return m_fifoDepth; }

    // Queue operations
    void InsertElement(T msg) { m_FIFO.push(msg); }
    void PopElement() { m_FIFO.pop(); }
    T GetFrontElement() { return m_FIFO.front(); }
    void UpdateFrontElement(T msg) { m_FIFO.front() = msg; }

    // Queue status
    int GetQueueSize() { return m_FIFO.size(); }
    bool IsEmpty() { return m_FIFO.empty(); }
    bool IsFull() { return (m_FIFO.size() == m_fifoDepth) ? true : false; }
  };

  /**
   * @brief CPU FIFO interface for memory system communication
   * 
   * Implements request/response communication between CPU and cache.
   * Supports both memory operations (READ/WRITE) and compute instructions.
   * Extended to support Out-of-Order execution with ROB and LSQ.
   */
  class CpuFIFO : public CommunicationInterface
  {
  public:
    /**
     * @brief Request types supported by the CPU
     */
    enum REQTYPE
    {
      READ = 0,      // Load operation
      WRITE = 1,     // Store operation
      REPLACE = 2,   // Cache line replacement
      COMPUTE = 3    // Compute instruction
    };

    /**
     * @brief Memory request message structure
     * 
     * Contains all information needed for memory operations:
     * - Request metadata (ID, core, type)
     * - Memory access info (address, data)
     * - Timing info (cycle, insertion time)
     * - OoO execution status (ready flag)
     * - Compute instruction count for COMPUTE type
     */
    struct ReqMsg
    {
      uint64_t msgId;                // Unique message identifier
      uint16_t reqCoreId;            // Requesting core ID
      uint64_t addr;                 // Memory address
      uint64_t cycle;                // Request cycle
      uint64_t fifoInserionCycle;    // FIFO insertion cycle
      REQTYPE type;                  // Request type
      uint8_t data[8];              // Data payload
      bool ready;                   // OoO execution ready flag
    };

    /**
     * @brief Memory response message structure
     * 
     * Contains response information for memory operations:
     * - Response metadata (ID, address)
     * - Timing info (request cycle, response cycle)
     */
    struct RespMsg
    {
      uint64_t msgId;      // Original request ID
      uint64_t addr;       // Memory address
      uint64_t reqcycle;   // Original request cycle
      uint64_t cycle;      // Response cycle
    };

  public:
    GenericFIFO<ReqMsg> m_txFIFO;    // Transmit FIFO for requests
    GenericFIFO<RespMsg> m_rxFIFO;   // Receive FIFO for responses

    /**
     * @brief Constructor
     * @param id Interface ID
     * @param FIFOs_depth Maximum depth of FIFOs
     */
    CpuFIFO(int id, int FIFOs_depth) : CommunicationInterface(id)
    {
      m_txFIFO.SetFifoDepth(FIFOs_depth);
      m_rxFIFO.SetFifoDepth(FIFOs_depth);
    }

    /**
     * @brief Peek at next message without removing it
     * @param out_msg Output message buffer
     * @return true if message available
     */
    virtual bool peekMessage(Message *out_msg)
    {
      if (!m_txFIFO.IsEmpty())
      {
        *out_msg = Message(m_txFIFO.GetFrontElement().msgId,          //id
                          m_txFIFO.GetFrontElement().addr,            //Addr
                          m_txFIFO.GetFrontElement().cycle,           //Cycle
                          (uint64_t)m_txFIFO.GetFrontElement().type,  //Type
                          m_txFIFO.GetFrontElement().reqCoreId);      //Owner
        return true;
      }
      return false;
    }

    /**
     * @brief Remove front message from transmit FIFO
     */
    virtual void popFrontMessage()
    {
      m_txFIFO.PopElement();
    }

    /**
     * @brief Push response message to receive FIFO
     * @param msg Message to push
     * @param cycle Current cycle
     * @param type Message type
     * @return true if push successful
     */
    virtual bool pushMessage(Message &msg, uint64_t cycle, MessageType type = MessageType::REQUEST)
    {
      // Treat as response if:
      // 1. Type is DATA_RESPONSE, or
      // 2. Message has a valid request cycle (indicating it's a response)
      if (type == MessageType::DATA_RESPONSE || msg.cycle > 0) {
        RespMsg resp_msg;
        resp_msg.msgId = msg.msg_id;
        resp_msg.addr = msg.addr;
        resp_msg.reqcycle = msg.cycle;
        resp_msg.cycle = cycle;
        m_rxFIFO.InsertElement(resp_msg);
        return true;
      }
      return false;  // Not a valid response
    }
  };
}

#endif /* _MemTemplate */
