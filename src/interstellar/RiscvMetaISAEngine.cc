/*#################################################################
# RISC-V Interstellar Centralized Engine GEM5 Simulation Object
# riscvInterstellarEngine.cc for RISC-V architecture 
#  RiscvInterstellarEngine class implementation
#
# Author: Abdelrhman Mohamed Abotaleb
# Date  : 2 March 2022
#
##################################################################*/

#include <string>
#include <vector>

#include "RiscvMetaISAEngine.hh"
#include "cpu/o3/dyn_inst.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Interstellar_IPP.hh"
#include "debug/MetaISA_IPP_CSR.hh"
#include "debug/Interstellar_Filter_Pkt.hh"
#include "debug/Interstellar_IPP_CPU.hh"
#include "debug/MetaISA_IPP_PARSE.hh"
#include "debug/MetaISA_IPP_PC.hh"
#include "debug/MetaISA_L1DCache.hh"
#include "debug/MetaISA_TLB.hh"
#include "debug/MetaISA_LLC_Miss_All.hh"
#include "debug/MetaISA_IPP_PERIOIDC.hh"
#include "debug/MetaISA_IPP_PC_TICK.hh"
#include "debug/MetaISA_IPP_From_LLC_MPKT.hh"
#include "debug/MetaISA_IPP_From_MC_MPKT.hh"
#include "debug/MetaISA_Ptr_Dump_Queue.hh"
#include "debug/MetaISA_Ptr_Logic.hh"

namespace gem5
{

	namespace RiscvISA
	{
		RiscvMetaISAEngine::MetaISAStats::MetaISAStats(RiscvMetaISAEngine &obj):
		statistics::Group(&obj),
		ADD_STAT(metaisa_blocked_cycles, statistics::units::Count::get(),
             	"number of blocked cycles in MetaISA Engine") 
		{

		}

		void RiscvMetaISAEngine::MetaISAStats::regStats() 
		{
			    using namespace statistics;
    			statistics::Group::regStats();

		}
		RiscvMetaISAEngine::RiscvMetaISAEngine(const RiscvMetaISAEngineParams &p)
			: BaseInterstellarEngine(p),
  		   stats(*this)
		{
			// if(cpu_type == O3)
			//     cpu_vec=reinterpret_cast<CPU*>(p.cpu_list)
			totL1misses = new int[cpu_vec.size()];
			for (size_t i = 0; i < cpu_vec.size(); i++)
				totL1misses[i] = 0;
			for (size_t i = 0; i < MAX_LOOP_DESC_NUM; i++)
				loopInstrCommitted[i] = true; /* Initally unlock this guerd to incremese the iteration# once loop instruction is to be committed */
			/* Initalize the blocked queue maximum size and actual length */
			maxBPQsize = p.BPQSize;
			crntBPQLen =          0 ;
			stressors_list = p.stressors_list;
			stressors_probe_listerners_list = p.stressors_probe_listerners_list;
			
			
			/**** Pointer Chasing ***/
			this->numRemPtrStreams = 0;

			DPRINTF(Interstellar_IPP, "RISCV MetaISA Centralized Object Is Initialized!\n");

			//[Abotaleb] Hardwire TLBInverse
			if(cpu_vec.size()==1)
			{
				this->tlb_inverse.insert({2,1});			
				DPRINTF(Interstellar_IPP, "cpu_vec length = 1 \n");
			}
			if(cpu_vec.size()==4)
			{			
			  this->tlb_inverse.insert({8,1});
			  this->tlb_inverse.insert({9,1});
			  this->tlb_inverse.insert({10,1});
			  this->tlb_inverse.insert({11,1});
				DPRINTF(Interstellar_IPP, "cpu_vec length= 4 \n");

			}
			if(cpu_vec.size()==8)
			{			
			  this->tlb_inverse.insert({0x10,1});
			  this->tlb_inverse.insert({0x11,1});
			  this->tlb_inverse.insert({0x12,1});
			  this->tlb_inverse.insert({0x13,1});
			  this->tlb_inverse.insert({0x14,1});
			  this->tlb_inverse.insert({0x15,1});
			  this->tlb_inverse.insert({0x16,1});
			  this->tlb_inverse.insert({0x17,1});
			  DPRINTF(Interstellar_IPP, "cpu_vec length= 8 \n");

			}
			if(cpu_vec.size()==16)
			{			
			  this->tlb_inverse.insert({0x20,1});
			  this->tlb_inverse.insert({0x21,1});
			  this->tlb_inverse.insert({0x22,1});
			  this->tlb_inverse.insert({0x23,1});
			  this->tlb_inverse.insert({0x24,1});
			  this->tlb_inverse.insert({0x25,1});
			  this->tlb_inverse.insert({0x26,1});
			  this->tlb_inverse.insert({0x27,1});
			  this->tlb_inverse.insert({0x28,1});
			  this->tlb_inverse.insert({0x29,1});
			  this->tlb_inverse.insert({0x2a,1});
			  this->tlb_inverse.insert({0x2b,1});
			  this->tlb_inverse.insert({0x2c,1});
			  this->tlb_inverse.insert({0x2d,1});
			  this->tlb_inverse.insert({0x2e,1});
			  this->tlb_inverse.insert({0x2f,1});			  
     		  DPRINTF(Interstellar_IPP, "cpu_vec length= 16 \n");

			}
			if(cpu_vec.size()==32)
			{			
			  this->tlb_inverse.insert({0x40,1});
			  this->tlb_inverse.insert({0x41,1});
			  this->tlb_inverse.insert({0x42,1});
			  this->tlb_inverse.insert({0x43,1});
			  this->tlb_inverse.insert({0x44,1});
			  this->tlb_inverse.insert({0x45,1});
			  this->tlb_inverse.insert({0x46,1});
			  this->tlb_inverse.insert({0x47,1});
			  this->tlb_inverse.insert({0x48,1});
			  this->tlb_inverse.insert({0x49,1});
			  this->tlb_inverse.insert({0x4a,1});
			  this->tlb_inverse.insert({0x4b,1});
			  this->tlb_inverse.insert({0x4c,1});
			  this->tlb_inverse.insert({0x4d,1});
			  this->tlb_inverse.insert({0x4e,1});
			  this->tlb_inverse.insert({0x4f,1});
			  this->tlb_inverse.insert({0x50,1});
			  this->tlb_inverse.insert({0x51,1});
			  this->tlb_inverse.insert({0x52,1});
			  this->tlb_inverse.insert({0x53,1});
			  this->tlb_inverse.insert({0x54,1});
			  this->tlb_inverse.insert({0x55,1});
			  this->tlb_inverse.insert({0x56,1});
			  this->tlb_inverse.insert({0x57,1});
			  this->tlb_inverse.insert({0x58,1});
			  this->tlb_inverse.insert({0x59,1});
			  this->tlb_inverse.insert({0x5a,1});
			  this->tlb_inverse.insert({0x5b,1});
			  this->tlb_inverse.insert({0x5c,1});
			  this->tlb_inverse.insert({0x5d,1});
			  this->tlb_inverse.insert({0x5e,1});
			  this->tlb_inverse.insert({0x5f,1});			  			  
     		  DPRINTF(Interstellar_IPP, "cpu_vec length= 32 \n");

			}			
			DPRINTF(Interstellar_IPP, "RISCV TLBInverse Is Initialized!\n");

			

		}

