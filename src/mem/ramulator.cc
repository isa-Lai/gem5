#include "base/callback.hh"
#include "mem/ramulator.hh"
#include "Ramulator/src/Gem5Wrapper.h"
#include "Ramulator/src/Request.h"
//#include "Ramulator/src/Iprefetcher.h"
#include "Ramulator/src/iPrefetcherCircBuffer.h"

#include "sim/system.hh"
#include "debug/Ramulator.hh"
#include "debug/RamulatorData.hh"
#include "debug/IPREFETCHER.hh"

using namespace gem5;
using namespace gem5::memory;

// daz3
ramulator::Gem5Wrapper *wrapper1 = NULL;
bool del_wrapper = false;
Tick begin_tick = 0;
Tick print_interval = 200000000; // 0.2ms
unsigned long my_read_cnt = 0;
unsigned long my_write_cnt = 0;
unsigned long my_total_cnt = 0;
int const COREID_MASK              = 0xFFFF;


Ramulator::Ramulator(const RamulatorParams &p) : AbstractMemory(p),
                                                 port(name() + ".port", *this),
                                                 requestsInFlight(0),
                                                 config_file(p.config_file),
                                                 cmdTracePath(p.cmdTracePath),
                                                 configs(p.config_file),
                                                 wrapper(NULL),
                                                 read_cb_func(std::bind(&Ramulator::readComplete, this, std::placeholders::_1,std::placeholders::_2)),
                                                 write_cb_func(std::bind(&Ramulator::writeComplete, this, std::placeholders::_1,std::placeholders::_2)),
                                                 ticks_per_clk(0),
                                                 resp_stall(false),
                                                 req_stall(false),
                                                 is_ideal_mem(p.is_ideal_mem),
                                                 send_resp_event(this),
                                                 tick_event(this)
{
    warmuptime    = p.real_warm_up ;
    last_req_tick =             0  ;
    // Abotaleb: Construct the intelligenet preftecher
    iBatchAllocated        =   true;    
    channels     = stoi(configs["channels"], NULL, 0);

    for(int ch_id  = 0 ; ch_id  < channels  ; ch_id++ )
    {
        for(int ba_ind = 0 ; ba_ind < MAX_BANKS ; ba_ind++)
        {
            iBatchBuffer[ch_id][ba_ind] = new ramulator::circBuffer<ramulator::iBufferKeyClass, ramulator::IpreftecherQueueEntry>();
        }
    }
 
    configs.set_core_num(p.num_cpus);
    configs.set_tracefile_directory(p.output_dir);


    tot_Req_iBatched_Completed
        .name("Total_Requests_Completed_iBatch")
        .desc("Total Requests iBatched before and consumed (Completed by demand/HWP Request).")
        .precision(0);

    iprefetches_completed_cnt_per_Stream
        .init(TOT_STREAMS)
        .name("iBatches_Completed_count_per_stream_")
        .desc("Actual number of requested and completed intelligent batch commands per stream")
        .precision(0);

   iprefetches_wasted_per_Stream
        .init(TOT_STREAMS)
        .name("IPrefetches_Wasted_count_per_stream_")
        .desc("Actual number of requested and completed intelligent prefectches commands per stream")
        .precision(0);

}
Ramulator::~Ramulator()
{
    // delete wrapper;
    // daz3
    if (del_wrapper == false)
    {
        delete wrapper;
        del_wrapper = true;
    }
	for(int i = 0 ; i < 8 ; i ++)
	{
		mem_trace_file[i].close();
	}

}

void Ramulator::init()
{
    if (!port.isConnected())
    {
        fatal("Ramulator port not connected\n");
    }
    else
    {
        port.sendRangeChange();
    }

    if (wrapper1 != NULL)
    {
        wrapper = wrapper1;
    }
    else
    {
        wrapper = new ramulator::Gem5Wrapper(configs,cmdTracePath, system()->cacheLineSize());
        wrapper1 = wrapper;
    }
    // daz3
    // wrapper = new ramulator::Gem5Wrapper(configs, system()->cacheLineSize());
    ticks_per_clk = Tick(wrapper->tCK * sim_clock::as_float::ns);

    DPRINTF(Ramulator, "Instantiated Ramulator with config file '%s' (tCK=%lf, %d ticks per clk)\n",
            config_file.c_str(), wrapper->tCK, ticks_per_clk);
    DPRINTF(Ramulator, "begin_tick = %llu warmuptime = %llu\n", begin_tick, warmuptime);
    // Callback* cb = new MakeCallback<ramulator::Gem5Wrapper, &ramulator::Gem5Wrapper::finish>(wrapper);
    // registerExitCallback(cb);
    //**** ramulator integration ******
    registerExitCallback([this]()
                         { wrapper->finish(); });
}

