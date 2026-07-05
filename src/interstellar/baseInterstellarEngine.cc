/*#################################################################
# Base Interstellar  Centralized Engine GEM5 Simulation Object
# baseInterstellarEngine.cc for generic architecture base
#  BaseInterstellarEngine class implementation
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 2 March 2022
#
##################################################################*/

#include <string>
#include <vector>

#include "interstellar/baseInterstellarEngine.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Interstellar_IPP.hh"
#include "debug/Interstellar_IPP_From_LLC_MPKT.hh"
#include "debug/Interstellar_IPP_From_MC_MPKT.hh"

namespace gem5
{
	/*************************************************************************
	 *     Constructor   BaseInterstellarEngine
	 *
	 * @param p parameters used to initalize BaseInterstellarEngine object
	 * @return  The constructor has no return
	 *
	 ***********************************************************************/
	BaseInterstellarEngine::BaseInterstellarEngine(const BaseInterstellarEngineParams &p) : ClockedObject(p),
	        _type(p.optimization_policy),
			cpu_type(p.cpu_type),
			interstellar_probe_enable(p.interstellar_probe_enable),
			llcSidePort(p.name + ".llcSidePort", this),
			memSidePort(p.name + ".memSidePort", this),
			event([this]
			   { processEvent(); },name()),
			PCTrackEvent([this]
			  { trackPCEvent(); },name()),
			cumCycles(-1),
			// Assigning the pointers to simObjects we are probing to
			isa_vec(p.isa_list),
			cpu_vec(p.cpu_list), l1_dcaches_vec(p.l1_DCache_list),
			dTLB_vec(p.dTLB_list), l2_cache(p.l2_cache),
			// Assigning listeners to simObjects we are probing to
			interstellar_csr_listerners(p.interstellar_csr_listerners_list),
			cpu_probe_listeners(p.cpu_probe_listeners_list),
			l1DCaches_probe_listerners_list(p.l1DCaches_probe_listerners_list),
			dTLB_probe_listerners_list(p.dTLB_probe_listerners_list)
	{

		DPRINTF(Interstellar_IPP, "Interstellar Centralized Object Is Initialized!\n");

	}

	/*******************************************************************
	 *  Memory Requests and Responses' Ports Related Functions
	 *
	 *
	 *******************************************************************/
	Port &
	BaseInterstellarEngine::getPort(const std::string &if_name, PortID idx)
	{
		panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

		if (if_name == "memSidePort")
		{
			return memSidePort;
		}
		else if (if_name == "llcSidePort")
		{
			return llcSidePort;
		}
		else
		{
			// pass it along to our super class
			return SimObject::getPort(if_name, idx);
		}
	}

	/*******************************************************************
	 *  processEvent : call back executes every 100 ticks
	 *
	 *
	 *******************************************************************/
	void
	BaseInterstellarEngine::processEvent()
	{
		cumCycles++;
		if (cumCycles % 100 == 0)
			DPRINTF(Interstellar_IPP, "Base Interstellar Centralized Object! Processing the event!\n");

		// Always make the Base Interstellar engine works every clock cycle

		schedule(event, curTick() + 1);
	}

	/*********************************************************************************
	 *  trackPCEvent virtual function will be overwritten in
	 *               inherited  InterstellarEngine specific to a certain architecture
	 *
	 *
	 *********************************************************************************/
	void
	BaseInterstellarEngine::trackPCEvent()
	{
	}

	void
	BaseInterstellarEngine::resendBlockedPktEvent()
	{

		// Grab the blocked packet.
		PacketPtr pkt = memSidePort.blockedPacket;
		memSidePort.blockedPacket = nullptr;
		DPRINTF(Interstellar_IPP_From_LLC_MPKT, "Base InterstellarEngine Retry to send a single blocked request");
		memSidePort.sendPacket(pkt);
	}
	/*******************************************************************
	 *  startup : event executed at tick 0
	 *
	 *
	 *******************************************************************/
	void
	BaseInterstellarEngine::startup()
	{
		schedule(event, 0); // Interstellar Centralized engine should start when the processor resets (at time 0)
	}