		void RiscvMetaISAEngine::regProbeListeners()
		{
			DPRINTF(Interstellar_IPP, "MetaISA Probe listenser Enable Config = %d\n", static_cast<int>(interstellar_probe_enable));

			// MetaISA Engine Working with CPU Listeners
			for (size_t i = 0; i < cpu_probe_listeners.size(); i++)
			{
				drivingCoreType = CPU;

			  
				
				//[Abotaleb] For Fast Simulation : Hardwire TLBInverse (succed with 1 page for whole heap -> for more pages and multicore won't work)
				
				// Attach listener to "va2pa" probe "ppTLB"  [ Virtual to Physical Translation ]
				dTLB_probe_listerners_list[i]->connectListener<ProbeListenerArg<RiscvMetaISAEngine, gem5::RequestPtr>>(this, "va2pa", &RiscvMetaISAEngine::processVA2PAOptimized);
				// Attach listener to "metaisa_csrwt" probe "ppMetaCSRWT"  [ Writing to Meta CSR Registers ]
				interstellar_csr_listerners[i]->connectListener<ProbeListenerArg<RiscvMetaISAEngine, gem5::RiscvISA::MetaIsaCsrDataPtr>>(this, "metaisa_csrwt", &RiscvMetaISAEngine::processCSRWT);
			}

			// MetaISA Engine Working with Memory Stressor Listeners
			for (size_t i = 0; i < stressors_list.size(); i++)
			{
				drivingCoreType = MEM_STRESSOR;

				stressors_probe_listerners_list[i]->connectListener<ProbeListenerArg<RiscvMetaISAEngine, gem5::RiscvISA::MetaIsaCsrDataPtr>>(this, "memStressor_csrwt", &RiscvMetaISAEngine::processCSRWT);
				stressors_probe_listerners_list[i]->connectListener<ProbeListenerArg<RiscvMetaISAEngine, gem5::RequestPtr>>(this, "va2pa", &RiscvMetaISAEngine::processVA2PAOptimized);
				DPRINTF(Interstellar_IPP, "MetaISA Registering listeners on the memory stressor's probes\n");																										
			}
		}

			int RiscvMetaISAEngine::determineIsaCPU(gem5::RiscvISA::ISA *pISA)
			{

				int crntCPU = -1;
				/* Search which cpu id is associated with this cache packet*/
				for (size_t i = 0; i < isa_vec.size(); i++)
				{
					if (isa_vec[i] == pISA)
					{
						crntCPU = i;
					}
				}
				return crntCPU;
			}

