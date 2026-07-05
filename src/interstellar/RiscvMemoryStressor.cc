/*#########################################################################
# Memory stressor GEM5 Simulation Object
# MemoryStressor.cc for generic architecture base
#  MemoryStressor class implementation
#
# Author: Abdelrhman Mohamed Abotaleb
# email : abotalea@mcmaster.ca
# Date  : 2 March 2022
#
# The RISCV memory stressor will make use of the following RISCV compoennts:
#     1) The CSRs
#     3) The MMU
#     4) The TLB
#########################################################################*/

#include <string>
#include <vector>
#include "arch/riscv/isa.hh"

#include <ctime>
#include <set>
#include <sstream>

#include "arch/riscv/interrupts.hh"
#include "arch/riscv/mmu.hh"
#include "arch/riscv/pagetable.hh"
#include "arch/riscv/pmp.hh"
#include "arch/riscv/regs/float.hh"
#include "arch/riscv/regs/int.hh"
#include "arch/riscv/regs/misc.hh"
#include "interstellar/RiscvMemoryStressor.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/MemStress_MEMBUS_PKT.hh"
#include "debug/MemStress_LOAD.hh"
#include "debug/MemStress.hh"
#include "debug/MemStress_CSR.hh"
#include "debug/MemStress_IAD.hh"



namespace gem5
{
    /****************************************************
     *  Data Structures for Probe Points
     * *************************************************/
    //1) MetaIsaCsrDataPtr defined in "arch/riscv/isa.hh"
    //2) PCptr

