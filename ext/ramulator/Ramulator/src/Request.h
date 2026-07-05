#ifndef __REQUEST_H
#define __REQUEST_H

/**
 * Mofifications Notifications from Original Ramulator Request.h 
 * 
 * @file Request.h
 * @brief  Modified to add nextAddr & next_addr_vec, to account for MetaISA interface 
 *         Extra constructors and utility functions are defined
 * @author Abdelrhman Abotaleb  
 * @contact abotaleb@mcmaster.ca  
 * 
 */

#include <list>
#include <vector>
#include <functional>
#include <iostream>
#include <cassert>
#include <cstdint>
#define  COREID_MASK_  0xFFFF
#define  coresNum_     4
using namespace std;
 
namespace ramulator
{
	static const string request_name[]  = {"RD", "WT", "REF",	"PD",  "SRE", "EX"};
    int const METAISA_REQID_BITS       = 32;
    int const METAISA_STRID_BITS       = 16;
    int const METAISA_REQID_STRID_BITS = METAISA_REQID_BITS + METAISA_STRID_BITS ; // Requestor ID Bits (Processor + Threads bits) + Stream ID Bits = 32 + 16 = 48 
	enum class DIRREQ_RT_TYPE{
		rtbh      ,  // hit in rtBatch and data is ready in iBuffer
		rtbw      ,  // hit in rtBatch but waiting fir data 
		rtbs_hit  ,  // Start of the rtBatch and Hit in the opened row buffer
		rtbs_miss ,  // Start of the rtBatch and miss 	
		rtbs_conf ,  // Start of the rtBatch and conflict 	
		none_dir  ,  // Not direct stream 
		size      , 
	};

	class Request
	{
	public:

		bool is_first_command;
		long addr    		  ;
		vector<int> addr_vec  ;

	   /**^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
         Abotaleb : Modifications in the interconnections needed 
         for MetaISA Engine 
    	*/
		bool     deactive_stream;
		// Packet IPP Type : Direct or Indirect Stream
		uint8_t  metaISAStreamType;
		// Stream ID (Maximum 256 streams)
		uint8_t  metaISAStreamID;
		// Encode both the processor ID and Thread ID
		uint32_t metaISARequestorID;
		// Next expected LLC address  (This is MetaISA Engine requirement)
		long     nextAddr;
		//Stride can be send encoded into requestor ID (for some cycles at the beginning)
		uint16_t   stride;  
		//Contains the different fields within the address  Channel , Rak , BankGroup ,Bank , Row , and Column 
		vector<int> next_addr_vec;



		/************************* iBatch Related  **************************/
		bool is_demand        ;  // Is demand request              -->  (Already exist in GEM5 Packet)
		bool is_hwp           ;  // Is Hardware Prefetched Request -->  (Already exist in GEM5 Packet)
		bool is_iprefetched   ;  //If the request is issued by intelligent prefetcher  
		bool lead_iBatch      ; // If first request is first in the iBatch (Useful in case of Future req in AWiBatch issues group of requests , we need to identify first one of them to count conflict/miss/hit stats)
		bool do_winUpdate     ;  //Update the Misses Tracking Window (Needed for iPrefetched Req under some special cases)
		//Is the request indicates the start of the stream address 
		bool       is_base_addr       ;
		bool       is_last_inIBatch   ;
		bool       is_start_inIBatch  ;// true for start request in iBatch / start of each segment in rtBatch.
		
		int        rt_segment_id      ; 
		int        rt_enforce_cmd     ;  //Some "start rtBatch" requests can enforce PRE/ACT to make all rtBatch segments are matched, this field contain 1: if P is reuired , 2: if ACT is required , 0: otherwise. (i.e. RD)
		/**vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
 
		/***************************** RTNS  Version **********************/
		// Realtime version 
		DIRREQ_RT_TYPE req_interstellarRT_type ; 


	   /**^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
         Abotaleb : PAR-BS Implementation   	                       */
		// specify which core this request sent from, for virtual address translation
		int coreid;
        bool marked;
		/**vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



		enum class Type
		{
			READ,
			WRITE,
			REFRESH,
			POWERDOWN,
			SELFREFRESH,
			EXTENSION,
			PREFETCH,
			MAX
		} type;

		long arrive = -1;
		long depart;
    	function<void(Request&,int)> callback; // call back with more info
    	function<void(Request&,int)> proc_callback; // FIXME: ugly workaround
		// gagan : is prefetch
		bool is_prefetch;


	    /* *^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
		/*  **********************  Constructors  ************************ */
		Request()
			: is_first_command(true), is_iprefetched(false),lead_iBatch(false),do_winUpdate(false), coreid(0) ,marked(false)
			{   deactive_stream = false;
				is_demand 		= false;
				is_hwp    		= false;
				req_interstellarRT_type = DIRREQ_RT_TYPE::none_dir;
				is_start_inIBatch = false; }