		/***********************************************************************
		 *                 RiscvMetaISAEngine::preProcessCSRDesc
		 *  Prior to insert a new MetaISA descriptor  into the descriptor table,
		 *    This function combines two 64-bit CSRs into 128-bit , it is
		 *    also  pre-processes some of them like the loop descriptor
		 *    and replace the PC offset with the actual absolute PC of the
		 *    loop start, and also this function will print some of the
		 *    MetaISA descriptors debugging messages when they are captured.
		 *
		 * @param pmisa_desc pointer to the
		 *
		 * *********************************************************************/
		MISA_Desc_t
		RiscvMetaISAEngine::preProcessCSRDesc(int crntCPU, uint64_t newCSR)
		{
			/*********/
			MISA_Desc_t misa_desc;

			MISA_toCSR meta_isa_obj;
			meta_isa_obj.desc_word[0] = tempCSR[crntCPU];
			meta_isa_obj.desc_word[1] = newCSR;
			/********* Prior to Descriptor Table Insertion Some Preprocessing is done */
			string drivingCoreName = (drivingCoreType==MEM_STRESSOR)?"Mem_Stressor":"CPU";

			switch (meta_isa_obj.MISA_Desc_obj.type)
			{
			case (LOOP):
			{
				// Replace the PC offset with the absolute PC of the loop
				//  TODO Get the current Thread ID from the probem CSR write change
				int tid = 0;
				uint64_t crntPC;
				if (cpu_type == TypeCPU::O3CPU || cpu_type == TypeCPU::DerivO3CPU)
					uint64_t crntPC = ((gem5::o3::CPU*) (cpu_vec[crntCPU]))->pcState(tid).instAddr();
				else 
					uint64_t crntPC =  0; // @TODO : How to compute address in other cpu models ? (gem5::BaseCPU)  cpu_vec[crntCPU]->getContext(0)->get
				/* Take care that this field is of 32-bit length */
				/* it may be recommended to hash the PC  % (2^32)
				which is simply in c ; casting uint64_t to uint32_t */
				DPRINTF(Interstellar_IPP, "%s[%d] : Loop Descriptor Object Inserted to Descriptor Table\n",drivingCoreName, crntCPU);
				DPRINTF(Interstellar_IPP, "%s[%d] : Loop Start     = 0x%llx \n",drivingCoreName, crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.loopDesc.initVal);
				DPRINTF(Interstellar_IPP, "%s[%d] : Loop End       = 0x%llx \n",drivingCoreName, crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.loopDesc.endVal);
				DPRINTF(Interstellar_IPP, "%s[%d] : Loop PC Offset = 0x%llx \n",drivingCoreName, crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.loopDesc.headerPCOffset);
				meta_isa_obj.MISA_Desc_obj.descInfo.loopDesc.headerPCOffset += crntPC;
				DPRINTF(Interstellar_IPP, "%s[%d] : Loop PC     = 0x%llx \n", drivingCoreName,crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.loopDesc.headerPCOffset);

				break;
			}
			case (DIR_STREAM):
			{
				DPRINTF(Interstellar_IPP, "%s[%d] : Direct Stream Object Inserted to Descriptor Table\n", drivingCoreName,crntCPU);
				DPRINTF(Interstellar_IPP, "%s[%d] : Direct Stream VA = 0x%llx \n",drivingCoreName, crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.streamDesc.baseAddr);
				DPRINTF(Interstellar_IPP, "%s[%d] : Direct Stream Stride = 0x%llx \n",drivingCoreName, crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.streamDesc.stride);

				break;
			}
			case (INDIR_STREAM):
			{
				DPRINTF(Interstellar_IPP, "%s[%d] : Indirect Stream Object Inserted to Descriptor Table\n", drivingCoreName,crntCPU);
				DPRINTF(Interstellar_IPP, "%s[%d] : Indirect Stream VA = 0x%llx \n",drivingCoreName, crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.stream.baseAddr);
				DPRINTF(Interstellar_IPP, "%s[%d] : Indirect Stream Stride = 0x%llx \n",drivingCoreName, crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.stream.stride);

				break;
			}	
			case  (PTR_CHASE):
			{
				DPRINTF(Interstellar_IPP, "%s[%d] : Pointer Chasing Stream Object Inserted to Descriptor Table\n", drivingCoreName,crntCPU);
				DPRINTF(Interstellar_IPP, "%s[%d] : Pointer Chasing Head Offset = %d - Tail Offset = %d \n", drivingCoreName,crntCPU, meta_isa_obj.MISA_Desc_obj.descInfo.ptrChaseDesc.offsetNext,meta_isa_obj.MISA_Desc_obj.descInfo.ptrChaseDesc.offsetPrev);
				DPRINTF(Interstellar_IPP, "%s[%d] : Pointer Chasing Head Address = %p - Tail Address = %p \n", drivingCoreName,crntCPU,meta_isa_obj.MISA_Desc_obj.descInfo.ptrChaseDesc.headAdddr,meta_isa_obj.MISA_Desc_obj.descInfo.ptrChaseDesc.tailAdddr);				
				this->numRemPtrStreams++;
				break;
			}		
			default:
				break;
			}
			misa_desc = meta_isa_obj.MISA_Desc_obj;
			return misa_desc;
		}

		void
		RiscvMetaISAEngine::processCSRWT(const gem5::RiscvISA::MetaIsaCsrDataPtr &metaIsaCsrData)
		{

			// Determine the current CPU (TODO : and Thread)
			// TODO Map the position of ISA within interstellar_csr_listerners to a specific thread and CPU.
			int crntDriver=-1;
			if(stressors_list.size()>0)
				crntDriver = 0;
			else 	
			    crntDriver = this->determineIsaCPU((gem5::RiscvISA::ISA *)metaIsaCsrData->pISA);

			int metaISAIdx = (metaIsaCsrData->misc_reg - MISCREG_METAISA0L);
			//printf("CSR MetaISAIdx = %d\n",metaISAIdx);
			if (metaISAIdx % 2 == 0)
				tempCSR[crntDriver] = metaIsaCsrData->val;
			else
			{
				int streamID = (metaIsaCsrData->misc_reg - MISCREG_METAISA0L)/2;
				printf("Inserting Data For Stream ID = %d\n",streamID);
				MISA_Desc_t preProcessedDesc = preProcessCSRDesc(crntDriver, metaIsaCsrData->val);
				if(!preProcessedDesc.valid)
				{
					descTable.removeDesc(streamID,crntDriver);
					// Send Fake Packet to ramulator
					Request::Flags   reqFlags(0); 
					// REquest size = 1 will be the same as packet size 
					RequestPtr req = std::make_shared<Request>(0,1,reqFlags,gem5::Request::funcRequestorId );
					PacketPtr invalidatePkt = new Packet(req,MemCmd::Command::WriteClean);
					invalidatePkt->setMetaISAStreamID(streamID);
					invalidatePkt->setInValidateStream( true); 
					invalidatePkt->setMetaISARequestorID(crntDriver);
					PacketDataPtr* data = (PacketDataPtr*) malloc(sizeof(PacketDataPtr));//Dummy Data of size = 1 
					invalidatePkt->dataStatic(data);
					if(!this->blockedPacketQueue.empty())
					{
						insertToBlockedPacketQueue(invalidatePkt);
					}
					else
					{
						if(!memSidePort.sendPacket(invalidatePkt))
						{
							insertToBlockedPacketQueue(invalidatePkt);
						}
					}
					printf("Remove Descriptor : %d\n",streamID);
				}
				else
				{

             		descTable.insertDesc(preProcessedDesc,streamID, crntDriver);
					printf(" -> Parent  Loop  ID = %d\n",preProcessedDesc.descInfo.streamDesc.loopDescId);

				}

				/* Start to track PC :
				   Make sure that the PCTrackEvent is not scheduled  before */
				if (::gem5::debug::MetaISA_IPP_PC_TICK)
					if (!PCTrackEvent.scheduled())
						schedule(PCTrackEvent, curTick() + 1);
			}
			DPRINTF(MetaISA_IPP_CSR, "CSR of CPU[%d] Being Written : CSRs[META%d][REG%d] = 0x%016llx\n",
					crntDriver,(metaIsaCsrData->misc_reg - MISCREG_METAISA0L) / 2,
					(metaIsaCsrData->misc_reg - MISCREG_METAISA0L),
					metaIsaCsrData->val);
		}