void Ramulator::startup()
{
    schedule(tick_event, clockEdge());
}

DrainState Ramulator::drain()
{
    return DrainState::Drained;
}

Port &Ramulator::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port")
    {
        return AbstractMemory::getPort(if_name, idx);
    }
    else
    {
        return port;
    }
}

void Ramulator::sendResponse()
{
    assert(!resp_stall);
    assert(!resp_queue.empty());

    DPRINTF(Ramulator, "Attempting to send response\n");

    long addr = resp_queue.front()->getAddr();
    if (addr)
    { /*DO NOTHING. For avoid error unused-variable*/
    }
    if (port.sendTimingResp(resp_queue.front()))
    {
        DPRINTF(Ramulator, "Response to %#lx sent. (Requestor = %#lx) \n", addr, resp_queue.front()->getMetaISARequestorID());
        resp_queue.pop_front();
        if (resp_queue.size() && !send_resp_event.scheduled())
            schedule(send_resp_event, curTick());

        // check if we were asked to drain and if we are now done
    }
    else
        resp_stall = true;

    if(curTick()==6609867)
    {
        cout<<"Reads Queue :"<<endl;
        for (auto const& x : reads)
        {
            std::cout << x.first <<  std::endl;
        }
    }
}

void Ramulator::tick()
{
    wrapper->tick();
    
    
    if (req_stall)
    {
        req_stall = false;
        port.sendRetryReq();
    }
    // AbstractMemory::occupancyL3Cache = L3->occupancy();
    schedule(tick_event, curTick() + ticks_per_clk);
}

// added an atomic packet response function to enable fast forwarding
Tick Ramulator::recvAtomic(PacketPtr pkt)
{
    access(pkt);
    // L3->call(pkt->getAddr());
    //  set an fixed arbitrary 50ns response time for atomic requests
    return pkt->cacheResponding() ? 0 : 50000;
}

void Ramulator::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());
    functionalAccess(pkt);
    for (auto i = resp_queue.begin(); i != resp_queue.end(); ++i)
        pkt->trySatisfyFunctional(*i);
    pkt->popLabel();
}

