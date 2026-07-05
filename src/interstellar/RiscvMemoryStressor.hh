/*#################################################################
# Memory Stressor GEM5 Simulation Object
# MemoryStressor.hh for generic architecture base
#  MemoryStressor class declaration
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 15 July 2022
#
##################################################################*/

#ifndef __RISCV_MEMORY_STRESSOR_HH__
#define __RISCV_MEMORY_STRESSOR_HH__

#include <string>
#include <iostream>
#include <queue>
#include <map>
#include <vector>
using namespace std;

#include <type_traits>

#include "params/RiscvMemoryStressor.hh"
#include "mem/request.hh"
#include "mem/cache/cache.hh"       /* to point to Cache or use BaseCache
                                   it also include Packet,Port,Requests                  */
#include "cpu/o3/lsq.hh"            //LSQRequest as Trnaslation data structure will be used for now @todo : implement reduced version of it for the stressor
#include "cpu/o3/thread_context.hh" // Thread context to used with the transaltion @todo : implement new version of stressor
#include "arch/riscv/isa.hh"

#include "sim/probe/probe.hh" /* to declare a probe points               */
#include "sim/clocked_object.hh"
#include "base/logging.hh"
#include "base/trace.hh"


#include "enums/TypeCPU.hh"
#include "enums/TypeProbeEnable.hh"
#include <queue> //To store blocked memory requests and try to re-send them later
#include "debug/MemStress_MEMBUS_PKT.hh"

namespace gem5
{


  class CSR
  {
    static int const MAX_SIZE = 1024;
    uint64_t CSR_Reg[MAX_SIZE];

  public:

    static int const BASE_META_CSR = 0x800;
    bool readCSR(uint64_t regIndex, uint64_t &regValue)
    {
      if (regIndex >= 0 && regIndex < MAX_SIZE)
      {
        regValue = CSR_Reg[regIndex];
        return true;
      }
      return false;
    }
    bool writeCSR(uint64_t regIndex, uint64_t regValue)
    {
      if (regIndex >= 0 && regIndex < MAX_SIZE)
      {
        CSR_Reg[regIndex] = regValue;
        return true;
      }
      return false;
    }
  };


  struct mslConfig{

    enum          IAD_MODE {STATIC,MULTIPLE,DYNAMIC_Single,DYNAMIC_Multiple};
    enum     IAD_MODE_MASK {SING=0x1,MULT=0x2};
    IAD_MODE      iad_mode;
    int           iad_LD_static;//Static Load Inter Arrival Delay
    uint64_t      iad_LD_mult_mask  ; //Mask for consecutive Load's dynamic iid
    int           iad_LD_mult_TVal ; // IAD Value for consecutive LD requests with same value after the mask
    int           iad_LD_mult_FVal_Pre,iad_LD_mult_FVal_Post ; // IAD Value for consecutive LD requests with different value after the mask
    int           iad_LD_dyn_N ; //Number of instructions needed to be completed before proceed
    /* Direct Stream Related Parameters */
    uint64_t dirStreamStride;
    uint64_t dirStreamMaxSize;
    uint64_t dirStreamStartVA;

    //Stressor Input Workload Code File Name
    string stressorWorkLoadFName;

  };

  class RiscvMemoryStressor : public ClockedObject
  {
  public:
    /**
     * Port on the memory-side that receives responses.
     * Mostly just forwards requests to the owner
     */
    class DcachePort : public RequestPort
    {
    private:
      /// The object that owns this DCachePort (RiscvMemoryStressor)
      RiscvMemoryStressor *owner;

    public:
      /**
       * Constructor. Just calls the superclass constructor.
       */
      /// If we tried to send a packet and it was blocked, store it here
      PacketPtr blockedPacket;

      DcachePort(const std::string &name, RiscvMemoryStressor *owner) : RequestPort(name, owner), owner(owner), blockedPacket(nullptr)
      {
      }

      /**
       * Send a packet across this port. This is called by the owner and
       * all of the flow control is hanled in this function.
       *
       * @param packet to send.
       */
      bool sendPacket(PacketPtr pkt);

    protected:
      /**
       * Receive a timing response from the response port.
       */
      bool recvTimingResp(PacketPtr pkt) override;