		/**
		 * @brief
		 *  I- If this address not belong to any MetaISA Stream:
		 *		Simply forward this packet to MC
		 *  II-If this address belong to a stream:
		 *      1- Compute the next expected LLC miss
		 *      2- Convey this LLC miss packet into the memory packet
		 *      3- Send the modified Packet to the memory controller
		 ***/
		bool
		RiscvMetaISAEngine::handleRequest(PacketPtr pkt)
		{
			DPRINTF(MetaISA_IPP_From_LLC_MPKT, "Got Timing request for addr %#x (Requestor ID =%#lx)\n", pkt->getAddr() , pkt->getMetaISARequestorID());
			
			//TODO  Corner Casre: Requestor ID is not set 
			int num_cores = cpu_vec.size();
			if(pkt->getMetaISARequestorID()>num_cores)
			{
				//In case of such request recieved 
				//printf("FUN::Request from un-initalized source requestor ID is received!\n");
				pkt->setMetaISARequestorID(0);
			}
			// // @ Uncomment for Debugging Purposes
			// int page_num  = pkt->getAddr()>>PageShift;
			// if(num_cores==4)
			// {
			// 	switch (page_num)
			// 	{
			// 		case 8:
			// 			pkt->setMetaISARequestorID(0);
			// 			break;
			// 		case 9:
			// 			pkt->setMetaISARequestorID(1);
			// 			break;
			// 		case 10:
			// 			pkt->setMetaISARequestorID(2);
			// 			break;
			// 		case 11:
			// 			pkt->setMetaISARequestorID(3);
			// 			break;
			// 		default:
			// 			break;
			// 	}
			// }
 
							

			// blocked = true;
			uint64_t crntMissPAddr = pkt->getAddr();
			// printf("Miss paddr = %lu\n",crntMissPAddr);

			uint64_t stride, strideMultiple, cacheLineSize, nextLLCMissPAddr = 0x0;
			// Get LLC Line Size
			if (this->l2_cache != nullptr) // LLC is L2
				cacheLineSize = this->l2_cache->getBlockSize();
			else // LLC is L1
				cacheLineSize = this->l1_dcaches_vec[0]->getBlockSize();

			// Search for the direct stream entry with same PFN as miss request
			Table1_Entry *pAssocStream;
			descType     _descType;
			const std::string cmdType = pkt->cmdString();
			bool      is_base_addr = false;

			pAssocStream = descTable.getStrEntMatchLLCPAOptPerCore(crntMissPAddr, cmdType , _descType,pkt->getMetaISARequestorID(),is_base_addr,&tlb_inverse);

			pkt->setMetaISAStreamType(NONE);      // Inital value work for any other requests
			pkt->setMetaISAStreamID(NONE_STR_ID); // Assume None Stream ID at first 
			pkt->setMetaISAStride(0);
			pkt->setInValidateStream (false       ) ; // Valid Stream
			pkt->setIsMetaISABaseAddr(is_base_addr) ; 

			
			//With Direct Stream , Send the Next Address
			if(pAssocStream != nullptr)
			{
				uint8_t metaISAStreamID = pAssocStream->metaISAStreamID;
				pkt->setMetaISAStreamID(metaISAStreamID);
			 
				if ( _descType==DIR_STREAM  )
				{
					stride = pAssocStream->entryCSRData.descInfo.streamDesc.stride;
					pkt->setMetaISAStreamType((uint8_t)DIR_STREAM);
					pkt->setMetaISAStride((uint16_t) stride)      ; //For iPrefetcher Use 
					/***
				 	*  Search for least stride multiple that is equal to or greater cache line size
				 	* i.e. next expected LLC miss
				 	* @todo efficient hardware implementation !
				 	*/
					strideMultiple = stride;
					while (strideMultiple < cacheLineSize)
						strideMultiple += stride;
					nextLLCMissPAddr = crntMissPAddr + strideMultiple;
					// descTable.incDirStrVAMultStride(pAssocDirStream,strideMultiple);
					DPRINTF(MetaISA_LLC_Miss_All, "Direct stream is accepted at MetaISA Engine\n");

				}
				// If Indirect Stream 
				else 
				{
					if ( _descType==INDIR_STREAM ||  _descType==PTR_CHASE)
					{

						pkt->setMetaISAStreamType( _descType );
						string typeName = (_descType==INDIR_STREAM)?"Indirect":"Ptr chasing";
						typeName+=" stream (type = %d) is accepted at MetaISA Engine\n";
						DPRINTF(MetaISA_LLC_Miss_All,typeName.c_str() ,_descType);
					}
					if( _descType == INDIR_STREAM)
					{
						stride = pAssocStream->entryCSRData.descInfo.stream.stride;
						pkt->setMetaISAStride((uint16_t) stride)      ; //For iPrefetcher Use 
					}
						
					 
				}
			}
			pkt->setNextAddr(nextLLCMissPAddr);
			
			//printf("llcMissPA = %#lx requestor id = %d , is first address = %d\n",crntMissPAddr,pkt->getMetaISARequestorID() , pkt->getIsMetaISABaseAddr()  );

			DPRINTF(MetaISA_LLC_Miss_All, "Got LLC timing request on MetaISA ->  LLC (Line size = %d) Miss Req on MemBus: Addr=%#lx , Next Addr = %#lx\n",
					cacheLineSize, pkt->getAddr(), pkt->getNextAddr());
			//Check if request has valid data
			string validData = pkt->req->hasSize()?" Yes":" No";
			DPRINTF(MetaISA_LLC_Miss_All, "\t Of Valid size :%s of size =%d \n",
					validData, pkt->req->getSize());


			/* Forward the modified packet to the memory controller */
			//If there are any blocked packets , don't try to send before sending old blocked packets
			if(!this->blockedPacketQueue.empty())
			{
				if(!insertToBlockedPacketQueue(pkt))
					return false;
			}
			else // if there is no pending blocked packets
				if(!memSidePort.sendPacket(pkt))
				{
					if(!insertToBlockedPacketQueue(pkt))
						return false;
				}

			// either request sent or blocked . handleRequest should return true to add it to the route in the crossbar
			return true;
				}