    RiscvMemoryStressor::RiscvMemoryStressor(const RiscvMemoryStressorParams &params)
    : ClockedObject(params),
	  mmu(params.mmu),
      tickEvent([this]{ tick(); }, "Riscv Memory Stressor tick"),
	  resendBlockedPktEvent ([this]{ sendBlockedRequests(); }, "Re-Send Blocked Request Event"),
	  _stressorId(params.stressorId),
	  _cacheLineSize (params.system->cacheLineSize()),
	  dcachePort(params.name + ".memSidePort",this)
	{

		mslConfigObj.dirStreamStride   = params.dirStreamStride;
    	mslConfigObj.dirStreamMaxSize  = params.dirStreamMaxSize;
    	cntrIAD  =  mslConfigObj.iad_LD_static =  params.loadReqInterArrivalDelay;
		crntAddr 				 =  params.dirStreamStartVA;
		mslConfigObj.dirStreamStartVA         =  params.dirStreamStartVA;
		mslConfigObj.stressorWorkLoadFName    =  params.stressorWorkLoad;
		numThreads               =  params.numThreads;
		for (ThreadID tid = 0; tid < numThreads; tid++) {
			pISA.push_back((RiscvISA::ISA *)(params.isa[tid]));
		}
		stressorWorkLoadFName = params.stressorWorkLoad;
	    DPRINTF(MemStress,"Configuration File Name = %s\n",stressorWorkLoadFName);

		if(stressorWorkLoadFName!="")
		{
			this->ConfigParse(stressorWorkLoadFName);
			mslConfigObj.dirStreamStride   = atoi(configs["LD_inc"].c_str());
    		mslConfigObj.dirStreamMaxSize  = atoi(configs["LD_Size"].c_str());
			crntAddr 	= mslConfigObj.dirStreamStartVA		  =   atoi(configs["LD_start"].c_str());
			if (configs.find("LD_iad") != configs.end())
    		{
				mslConfigObj.iad_LD_static   =   atoi(configs["LD_iad"].c_str());
				mslConfigObj.iad_mode = mslConfig::IAD_MODE::STATIC;
			}
			if (configs.find("LD_iad_mask") != configs.end())
    		{

				mslConfigObj.iad_LD_mult_TVal =   atoi(configs["LD_iad_TVal"].c_str());
				// Assign iad_LD_mult_FVal_Pre,iad_LD_mult_FVal_Post the default values first
				mslConfigObj.iad_LD_mult_FVal_Pre  =
				mslConfigObj.iad_LD_mult_FVal_Post =
				  atoi(configs["LD_iad_TVal"].c_str());

				if(configs["LD_iad_FVal"].find('[')!= string::npos)
				{
					string str = configs["LD_iad_FVal"];
					string iad1 = str.substr(str.find("[")+1,str.find(",")-str.find("[")-1);
					char *p;
    				int iad1Int = strtol(iad1.c_str(),&p,10);
					if(iad1!=p)//IAD1 defined by the user
						mslConfigObj.iad_LD_mult_FVal_Pre  = iad1Int;
					string iad2 = str.substr(str.find(",")+1,str.find("]")-str.find("[")-1);
    				int iad2Int = strtol(iad2.c_str(),&p,10);
					if(iad1!=p)//IAD1 defined by the user
						mslConfigObj.iad_LD_mult_FVal_Post  = iad2Int;
				}
				// Mask can start with "~" operator
				if(configs["LD_iad_mask"][0]=='~')
				{
						string mask= (configs["LD_iad_mask"].substr(1));
						mslConfigObj.iad_LD_mult_mask = ~(strtoull(mask.c_str(),NULL,16));
				}
				else
						mslConfigObj.iad_LD_mult_mask = strtoull(configs["LD_iad_mask"].c_str(),NULL,16);

				mslConfigObj.iad_mode = mslConfig::IAD_MODE::MULTIPLE;
				DPRINTF(MemStress," Multiple IAD Mask = %#llx\n",mslConfigObj.iad_LD_mult_mask);
			}
			if (configs.find("LD_dyn_N") != configs.end())
			{
				mslConfigObj.iad_LD_dyn_N=atoi(configs["LD_dyn_N"].c_str());
			   if(mslConfigObj.iad_mode == mslConfig::IAD_MODE::MULTIPLE)
					mslConfigObj.iad_mode = mslConfig::IAD_MODE::DYNAMIC_Multiple;
			   else
					mslConfigObj.iad_mode = mslConfig::IAD_MODE::DYNAMIC_Single;
			}

		}
		startNewCycle=true;

		this->initcsrReqW();
		/*** Logic Reklated Initalizations ***/

		crntBPQLen    = 0;
		pc_reg        = 0;
        // Setup the TC that will serve as the interface to the threads/CPU.
        tc = new o3::ThreadContext;

		//tc->setProcessPtr();
		crntThreadID=0;
 		crntContextID = 0;
	    DPRINTF(MemStress,"Memory Stressor Object is initalized\n");
		DPRINTF(MemStress,"Dir stream start address = %d - Dir stream stride = %d - direct stream size = %d ",mslConfigObj.dirStreamStartVA,mslConfigObj.dirStreamStride,mslConfigObj.dirStreamMaxSize);
		if(mslConfigObj.iad_mode==mslConfig::IAD_MODE::STATIC)
	    	DPRINTF(MemStress,"- IID = %d\n",mslConfigObj.iad_LD_static);
		if(mslConfigObj.iad_mode==mslConfig::IAD_MODE::MULTIPLE)
	    	DPRINTF(MemStress,"- Mask Hit IID = %d - Mask Miss IID(Before) = %d - Mask Miss IID(After) = %d\n",mslConfigObj.iad_LD_mult_TVal,mslConfigObj.iad_LD_mult_FVal_Pre,mslConfigObj.iad_LD_mult_FVal_Post);

	}

	void RiscvMemoryStressor::ConfigParse(const std::string& fname)
	{
		ifstream file(fname);
    	assert(file.good() && "Bad config file");
    	string line;
    	while (getline(file, line)) {
			DPRINTF(MemStress,"Current line : %s\n",line.c_str());
			char delim[] = " \t=";
        	vector<string> tokens;


			while (true) {
				size_t start = line.find_first_not_of(delim);
				if (start == string::npos)
					break;

				size_t end = line.find_first_of(delim, start);
				if (end == string::npos) {
					tokens.push_back(line.substr(start));
					break;
				}

				tokens.push_back(line.substr(start, end - start));
				line = line.substr(end);
			}
        	// empty line
        	if (!tokens.size())
            	continue;

        	// comment line
        	if (tokens[0][0] == '#')
            	continue;
			configs[tokens[0]] = tokens[1];
			DPRINTF(MemStress,"Configuartion[%s]=%s\n",tokens[0],tokens[1]);
		}

	}