	void BaseInterstellarEngine::regStats()
	{
		ClockedObject::regStats();
    	using namespace statistics;
		DPRINTF(Interstellar_IPP, "Interstellar Centralized Object Stats are Registered!\n");
	}
	/*******************************************************************
	 *  startup : event executed at tick 0
	 *
	 *
	 *******************************************************************/
	void BaseInterstellarEngine::regProbeListeners()
	{

		//		for (auto &cpu_probe_listener : cpu_probe_listeners)
		for (size_t i = 0; i < cpu_probe_listeners.size(); i++)
		{


			if( (static_cast<int>(interstellar_probe_enable)&PorbeEnableMasks::OoOPipeProbeMask))
			{

				if( (static_cast<int>(interstellar_probe_enable)&PorbeEnableMasks::ExecuteProbeMask))
				{
					// Attach listener to "Execute" stage probe "ppExecute"  [This stage found in O3CPU - iew.cc  ]
					cpu_probe_listeners[i]->connectListener<ProbeListenerArg<BaseInterstellarEngine, gem5::o3::DynInstConstPtr>>(this, "Execute", &BaseInterstellarEngine::processInstToExec);
				}

				if( (static_cast<int>(interstellar_probe_enable)&PorbeEnableMasks::ToCommitProbeMask))
				{
					// Attach listener to "ToCommit" stage probe "ppToCommit" [This stage found in O3CPU - iew.cc  ]
					cpu_probe_listeners[i]->connectListener<ProbeListenerArg<BaseInterstellarEngine, gem5::o3::DynInstConstPtr>>(this, "ToCommit", &BaseInterstellarEngine::processInstExecuted);
				}

				if( (static_cast<int>(interstellar_probe_enable)&PorbeEnableMasks::CommitProbeMask))
				{
					// Attach listener to "Commit" stage probe "ppCommit"    [This stage found in O3CPU - commit.cc]
					cpu_probe_listeners[i]->connectListener<ProbeListenerArg<BaseInterstellarEngine, gem5::o3::DynInstConstPtr>>(this, "Commit", &BaseInterstellarEngine::processCommittedInst);
				}
			}
		}
	}

	/*******************************************************************
	 *  processInstToExec :call back to probe point at toExecute
	 *
	 *
	 *******************************************************************/
	void BaseInterstellarEngine::processInstToExec(const gem5::o3::DynInstConstPtr &dynInst)
	{

		DPRINTF(Interstellar_IPP_CPU, " CPU[%u] : Start Execution of 0x%016llx %s.\n",
				dynInst->cpu->cpuId(),
				dynInst->pcState().instAddr(),
				dynInst->staticInst->disassemble(dynInst->pcState().instAddr()));
	}

	/*******************************************************************
	 *  processInstExecuted :call back to probe point at Execute
	 *
	 *
	 *******************************************************************/
	void BaseInterstellarEngine::processInstExecuted(const gem5::o3::DynInstConstPtr &dynInst)
	{
		DPRINTF(Interstellar_IPP_CPU, " CPU[%u] : End Execution of 0x%016llx %s.\n",
				dynInst->cpu->cpuId(),
				dynInst->pcState().instAddr(),
				dynInst->staticInst->disassemble(dynInst->pcState().instAddr()));
	}

	/*******************************************************************
	 *  processInstExecuted :call back to probe point at Commit
	 *
	 *
	 *******************************************************************/
	void BaseInterstellarEngine::processCommittedInst(const gem5::o3::DynInstConstPtr &dynInst)
	{
		DPRINTF(Interstellar_IPP_CPU, " CPU[%u] : Committing of 0x%016llx %s.\n",
				dynInst->cpu->cpuId(),
				dynInst->pcState().instAddr(),
				dynInst->staticInst->disassemble(dynInst->pcState().instAddr()));
	}

	/*********************************************************************
	 *********************************************************************
	 *      														     *
	 *   Memory System Related Functions Inside BaseInterstellarEngine 	 *
	 * 					BaseInterstellarEngine							 *
	 *                         Start 								 	 *
	 *    						 v										 *
	 *     						 v										 *
	 *     					     v									 	 *
	 * 																 	 *
	 *********************************************************************
	 *********************************************************************/

	bool
	BaseInterstellarEngine::handleRequest(PacketPtr pkt)
	{
		if (blocked)
		{
			DPRINTF(Interstellar_IPP_From_LLC_MPKT, "[Blocked Request!]Got request for addr %#x\n", pkt->getAddr());
			// There is currently an outstanding request. Stall.
			return false;
		}

		DPRINTF(Interstellar_IPP_From_LLC_MPKT, "Got Timing request for addr %#x\n", pkt->getAddr());

		// This memobj is now blocked waiting for the response to this packet.
		blocked = true;

		// Simply forward to the memory port
		if (!memSidePort.sendPacket(pkt)) {
			// Packet was blocked, need to unblock the engine and signal retry
			blocked = false;
			return false;
		}

		return true;
	}

	bool
	BaseInterstellarEngine::handleResponse(PacketPtr pkt)
	{
		//assert(blocked);
		DPRINTF(Interstellar_IPP_From_MC_MPKT, "Got response for addr %#x\n", pkt->getAddr());

		// The packet is now done. We're about to put it in the port, no need for
		// this object to continue to stall.
		// We need to free the resource before sending the packet in case the CPU
		// tries to send another request immediately (e.g., in the same callchain).
		blocked = false;

		// Simply forward to the llcSidePort (That is connected to LLC)

		llcSidePort.sendPacket(pkt);
 		// if it needs to send a retry, it should do it
		// now since this memory object may be unblocked now.
		llcSidePort.trySendRetry();

		return true;
	}