		Request(long addr, Type type, int coreid = 0)
			: is_first_command(true), is_iprefetched(false),lead_iBatch(false),do_winUpdate(false),addr(addr), coreid(coreid) ,marked(false) , type(type), callback([](Request& req,int bank_id){}), is_prefetch(false) ,is_base_addr(false)	, is_last_inIBatch(false)	
			{   deactive_stream = false;
				is_demand 		= false;
				is_hwp    		= false; 
				req_interstellarRT_type = DIRREQ_RT_TYPE::none_dir; 
				is_start_inIBatch = false; }

		Request(long addr, Type type, function<void(Request&,int)> callback,  bool is_prefetch = false, int coreid = 0)
			: is_first_command(true), is_iprefetched(false),lead_iBatch(false),do_winUpdate(false), addr(addr), coreid(coreid) , marked(false) , type(type), callback(callback), is_prefetch(is_prefetch) ,is_base_addr(false)  , is_last_inIBatch(false)	
			{   deactive_stream = false;
				is_demand 		= false;
				is_hwp    		= false; 
				req_interstellarRT_type = DIRREQ_RT_TYPE::none_dir; 
				is_start_inIBatch = false; }

		Request(vector<int>& addr_vec, Type type, function<void(Request&,int)> callback, bool is_prefetch = false, int coreid = 0)
			: is_first_command(true), is_iprefetched(false),lead_iBatch(false),do_winUpdate(false), addr_vec(addr_vec), coreid(coreid) ,marked(false) , type(type), callback(callback) ,is_base_addr(false)	, is_last_inIBatch(false)	
			{   deactive_stream = false;
				is_demand 		= false;
				is_hwp    		= false; 
				req_interstellarRT_type = DIRREQ_RT_TYPE::none_dir;
				is_start_inIBatch = false; }

		/***************** MetaISA Compatiable Interface Start  *************/
		/* Abotaleb: Request after modifications for the MetaISAEngine : */
		Request(long addr,long nextAddr, Type type, int coreid = 0)
			: is_first_command(true), is_iprefetched(false),lead_iBatch(false),do_winUpdate(false), addr(addr),nextAddr(nextAddr), coreid(coreid) ,marked(false) , type(type), callback([](Request& req,int bank_id){}), is_prefetch(false) ,is_base_addr(false)	, is_last_inIBatch(false)	
			{   deactive_stream = false;
				is_demand 		= false;
				is_hwp    		= false;
				req_interstellarRT_type = DIRREQ_RT_TYPE::none_dir; 	
				is_start_inIBatch = false; 
			}

		Request(long addr,long nextAddr, Type type, function<void(Request&,int)> callback,  bool is_prefetch= false, int coreid = 0)
		: is_first_command(true), is_iprefetched(false),lead_iBatch(false),do_winUpdate(false), addr(addr),nextAddr(nextAddr), coreid(coreid) ,marked(false) , type(type), callback(callback), is_prefetch(is_prefetch),is_base_addr(false)	 , is_last_inIBatch(false)	
			{   deactive_stream = false;
				is_demand 		= false;
				is_hwp    		= false;
			    req_interstellarRT_type = DIRREQ_RT_TYPE::none_dir; 				
				is_start_inIBatch = false; 
			}


		Request(vector<int>& addr_vec,vector<int>& next_addr_vec, Type type, function<void(Request&,int)> callback, bool is_prefetch = false, int coreid = 0)
			: is_first_command(true), is_iprefetched(false),lead_iBatch(false),do_winUpdate(false), addr_vec(addr_vec),
			next_addr_vec(next_addr_vec), coreid(coreid) ,marked(false) , type(type), callback(callback) ,is_base_addr(false)	, is_last_inIBatch(false)	
			{   deactive_stream = false;
				is_demand 		= false;
				is_hwp    		= false;			 	  
				req_interstellarRT_type = DIRREQ_RT_TYPE::none_dir; 
				is_start_inIBatch = false;
			}