	void RiscvMemoryStressor::initcsrReqW()
	{
		uint64_t  csrInd,csrVal;
		for (auto const& x : configs)
		{
		    string key = x.first;  // string (key)
			if(key.find("csrw")!=std::string::npos)
			{
				size_t start = key.find_first_of("[");
				size_t end = key.find_first_of("]", start);
				string csrValStr = (key.substr(start+1, end - start));
				DPRINTF(MemStress_CSR,"csrValStr  %s ",csrValStr.c_str());
				csrInd = strtol(csrValStr.c_str(),NULL,16 );
				csrInd = csrInd-CSR::BASE_META_CSR +  RiscvISA::MISCREG_METAISA0L;
				csrVal = strtoull(x.second.c_str(),NULL,16);
				DPRINTF(MemStress_CSR,"CSR [%d]=%#llx\n",csrInd,csrVal);
				csrReqW.push(pair<int,RegVal>(csrInd,csrVal));
			}
		}
	}
	RiscvMemoryStressor::~RiscvMemoryStressor()
	{

	}

	/************* Memory Stressor Logic FUnctions ***************/
	void
	RiscvMemoryStressor::startup()
	{
		// schedule(PCTrackEvent, 0 );
		schedule(tickEvent, 0); //
	}

	void RiscvMemoryStressor::regStats()
	{
		ClockedObject::regStats();
    	using namespace statistics;
		instcount.name(name() + ".InstructionsCount")
        .desc("Number of Non memory instructions")
        ;

		ldcount.name(name() + ".LoadsCount")
        .desc("Number of load instructions")
        ;

		stcount.name(name() + ".StoresCount")
        .desc("Number of store instructions")
        ;


	    DPRINTF(MemStress,"Memory Stressor Object Stats are registered\n");

	}

	void RiscvMemoryStressor::tick()
	{

		//At first clock cycles , Service CSR write requests
		if(!csrReqW.empty())
		{
			this->writeCSR();//ppMemStressCSRWT Probe is being notified here
		}
		//Then at each IID (Inter-Arrival-Delay) Service a new memory request
		if(csrReqW.size()==0)
		{
			if(startNewCycle)
			{
				startNewCycle=false;

				//Actual current address will be translated with timing considerations
				RequestPtr 	crntReq   = translateAddrTiming(crntAddr,1);
				//Future request physical address is needed to account for iad_LD_mult_FVal_Pre , this will be translated funcitonally only
				RequestPtr 	PastReq =nullptr,futureReq=nullptr;
				if(crntAddr!=mslConfigObj.dirStreamStartVA)//Past request can't be exist for first address
					PastReq = translateAddrFunctional(crntAddr-mslConfigObj.dirStreamStride,1);
				if(crntAddr!=(mslConfigObj.dirStreamStartVA + mslConfigObj.dirStreamMaxSize))//Past request can't be exist for first address
					futureReq = translateAddrFunctional(crntAddr+mslConfigObj.dirStreamStride,1);

				executeLoad(crntReq);
				crntAddr+=  mslConfigObj.dirStreamStride; // Advance to next VA Address

				// Assignment of IAD Decreasing Counter roof will depend on Next Address Not Current
				// If next expected will cause Miss , Then we need to wait more
				long long int  maskCrnt,maskPrevAddr,maskNextAddr;
				if(mslConfigObj.iad_mode&mslConfig::IAD_MODE_MASK::MULT==mslConfig::IAD_MODE::MULTIPLE)
				{
					if(PastReq==nullptr)//Assume First request will be miss (need more iad cycles)
						cntrIAD = mslConfigObj.iad_LD_mult_FVal_Post;
					else
					{

						maskCrnt= (crntReq->getPaddr())&mslConfigObj.iad_LD_mult_mask;
						if(PastReq!=nullptr)
							maskPrevAddr = (PastReq->getPaddr())&mslConfigObj.iad_LD_mult_mask;
						if(futureReq!=nullptr)
							maskNextAddr= (futureReq->getPaddr())&mslConfigObj.iad_LD_mult_mask;

						if(futureReq==nullptr && maskCrnt==maskPrevAddr)
							cntrIAD = mslConfigObj.iad_LD_mult_TVal;
						else if(maskCrnt==maskPrevAddr && maskCrnt==maskNextAddr)//Hit request , Hit before and Hit after
							cntrIAD = mslConfigObj.iad_LD_mult_TVal;
						if(maskCrnt!=maskPrevAddr)//Miss Request is the current one ex:  15C00 , 16000 , 16400
							cntrIAD = mslConfigObj.iad_LD_mult_FVal_Post;
 						if(maskCrnt!=maskNextAddr)//Miss Request will come  ex: 15800 15C00 16000
							cntrIAD = mslConfigObj.iad_LD_mult_FVal_Pre;
						// if the prev is miss and next is miss , the IAD will be the larger (The pre)

						DPRINTF(MemStress_IAD,"Stressor PAddr[%llx] Mask(PrevAddr)= %#llx - Mask(CrntAddr)= %#llx - Mask(NextAddr)= %#llx - cntrIAD =%d\n",crntReq->getPaddr() ,maskPrevAddr,maskCrnt,maskNextAddr,cntrIAD);

					}
				}
				else
					cntrIAD  = mslConfigObj.iad_LD_static;

			}
			else
			{
				if(cntrIAD==1)
				{
					startNewCycle=true;
				}
				else
					cntrIAD--;
			}

		}

		pc_reg++; // Increment Program Counter
		if (!tickEvent.scheduled())
 			schedule(tickEvent, clockEdge(Cycles(1)));
	}