		/**
		 *            interstellarFilterPkt   function 
		 * @brief
		 *  I- If this address not belong to any MetaISA Stream:
		 *		return
		 *  II-If this address belong to a stream:
		 *      1- Compute the next expected LLC miss Address - Stream ID   Stream Type 
		 *      2- Update the packet 
		 *      3- return 
		 ***/
		bool
		RiscvMetaISAEngine::interstellarFilterPkt(PacketPtr pkt)	
		{
			DPRINTF(Interstellar_Filter_Pkt, "Filter Packet of addr= %#x (Interstellar Requestor ID =%#lx)\n", pkt->getAddr() , pkt->getMetaISARequestorID());
			
			//TODO  Corner Casre: Requestor ID is not set 
			int num_cores = cpu_vec.size();
			if(pkt->getMetaISARequestorID()>num_cores)
			{
				//In case of such request recieved 
				printf("interstellarFilterPkt::Filter packet from un-initalized source requestor ID !\n");
				pkt->setMetaISARequestorID(0);
			}
 
 
			uint64_t crntMissPAddr = pkt->getAddr();
			uint64_t stride, strideMultiple, cacheLineSize, nextLLCMissPAddr = 0x0;
			// Get LLC Line Size
			if (this->l2_cache != nullptr) // LLC is L2
				cacheLineSize = this->l2_cache->getBlockSize();
			else // LLC is L1
				cacheLineSize = this->l1_dcaches_vec[0]->getBlockSize();

			// Search for the direct stream entry with same PFN as miss request
			Table1_Entry *pAssocStream;
			descType     _descType;
			const std::string cmdType = pkt->cmdString();
			bool      is_base_addr = false;

			pAssocStream = descTable.getStrEntMatchLLCPAOptPerCore(crntMissPAddr, cmdType , _descType,pkt->getMetaISARequestorID(),is_base_addr,&tlb_inverse);

			pkt->setMetaISAStreamType(NONE);      // Inital value work for any other requests
			pkt->setMetaISAStreamID(NONE_STR_ID); // Assume None Stream ID at first 
			pkt->setMetaISAStride(0);
			pkt->setInValidateStream (false       ) ; // Valid Stream
			pkt->setIsMetaISABaseAddr(is_base_addr) ; 

			
			//With Direct Stream , Send the Next Address
			if(pAssocStream != nullptr)
			{
				uint8_t metaISAStreamID = pAssocStream->metaISAStreamID;
				pkt->setMetaISAStreamID(metaISAStreamID);
			 
				if ( _descType==DIR_STREAM  )
				{
					stride = pAssocStream->entryCSRData.descInfo.streamDesc.stride;
					pkt->setMetaISAStreamType((uint8_t)DIR_STREAM);
					pkt->setMetaISAStride((uint16_t) stride)      ; //For iPrefetcher Use 
					/***
				 	*  Search for least stride multiple that is equal to or greater cache line size
				 	* i.e. next expected LLC miss
				 	* @todo efficient hardware implementation !
				 	*/
					strideMultiple = stride;
					while (strideMultiple < cacheLineSize)
						strideMultiple += stride;
					nextLLCMissPAddr = crntMissPAddr + strideMultiple;
					// descTable.incDirStrVAMultStride(pAssocDirStream,strideMultiple);
					DPRINTF(Interstellar_Filter_Pkt, "Direct stream is accepted at Interstellar Engine\n");

				}
				// If Indirect Stream 
				else 
				{
					if ( _descType==INDIR_STREAM ||  _descType==PTR_CHASE)
					{

						pkt->setMetaISAStreamType( _descType );
						string typeName = (_descType==INDIR_STREAM)?"Indirect":"Ptr chasing";
						typeName+=" stream (type = %d) is accepted at MetaISA Engine\n";
						DPRINTF(MetaISA_LLC_Miss_All,typeName.c_str() ,_descType);
					}
					if( _descType == INDIR_STREAM)
					{
						stride = pAssocStream->entryCSRData.descInfo.stream.stride;
						pkt->setMetaISAStride((uint16_t) stride)      ; //For iPrefetcher Use 
					}
						
					 
				}
				pkt->setNextAddr(nextLLCMissPAddr);
				return true; // Packet belongs to a stream 
			}
			return false; //Packet doesn't belong to a stream 
		}	