		/***************** MetaISA Compatiable Interface End *************/
		/**vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

		/**************************** Setters   ****************************/
		void setMetaISAParams(uint8_t metaIsaStreamType , uint8_t metaIsaStreamID , uint32_t metaIsaRequestorID)
		{
			this->metaISAStreamType  = metaIsaStreamType  ;
			this->metaISAStreamID    = metaIsaStreamID    ; 
			this->metaISARequestorID = metaIsaRequestorID ;
			//int  core_id = metaISARequestorID & COREID_MASK;    //  req->coreid;
            //this->coreid     = core_id >= coresNum ? 0 :core_id;
		}

		void setiBatchParams(uint16_t stride  , bool is_demand , bool is_hwp , bool is_base_addr)
		{
			  this->stride       = stride       ; 
			  this->is_demand    = is_demand    ; 
			  this->is_hwp       = is_hwp       ;          
			  this->is_base_addr = is_base_addr ;
		}

		/********************************************************************/


		//Copy Constructor 
		/*
		Request(const Request& req)
		{
			this->coreid           = req.coreid               ;
			this->is_first_command = req.is_first_command     ;
			this->addr             = req.addr                 ;
			this->addr_vec         = req.addr_vec             ; 
			this->nextAddr         = req.nextAddr             ; 
			this->next_addr_vec    = req.next_addr_vec        ; 
			this->metaISARequestorID = req.metaISARequestorID ;
			this->metaISAStreamID    = req.metaISAStreamID    ;
			this->metaISAStreamType  = req.metaISAStreamType  ;
		}*/
		
 
		//Copy Constructor 
		Request(list<Request>::iterator req)
		{
			this->coreid             = req->coreid                       ;
			this->is_first_command   = req->is_first_command             ;
			this->is_iprefetched     = req->is_iprefetched               ;
			this->lead_iBatch        = req->lead_iBatch                  ;
			this->do_winUpdate       = req->do_winUpdate                 ;
			this->addr               = req->addr                         ;
			this->addr_vec           = req->addr_vec                     ; 
			this->nextAddr           = req->nextAddr                     ; 
			this->stride             = req->stride                       ;
			this->is_base_addr       = req->is_base_addr                 ; 
			this->is_last_inIBatch   = req->is_last_inIBatch             ; 
			this->is_start_inIBatch  = req->is_start_inIBatch            ; 
			this->next_addr_vec      = req->next_addr_vec                ; 
			this->metaISARequestorID = req->metaISARequestorID           ;
			this->metaISAStreamID    = req->metaISAStreamID      		 ;
			this->metaISAStreamType  = req->metaISAStreamType    		 ;
			this->marked       		 = req->marked        			     ;
			this->is_demand          = req->is_demand            		 ;
			this->is_hwp             = req->is_hwp               		 ;
			this->req_interstellarRT_type = req->req_interstellarRT_type ; 
		}
		

		void print()
		{
			switch(type)
			{
			case Request::Type::READ:
			std::cout << "[READ] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << addr_vec[1] << "] bg[" << addr_vec[2] << "] b["
					<< addr_vec[3] << "] ch[" << addr_vec[0] << "] row[" << addr_vec[4] << "] col[" << addr_vec[5] << "]" << std::endl;
			break;
			case Request::Type::WRITE:
			assert(is_prefetch == false);
			std::cout << "[WRITE] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << addr_vec[1] << "] bg[" << addr_vec[2] << "] b["
					<< addr_vec[3] << "] ch[" << addr_vec[0] << "] row[" << addr_vec[4] << "] col[" << addr_vec[5] << "]" << std::endl;
			break;
			case Request::Type::REFRESH:
			assert(is_prefetch == false);
			std::cout << "[REFRESH] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << addr_vec[1] << "] bg[" << addr_vec[2] << "] b["
					<< addr_vec[3] << "] ch[" << addr_vec[0] << "] row[" << addr_vec[4] << "] col[" << addr_vec[5] << "]" << std::endl;
			break;
			case Request::Type::POWERDOWN:
			assert(is_prefetch == false);
			std::cout << "[POWERDOWN] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << addr_vec[1] << "] bg[" << addr_vec[2] << "] b["
					<< addr_vec[3] << "] ch[" << addr_vec[0] << "] row[" << addr_vec[4] << "] col[" << addr_vec[5] << "]" << std::endl;
			break;
			case Request::Type::SELFREFRESH:
			assert(is_prefetch == false);
			std::cout << "[SELFREFRESH] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << addr_vec[1] << "] bg[" << addr_vec[2] << "] b["
					<< addr_vec[3] << "] ch[" << addr_vec[0] << "] row[" << addr_vec[4] << "] col[" << addr_vec[5] << "]" << std::endl;
			break;
			case Request::Type::EXTENSION:
			assert(is_prefetch == false);
			std::cout << "[EXTENSION] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << addr_vec[1] << "] bg[" << addr_vec[2] << "] b["
					<< addr_vec[3] << "] ch[" << addr_vec[0] << "] row[" << addr_vec[4] << "] col[" << addr_vec[5] << "]" << std::endl;
			break;
			case Request::Type::MAX:
			assert(is_prefetch == false);
			std::cout << "[MAX] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << addr_vec[1] << "] bg[" << addr_vec[2] << "] b["
					<< addr_vec[3] << "] ch[" << addr_vec[0] << "] row[" << addr_vec[4] << "] col[" << addr_vec[5] << "]" << std::endl;
			break;
			default:
			std::cout << "Invalid Request" << std::endl;
			}
		}