	RequestPtr RiscvMemoryStressor::translateAddr(uint64_t reqVAddr, size_t reqSize)
	{
		//Prepare the requst
		Request::Flags   reqFlags(0);
	    //gem5::Request::funcRequestorId   requestorID = gem5::RequestorID   RequestorID.funcRequestorId;
		RequestPtr req = std::make_shared<Request>(reqVAddr,reqSize,reqFlags,gem5::Request::funcRequestorId,pc_reg,crntContextID);
		//req->setFlags(Request::UNCACHEABLE | Request::STRICT_ORDER);
    	/*Request(Addr vaddr, unsigned size, Flags flags,
            RequestorID id, Addr pc, ContextID cid,
            AtomicOpFunctorPtr atomic_op=nullptr)*/

		// 1- Translate the  address first using TLB
		//this->mmu->translateTiming(req,tc,pTranslate,BaseMMU::Read);//if mode is execute , it will call itb , otherwise dtb
		req->setPaddr(reqVAddr);

		DPRINTF(MemStress_LOAD,"Stressor VA Translation : PA(%#lx)=%#lx\n",req->getVaddr(),req->getPaddr());

		ppMemStressVA2PA->notify(req);
		return req;
	}

	RequestPtr RiscvMemoryStressor::translateAddrTiming(uint64_t reqVAddr, size_t reqSize)
	{
		//Prepare the requst
		Request::Flags   reqFlags(0);
	    //gem5::Request::funcRequestorId   requestorID = gem5::RequestorID   RequestorID.funcRequestorId;
		RequestPtr req = std::make_shared<Request>(reqVAddr,reqSize,reqFlags,gem5::Request::funcRequestorId,pc_reg,crntContextID);
		//req->setFlags(Request::UNCACHEABLE | Request::STRICT_ORDER);
    	/*Request(Addr vaddr, unsigned size, Flags flags,
            RequestorID id, Addr pc, ContextID cid,
            AtomicOpFunctorPtr atomic_op=nullptr)*/

		// 1- Translate the  address first using TLB
		//this->mmu->translateTiming(req,tc,pTranslate,BaseMMU::Read);//if mode is execute , it will call itb , otherwise dtb
		req->setPaddr(reqVAddr);

		DPRINTF(MemStress_LOAD,"Stressor Timing VA Translation : PA(%#lx)=%#lx\n",req->getVaddr(),req->getPaddr());

		ppMemStressVA2PA->notify(req);
		return req;

	}

	RequestPtr RiscvMemoryStressor::translateAddrFunctional(uint64_t reqVAddr, size_t reqSize)
	{
		//Prepare the requst
		Request::Flags   reqFlags(0);
	    //gem5::Request::funcRequestorId   requestorID = gem5::RequestorID   RequestorID.funcRequestorId;
		RequestPtr req = std::make_shared<Request>(reqVAddr,reqSize,reqFlags,gem5::Request::funcRequestorId,pc_reg,crntContextID);
		//req->setFlags(Request::UNCACHEABLE | Request::STRICT_ORDER);
    	/*Request(Addr vaddr, unsigned size, Flags flags,
            RequestorID id, Addr pc, ContextID cid,
            AtomicOpFunctorPtr atomic_op=nullptr)*/

		// 1- Translate the  address first using TLB
		//this->mmu->translateTiming(req,tc,pTranslate,BaseMMU::Read);//if mode is execute , it will call itb , otherwise dtb
		req->setPaddr(reqVAddr);

		DPRINTF(MemStress_LOAD,"Stressor Functional VA Translation : PA(%#lx)=%#lx\n",req->getVaddr(),req->getPaddr());

		return req;
	}