		bool 
		RiscvMetaISAEngine::insertToBlockedPacketQueue(PacketPtr pkt)
		{
				// Increment current blocked packet queue length 
				crntBPQLen++;
				if(crntBPQLen==maxBPQsize)
				{
					printf( "Blocked request (%d/%d) on MetaISA -> Addr=%#lx , Next Addr = %#lx\n" , crntBPQLen,maxBPQsize , pkt->getAddr(), pkt->getNextAddr());
					//DPRINTF(MetaISA_IPP_From_LLC_MPKT, "Blocked request (%d/%d) on MetaISA -> Addr=%#lx , Next Addr = %#lx\n" , crntBPQLen,maxBPQsize , pkt->getAddr(), pkt->getNextAddr());
					return false; // can't insert the blocked packet now , need to do re-try
				}
				
				//Insert packet into the blocked packets queue 
				//(Copy it first to avoid strage error happens when just push it then send to the coherent cross bar then memoiry (its contents are altered !))
				//PacketPtr pPktCopy = new Packet(pkt,false,true);
				//pPktCopy->setNextAddr(pkt->getNextAddr());
				//blockedPacketQueue.push(pPktCopy);
				blockedPacketQueue.push_back(pkt);
				
				/*if(!sendBlockedRequests.scheduled())
					schedule(sendBlockedRequests,curTick() + 2*clockPeriod());
				*/	
			return true; 
		}

		void
		RiscvMetaISAEngine::resendBlockedPktEvent()
		{
			// v25.1 adaptation: this is called ONLY from McSidePort::recvReqRetry()
			// when the XBar signals it is ready to accept a new packet.
			// No event-based retry — purely callback-driven.
			while(!this->blockedPacketQueue.empty())
			{
				stats.metaisa_blocked_cycles++;
				PacketPtr pkt = this->blockedPacketQueue.front();
				DPRINTF(MetaISA_IPP_From_LLC_MPKT,"RiscvMetaISA Retry send pkt addr = %#lx next addr:%#lx\n",pkt->getAddr(),pkt->getNextAddr());
				if(memSidePort.sendPacket(pkt))
				{
					DPRINTF(MetaISA_IPP_From_LLC_MPKT, "Re-send Request Packet Done (UnBlocking it) : Timing request for addr %#lx\n", pkt->getAddr());
					blockedPacketQueue.pop_front();
					crntBPQLen--;
				}
				else
				{
					DPRINTF(MetaISA_IPP_From_LLC_MPKT, "Re-send Request Packet Fail (blocked again)\n");
					break; // Stop sending since the port is blocked again (xbar will trigger recvReqRetry later)
				}
			}
		}