	void
	BaseInterstellarEngine::handleAtomic(PacketPtr pkt)
	{
		memSidePort.sendAtomic(pkt);
	}

	void
	BaseInterstellarEngine::handleFunctional(PacketPtr pkt)
	{
		// In the BaseInterstellarEngine Just pass the request to memPort
		DPRINTF(Interstellar_IPP_From_LLC_MPKT, "Got functional request for addr %#x\n", pkt->getAddr());

		memSidePort.sendFunctional(pkt);
	}

	// But in RISCV  InterstellarEngine the LLC Miss Logic should be decided

	AddrRangeList
	BaseInterstellarEngine::getAddrRanges() const
	{
		return memSidePort.getAddrRanges();
	}

	void
	BaseInterstellarEngine::sendRangeChange()
	{
		llcSidePort.sendRangeChange();
	}

	/*********************************************************************
	 *********************************************************************
	 *      														     *
	 *   Memory System Related Functions Inside BaseInterstellarEngine 	 *
	 * 				BaseInterstellarEngine::McSidePort					 *
	 *                         Start 								     *
	 *    						 v									     *
	 *     						 v									     *
	 *     					     v									     *
	 * 																     *
	 *********************************************************************
	 *********************************************************************/
	bool
	BaseInterstellarEngine::McSidePort::sendPacket(PacketPtr pkt)
	{
		// Note: UnBlocking Sending (Return the sending status) and try to resend if fail
        DPRINTF(Interstellar_IPP_From_LLC_MPKT,"Base InterstellarEngine pkt addr = %#lx Next Addr:%#lx\n",pkt->getAddr(),pkt->getNextAddr());

		if (!sendTimingReq(pkt))
		{
			//Interstellar Engine will add the pkt to blocked packets queues
			return false;
		}
		return true;
	}

	bool
	BaseInterstellarEngine::McSidePort::recvTimingResp(PacketPtr pkt)
	{
		// Just forward to the memobj.
		return owner->handleResponse(pkt);
	}

	void
	BaseInterstellarEngine::McSidePort::recvReqRetry()
	{
		// The xbar is ready for us to retry - immediately retry the blocked packet
		DPRINTF(Interstellar_IPP_From_LLC_MPKT, "Retrying blocked packet\n");
		owner->resendBlockedPktEvent();
	}

	void
	BaseInterstellarEngine::McSidePort::recvRangeChange()
	{
		owner->sendRangeChange();
	}

	/*********************************************************************
	 *********************************************************************
	 *      														     *
	 *   Memory System Related Functions Inside BaseInterstellarEngine 	 *
	 * 				BaseInterstellarEngine::DevSidePort					 *
	 *                         Start 								     *
	 *    						 v									     *
	 *     						 v									     *
	 *     					     v									     *
	 * 																     *
	 *********************************************************************
	 *********************************************************************/
	void
	BaseInterstellarEngine::DevSidePort::sendPacket(PacketPtr pkt)
	{
		// Simple Flow since the BaseInterstellarEngine is blocking.

		panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

		// If we can't send the packet across the port, store it for later.
		if (!sendTimingResp(pkt))
		{
			DPRINTF(Interstellar_IPP_From_LLC_MPKT, "Critical Warning !! Response Packet had not been sent !\n");
			blockedPacket = pkt;
		}
	}

	AddrRangeList
	BaseInterstellarEngine::DevSidePort::getAddrRanges() const
	{
		return owner->getAddrRanges();
	}

	void
	BaseInterstellarEngine::DevSidePort::trySendRetry()
	{
		if (needRetry && blockedPacket == nullptr)
		{
			// Only send a retry if the port is now completely free
			needRetry = false;
			DPRINTF(Interstellar_IPP_From_LLC_MPKT, "Sending retry req for %d\n", id);
			sendRetryReq();
		}
	}

	void
	BaseInterstellarEngine::DevSidePort::recvFunctional(PacketPtr pkt)
	{
		// Just forward to the memobj.
		return owner->handleFunctional(pkt);
	}

	bool
	BaseInterstellarEngine::DevSidePort::recvTimingReq(PacketPtr pkt)
	{
		// Just forward to the memobj.

		//If Interstellar Engine's handleRequest can consume the packet then return true
		//   Interstellar Engine can consume the request in the following case:
		//   1) it can forward it to the memory controller
		//   2) it is blocked but can be inserted into local queue of the blocked packets
		//      [This internal queue is of maximum size = 16 (Can be configured)]
		if (!owner->handleRequest(pkt))
		{
			//needRetry = true;
			return false;
		}
		else
		{
			return true;
		}
	}

	void
	BaseInterstellarEngine::DevSidePort::recvRespRetry()
	{
		// We should have a blocked packet if this function is called.
		assert(blockedPacket != nullptr);

		// Grab the blocked packet.
		PacketPtr pkt = blockedPacket;
		blockedPacket = nullptr;

		// Try to resend it. It's possible that it fails again.
		sendPacket(pkt);
	}

}