      /**
       * Called by the response port if sendTimingReq was called on this
       * request port (causing recvTimingReq to be called on the response
       * port) and was unsuccesful.
       */
      void recvReqRetry() override;
    };

  private:
    /**         Events         **/
    // The tick event used for scheduling RISCV memory stressor ticks.
    EventFunctionWrapper tickEvent;
    // Resend Blocked Packets events
    EventFunctionWrapper resendBlockedPktEvent;

    /**         Events Functions        **/
    // The function where trial to resend the blocked packets is done
    void sendBlockedRequests();
    // Schedule tick event, rwith a specified number of cycles delay.
    void
    scheduleTickEvent(Cycles delay)
    {
      if (!tickEvent.scheduled())
        schedule(tickEvent, clockEdge(delay));
    }

    /*** Probe Points  **/
    ProbePointArg<gem5::RiscvISA::MetaIsaCsrDataPtr> *ppMemStressCSRWT;
    ProbePointArg<uint64_t *> *ppMemStressPC;
    ProbePointArg<gem5::RequestPtr> *ppMemStressVA2PA;

  protected:
    Tick instCnt;

    int _stressorId;

    /** Cache the cache line size that we get from the system */
    const unsigned int _cacheLineSize;

    /** Data port. */
    DcachePort dcachePort;
    /* Total Number of Threads and Vector for ISA
    to manage read/write on CSR registers */
    std::vector<RiscvISA::ISA *>pISA;
    ThreadID numThreads;
    // CSR Register File
    CSR csrRegFile;

    /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
    /********** WorkLoad COnfiguations and Fetched Data *******/
    /****  Stressor Related Parameters ***/

    mslConfig mslConfigObj;
    string stressorWorkLoadFName;
    void ConfigParse(const std::string &fname);
    void initcsrReqW();
    queue<pair<int, RegVal>> csrReqW; // CSR Write Requests
    std::map<std::string, std::string> configs;
    void writeCSR();
    /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

    // Blocked Packets to try to re-send them (Unblocking)
    std::queue<PacketPtr> blockedPacketQueue;
    uint16_t maxBPQsize; // Maximum size for the Blocked Packet Queue.
    int crntBPQLen;
    int cntrIAD;       // Counter for the inter-arrival delay
    uint64_t crntAddr; // Currrent VA
                       // Handle requests
    bool startNewCycle; // mark start of new request cycle
    bool insertToBlockedPacketQueue(PacketPtr pkt);
    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
    void handleAtomic(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);
    AddrRangeList getAddrRanges() const;

  public:
    BaseMMU *mmu;

    /**    Constructor and destructor **/
    RiscvMemoryStressor(const RiscvMemoryStressorParams &params);
    ~RiscvMemoryStressor();

    // Overriding some of simulation object functions
    void startup() override;
    void regStats() override;
    // Register probe points : CSR Write and PC Increment (needed for MetaISA)
    void regProbePoints() override;

    /* Tick the memory stressor */
    void tick();

    /** Reads this Memory stressor's ID. */
    int stressorId() const { return _stressorId; }
    RequestPort &getDataPort();

    /** Reads a miscellaneous register. */
    RegVal readMiscRegNoEffect(int misc_reg, ThreadID tid) const;

    /**
     * Get a port on this memory stressor.
     *
     * @param if_name the port name
     * @param idx ignored index
     *
     * @return a reference to the port with the given name
     */
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    // Count the total number of instructions - load instructions , and store instructions
    statistics::Scalar instcount, ldcount, stcount;

    /** Pointer to the system. */
    System *system;

    /***** Memory Stressor Core Logic  ***/
  private:
    uint64_t pc_reg;
    static int const STARTUP_CYCLES = 0;
    gem5::ThreadContext *tc;
    int crntThreadID;
    int crntContextID;
    RequestPtr translateAddr(uint64_t reqVAddr, size_t reqSize);
    RequestPtr translateAddrTiming(uint64_t reqVAddr, size_t reqSize);
    RequestPtr translateAddrFunctional(uint64_t reqVAddr, size_t reqSize);
    bool executeLoad(RequestPtr req);
    //@todo implement reduced version for translate data structure of stressor
    o3::LSQ::LSQRequest *pTranslate;
  };
}

#endif