	bool RiscvMemoryStressor::executeLoad(RequestPtr req)
	{

		// 2- Then after a cycle , encapsulate the physical address in a memory request
		PacketPtr pkt = new Packet(req,MemCmd::Command::ReadReq);
        uint8_t *memData = new uint8_t;
		pkt->dataStatic(memData);
		DPRINTF(MemStress_LOAD,"Stressor Send Load Req : PA[%#lx]\n",req->getPaddr());
		this->handleRequest(pkt);
		ldcount++;
		return true;
	}


	void RiscvMemoryStressor::writeCSR()
	{
		pair<int,RegVal> x= csrReqW.front();
		csrRegFile.writeCSR(x.first,x.second);
		// for now single thread
		RiscvISA::MetaIsaCsrData metaIsaCsrData={pISA[crntThreadID],x.first,x.second};
		ppMemStressCSRWT->notify(&metaIsaCsrData);
		csrReqW.pop();
	}


	void
	RiscvMemoryStressor::sendBlockedRequests()
	{
		if(!this->blockedPacketQueue.empty())
		{
			//Try to call copy constructor
			//PacketPtr blockedPktQueueFront = this->blockedPacketQueue.front()(false,true);
			//Try to send the packet at the blocked requests queue head.
			DPRINTF(MemStress_MEMBUS_PKT,"Riscv Memory Stressor Retry send pkt addr = %#lx next addr:%#lx\n",this->blockedPacketQueue.front()->getAddr(),this->blockedPacketQueue.front()->getNextAddr());
			Packet pktBuffer(this->blockedPacketQueue.front(),false,true);
			pktBuffer.setNextAddr(this->blockedPacketQueue.front()->getNextAddr());
			if(this->dcachePort.sendPacket(this->blockedPacketQueue.front()))
			{
				DPRINTF(MemStress_MEMBUS_PKT, "Re-send Request Packet Done (UnBlocking it) : Timing request for addr %#lx , next addr =%#lx\n", pktBuffer.getAddr(),pktBuffer.getNextAddr());
				blockedPacketQueue.pop();
				crntBPQLen--;
			}
			else
				DPRINTF(MemStress_MEMBUS_PKT, "Re-send Request Packet Fail : Timing request for addr %#lx , next addr =%#lx\n", pktBuffer.getAddr(),pktBuffer.getNextAddr());

		}
		//After pop , There is a chance that all blocked requests are already serviced
		if(!this->blockedPacketQueue.empty())
			schedule(resendBlockedPktEvent, curTick() + clockPeriod());
	}



    /**************************************************************
     *
     * getPort ; gets the reference to memory stressor
     *
     * @param if_name Interfeace name (dcache_port) onlt for the memory stressor
     * @param idx  Port ID
     * ************************************************************/
    Port &
    RiscvMemoryStressor::getPort(const std::string &if_name, PortID idx)
    {
        // Memory stressor has only Data Cache Port
        if (if_name == "dcache_port")
            return getDataPort();
        else
            return ClockedObject::getPort(if_name, idx);
    }

    RequestPort &
	RiscvMemoryStressor::getDataPort()
	{ return dcachePort; }

    void
    RiscvMemoryStressor::regProbePoints()
    {
        ppMemStressCSRWT = new ProbePointArg<gem5::RiscvISA::MetaIsaCsrDataPtr>(
            this->getProbeManager() , "memStressor_csrwt");
        ppMemStressPC    = new ProbePointArg<uint64_t*>             (
            this->getProbeManager() , "memStressor_pc");
		ppMemStressVA2PA = new ProbePointArg<gem5::RequestPtr>      (
			this->getProbeManager() ,"va2pa");

    }

	/*****************************************************************
	 *****************************************************************
	 *      														 *
	 *   Memory System Related Functions Inside RiscvMemoryStressor 	 *
	 * 					RiscvMemoryStressor											 *
	 *                         Start 								 *
	 *    						 v									 *
	 *     						 v									 *
	 *     					     v									 *
	 * 																 *
	 *****************************************************************
	 *****************************************************************/
	//@todo to mimic LSQ in O3 or Minor , the function name is tryToSend()
	// which sends the timing req to cache
	bool
	RiscvMemoryStressor::handleRequest(PacketPtr pkt)
	{
		//MetaISA requirement : setup the packet driver + thread IDs when sending the req
		pkt->setMetaISARequestorID(_stressorId+(crntThreadID<<16));

		DPRINTF(MemStress_MEMBUS_PKT, "Execute Timing request for addr %#x\n", pkt->getAddr());
        DPRINTF(MemStress_MEMBUS_PKT,"tryToSend from stressor  = %d and Thread  ID =  %d\n",_stressorId,crntThreadID);

		pkt->setNextAddr(0);
		//If there are any blocked packets , don't try to send before sending old blocked packets
		if(!this->blockedPacketQueue.empty())
		{
			if(!insertToBlockedPacketQueue(pkt))
				return false;
		}
		else // if there is no pending blocked packets
			if(!dcachePort.sendPacket(pkt))//@todo There are also .sendTimingReq
			{
				if(!insertToBlockedPacketQueue(pkt))
					return false;
			}
		return true;
	}

