/*#################################################################
# Base Interstellar Centralized Engine GEM5 Simulation Object
# baseInterstellarEngine.hh for generic architecture base
#  BaseInterstellarEngine class declaration
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 2 March 2022
#
##################################################################*/

#ifndef __BASE_Interstellar_ENGINE_HH__
#define __BASE_Interstellar_ENGINE_HH__

#include <type_traits>

#include "params/BaseInterstellarEngine.hh"
#include "cpu/o3/cpu.hh"           /* to point to base CPU                               */
#include "mem/cache/cache.hh"      /* to point to Cache or use BaseCache
                                   it also include Packet,Port,Requests                  */
#include "cpu/o3/dyn_inst.hh"      /* to point to DynInstConstPtr inside probe listeners */
#include "sim/probe/probe.hh"
#include "sim/probe/probe_listener_object.hh"      /* to declare a probe listener as param               */
#include "sim/clocked_object.hh"
#include <sim/sim_object.hh>
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Interstellar_IPP.hh"
#include "debug/Interstellar_IPP_CPU.hh"

#include "enums/TypeInterstellarEngine.hh"
#include "enums/TypeCPU.hh"
#include "enums/TypeProbeEnable.hh"

namespace gem5
{

	class BaseInterstellarEngine : public ClockedObject
	{
	  protected:

          /**
       * Port on the CPU-side that receives requests.
       * Mostly just forwards requests to the owner.
       * Part of a vector of ports. One for each CPU port (e.g., data, inst)
       */
      class DevSidePort : public ResponsePort
      {
        private:
          /// The object that owns this object (SimpleMemobj)
          BaseInterstellarEngine *owner;

          /// True if the port needs to send a retry req.
          bool needRetry;

          /// If we tried to send a packet and it was blocked, store it here
          PacketPtr blockedPacket;

        public:
          /**
           * Constructor. Just calls the superclass constructor.
           */
          DevSidePort(const std::string& name, BaseInterstellarEngine *owner) :
              ResponsePort(name), owner(owner), needRetry(false),
              blockedPacket(nullptr)
          { }

          /**
           * Send a packet across this port. This is called by the owner and
           * all of the flow control is hanled in this function.
           *
           * @param packet to send.
           */
          void sendPacket(PacketPtr pkt);

          /**
           * Get a list of the non-overlapping address ranges the owner is
           * responsible for. All response ports must override this function
           * and return a populated list with at least one item.
           *
           * @return a list of ranges responded to
           */
          AddrRangeList getAddrRanges() const override;

          /**
           * Send a retry to the peer port only if it is needed. This is called
           * from the SimpleMemobj whenever it is unblocked.
           */
          void trySendRetry();

        protected:
          /**
           * Receive an atomic request packet from the request port.
           * Bypass it to memory
           */
          Tick recvAtomic(PacketPtr pkt) override
          {
              owner->handleAtomic(pkt);
              //latency introduced by the current SimObject.
              //ClockedObject::cyclesToTicks
              gem5::Cycles baseMetaObjCycles(1);
              return owner->cyclesToTicks(baseMetaObjCycles);
          }

          /**
           * Receive a functional request packet from the request port.
           * Performs a "debug" access updating/reading the data in place.
           *
           * @param packet the requestor sent.
           */
          void recvFunctional(PacketPtr pkt) override;

          /**
           * Receive a timing request from the request port.
           *
           * @param the packet that the requestor sent
           * @return whether this object can consume the packet. If false, we
           *         will call sendRetry() when we can try to receive this
           *         request again.
           */
          bool recvTimingReq(PacketPtr pkt) override;

          /**
           * Called by the request port if sendTimingResp was called on this
           * response port (causing recvTimingResp to be called on the request
           * port) and was unsuccesful.
           */
          void recvRespRetry() override;
      };

      /**
       * Port on the memory-side that receives responses.
       * Mostly just forwards requests to the owner
       */
      class McSidePort : public RequestPort
      {
        private:
          /// The object that owns this object (SimpleMemobj)
          BaseInterstellarEngine *owner;

        public:
          /**
           * Constructor. Just calls the superclass constructor.
           */
          /// If we tried to send a packet and it was blocked, store it here
          PacketPtr blockedPacket;

          McSidePort(const std::string& name, BaseInterstellarEngine *owner) :
              RequestPort(name), owner(owner), blockedPacket(nullptr)
          { }