bool Ramulator::recvTimingReq(PacketPtr pkt)
{
    // we should never see a new request while in retry
    assert(!req_stall);
    int num_cores    = wrapper->get_num_cores(0);
    int num_channels = wrapper->get_ctrls_num();

    for (PacketPtr pendPkt : pending_del)
        delete pendPkt;
    pending_del.clear();

    // daz3
    if (begin_tick == 0)
    {
		ofstream master_trace;
		master_trace.open(cmdTracePath+"/master_trace.trc");
		std::string truncatedPath = cmdTracePath;
		std::size_t pos = cmdTracePath.find("BM");
		if (pos != std::string::npos) {
			std::string truncatedPath = cmdTracePath.substr(pos);
		} else {
			std::cout << "Substring 'BM' not found in cmdTrace!" << std::endl;
		}


		for(int i = 0 ; i < num_cores ; i ++)
		{
			string mem_trace_path    =  truncatedPath+"/mem_" + to_string(i) +"_trace.in" ;		
			master_trace << mem_trace_path <<endl;
			mem_trace_file[i].open(mem_trace_path) ;
		}
		master_trace.close();
		
        begin_tick = curTick();
    }

    if (pkt->cacheResponding())
    {
        // snooper will supply based on copy of packet
        // still target's responsibility to delete packet
        pending_del.push_back(pkt);
        return true;
    }

    // daz3
    if (warmuptime != 0)
        if (curTick() <= (begin_tick + warmuptime))
        {
            my_total_cnt++;
            DPRINTF(Ramulator, " curTick(%llu) <=(begin_tick(%llu) + warmuptime(%llu))\n", curTick(), begin_tick, warmuptime);
            accessAndRespond(pkt);
            return true;
        }


 

    /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
      Abotaleb : Receiving info specifying Streams
      (Needed for any non-squential IPP)                        */
    uint8_t metaIsaStreamType = pkt->getMetaISAStreamType()           ;
    // To avoid use wrong streamID in case of No MetaISA is used
    //@todo : remove the following line and construct Packet to init streamID  to None
    if (pkt->getMetaISAStreamID() > NONE_STR_ID || pkt->getMetaISAStreamID() < 0)
    {
        pkt->setMetaISAStreamID(NONE_STR_ID);
    }
    string descNames[] = {"None", "Dir", "Indir", "Ptr", " Branch", "Code Slice"};
    string metaIsaTypeStr;
    if (metaIsaStreamType > 5 || metaIsaStreamType < 0)
    {
        DPRINTF(Ramulator, "Accepting Request of None Type = %d!\n", metaIsaStreamType);
    }
    else
        metaIsaTypeStr = descNames[metaIsaStreamType];
    DPRINTF(Ramulator, "Accepting Req of Addr=%#lx and Interstellar Requestor ID =%#lx - GEM5 Requestor ID=%#lx\n", pkt->getAddr(), pkt->getMetaISARequestorID(), pkt->requestorId());

    uint8_t  metaIsaStreamID     = pkt->getMetaISAStreamID()        ;
    uint32_t metaIsaRequestorID  = pkt->getMetaISARequestorID()     ; 

    if(metaIsaRequestorID>num_cores)
	{
		//In case of such request recieved 
		//printf("\t::Request from un-initalized source requestor ID is received(A:%#lx)(Tick:%ld)!\n",pkt->getAddr(),curTick());
		metaIsaRequestorID=0;
	}     

    /******************** Ideal DRAM = To create memory request traces   **********************/    
    // Ideal DRAM is used to generate the memory traces.
	//is_ideal_mem = true ;
    if(is_ideal_mem)
    {
		
        // compute elapsed cycles since last request ; last_req_tick = 0 initally 
        uint64_t cur_cycle = (curTick()/clockPeriod());
        uint64_t cpu_inst = cur_cycle- last_req_tick ;
        // produce memory_trace with the following format:
        // <addr> <read/write> <cpu-inst>
        mem_trace_file[metaIsaRequestorID]<<  "0x"<< std::hex << pkt->getAddr()<< " "<<((pkt->isRead())?"READ":"WRITE")<<" "<<std::dec<<cpu_inst<<endl;
        last_req_tick     = cur_cycle;
        // respond to the request immediately [Zero Latency]
        accessAndRespond(pkt);
        return true;
    }	

    int      coreid              = metaIsaRequestorID & COREID_MASK ;
    uint16_t threadID            = metaIsaRequestorID >> 16         ;  
    uint16_t stride              = pkt->getMetaISAStride()          ;
    bool     is_demand           = !pkt->is_HWP()                   ;
    bool     is_hwp              = pkt->is_HWP()                    ;
    bool     deactivate_stream   = pkt->getInvalidateStream()       ;
    bool     is_base_addr        = pkt->getIsMetaISABaseAddr()      ; 
    /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

    bool accepted = true , iPrefetcherAccepted = false;
    if (pkt->isRead())
    {
        // DPRINTF(Ramulator, "context id: %d, thread id: %d\n", pkt->req->contextId(),
        //     pkt->req->threadId());
        // printf("Read Request - addr = %lu - nextaddr = %lu\n",pkt->getAddr(),pkt->getNextAddr());
        ramulator::Request req(pkt->getAddr(), pkt->getNextAddr(), ramulator::Request::Type::READ, read_cb_func, pkt->req->isPrefetch(), coreid); // daz3
        req.setMetaISAParams(metaIsaStreamType, metaIsaStreamID, metaIsaRequestorID)    ;
        req.setiBatchParams ( stride , is_demand , is_hwp ,is_base_addr )               ;    
        req.deactive_stream = deactivate_stream                                         ;
        // ramulator::Request req(pkt->getAddr(), ramulator::Request::Type::READ, read_cb_func, 0);
        
        //@todo Update for MC with many channels: Assume single channel for now
        for(int ch_id  = 0 ; ch_id  < channels  ; ch_id++ )
        {        
            wrapper->updatePrefetcher(ch_id, iBatchBuffer[ch_id]);
        }
        /* 
            Once iPrefetched Request is sent to Memory Controller 
            It will complete its request ( if data is ready)
            So reads queue should be updated before the sent in this case
        */        
        ramulator::iBufferKeyClass iPrefetcherQkey(req.addr, req.metaISAStreamID, req.metaISARequestorID);
        bool inIPDQ = false;
        for(int ch_id  = 0 ; ch_id  < channels  ; ch_id++ )
        {
            for(int ba_ind = 0 ; ba_ind < MAX_BANKS ;  ba_ind++)
            {
                inIPDQ |= (this->iBatchBuffer[ch_id][ba_ind]->find(iPrefetcherQkey) );
            }
        }
        if (inIPDQ)
        {
            reads[req.addr].push_back(pkt);
            // added counter to track requests in flight
            ++requestsInFlight;
            // daz3
            my_read_cnt++;
            my_total_cnt++;
        }    
        accepted            = wrapper->send(req);
        //printf("Ramulator Wrapper Accepted for read pkt %#lx = %d\n",pkt->getAddr(),accepted);
        // accepted = wrapper->send(req);
        if(!inIPDQ)
        {
            if (accepted  )
            {
                reads[req.addr].push_back(pkt);

                if (metaIsaStreamType > 0x0 && metaIsaStreamType < 0x5)
                    DPRINTF(Ramulator, "(%llu) %s: RD to 0x%lx accepted. P %d ,TID %d\n", curTick() / ticks_per_clk, metaIsaTypeStr, req.addr, coreid, threadID);
                else
                    DPRINTF(Ramulator, "(%llu) None: RD to 0x%lx accepted.\n", curTick() / ticks_per_clk, req.addr);

                // added counter to track requests in flight
                ++requestsInFlight;
                // daz3
                my_read_cnt++;
                my_total_cnt++;
            }
            else
            {
                req_stall = true;
            }
        }
        
    }
    else if (pkt->isWrite())
    {
        // Detailed CPU model always comes along with cache model enabled and
        // write requests are caused by cache eviction, so it shouldn't be
        // tallied for any core/thread
        // printf("Write Request - addr = %lu - nextaddr = %lu\n",pkt->getAddr(),pkt->getNextAddr());
        ramulator::Request req(pkt->getAddr(), pkt->getNextAddr(), ramulator::Request::Type::WRITE, write_cb_func, false, coreid);
        req.setMetaISAParams(metaIsaStreamType, metaIsaStreamID, metaIsaRequestorID)   ;
        req.setiBatchParams ( stride , is_demand , is_hwp ,is_base_addr )              ;    
        req.deactive_stream = deactivate_stream                                        ;  

        //  ramulator::Request req(pkt->getAddr(), ramulator::Request::Type::WRITE, write_cb_func, false, 0);       
        //@todo Update for MC with many channels: Assume single channel for now
        for(int ch_id  = 0 ; ch_id  < channels  ; ch_id++ )
        {
            wrapper->updatePrefetcher(ch_id, iBatchBuffer[ch_id]);
        }

        accepted = wrapper->send(req);

        //printf("Ramulator Wrapper Accepted for write pkt %#lx = %d\n",pkt->getAddr(),accepted);

        // accepted = wrapper->send(req);
        if (accepted)
        {

            /*uint8_t * oldDataPtr =pkt->getPtr<uint8_t>();
            for(int pktDataIndx=0;pktDataIndx<pkt->getSize();pktDataIndx++)
            {
                DPRINTF(RamulatorData,"[Write] Addr %#lx has old data is %d\n",pkt->getAddr()+pktDataIndx,oldDataPtr[pktDataIndx]);
            }*/

            accessAndRespond(pkt);
            if (metaIsaStreamType > 0x0 && metaIsaStreamType < 0x4)
                DPRINTF(Ramulator, "(%llu) IPP[%s]: Write to 0x%lx accepted and served. Source:CPU[%d],Thread[%d]\n", curTick() / ticks_per_clk, metaIsaTypeStr, req.addr, coreid, threadID);
            else
                DPRINTF(Ramulator, "(%llu) Non-IPP: Write to 0x%lx accepted and served.\n", curTick() / ticks_per_clk, req.addr);

            // added counter to track requests in flight
            ++requestsInFlight;
            // daz3
            my_write_cnt++;
            my_total_cnt++;
        }
        else
        {
            req_stall = true;
        }
    }
    else
    {
        // keep it simple and just respond if necessary
        DPRINTF(Ramulator, " Request type is %s , Not Read nor Write , so Just simply respond!", pkt->cmdString().c_str());
        accessAndRespond(pkt);
        // daz3
        my_total_cnt++;
    }
    return accepted;
}