	bool
	RiscvMemoryStressor::insertToBlockedPacketQueue(PacketPtr pkt)
	{
			// Increment current blocked packet queue length
			crntBPQLen++;
			if(crntBPQLen==maxBPQsize)
				return false; // can't insert the blocked packet now , need to do re-try

			DPRINTF(MemStress_MEMBUS_PKT, "Blocked request (%d/%d) on MetaISA -> Addr=%#lx , Next Addr = %#lx\n" , crntBPQLen,maxBPQsize , pkt->getAddr(), pkt->getNextAddr());
			//Insert packet into the blocked packets queue
			//(Copy it first to avoid strage error happens when just push it then send to the coherent cross bar then memoiry (its contents are altered !))
			PacketPtr pPktCopy = new Packet(pkt,false,true);
			pPktCopy->setNextAddr(pkt->getNextAddr());
			blockedPacketQueue.push(pPktCopy);
			if(!resendBlockedPktEvent.scheduled())
				schedule(resendBlockedPktEvent,curTick() + clockPeriod());
		return true;
	}


	bool
	RiscvMemoryStressor::handleResponse(PacketPtr pkt)
	{
		DPRINTF(MemStress_MEMBUS_PKT, "Request for addr %#x is completely serviced\n", pkt->getAddr());
		if(pkt->getAddr()-mslConfigObj.dirStreamStartVA==mslConfigObj.dirStreamMaxSize)
			exitSimLoop("Memory Stressor Finished all Tasks! ");
		return true;
	}

	void
	RiscvMemoryStressor::handleAtomic(PacketPtr pkt)
	{
		dcachePort.sendAtomic(pkt);
	}

	void
	RiscvMemoryStressor::handleFunctional(PacketPtr pkt)
	{
		// In the RiscvMemoryStressor Just pass the request to memPort
		DPRINTF(MemStress_MEMBUS_PKT, "Got functional request for addr %#x\n", pkt->getAddr());
		dcachePort.sendFunctional(pkt);
	}

	// But in RISCVMetaISAEngine the LLC Miss Logic should be decided

	AddrRangeList
	RiscvMemoryStressor::getAddrRanges() const
	{
		return dcachePort.getAddrRanges();
	}



	/*****************************************************************
	 *****************************************************************
	 *      														 *
	 *   Memory System Related Functions Inside RiscvMemoryStressor 	 *
	 * 				RiscvMemoryStressor::DcachePort					 *
	 *                         Start 								 *
	 *    						 v									 *
	 *     						 v									 *
	 *     					     v									 *
	 * 																 *
	 *****************************************************************
	 *****************************************************************/
	bool
	RiscvMemoryStressor::DcachePort::sendPacket(PacketPtr pkt)
	{
		// Note: UnBlocking Sending (Return the sending status) and try to resend if fail
        DPRINTF(MemStress_MEMBUS_PKT, "BaseMetaISA pkt addr = %#lx Next Addr:%#lx\n",pkt->getAddr(),pkt->getNextAddr());

		if (!sendTimingReq(pkt))
		{
			//panic_if(blockedPacket != nullptr, "Should never try to send if 2 packets are blocked!");
			//blockedPacket = pkt;
			//MetaISA Engine will add the pkt to blocked packets queues
			return false;
		}
		return true;
	}

	bool
	RiscvMemoryStressor::DcachePort::recvTimingResp(PacketPtr pkt)
	{
		// Just forward to the memobj.
		return owner->handleResponse(pkt);
	}

	void
	RiscvMemoryStressor::DcachePort::recvReqRetry()
	{
		//Call the resendBlockedEvent Function
		DPRINTF(MemStress_MEMBUS_PKT, "Retrying to send a request !\n");
		//owner->resendBlockedPktEvent();
	}


}
