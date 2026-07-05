/*#################################################################
# Riscv Interstellar Centralized Engine GEM5 Simulation Object
# baseInterstellarEngine.hh for generic architecture base
#  RISCV Interstellar Engine class declaration
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 7 March 2022
#
##################################################################*/
#ifndef __RISCV_META_ISA_ENGINE_HH__
#define __RISCV_META_ISA_ENGINE_HH__

#include <list>

#include "interstellar/base_metaisa.hpp"
#include "RiscvMetaISADescTable.hh"
#include "interstellar/baseInterstellarEngine.hh"
#include "RiscvMemoryStressor.hh"
#include "cpu/base.hh"           /* to point to base CPU */
#include "cpu/o3/cpu.hh"           /* to point to base CPU */
#include "mem/cache/cache.hh"    /* to point to Cache or use BaseCache  */
#include "arch/riscv/isa.hh"
#include "arch/riscv/regs/misc.hh"
#include "arch/riscv/utility.hh"
#include "params/RiscvMetaISAEngine.hh"
#include "base/statistics.hh"


#include "arch/riscv/pagetable.hh"
#include "arch/riscv/pma_checker.hh"

#include "sim/clocked_object.hh"
#include "base/statistics.hh"
#include "base/logging.hh"
#include "mem/request.hh"        /* Probe on memory requests [For L1 Cache] */
#include "sim/sim_object.hh"
#include <deque> //To store blocked memory requests and try to re-send them later


namespace gem5
{
  namespace RiscvISA {

	enum DrivingCoreType {CPU,MEM_STRESSOR};


	class RiscvMetaISAEngine : public BaseInterstellarEngine
	{

		protected:
			struct MetaISAStats : public statistics::Group
			{
			    MetaISAStats(RiscvMetaISAEngine &obj);

		        void regStats() override;
				/** Number of blocked cycles */
				statistics::Scalar  metaisa_blocked_cycles;
			} stats;

			int*         totL1misses;/* Array of L1 Misses , its length = num_cpu decided in teh constructor */
			int          totL2misses;/* Only One Shared L2 Cache exists */
	        //TLB Inverse   PFN ->VPN
			map< Addr ,  Addr> tlb_inverse;


			void         resendBlockedPktEvent() override;
			void         parseCSR         (uint64_t meta_csrLow , uint64_t meta_csrHigh);
            int          determineIsaCPU  (gem5::RiscvISA::ISA * pISA    );
			MISA_Desc_t  preProcessCSRDesc(int crntCPU , uint64_t newCSR );

			DescTable       descTable   	                ;
			uint64_t        tempCSR[MAX_PROCESSORS]         ;
            uint64_t        loopStartAddress; /* offset + PC when MetaISA CSR of loop changed */

            bool loopInstrCommitted[MAX_LOOP_DESC_NUM];

			/*   Memory Bus System Related Function */
            bool handleRequest(PacketPtr pkt) override;
			bool handleResponse(PacketPtr pkt) override;
			// Blocked Packets to try to re-send them (Unblocking)
            std::deque<PacketPtr> blockedPacketQueue;
			uint16_t              maxBPQsize        ; //Maximum size for the Blocked Packet Queue.
			int                   crntBPQLen        ;
			/********* Add the memory stressor interfacing ability *************/
			DrivingCoreType drivingCoreType ;
			std::vector<gem5::ProbeListenerObject*>  stressors_probe_listerners_list     ; /* list of Stressors probes      */
   			std::vector<gem5::RiscvMemoryStressor*>              stressors_list          ; /* List of Stressors             */

		public:
			RiscvMetaISAEngine(const RiscvMetaISAEngineParams &p);
			void   startup()					override;
	     	void   regProbeListeners() 		    override;
			void   processCSRWT          (const gem5::RiscvISA::MetaIsaCsrDataPtr & metaIsaCsrData);
			void   processVA2PAOptimized (const gem5::RequestPtr& pkt    			);
			bool   interstellarFilterPkt(PacketPtr pkt)	                    override ;

			//UnBlocking Module needs to store the blocked packets for future re-try to send them
			bool   insertToBlockedPacketQueue(PacketPtr pkt	);

			/*********************  Pointers Filteration   **************/
			// Queue of sliding window of data to be compared against addresses
			// It is in cache line granulaity
			int static const cache_line_size = 64;
			typedef struct cacheLineS
			{
				//Store the address to avoid adding redundent respData if related to static variable
				Addr    ownerAddr; //Physical Address of data owner
				uint8_t respData[cache_line_size];
				int     comparedTimes ;
				cacheLineS(){comparedTimes=5;};
			} cacheLine;

			vector<cacheLine>  respDataQueue;
			uint8_t      numRemPtrStreams; // Remaining Pointer Streams to be to be found in the response queue

	};
  } // namespace RiscvISA
} // namespace gem5

#endif // __RISCV_META_ISA_ENGINE_HH__

			// Override McSidePort to process blocked packet queue on retry