		/***********************************************************************
		 *                 RiscvMetaISAEngine::handleResponse
		 *  Handle Resoinses from the memory controller
		 *
		 * @param pkt Memory Packet
		 *
		 * *********************************************************************/
		bool
		RiscvMetaISAEngine::handleResponse(PacketPtr pkt)
		{
			
			DPRINTF(MetaISA_IPP_From_MC_MPKT, "Got response for addr %#x - Type = %d\n", pkt->getAddr(),pkt->getMetaISAStreamType());
			
			//If the sent packet was marked as pointer then extract VA from its response
			if(pkt->getMetaISAStreamType()==PTR_CHASE)
			{

				//Extract response at position offset 
				Table1_Entry *pAssocDirStream;
				descType     _descType;
				const std::string cmdType = pkt->cmdString();

				
				pAssocDirStream = descTable.getStrEntMatchLLCPA(pkt->getAddr(), cmdType , _descType);
				//It must not be Null
				assert(pAssocDirStream!=nullptr);

				int loc2 = pAssocDirStream->extraFieldsLoc;
				Addr nodePA          = descTable.getPtrChasePA(loc2);
				//assert(_descType==PTR_CHASE);
				/*

				//Search for all possible VA locations within cache line
				int offset    = pAssocDirStream->entryCSRData.descInfo.ptrChaseDesc.offsetNext;
				Addr ptrVAddr[4],
				int offset2,vaInd=0;
				for(int i = -4 ; i<=4 ; i++)
				{
					ptrVAddr[vaInd]=0x0;
					offset2 = offset + (i*NODE_SIZE);
					if(offset2<=0 || offset2>64)
						continue;
					if(gem5::debug::MetaISA_Ptr_Logic)
					{					
						printf("Offset2 = %d\n",offset2);
					}

					for(int j = 0	;	j<8	;	j++)
					{
						ptrVAddr[vaInd] |=  (pkt->getPtr<uint8_t>()[offset2+j]<<(8*j));				
					}
					vaInd++;

				}

				this->descTable.setPtrChaseVAs(loc2,ptrVAddr);

				for(int i = 0       ; i<4;i++)
					DPRINTF(MetaISA_Ptr_Logic, "Pointer descriptor fetched from node: PA =%#lx VA[%d] is updated to %#lx\n",pkt->getAddr(),i,ptrVAddr[i]);

				*/

				//If always we read the next pointer
				//Then the correct offset will be :
				//  PA of the node  - pkt->getAddr()  
				int actualOffset = nodePA- pkt->getAddr();
				Addr ptrTrueVAddr;
				if(gem5::debug::MetaISA_Ptr_Logic)
				{
					printf("\tWhole pointer data :\n");
					for(int i = 0 ; i <64 ;i++)
					{
						if(i%16==0)
							printf("\n");
						printf("\t%#x",pkt->getPtr<uint8_t>()[i]);
					}
					printf("\tExpected VA at offset = %d\n",actualOffset);
				}

				//Assume data are aligned on 16 Bytes (Size of Node)
				
				ptrTrueVAddr=0x0;
				for(int j = 0	;	j<8	;	j++)
				{
					ptrTrueVAddr |=  (pkt->getPtr<uint8_t>()[actualOffset+j]<<(8*j));				
				}
				
				//The response will be the new pointer stream expected virtual address
				this->descTable.setPtrChaseVA(loc2,ptrTrueVAddr);
				DPRINTF(MetaISA_Ptr_Logic, "Pointer descriptor fetched from node: PA =%#lx VA is updated to %#lx\n",pkt->getAddr(),ptrTrueVAddr);

				llcSidePort.sendPacket(pkt);
				llcSidePort.trySendRetry();
				return true;

			}			


			/** Only Run "Pointer Chasing Logic" if:
			 *  Pointer Chasing descriptor is enabled and not consumed
			 * *******************************************************/
			if(numRemPtrStreams==0)
			{
				DPRINTF(MetaISA_Ptr_Dump_Queue, "No Pointers to be examined in the response\n");
				llcSidePort.sendPacket(pkt);
 				// if it needs to send a retry, it should do it
				// now since this memory object may be unblocked now.
				llcSidePort.trySendRetry();
				return true;
			}
			//Don't continue if this packet is marked to be a known stream
			if(pkt->getMetaISAStreamType()!=NONE)
			{
				llcSidePort.sendPacket(pkt);
				llcSidePort.trySendRetry();
				return true;
			}				

			//Check if inserted before 
			bool done = false;
			for(std::vector<cacheLine>::iterator it = respDataQueue.begin(); it != respDataQueue.end(); ++it)
        		if((*it).ownerAddr==pkt->getAddr()) 
				{
					//May be it is useful to mark this address 
					// as a static data and remove it from respDataQueue
					respDataQueue.erase(it);
					DPRINTF(MetaISA_Ptr_Dump_Queue, " PTR Examination: Static addr %#x\n", pkt->getAddr());
					llcSidePort.sendPacket(pkt);
					llcSidePort.trySendRetry();					
					return true;
				}

			// Store the response -> if not of known stream type (i.e. direct stream or indirect stream)
			cacheLine newDataLine;
			//Optimize use memcpy
			for(int i=0;i<cache_line_size;i++)
				newDataLine.respData[i] = pkt->getPtr<uint8_t>()[i];
			newDataLine.ownerAddr = pkt->getAddr();//physical address used to elimnate static data 
			respDataQueue.push_back(newDataLine);
			//DPRINTF(MetaISA_Ptr_Dump_Queue,"Response Queue related to PAddress = %lx",(*it).ownerAddr);
			
			if (::gem5::debug::MetaISA_Ptr_Dump_Queue)
			{
				DPRINTF(MetaISA_Ptr_Dump_Queue,"Response Data Queue : ");
				for(std::vector<cacheLine>::iterator it = respDataQueue.begin(); it != respDataQueue.end(); ++it)
        		{
					DPRINTF(MetaISA_Ptr_Dump_Queue,"Response Queue related to PAddress = %lx\n",(*it).ownerAddr);
					
					printf("\t\tData = ");
					for(int i=0;i<cache_line_size;i++)
					{
						if(i%16==0)
							printf("\n\t\t");
						printf("%x ",(*it).respData[i]);	
					}
					printf("\n");
				}

			}

			llcSidePort.sendPacket(pkt);
 			// if it needs to send a retry, it should do it
			// now since this memory object may be unblocked now.
			llcSidePort.trySendRetry();
			return true;
		}