void Ramulator::recvRespRetry()
{
    //DPRINTF(Ramulator, "Retrying\n");

    assert(resp_stall);
    resp_stall = false;
    sendResponse();
}

void Ramulator::accessAndRespond(PacketPtr pkt)
{
    bool need_resp = pkt->needsResponse();
    //uint8_t *oldDataPtr = pkt->getPtr<uint8_t>();
    // pkt->setSize(req.reqDataSize);
    access(pkt);

    if (need_resp)
    {
        assert(pkt->isResponse());
        pkt->headerDelay = pkt->payloadDelay = 0;

        DPRINTF(Ramulator, "Queuing response for address %#llx\n",
                pkt->getAddr());

        resp_queue.push_back(pkt);
        // gagan : added 18 ns latency for the L3 cache
        if (!resp_stall && !send_resp_event.scheduled())
        {

            schedule(send_resp_event, curTick());
        }
    }
    else
        pending_del.push_back(pkt);
}

void Ramulator::readComplete(ramulator::Request &req , int bank_id)
{


    // Abotaleb : Intelligenet Prefetcher
    //  if it is prefetched but not requested add it only to IPDQ
    ramulator::iBufferKeyClass iPrefetcherQkey(req.addr, req.metaISAStreamID, req.metaISARequestorID);
    // respond if not exist in prefetcher queue or requested already
    int ch_idx = 0  ; // In all DRAM Standrads , Channel is at level 0 -> int(T::Level::Channel);
    int ch_id  = req.addr_vec[ch_idx];  
                                                     
    bool inIPDQ =  this->iBatchBuffer[ch_id][bank_id]->find(iPrefetcherQkey)   ;
    bool inIPDQandRequested = false;

   if (inIPDQ)
    {
        //iPrefetch Request is valid now 
        (*this->iBatchBuffer[ch_id][bank_id])[iPrefetcherQkey].W = 0;
        // It will be valid data as this is the callback for ramulator finish of the read request
        if ((*this->iBatchBuffer[ch_id][bank_id])[iPrefetcherQkey].R == 1)
        {
            inIPDQandRequested = true;
            iprefetches_completed_cnt_per_Stream[req.metaISAStreamID]++;
            tot_Req_iBatched_Completed++;
            // Remove from iBatch Queue    [Comment next line Assume Large iPref]
            this->iBatchBuffer[ch_id][bank_id]->delete_data(iPrefetcherQkey);
            //this->iBatchBuffer[bank_id]->DisplayIprefetchFull();
            DPRINTF(IPREFETCHER, "Request  %#lx wait in iBatch is completed now!\n", req.addr);
        }
        
        else
        { // Request can be done before it is iPrefetched inside MC
            if (reads.find(req.addr) != reads.end())
            {
                (*this->iBatchBuffer[ch_id][bank_id])[iPrefetcherQkey].R = 1;
                inIPDQandRequested = true;
                iprefetches_completed_cnt_per_Stream[req.metaISAStreamID]++;
                tot_Req_iBatched_Completed++;
                (*this->iBatchBuffer[ch_id][bank_id])[iPrefetcherQkey].R = 0; // Now not requested (Deleted)                
                this->iBatchBuffer[ch_id][bank_id]->delete_data(iPrefetcherQkey);
                //this->iBatchBuffer[bank_id]->DisplayIprefetchFull();
                DPRINTF(IPREFETCHER, "Request at %#lx is iBatched , req before iBatch , it is completed now!\n", req.addr);

                // But this means that the related address is already put in reads Ramulator queue and will be scheduled for unnecassary future command (need to be removed from there)
            }
        }
        
    }
    else // not in inIPDQ and not in reads -> overwritten iPrefetch 
        if(reads.find(req.addr)==reads.end())
        {
            iprefetches_wasted_per_Stream[req.metaISAStreamID]++;
            return;
        }
    if (!inIPDQ || inIPDQandRequested)
    {

        //DPRINTF(IPREFETCHER, " inIPDQandRequested = %d , inIPDQ = %d , req.addr = %lx\n", inIPDQandRequested, inIPDQ, req.addr);
        DPRINTF(Ramulator, "(%llu) Read to 0x%lx completed.\n", curTick() / ticks_per_clk, req.addr);
        // It will be removed from reads queue( if the corrsponding request still in readq!)
        // sometimes , iBatch may be formed for a request that is in the readq and then remove the corresponding entry from the readq !
        auto &pkt_q = reads.find(req.addr)->second;
        //assert(pkt_q!=reads.end());
        {
            PacketPtr pkt = pkt_q.front();
            pkt_q.pop_front();
            if (!pkt_q.size())
                reads.erase(req.addr);
            // added counter to track requests in flight
            --requestsInFlight;
            accessAndRespond(pkt);                
        }    

    }
     
}

void Ramulator::writeComplete(ramulator::Request &req , int bank_id)
{
    DPRINTF(Ramulator, "(%llu) Write to %ld completed.\n", curTick() / ticks_per_clk, req.addr);

    // added counter to track requests in flight
    --requestsInFlight;

    // check if we were asked to drain and if we are now done
}

//

//} // namespace memory

/*Ramulator * RamulatorParams::create(){
    return new Ramulator(this);
}*/

//} // namespace gem5