          /**
           * Send a packet across this port. This is called by the owner and
           * all of the flow control is hanled in this function.
           *
           * @param packet to send.
           */
          bool  sendPacket(PacketPtr pkt);

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

          /**
           * Called to receive an address range change from the peer response
           * port. The default implementation ignores the change and does
           * nothing. Override this function in a derived class if the owner
           * needs to be aware of the address ranges, e.g. in an
           * interconnect component like a bus.
           */
          void recvRangeChange() override;
       };

      enum PorbeEnableMasks{
        ExecuteProbeMask = 0x1 , ToCommitProbeMask = 0x2,CommitProbeMask=0x4 , L1DCacheProbeMask=0x8 , OoOPipeProbeMask = 0x7
      };
      /**
       * Handle the request from the CPU side
       *
       * @param requesting packet
       * @return true if we can handle the request this cycle, false if the
       *         requestor needs to retry later
       */
       virtual bool handleRequest(PacketPtr pkt);

      /**
       * Handle the response from the memory side
       *
       * @param responding packet
       * @return true if we can handle the response this cycle, false if the
       *         responder needs to retry later
       */
      virtual bool handleResponse(PacketPtr pkt);

      /**
       * Handle a packet functionally. Update the data on a write and get the
       * data on a read.
       *
       * @param packet to functionally handle
       */
      void handleFunctional(PacketPtr pkt);
	    void handleAtomic(PacketPtr pkt);
      /**
       * Return the address ranges this memobj is responsible for. Just use the
       * same as the next upper level of the hierarchy.
       *
       * @return the address ranges this memobj is responsible for
       */
      AddrRangeList getAddrRanges() const;

      /**
       * Tell the CPU side to ask for our memory ranges.
       */
      void sendRangeChange();

      /// True if this is currently blocked waiting for a response.
      bool blocked;



		protected:
			TypeInterstellarEngine   _type                    ;
			TypeCPU                 cpu_type                  ;
      TypeProbeEnable         interstellar_probe_enable            ;
			DevSidePort             llcSidePort                          ;
      McSidePort              memSidePort                          ;
			virtual void            processEvent()                       ;
			virtual void            trackPCEvent()                       ;
      virtual void            resendBlockedPktEvent()              ;
			EventFunctionWrapper    event                                ;
			EventFunctionWrapper    PCTrackEvent                         ;
			int                     cumCycles                            ;



      /********* TODO cpu_vec will be pointer to cpu_type class (More General)
			           and dynamic casted to the runtime type at the constructor *********/
			std::vector<gem5::BaseISA*>              isa_vec                 ; /* List of ISA CPUs to Probe          */
			std::vector<gem5::BaseCPU*>              cpu_vec                 ; /* List of CPUs Probe                 */
			std::vector<BaseCache*>                  l1_dcaches_vec          ; /* L1 Data Cache Probing on it        */
			std::vector<BaseTLB*>                    dTLB_vec                ; /* Data TLB Probing on it             */
			BaseCache*                               l2_cache                ; /* L2 Cache Probing on it             */


			std::vector<gem5::ProbeListenerObject*>  interstellar_csr_listerners         ; /* list of ISA probes                 */
			std::vector<gem5::ProbeListenerObject*>  cpu_probe_listeners                 ; /* list of CPU probes                 */
			std::vector<gem5::ProbeListenerObject*>  l1DCaches_probe_listerners_list     ; /* list of CPU's L1 data cache probes */
			std::vector<gem5::ProbeListenerObject*>  dTLB_probe_listerners_list          ; /* list of CPU's data TLB probes      */


		public:
        BaseInterstellarEngine(const BaseInterstellarEngineParams &p);
			  virtual void   regProbeListeners()  override;
        virtual bool            interstellarFilterPkt(PacketPtr pkt) { return false; }

			  void   startup()                    override;
        void   regStats()                   override;
		    void   processInstToExec   (const gem5::o3::DynInstConstPtr& dynInst);
		    void   processInstExecuted (const gem5::o3::DynInstConstPtr& dynInst);
		    void   processCommittedInst(const gem5::o3::DynInstConstPtr& dynInst);

        /**
         * Get a port with a given name and index. This is used at
         * binding time and returns a reference to a protocol-agnostic
         * port.
         *
         * @param if_name Port name
         * @param idx Index in the case of a VectorPort
         *
         * @return A reference to the given port
         */
        Port &getPort(const std::string &if_name,
                      PortID idx=InvalidPortID) override;


 	};
}

#endif // __LEARNING_GEM5_HELLO_OBJECT_HH__