	bool operator==(const Request req2)
	{
		//This function is used in find list of req search 
		if(this->addr==req2.addr){
			return true;
		}
		else
		return false;
	}
        //New utility is defined to print the info of the new expected LLC miss address (MetaISA Engine Related)
		void print_next_addr()
		{
			switch(type)
			{
				case Request::Type::READ:
				std::cout << "[READ] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << next_addr_vec[1] << "] bg[" << next_addr_vec[2] << "] b["
						<< next_addr_vec[3] << "] ch[" << next_addr_vec[0] << "] row[" << next_addr_vec[4] << "] col[" << next_addr_vec[5] << "]" << std::endl;
				break;
				case Request::Type::WRITE:
				assert(is_prefetch == false);
				std::cout << "[WRITE] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << next_addr_vec[1] << "] bg[" << next_addr_vec[2] << "] b["
						<< next_addr_vec[3] << "] ch[" << next_addr_vec[0] << "] row[" << next_addr_vec[4] << "] col[" << next_addr_vec[5] << "]" << std::endl;
				break;
				case Request::Type::REFRESH:
				assert(is_prefetch == false);
				std::cout << "[REFRESH] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << next_addr_vec[1] << "] bg[" << next_addr_vec[2] << "] b["
						<< next_addr_vec[3] << "] ch[" << next_addr_vec[0] << "] row[" << next_addr_vec[4] << "] col[" << next_addr_vec[5] << "]" << std::endl;
				break;
				case Request::Type::POWERDOWN:
				assert(is_prefetch == false);
				std::cout << "[POWERDOWN] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << next_addr_vec[1] << "] bg[" << next_addr_vec[2] << "] b["
						<< next_addr_vec[3] << "] ch[" << next_addr_vec[0] << "] row[" << next_addr_vec[4] << "] col[" << next_addr_vec[5] << "]" << std::endl;
				break;
				case Request::Type::SELFREFRESH:
				assert(is_prefetch == false);
				std::cout << "[SELFREFRESH] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << next_addr_vec[1] << "] bg[" << next_addr_vec[2] << "] b["
						<< next_addr_vec[3] << "] ch[" << next_addr_vec[0] << "] row[" << next_addr_vec[4] << "] col[" << next_addr_vec[5] << "]" << std::endl;
				break;
				case Request::Type::EXTENSION:
				assert(is_prefetch == false);
				std::cout << "[EXTENSION] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << next_addr_vec[1] << "] bg[" << next_addr_vec[2] << "] b["
						<< next_addr_vec[3] << "] ch[" << next_addr_vec[0] << "] row[" << next_addr_vec[4] << "] col[" << next_addr_vec[5] << "]" << std::endl;
				break;
				case Request::Type::MAX:
				assert(is_prefetch == false);
				std::cout << "[MAX] pa[0x" << std::hex << addr << "] " << std::dec << " r[" << next_addr_vec[1] << "] bg[" << next_addr_vec[2] << "] b["
						<< next_addr_vec[3] << "] ch[" << next_addr_vec[0] << "] row[" << next_addr_vec[4] << "] col[" << next_addr_vec[5] << "]" << std::endl;
				break;
				default:
				std::cout << "Invalid Request" << std::endl;
			}
		}

		int getRank()
		{
		return addr_vec[1];
		}

        //New utility is defined to get the rank of the new expected LLC miss address (MetaISA Engine Related)		
		int getRankAddr2()
		{
			return next_addr_vec[1];
		}
		
	};

} /*namespace ramulator*/

#endif /*__REQUEST_H*/