		/***********************************************************************
		 *                 RiscvMetaISAEngine::processVA2PAOptimized
		 *  TLB Accesses (Misses in SE Mode) CallBack
		 *  Optimized Using TLB Inverse 
		 * @param req request processed by the TLB
		 *
		 * *********************************************************************/
		void RiscvMetaISAEngine::processVA2PAOptimized(const gem5::RequestPtr &req)
		{
			//Skip Insertion to TLB Inverse if added before 
			Addr  reqPA = req->getPaddr();
			Addr  pfn   = reqPA >> PageShift;
			//int    p    = req->
			if(tlb_inverse.find(pfn)!=tlb_inverse.end())
				return; //It is already exist in TLB Inverse (Exit)

			Addr     reqVA = req->getVaddr();
			uint64_t vpn = reqVA >> PageShift;
			 
			Table1_Entry *pAssocStream = descTable.getStrEntByVARangeOpt(reqVA);
			
			string message_stream  = "";
			//Only Add it to TLB Inverse if it is a valid stream 
			if (pAssocStream != nullptr)
			{
				tlb_inverse[pfn] = vpn ; 
				printf("TLB-1[%lx]=%lx\n",pfn,vpn);
				//Note that the message indicates the first stream that has this PFN 
				//As PFN can be shared between more than a stream .
				message_stream  =  "First found to Lie in "+descNames[pAssocStream->entryCSRData.type]+" stream";							 		
			}
			else 
			// If Not Direct Stream or Indirect Stream , It Can be pointer Chasing
			{

				if(numRemPtrStreams>0)
				{
					//In Slide Window Fashion , Compare the VA against the data stored in respDataQueue
					//@TODO change the masking method to filter VAs to another suitable
					uint64_t reqVAddr =  req->getVaddr() &  ~(CACHE_GRANULVL_MASK); /* Mask the least 6 bits -> Cache level granularity */ 
					//Little Endian Comparison 
					for(std::vector<cacheLine>::iterator it = respDataQueue.begin(); it != respDataQueue.end(); ++it)
					{
						bool areEqual ;
						//Iterate over 64 byte
						int i = 0; /* will be used to store the next pointer offset */
						DPRINTF(MetaISA_Ptr_Logic,"Compare Request at %#lx with data of address %#lx\n",reqVAddr,(*it).ownerAddr);
						//Debug at Compare Request at 0x20140 with data of address 0x20600
						for(i = 0 ; i <56 ;i++) // indexing in i+j
						{

							areEqual = true;
							// at j=0 (The response and address in response ) must be in equal but with masking cache level granularity
							if( ((*it).respData[i]& ~(CACHE_GRANULVL_MASK)) != ((reqVAddr& 0xFF )))
							{
								areEqual = false;
								continue;
							}							

							for(int j = 1 ; j < 8 ;j++)
							{

								if( (*it).respData[i+j] != ((reqVAddr& (0xFF <<(8*j)))>>(8*j)))
								{
									//DPRINTF(MetaISA_Ptr_Logic,"i=%d , j=%d , respdata = %d , addr part = %d\n",i,j,(*it).respData[i+j],((reqVAddr& (0xFF <<(8*j)))>>(8*j)));

									areEqual = false;
									break;
								}	
							}
							if(areEqual)//Address is found in the data
								break;
						}
						if(areEqual)
						//Address is found in data -> Pointer Type 
						// @TODO more safety -> don't push in descriptor table unless this behaviour repeated for let's say 5 times 
						{
							if(::gem5::debug::MetaISA_Ptr_Logic)
							{
								DPRINTF(MetaISA_Ptr_Logic,"Pointer Chase is detected in TLB-1 against respDataTable:\n");
								printf("\tnode at addr(%#lx) -> node at addr(%#lx)\n",(*it).ownerAddr,reqVAddr);
								printf("\tNext pointer is at offset = %d\n",i);
								printf("\tnumRemPtrStreams=%d\n",numRemPtrStreams);

								for(int j = 0 ; j < 8 ;j++)
								{
									printf("\trespData[%d+%d]=%#x, addr[%d]=%#lx\n",i,j, (*it).respData[i+j] ,j, ((reqVAddr& (0xFF <<(8*j)))>>(8*j)));
								}

							}
							// insert to descriptor Table 
							MISA_Desc_t ptrChaseMetaISA ;
							ptrChaseMetaISA.type = PTR_CHASE;
							ptrChaseMetaISA.valid  = 1;
							ptrChaseMetaISA.active = 1;
							ptrChaseMetaISA.descInfo.ptrChaseDesc.offsetNext=i;
							descTable.insertPtrDesc(ptrChaseMetaISA , req->getVaddr() , req->getPaddr() );
							//Decrement the number of remaining pointer streams to be found in the response queue
							numRemPtrStreams--;
							if(numRemPtrStreams==0)
								respDataQueue.clear();
							return;//In this case return as pointer is found 
						}
					}	
					
				}
				
				//Other possible case is that the pointer is already fetched 
				// But not fetched by the bound examination !!!?
				// @todo : uncomment the following line if you want to do exhastive search rather than boundary check
				//this->descTable.setPtrChasePABySrchAll(req->getPaddr(),req->getVaddr());


			}
			
			string drivingCoreName = (drivingCoreType==MEM_STRESSOR)?"Mem_Stressor":"CPU";
			
			/*
			Trial to identify the source CPU number: 
			ThreadID tid = request->instruction()->threadNumber;
        	int cpuID  = this->cpu->cpuId();
        	uint32_t _metaISARequestorID = (cpuID)+(tid<<16);*/

			DPRINTF(MetaISA_TLB, " %s[0] VA[0x%llx]->PA[0x%llx] %s\n",drivingCoreName,
					reqVA, reqPA ,   message_stream.c_str());
		}



		void
		RiscvMetaISAEngine::startup()
		{
			// schedule(event, 0); // MetaISA Centralized engine should start when the processor resets (at time 0)
			// With checkpoints , this should be updated to 
			//schedule(event, curTick() ); 
			// as after checkpoint is resumed , it will started from curTick >0 
		}

		
		
		void
		RiscvMetaISAEngine::parseCSR(uint64_t meta_csrLow, uint64_t meta_csrHigh)
		{
			MISA_toCSR meta_isa_obj;
			meta_isa_obj.desc_word[0] = meta_csrLow;
			meta_isa_obj.desc_word[1] = meta_csrHigh;
			switch (meta_isa_obj.MISA_Desc_obj.type)
			{
			case (LOOP):
			{

				DPRINTF(MetaISA_IPP_PARSE, "Loop descriptor found\n");
				break;
			}
			case (DIR_STREAM):
			{

				DPRINTF(MetaISA_IPP_PARSE, "Direct Stream descriptor found - stride = %d\n", meta_isa_obj.MISA_Desc_obj.descInfo.streamDesc.stride);
				break;
			}
			case (INDIR_STREAM):
			{
				DPRINTF(MetaISA_IPP_PARSE, "InDirect Stream descriptor found - stride = %d\n", meta_isa_obj.MISA_Desc_obj.descInfo.stream.stride);
				break;
			}
			default:
				break;
			}
		}

	}
} // namespace gem5
