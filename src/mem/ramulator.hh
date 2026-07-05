#ifndef __RAMULATOR_HH__
#define __RAMULATOR_HH__

#include <deque>
#include <tuple>
#include <map>

#include "mem/abstract_mem.hh"
#include "params/Ramulator.hh"
#include "Ramulator/src/Config.h"
//#include "Ramulator/src/Iprefetcher.h"
#include "Ramulator/src/iPrefetcherCircBuffer.h"
#include "interstellar/base_metaisa.hpp"
#include "Ramulator/src/Statistics.h"
#define MAX_PROCESSORS 16

namespace ramulator{
    class Request     ;
    class Gem5Wrapper ;
    class Iprefetcher ;
}
using namespace gem5;
using namespace gem5::memory;


#define MAX_BANKS          16           // Maximum number of banks in DRAM Channel -> may be better to have crntRowReqsCount as pointer and allocate the size inside controller constructor
#define MAX_CHANNELS       256 
#define MAX_BANKS_DDR4     16 

class Ramulator : public gem5::memory::AbstractMemory {
private:

    class MemoryPort: public ResponsePort {
    private:
        Ramulator& mem;
    public:
        MemoryPort(const std::string& _name, Ramulator& _mem): ResponsePort(_name), mem(_mem) {}
    protected:


        Tick recvAtomic(PacketPtr pkt) {
            // modified to perform a fixed latency return for atomic packets to enable fast forwarding
            // assert(false && "only accepts functional or timing packets");
            return mem.recvAtomic(pkt);
        }
        
        void recvFunctional(PacketPtr pkt) {
            mem.recvFunctional(pkt);
        }

        bool recvTimingReq(PacketPtr pkt) {
            return mem.recvTimingReq(pkt);
        }

        void recvRespRetry() {
            mem.recvRespRetry();
        }

        AddrRangeList getAddrRanges() const {
            AddrRangeList ranges;
            ranges.push_back(mem.getAddrRange());
            return ranges;
        }
    } port;

    unsigned int requestsInFlight;
    std::map<long, std::deque<PacketPtr> > reads;
    std::map<long, std::deque<PacketPtr> > writes;
    std::deque<PacketPtr> resp_queue;
    std::deque<PacketPtr> pending_del;

    std::string config_file;
    std::string cmdTracePath;
    ::ramulator::Config configs;
    ::ramulator::Gem5Wrapper *wrapper;
    std::function<void(ramulator::Request&,int bank_id)> read_cb_func;
    std::function<void(ramulator::Request&,int bank_id)> write_cb_func;
    Tick ticks_per_clk;
    bool     resp_stall     ;
    bool     req_stall      ;
    bool     is_ideal_mem   ;
    uint64_t last_req_tick  ; 
    int      channels       ; // To support multi-channels InterStellar
    // gagan :
    Tick warmuptime;
    ofstream mem_trace_file[8]; // Memory traces for 8 cores s
    unsigned int numOutstanding() const { return requestsInFlight + resp_queue.size(); }
    
    void sendResponse();
    void tick();
    
    EventWrapper<Ramulator, &Ramulator::sendResponse> send_resp_event;
    EventWrapper<Ramulator, &Ramulator::tick> tick_event;

public:
    typedef RamulatorParams Params;
    Ramulator(const RamulatorParams &p);
    virtual void init();
    virtual void startup();
    DrainState drain() override;
    virtual Port& getPort(const std::string& if_name, 
        PortID idx = InvalidPortID);
    ~Ramulator();

protected:
    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();
    void accessAndRespond(PacketPtr pkt);
    void readComplete(ramulator::Request& req,int bank_id);
    void writeComplete(ramulator::Request& req,int bank_id);
    //Aotaleb: Intelligent Prefetcher 
    ramulator::circBuffer<ramulator::iBufferKeyClass, ramulator::IpreftecherQueueEntry>* iBatchBuffer[MAX_CHANNELS][MAX_BANKS];
    bool         iBatchAllocated; 

    ramulator::ScalarStat tot_Req_iBatched_Completed ;   
    ramulator::VectorStat iprefetches_completed_cnt_per_Stream;
    ramulator::VectorStat iprefetches_wasted_per_Stream;

};

//} // namespace memory
//} // namespace gem5

#endif // __RAMULATOR_HH__
