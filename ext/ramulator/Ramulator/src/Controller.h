#ifndef __CONTROLLER_H
#define __CONTROLLER_H

// Per-stream-per-core stream cap. The fork kept this in a ramulator-local copy of
// base_metaisa.hpp; it is kept ramulator-local here because the canonical
// base_metaisa.hpp is shared (via packet.hh) with gem5's prefetch/base.hh, which
// declares its own `int const MAX_STREAMS` — a shared #define would collide.
#define MAX_STREAMS 100

#include <cassert>
#include <cstdio>
#include <deque>
#include <fstream>
#include <list>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <bitset> //Abotaleb: use bitset for sliding window :: indirect address m/h speculation


#include "Config.h"
#include "DRAM.h"
#include "Refresh.h"
#include "Request.h"
#include "Scheduler.h"
#include "Statistics.h"
//#include "Iprefetcher.h"
#include "iPrefetcherCircBuffer.h"
#include "IDirStreamBuffer.h"

#include "ALDRAM.h"
#include "SALP.h"
#include "TLDRAM.h"
// #include "debug/Ramulator_IPPLogic.hh"
#include "interstellar/base_metaisa.hpp"
 
using namespace std;
#define MAX_PROCESSORS     16
#define MAX_THREADS        16
#define MASK_CORES        0xF
#define MASK_THREADS      0xF
#define CORE_BITS         16 
#define MAX_REQUESTORS MAX_PROCESSORS*MAX_THREADS

namespace ramulator
{

    extern bool warmup_complete;
    const  int MAX_IBATCH_SIZE   = 128    ;
    const  int ROW_BUFFER_SIZE   = 8192  ; // 2^(column bits + prefetch bits [log2 channel->spec->prefetch_size])
    const  int UPPER_WIN_SIZE    = 150   ; // Unfortantually bitset must be of fixed size , so declare upper limit for compile time but only use part of it in runtime
    const  int MAX_PATTERN_ELEMS =  50   ; 
    const  int STREAM_CATEGORIES =   3   ; /* Regular - Irregular , and None*/
    enum PAGE_POLICY {OPEN_POLICY,CLOSE_POLICY};
    /* Closing the page cause : Not closed - Count reached  - Next Address is conflict - above hight threshold - in gray area but previous was closed */
    enum CLOSE_CAUSE {NOT_CLOSED , COUNT_REACHED , NEXT_ADDR_CONFLICT , MISSES_ABOVE_HIGH , GRAY_PREV_CLOSED , 
    SWITCH_TO_READ1,    SWITCH_TO_READ2,    SWITCH_TO_READ3,
    SWITCH_TO_WRITE1,
    INTER_STREAM    ,    
    TRUE_CLOSED};



    enum IPP_TYPE   { IPPN  , IPPC , IPPC_WAP}; // IPPN (Next Expected Address) , IPPC (Count of hits on row) , IPPC_WAP (Apply Count policy for read requests , AP for write requests)
    typedef struct Window_info
    {
        uint8_t crnt_window_length;
        int num_misses;                          /* Event Counter  */
        bitset<UPPER_WIN_SIZE> misses_shift_reg; /* sliding window */
    } Window_Info;

    typedef struct Unified_counter_Info
    {
        uint8_t crnt_window_length;
        int num_misses;                          /* Event Counter  */
        bitset<UPPER_WIN_SIZE> misses_shift_reg; /* sliding window */
    } Unified_Counter_Info;

    template <typename T>
    class Controller
    {
    protected:
        // For counting bandwidth
        ScalarStat read_transaction_bytes;
        ScalarStat write_transaction_bytes;

        ScalarStat row_hits;
        ScalarStat row_misses;
        ScalarStat row_conflicts;
        VectorStat read_row_hits;
        VectorStat read_row_misses;
        VectorStat read_row_conflicts;
        VectorStat write_row_hits;
        VectorStat write_row_misses;
        VectorStat write_row_conflicts;
        ScalarStat useless_activates;

        ScalarStat read_latency_avg;
        ScalarStat read_latency_sum;

        //##################   Start  ##################//
        ///##########  Realtime Stats //////////////// / #
        VectorStat   read_latency_max_per_core          ;                              
        VectorStat   read_latency_avg_per_core          ;                                
        VectorStat   read_latency_sum_per_core          ;                                
        VectorStat   PerRequest_WCL                     ;                  
        VectorStat   PerRequest_WCL_dir                 ;           
        VectorStat   PerRequest_WCL_None                ;       
        VectorStat   PerRequest_WCL_dir_rtbh            ; 
        VectorStat   PerRequest_WCL_dir_rtbw            ; 
        VectorStat   PerRequest_WCL_dir_rtbs            ;  
        VectorStat   PerRequest_WCL_dir_rtbs_hit        ; 
        VectorStat   PerRequest_WCL_dir_rtbs_miss       ; 
        VectorStat   PerRequest_WCL_dir_rtbs_conf       ;  
        VectorStat * pPerRequest_WCL[(int)DIRREQ_RT_TYPE::size] = {&PerRequest_WCL_dir_rtbh,&PerRequest_WCL_dir_rtbw,&PerRequest_WCL_dir_rtbs_hit,&PerRequest_WCL_dir_rtbs_miss,&PerRequest_WCL_dir_rtbs_conf,&PerRequest_WCL_None};
        
        VectorStat   PerRequest_AvgLat                  ;
        VectorStat   PerRequest_AvgLat_dir              ;
        VectorStat   PerRequest_AvgLat_None             ;
        VectorStat   PerRequest_AvgLat_dir_rtbh         ;
        VectorStat   PerRequest_AvgLat_dir_rtbw         ;
        VectorStat   PerRequest_AvgLat_dir_rtbs         ;
        VectorStat   PerRequest_AvgLat_dir_rtbs_hit     ;  
        VectorStat   PerRequest_AvgLat_dir_rtbs_miss    ;  
        VectorStat   PerRequest_AvgLat_dir_rtbs_conf    ;  
        VectorStat * pPerRequest_AvgLat[(int)DIRREQ_RT_TYPE::size] = {&PerRequest_AvgLat_dir_rtbh,&PerRequest_AvgLat_dir_rtbw,&PerRequest_AvgLat_dir_rtbs_hit,&PerRequest_AvgLat_dir_rtbs_miss,&PerRequest_AvgLat_dir_rtbs_conf,&PerRequest_AvgLat_None};

        VectorStat   AllRequests_SumLat                  ;
        VectorStat   AllRequests_SumLat_dir              ;
        VectorStat   AllRequests_SumLat_None             ;
        VectorStat   AllRequests_SumLat_dir_rtbh         ;
        VectorStat   AllRequests_SumLat_dir_rtbw         ;
        VectorStat   AllRequests_SumLat_dir_rtbs         ;
        VectorStat   AllRequests_SumLat_dir_rtbs_hit     ;  
        VectorStat   AllRequests_SumLat_dir_rtbs_miss    ;  
        VectorStat   AllRequests_SumLat_dir_rtbs_conf    ;  
        VectorStat * pAllRequests_SumLat[(int)DIRREQ_RT_TYPE::size] = {&AllRequests_SumLat_dir_rtbh,&AllRequests_SumLat_dir_rtbw,&AllRequests_SumLat_dir_rtbs_hit,&AllRequests_SumLat_dir_rtbs_miss,&AllRequests_SumLat_dir_rtbs_conf,&AllRequests_SumLat_None};

        VectorStat   ResponseTimeSum                    ; 

        VectorStat   totRReq__rtbh                    ; 
        VectorStat   totRReq__rtbw                    ; 
        VectorStat   totRReq__rtbs                    ; 
        VectorStat   totRReq__dir                     ; 
        VectorStat   totRReq__none                    ; 
        VectorStat   totRReq__rtbs_conf , totRReq__rtbs_miss , totRReq__rtbs_hit  ; 


        ///##########  Realtime Stats //////////////// / #
        //##################   End    ##################//

        ScalarStat req_queue_length_avg;
        ScalarStat req_queue_length_sum;

        ScalarStat read_req_queue_length_avg;
        ScalarStat read_req_queue_length_sum;
        ScalarStat read_req_queue_full_times;

        ScalarStat write_req_queue_length_avg;
        ScalarStat write_req_queue_length_sum;
        ScalarStat write_req_queue_full_times;

        ScalarStat actq_req_queue_full_times;
        ScalarStat others_req_queue_full_times;

        ScalarStat totalDirStreamRequests;
        ScalarStat totalInDirStreamRequests;
        ScalarStat totalDirStreamMisPredictions;
        ScalarStat totalInDirStreamMisPredictions;

		//# Reuqests done as iBatch
        ScalarStat Tot_Req_Hit_In_iBatch              ; 
        ScalarStat Tot_Req_wait_iBatch                ; 
        ScalarStat Tot_Req_From_Readq_to_iBatch       ; 
		//# Reuqests delayed because of AW iBatch 
        ScalarStat Tot_Req_deferred_In_Future_iBatch  ; 
		//# issue iBatch and finished as iBatch 
		ScalarStat Tot_Req_iBatched                   ; 
		ScalarStat Tot_Req_issue_iBatch               ; 

        //###  Abotaleb : Get R/W and W/R transitions number       ###     
        // 1- Due to mode change only
        ScalarStat  read_to_write_transitions                            ;
        ScalarStat  write_to_read_transitions                            ;
        ScalarStat  read_to_write_transitions_on_bank_level              ;
        ScalarStat  write_to_read_transitions_on_bank_level              ;
        // 2- Due to stream (and/or) mode change          
        ScalarStat  read_stream_tot_transitions_on_bank_level            ;          
        ScalarStat  read_to_read_stream_transitions_on_bank_level        ;          
        ScalarStat  write_to_read_diff_stream_transitions_on_bank_level  ;        
        ScalarStat  write_to_read_same_stream_transitions_on_bank_level  ;                    
		ScalarStat  write_stream_tot_transitions_on_bank_level           ;
        ScalarStat  write_to_write_stream_transitions_on_bank_level      ;    
        ScalarStat  read_to_write_diff_stream_transitions_on_bank_level  ;        
        ScalarStat  read_to_write_same_stream_transitions_on_bank_level  ;        

        // 3- Abotaleb : RDA_Reasons

        ScalarStat rda_count_reached       ;
        ScalarStat rda_nxt_addr_conflict   ;
        ScalarStat rda_misses_above_high   ;
        ScalarStat rda_gray_prev_closed    ;
        ScalarStat rda_inter_stream        ;

        /*************** Reasons of PRE and ACT  ***********************************/
        ScalarStat read_conflicts_by_an_interrupting_write_to_hit_batch             ;
        ScalarStat read_conflicts_by_inter_stream                                   ;
        ScalarStat read_conflicts_intra_stream                                      ;
        ScalarStat read_conflict_precedded_by_no_read_conflict                      ;
        ScalarStat read_conflict_preceeded_by_non_useful_conflict_or_miss           ;
        ScalarStat read_conflicts_by_others                                         ;
        VectorStat read_conflicts_by_an_interrupting_other_stream_per_stream        ;  
        //Write Misses
        ScalarStat write_misses_unnecassary_by_useless_wra                          ;
        ScalarStat write_misses_preceeded_by_non_useful_conflict_or_miss            ; 
        ScalarStat write_misses_by_useful_wra                                       ;
        ScalarStat write_misses_by_others                                           ;
        //Read Misses
        ScalarStat read_misses_unnecassary_by_useless_rda                           ;
        ScalarStat read_misses_preceeded_by_non_useful_conflict_or_miss             ;
        ScalarStat read_misses_by_useful_rda                                        ;
        ScalarStat read_misses_by_others                                            ;

        ////////////////* Abotaleb : Stats Per STream \\\\\\\\\\\\\\\*/
        // 1- MC Commands
        VectorStat pre_cnt_per_Stream;
        VectorStat act_cnt_per_Stream;
        VectorStat rd_cnt_per_Stream;
        VectorStat wr_cnt_per_Stream;
        VectorStat rda_cnt_per_Stream;
        VectorStat wra_cnt_per_Stream;
        // 2- MC Commands per bank 
        VectorStat misses_per_bank;
        VectorStat rd_misses_per_bank;
        VectorStat wr_misses_per_bank;

        VectorStat conflicts_per_bank;
        VectorStat rd_conflicts_per_bank;
        VectorStat wr_conflicts_per_bank;

        VectorStat hits_per_bank;
        VectorStat rd_hits_per_bank;
        VectorStat wr_hits_per_bank;

        // 3- Intelligent Prefetches 
        VectorStat iBatches_cnt_per_Stream             ;
        VectorStat iBatches_completed_cnt_per_stream   ;   
        VectorStat iprefetches_waiting_cycles_per_Stream  ;
        VectorStat normal_waiting_cycles_per_Stream       ;

        VectorStat iBatches_cnt_per_bank               ;
        VectorStat iprefetches_completed_cnt_per_bank     ;

#ifndef INTEGRATED_WITH_GEM5
        VectorStat record_read_hits;
        VectorStat record_read_misses;
        VectorStat record_read_conflicts;
        VectorStat record_write_hits;
        VectorStat record_write_misses;
        VectorStat record_write_conflicts;
#endif

    public:
        /***********  Member Variables ***************/
        //Those are params defined in Memory.h i.e. consistent along all of the channels
        //But they are defined here for sake of Iprefetcher (Passed to controller at the memory constrcuor )
        enum class MemMapping {
            ChRaBaRoCo                ,
            RoBaRaCoCh                ,
            RoCoBaRaCh                ,
            RoBaBg1CoBg0RaCh          , 
            StrmPartRoBaBg1CoBg0RaCh  , 
            InterStreamChRoCoBaRa     ,
            InterStellarRT            , 
            MAX,
        };
        int channels ; 
        vector<Controller<T> *> ctrls; // To support inter-channel comunication for InteRStellar/InterStellarRT [However the implementation can generate the batch in Nucleus]
        RTAddrMaping  rtAddrMaping = RTAddrMaping::map1;  
        enum class ArrivalSTATUS {
            enqueueQ        ,
            iBufferH        , 
            iBufferW        ,
            iBufferWS       , //Request that hits in rtBatch but it is start of an rtBatch segment
            iBatchInPending , 
            qneueueQFull    ,              
        }arrivalStatus = ArrivalSTATUS::enqueueQ;
        // RTSS 
        int         bgNumPeriBatch                              ;
        bool        force_Coordinated_rtBatch_Segments = false  ;
        /*******    Abotaleb: Memory Mappling Is needed in Controller's Intelligent Batching *****/
        MemMapping  memoryMapping;
        vector<int> addr_bits                  ; 
        int         tx_bits                    ;
        long        partition_mask             ;
        int         partbits,shift_bits_ba_bg  ;
        long *       last_finish_time_per_core ;
        /********************************************/

        long clk = 0;
        DRAM<T> *channel;

        Scheduler<T> *scheduler ; // determines the highest priority request whose commands will be issued
        RowPolicy<T> *rowpolicy ; // determines the row-policy (e.g., closed-row vs. open-row)
        RowTable<T> *rowtable   ;   // tracks metadata about rows (e.g., which are open and for how long)
        Refresh<T> *refresh     ;
        bool        isConfigIPP     , isConfigiBatch  , isConfigIntelligent;
        bool        isAdaptive      , isQoS ;

        /* Abotaleb Recommendation : Change Queue Max Size From Configuration */
        struct Queue
        {
            list<Request> q;
            unsigned int max = 32; // c++ inialization 
            unsigned int size() { return q.size(); }
            Queue(int queueMaxSize=32)
            {
                this->max = queueMaxSize;
            }
        };

        Queue readq  = Queue(32);   // queue for read requests
        Queue writeq = Queue(32);  // queue for write requests

        // Switching  Tracking Mechansim 
        Queue writeq_per_bank[MAX_BANKS]; // queue for write requests (Per Bank)
        
        Queue actq   = Queue(32);  // read and write requests for which activate was issued are moved to
                      // actq, which has higher priority than readq and writeq.
                      // This is an optimization
                      // for avoiding useless activations (i.e., PRECHARGE
                      // after ACTIVATE w/o READ of WRITE command)
        
        //Interstellar related queue that pipe requests from iBuffer to actq  
        Queue pendingActq = Queue(8192);

        Queue otherq = Queue(32); // queue for all "other" requests (e.g., refresh)
        Queue rtBatchQ; // Queue for rtBatches only has the highest priortiy to pick the requests in case of direct streams (InterStellarRT)

        /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
        /***  Abotaleb : Direct Stream MetaISA Table  *******/
        IDirStreamBuffer * iDirStreamBuffer;

        /***  Abotaleb : Indirect Stream MetaISA Table  *******/
        // Maps The StreamID+Thread/CPU/Stressor ID to Number of hits/Misses
        int const METAISA_INDIR_THRESHOLD  = 30;
        int const COREID_MASK              = 0xFFFF;
        ////////////* Actual Window Sizes   \\\\\\\\\\\\\\\\\*/ 
        //       -> IPP Policy
        int num_banks             ;
        int Irregular_Window_Size ; 
        int Regular_Window_Size   ;
        int NoStream_Window_Size  ;
        int dirStr_CntEn          ;
        //       -> Adaptive Policy (Found in some COTS)
        int Unified_Window_Size  ; 
        // Thresholds 
        float metaisa_none_window_threshold      ;
        float metaisa_none_window_threshold_Low  ;
        float metaisa_dir_window_threshold       ;
        float  metaisa_dir_window_threshold_Low  ;
        float metaisa_indir_window_threshold     ;
        float metaisa_indir_window_threshold_Low ;
        float     unified_window_threshold       ;
        float  unified_window_threshold_Low      ;
        int    enable_unified_watermark          ;
        int    enable_ipp_dir_watermark          ;
        int    enable_ipp_indir_watermark        ;
        int    enable_ipp_none_watermark         ;
        
        // Watermark Track Last POlicy          
        int    COTS_Last_Policy [MAX_BANKS]  ;
        map<uint64_t,int>    IPP_Last_Policy ; 

        //// intelligent prefetcher
        int  enableIPrefetcher           ;
        int  enableIDirStream            ;
        int enableIPrefetcherWinUpdate   ; // Enable Misses Window Tracking Update when iPrefetch request happens
        int enableIPP                    ; // Disable IPP (Page Policy Modification)
        int iBatchWidth                 ;
        uint32_t iBufferSize            ;
        uint8_t  iBuffer_Repl_Policy    ; // iBuffer Replacement Policy (0:None , 1:LRU )
        int  enableInterStreamKnowledge  ; // Phase 2 of the Project 
        /******* Fo stats collection purpose   (How many PRE because of interrupting write batches***********/
        enum ROW_BUF_STATUS {HIT,CONFLICT,MISS};
        long                     prev_rd_addr                    [MAX_BANKS] ; //Track the previous read accessed address per each bank.
        long                     prev_wr_addr                    [MAX_BANKS] ; 
        int                      prev_wr_stream_id               [MAX_BANKS] ; 
        int                      prev_rd_stream_id               [MAX_BANKS] ;
        typename T::Command      prev_rd_cmd                     [MAX_BANKS] ; // Track previous read command (PRE,ACT, CAS (RD)?)
        ROW_BUF_STATUS           prev_rd_rb_status               [MAX_BANKS] ; // Track previous row buffer status  
        typename T::Command  prev_wr_cmd                     [MAX_BANKS] ; //Track previous read command (PRE,ACT, CAS (RD)?)        
        typename T::Command  prev_cmd                        [MAX_BANKS] ;
        uint64_t             prev_rd_metaisa_id              [MAX_BANKS] ;
        Request::Type        prev_access_type                [MAX_BANKS] ; //Track previous access type per bank  
        vector<int>          last_accessed_addr_vec_per_bank [MAX_BANKS] ; //Track previous row
        /*** To keep track of the actual rd/wr and wr/rd transitions , this must be done per bank */
        int            read_batch_accessed_banks[MAX_BANKS] ;
        int           write_batch_accessed_banks[MAX_BANKS] ;
        bool                  transition_checked[MAX_BANKS] ;
        /*** Keep Track of stream to stream transitions   ***/
        //They will be indexed with (stream_id+processor_id)
        int            read_batch_accessed_banks_per_stream[TOT_STREAMS_CORES][MAX_BANKS] ;
        int           write_batch_accessed_banks_per_stream[TOT_STREAMS_CORES][MAX_BANKS] ;
        bool                  transition_checked_per_stream[TOT_STREAMS_CORES][MAX_BANKS] ;   
        int           last_stream_id_requested[MAX_BANKS];              
        /////////////* Window Based Counters  \\\\\\\\\\\\\\\\\*/
        vector<int> * prev_addr_vec ; //Dynamic Array of Prev Addresses = of Size = # of banks  
        map<uint64_t, long long int>         metaisa_counter;
        //       -> IPP Policy
        //      Windows for IPP stream
        enum WINDOW_TYPE{ REG_WINDOW=0,IRREG_WINDOW,NONE_WINDOW};
        map < uint64_t , Window_Info          *>  metaisa_window_counter   [STREAM_CATEGORIES] ;
        //       -> Adaptive Policy (Found in some COTS) (key is the bank_id)
        map < uint64_t , Unified_Counter_Info *>  adaptive_Policy_Counter     ;

        int totDirReq,totDirMisPredictions;
        int totInDirReq,totInDirMisPredictions;
        int coresNum ; 
        
        circBuffer  < ramulator::iBufferKeyClass, ramulator::IpreftecherQueueEntry> * iBuffer[MAX_BANKS]   ;      
        circBuffer  < uint64_t   , int  > * iBatchHistory                                                               ; 

        std::vector <ramulator::IpreftecherQueueEntry>  iBatchable_FIFO; //FIFO to store iBatchable requests (can fit in iBuffer size but are not scheduled due to full actq)    
        /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

        deque<Request> pending;         // read requests that are about to receive data from DRAM
        bool write_mode = false;        // whether write requests should be prioritized over reads
        float wr_high_watermark = 0.7f; // threshold for switching to write mode
        float wr_low_watermark  = 0.3f;  // threshold for switching back to read mode
        // long refreshed = 0;  // last time refresh requests were generated

        /* Command trace for DRAMPower v5 */
        string cmd_trace_prefix = "cmd-trace-";
        vector<ofstream> cmd_trace_files;
        /* Command trace for DRAMPower v3.1/v4*/
        string cmd_tracev4_prefix = "cmd-tracev4-";
        vector<ofstream> cmd_tracev4_files;

        bool record_cmd_trace = false;

        /* Commands to stdout */
        enum CMD_TRACE
        {
            OFF = 0,
            IPP = 1,
            NOT_IPP = 2,
            ALL = 3
        };

        std::map<std::string, CMD_TRACE> cmd_trace_map = {{"off", OFF}, {"ipp", IPP}, {"not_ipp", NOT_IPP}, {"all", ALL}};
        std::map<std::string, int> logic_trace_map = {{"off", 0}, {"on", 1}};
        std::map<std::string, bool> iprefetch_logic_trace_map = {{"off", false}, {"on", true}};
        std::map<std::string, bool> print_adaptive_logic_trace_map = {{"off", false}, {"on", true}};
        std::map<std::string, bool> off_on_to_bool = {{"off", false}, {"on", true}};
        // in rtBatch : Force un-interrupted rtBatch
        std::map<std::string, bool> forceRT_batch_mode_map = {{"off", false}, {"on", true}};
        bool              forceRT_batch_mode;
        bool              rtBatchStarted    ;
        std::map<DIRREQ_RT_TYPE, std::string> rtTypeStrMap = {
            {DIRREQ_RT_TYPE::rtbh,"rtbh"}           , {DIRREQ_RT_TYPE::rtbw,"rtbw"}           ,
            {DIRREQ_RT_TYPE::rtbs_hit,"rtbs_hit"}   , {DIRREQ_RT_TYPE::rtbs_miss,"rtbs_miss"} ,
            {DIRREQ_RT_TYPE::rtbs_conf,"rtbs_conf"} , {DIRREQ_RT_TYPE::none_dir,"none_dir"}   ,
        };
        std::map<ArrivalSTATUS, std::string> arrivalStatusMap = {
            {ArrivalSTATUS::enqueueQ        , "enqueueQ"        }   , {ArrivalSTATUS::iBufferW,"iBufferW"         }  ,
            {ArrivalSTATUS::iBatchInPending , "iBatchInPending" }   , {ArrivalSTATUS::qneueueQFull,"qneueueQFull" }  ,
            {ArrivalSTATUS::iBufferH        , "iBufferH"        }   ,
        };
        std::map<int, std::string> POLICY_CAUSE_STR = {
            {0, "NOT_CLOSED"}           , {1, "COUNT_REACHED"},
            {2, "NEXT_ADDR_CONFLICT"}   , {3, "MISSES_ABOVE_HIGH"},
            {4, "GRAY_PREV_CLOSED"}     , {5, "SWITCH_TO_READ1"},
            {6, "SWITCH_TO_READ2"}      , {7, "SWITCH_TO_READ3"},
            {8, "SWITCH_TO_WRITE1"}     , {9, "INTER_STREAM"},    
            {10, "TRUE_CLOSED"},      
        };
        // iBatching Direct Stream Demand in iBatch STatus 
        enum                 demandiBatchStatus {FUTURE,PAST,IN_BATCH} ; // NA i.e. indirect stream    , 1(Past) , 2 (Future ) , 3 (IN Range)

        CMD_TRACE print_cmd_trace = OFF;
        bool print_ipp_logic_trace        = false ;
        bool print_adaptive_logic_trace   = false ;
        bool print_iprefetcher_trace         = false  ;
        bool print_iprefetcher_trace_verbose = false; 
        bool print_arrivals          = false  , print_departures = false ; 
        int  enableHwpAwareiBatch      =  0    ; 
        bool refresh_disabled = false;

        /* Constructor */
        Controller(const Config &configs, std::string cmdTracePath , DRAM<T> *channel) : channel(channel),
                                                              scheduler(new Scheduler<T>(configs,this)),
                                                              rowpolicy(new RowPolicy<T>(configs, this)),
                                                              rowtable(new RowTable<T>(this)),
                                                              refresh(new Refresh<T>(this)),
                                                              cmd_trace_files(channel->children.size()),
                                                              cmd_tracev4_files(channel->children.size())
        {

 
            channels     = stoi(configs["channels"], NULL, 0);
            std::string cmd_trace_record_path = cmdTracePath ;
            int queueMaxSize=32;
            if(configs.contains("controller_queue_max_Size")) 
            {
                readq.max  = stoi(configs["controller_queue_max_Size"]);
                writeq.max = stoi(configs["controller_queue_max_Size"]);
                actq.max   = stoi(configs["controller_queue_max_Size"]);
                otherq.max = stoi(configs["controller_queue_max_Size"]);

            }
            // readq   = new Queue(queueMaxSize);
            // writeq  = new Queue(queueMaxSize);
            // actq    = new Queue(queueMaxSize);
            // otherq  = new Queue(queueMaxSize);
            readq.q.clear();
            writeq.q.clear();
            pendingActq.q.clear();
            actq.q.clear();
            rtBatchQ.q.clear();
            // Queue base-address scaffolding: quiet by default; only when an
            // InterStellar controller trace is explicitly requested via the .cfg.
            // (The print_* members are parsed from `configs` later in this ctor,
            //  so read the intent directly here.)
            if (configs.contains("print_arrivals") &&
                off_on_to_bool[configs["print_arrivals"]]) {
                printf("Readq      location = %p\n",&(readq.q))  ;
                printf("Writeq     location = %p\n",&(writeq.q)) ;
                printf("Actq       location = %p\n",&(actq.q))   ;
                printf("rtBatchQ   location = %p\n",&(rtBatchQ.q))   ;
            }

            for(int ba_ind = 0 ; ba_ind < MAX_BANKS ; ba_ind++)
            {
                transition_checked[ba_ind]         = false;
                read_batch_accessed_banks[ba_ind]  = false;
                write_batch_accessed_banks[ba_ind] = false;
                last_stream_id_requested[ba_ind]   = - 1;
                for(int stream_large_id=0 ; stream_large_id < TOT_STREAMS_CORES ;  stream_large_id++)
                {
                    read_batch_accessed_banks_per_stream   [stream_large_id] [ba_ind] = false;
                    write_batch_accessed_banks_per_stream  [stream_large_id] [ba_ind] = false;
                    transition_checked_per_stream          [stream_large_id] [ba_ind] = false;
                }

            }


            /****************  Direct Stream Buffer  Init  ****************/
            //Allocate Direct STream Buffer - Pass itsp ointer to teh scheduler 

            iDirStreamBuffer = new IDirStreamBuffer();
            scheduler->initIDirStreamTable(iDirStreamBuffer);

            isAdaptive   = (rowpolicy->type == RowPolicy<T>::Type::Adaptive);
			isQoS        =( scheduler->type == Scheduler<T>::Type::PARBS) || (scheduler->type == Scheduler<T>::Type::BLISS);

            if (configs.contains("refresh_disabled")) 
            {
                this->refresh_disabled =  configs.get_bool(configs["refresh_disabled"]);
            }
            else 
                this->refresh_disabled = false;
            if (configs.contains("enablePreSchedIPrefetcher"))
            {
                scheduler->enablePreSchedIPrefetcher(stoi(configs["enablePreSchedIPrefetcher"]));
                this->enableHwpAwareiBatch  = stoi(configs["enablePreSchedIPrefetcher"]); 
            }
            else 
            {
                scheduler->enablePreSchedIPrefetcher(0);
                this->enableHwpAwareiBatch   = 0;
            }
            
            enableInterStreamKnowledge   = 0 ; 
            if(configs.contains("enableInterStreamKnowledge"))
            {
                enableInterStreamKnowledge  =  stoi(configs["enableInterStreamKnowledge"]); 
            }


            coresNum = configs.get_core_num();
            last_finish_time_per_core = (long*) malloc(coresNum*sizeof(long));
            for(int core_idx = 0; core_idx < coresNum ; core_idx++)
            {
                last_finish_time_per_core[core_idx] = 0 ;
            }
            record_cmd_trace = configs.record_cmd_trace();
            if (configs.contains("print_cmd_trace"))
            {
                // can be re-written in scalable way : check configs["print_cmd_trace"] against keys of cmd_trace_map
                assert((configs["print_cmd_trace"] == "off") ||
                       (configs["print_cmd_trace"] == "ipp") ||
                       (configs["print_cmd_trace"] == "not_ipp") ||
                       (configs["print_cmd_trace"] == "all"));
                print_cmd_trace = cmd_trace_map[configs["print_cmd_trace"]];
                if (print_ipp_logic_trace)
                    printf("print_cmd_trace = %d\n", print_cmd_trace);
            }
            if (configs.contains("print_ipp_logic_trace"))
            {
                assert((configs["print_ipp_logic_trace"] == "off") ||
                       (configs["print_ipp_logic_trace"] == "on"));
                // if(print_ipp_logic_trace)
                print_ipp_logic_trace = logic_trace_map[configs["print_ipp_logic_trace"]];
            }
            if (configs.contains("print_iprefetcher_trace"))
            {
                assert((configs["print_iprefetcher_trace"] == "off") ||
                       (configs["print_iprefetcher_trace"] == "on"));
                // if(print_ipp_logic_trace)
                print_iprefetcher_trace = iprefetch_logic_trace_map[configs["print_iprefetcher_trace"]];
            }            
            if (configs.contains("print_iprefetcher_trace_verbose"))
            {
                assert((configs["print_iprefetcher_trace_verbose"] == "off") ||
                       (configs["print_iprefetcher_trace_verbose"] == "on"));
                // if(print_ipp_logic_trace)
                print_iprefetcher_trace_verbose = iprefetch_logic_trace_map[configs["print_iprefetcher_trace_verbose"]];
            }            

            
            if (configs.contains("print_adaptive_logic_trace"))
            {
                assert((configs["print_adaptive_logic_trace"] == "off") ||
                       (configs["print_adaptive_logic_trace"] == "on"));
                // if(print_ipp_logic_trace)
                print_adaptive_logic_trace = print_adaptive_logic_trace_map[configs["print_adaptive_logic_trace"]];
            }            


            /*  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
            /*  ####### Window Related Configuration ############## */

            if (configs.contains("No_Str_Win"))
                NoStream_Window_Size = stoi(configs["No_Str_Win"]);
            else
                NoStream_Window_Size = 50;

            if(configs.contains("dirStr_CntEn"))
                dirStr_CntEn = stoi(configs["dirStr_CntEn"]) ;
            else 
                dirStr_CntEn = 0                             ; 

            if (configs.contains("Dir_Str_Win"))
                Regular_Window_Size = stoi(configs["Dir_Str_Win"]);
            else
                Regular_Window_Size = 50;

            if (configs.contains("InDir_Str_Win"))
                Irregular_Window_Size = stoi(configs["InDir_Str_Win"]);
            else
                Irregular_Window_Size = 50;

            if (configs.contains("Unified_Win"))
                Unified_Window_Size = stoi(configs["Unified_Win"]);
            else
                Unified_Window_Size = 50;
 
            if (configs.contains("metaisa_dir_window_threshold"))
                metaisa_dir_window_threshold = stof(configs["metaisa_dir_window_threshold"]);
            else
                metaisa_dir_window_threshold = 0.6;

            if (configs.contains("metaisa_dir_window_threshold_Low"))
                metaisa_dir_window_threshold_Low = stof(configs["metaisa_dir_window_threshold_Low"]);
            else
                metaisa_dir_window_threshold_Low = 0.6;


            if (configs.contains("metaisa_none_window_threshold"))
                metaisa_none_window_threshold = stof(configs["metaisa_none_window_threshold"]);
            else
                metaisa_none_window_threshold = 0.5;

            if (configs.contains("metaisa_none_window_threshold_Low"))
                metaisa_none_window_threshold_Low = stof(configs["metaisa_none_window_threshold_Low"]);
            else
                metaisa_none_window_threshold_Low = 0.5;

            if (configs.contains("metaisa_indir_window_threshold"))
                metaisa_indir_window_threshold = stof(configs["metaisa_indir_window_threshold"]);
            else
                metaisa_indir_window_threshold = 0.5;
                
            if (configs.contains("metaisa_indir_window_threshold_Low"))
                metaisa_indir_window_threshold_Low = stof(configs["metaisa_indir_window_threshold_Low"]);
            else
                metaisa_indir_window_threshold_Low = 0.5;

            if (configs.contains("unified_window_threshold"))
                unified_window_threshold = stof(configs["unified_window_threshold"]);
            else
                unified_window_threshold = 0.6;

            if (configs.contains("unified_window_threshold_Low"))
                unified_window_threshold_Low = stof(configs["unified_window_threshold_Low"]);
            else           
                unified_window_threshold_Low = 0.4 ;

            if (configs.contains("enable_unified_watermark"))
                enable_unified_watermark = stoi(configs["enable_unified_watermark"]);
            else           
                enable_unified_watermark = 0 ;

            if (configs.contains("enable_ipp_dir_watermark"))
                enable_ipp_dir_watermark = stoi(configs["enable_ipp_dir_watermark"]);
            else           
                enable_ipp_dir_watermark = 0 ;

            if (configs.contains("enable_ipp_indir_watermark"))
                enable_ipp_indir_watermark = stoi(configs["enable_ipp_indir_watermark"]);
            else           
                enable_ipp_indir_watermark = 0 ;

            if (configs.contains("enable_ipp_none_watermark"))
                enable_ipp_none_watermark = stoi(configs["enable_ipp_none_watermark"]);
            else           
                enable_ipp_none_watermark = 0 ;


            for(int i =0;i<MAX_BANKS;i++)
                COTS_Last_Policy[i] = OPEN_POLICY;

            /*  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv */


            if (configs.contains("enableIPrefetcher"))
                enableIPrefetcher = stoi(configs["enableIPrefetcher"]);
            else
                enableIPrefetcher = 1;


            if (configs.contains("enableIDirStream"))
                enableIDirStream = stoi(configs["enableIDirStream"]);
            else
                enableIDirStream = 1;

            

            if (configs.contains("enableIPrefetcherWinUpdate"))
                enableIPrefetcherWinUpdate = stoi(configs["enableIPrefetcherWinUpdate"]);
            else
                enableIPrefetcherWinUpdate = 1;

            if (configs.contains("enableIPP"))
                enableIPP = stoi(configs["enableIPP"]);
            else
                enableIPP = 1;

            
            if  (configs.contains("print_arrivals"))
                print_arrivals   =  off_on_to_bool[configs["print_arrivals"]];
            else 
                print_arrivals   = false ; 
            if  (configs.contains("print_departures"))
                print_departures =  off_on_to_bool[configs["print_departures"]];
            else 
                print_departures = false ; 

            totDirReq   = totDirMisPredictions   = 0;
            totInDirReq = totInDirMisPredictions = 0;
 
            memoryMapping = MemMapping::ChRaBaRoCo;      
            if(configs.contains("address_mapping"))
            {
                assert(configs["address_mapping"]=="ChRaBaRoCo" 
                           || configs["address_mapping"]=="RoBaRaCoCh"
                           || configs["address_mapping"]=="InterStellarRT" || configs["address_mapping"]=="RoCoBaRaCh"
                           || configs["address_mapping"]=="RoBaBg1CoBg0RaCh" ||  configs["address_mapping"]=="StrmPartRoBaBg1CoBg0RaCh"  
                           || configs["address_mapping"]=="InterStreamChRoCoBaRa" ); 

                memoryMapping =  ( configs["address_mapping"]=="ChRaBaRoCo"      ? MemMapping::ChRaBaRoCo                :
                    ( (configs["address_mapping"]=="RoBaRaCoCh")                 ? MemMapping::RoBaRaCoCh                :
                    ( (configs["address_mapping"]=="InterStellarRT")             ? MemMapping::InterStellarRT            :
                    ( (configs["address_mapping"]=="InterStreamChRoCoBaRa")      ? MemMapping::InterStreamChRoCoBaRa     :
                    ( (configs["address_mapping"]=="RoBaBg1CoBg0RaCh")           ? MemMapping::RoBaBg1CoBg0RaCh          :
                    ( (configs["address_mapping"]=="StrmPartRoBaBg1CoBg0RaCh")   ? MemMapping::StrmPartRoBaBg1CoBg0RaCh  :                    
                    MemMapping::RoCoBaRaCh)))))
                  );                    
            }

            //%%%%%%%%%%%%%%%%%%%%%%%%%% InterStellarRT %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%//
            //// 1- RT Address Mapping 
            if(configs.contains("rtAddrMaping"))
            {
                assert(configs["rtAddrMaping"]=="map1" || configs["rtAddrMaping"]=="map2"
                            || configs["rtAddrMaping"]=="map4" );
                rtAddrMaping =( configs["rtAddrMaping"]=="map1"?RTAddrMaping::map1:
                ( (configs["rtAddrMaping"]=="map2")?RTAddrMaping::map2:RTAddrMaping::map4));
            } 
            bgNumPeriBatch=(rtAddrMaping==RTAddrMaping::map1)? 1:(rtAddrMaping==RTAddrMaping::map2)?2:4;           
            if(configs.contains("force_Coordinated_RTBatch_Segments"))
            {
                assert( configs["force_Coordinated_RTBatch_Segments"]=="off" ||
                        configs["force_Coordinated_RTBatch_Segments"]=="on")    ;
                force_Coordinated_rtBatch_Segments =off_on_to_bool[configs["force_Coordinated_RTBatch_Segments"]];
            } 
            

            //%%%%%%%%%%%%%%%%%%%%%%%%%% InterStellarRT %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%//
            forceRT_batch_mode = false; 
            rtBatchStarted     = false; 
            if (configs.contains("forceRT_batch_mode")) 
                this->forceRT_batch_mode =  forceRT_batch_mode_map[configs["forceRT_batch_mode"]];
            
            if (configs.contains("iBufferSize"))
               iBufferSize =  stoi(configs["iBufferSize"]) + 1;
            else
               iBufferSize =  ramulator::DEF_IPREFETCHER_BUF_SIZE   ;
            // TODO InterStellar Hack
            if(enableIPP)
            {
                actq.max   = iBufferSize*MAX_BANKS;
            }
            // Quiet by default: only print controller queue sizes when an
            // InterStellar controller trace is explicitly requested via the .cfg.
            if (configs.contains("print_arrivals") &&
                off_on_to_bool[configs["print_arrivals"]]) {
                printf("Init queues of controller %p :\n\t readq(%d/%d)\n\t writeq(%d/%d)\n\t othersq(%d/%d)\n\t actq(%d/%d)\n",this ,  readq.size(),readq.max,writeq.size(),writeq.max,otherq.size(),otherq.max,actq.size(),actq.max);
            }

            /****   Track Addresses Only  ****/
            for(int ba_id=0  ;  ba_id < MAX_BANKS ; ba_id ++ )
            {
                this->iBuffer[ba_id] = NULL;
            }
            this->iBatchHistory = new circBuffer<uint64_t,int>();
            this->iBatchHistory->setCircBufferSize(8192+1)      ;

            if(configs.contains("iBatchWidth"))
                this->iBatchWidth = stoi(configs["iBatchWidth"]);
            else
                this->iBatchWidth = ramulator::DEF_PREFTECH_STREAM_DEPTH;

            if(configs.contains("iBuffer_Repl_Policy"))
                this->iBuffer_Repl_Policy = stoi(configs["iBuffer_Repl_Policy"]);
            else
                this->iBuffer_Repl_Policy = ramulator::DEF_PREFTECH_STREAM_DEPTH;

                

            num_banks = channel->spec->org_entry.count[int(T::Level::Bank)];
            if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5" || channel->spec->standard_name == "HBM")
                    num_banks = channel->spec->org_entry.count[int(T::Level::Bank)-1]* channel->spec->org_entry.count[int(T::Level::Bank)];
                                   

            prev_addr_vec  = new std::vector<int>[num_banks];
            for(int i = 0 ; i< num_banks ; i++)
                prev_addr_vec[i].clear();
  

            /***************************************************/
            if (record_cmd_trace)
            {
                if (configs["cmd_trace_prefix"] != "")
                {
                    cmd_trace_prefix = configs["cmd_trace_prefix"];
                }
                string prefix          = cmd_trace_prefix   + "chan-" + to_string(channel->id) + "-rank-";
                string prefix_v4       = cmd_tracev4_prefix + "chan-" + to_string(channel->id) + "-rank-";
                string suffix          = ".cmdtrace";
                
                for (unsigned int i = 0; i < channel->children.size(); i++)
                {
                    string trace_file_full      =  cmdTracePath+"/"+prefix    + to_string(i) + suffix ;
                    string tracev4_file_full    =  cmdTracePath+"/"+prefix_v4 + to_string(i) + suffix ;        
                    //cmd_trace_files[i].open(trace_file_full)     ;
                    cmd_tracev4_files[i].open(tracev4_file_full) ;

                    printf("Record cmd trace for DRAMPower v5.4.1    :%s\n",trace_file_full.c_str());
                    printf("Record cmd trace for DRAMPower v3.1/v4   :%s\n",tracev4_file_full.c_str());

                }
            }
            isConfigIntelligent= (static_cast<int>(rowpolicy->type) == RowPolicy<T>::Type::IPP);
            isConfigIPP    = (static_cast<int>(rowpolicy->type) == RowPolicy<T>::Type::IPP)&& enableIPP         ;
            isConfigiBatch = (static_cast<int>(rowpolicy->type) == RowPolicy<T>::Type::IPP)&& enableIPrefetcher ;
            // regStats

            row_hits
                .name("row_hits_channel_" + to_string(channel->id))
                .desc("Number of row hits per channel ")
                .precision(0);
            row_misses
                .name("row_misses_channel_" + to_string(channel->id) )
                .desc("Number of row misses per channel ")
                .precision(0);
            row_conflicts
                .name("row_conflicts_channel_" + to_string(channel->id) )
                .desc("Number of row conflicts per channel ")
                .precision(0);

            read_row_hits
                .init(configs.get_core_num())
                .name("read_row_hits_channel_" + to_string(channel->id) + "_core")
                .desc("Number of row hits for read requests per channel per core")
                .precision(0);
            read_row_misses
                .init(configs.get_core_num())
                .name("read_row_misses_channel_" + to_string(channel->id) + "_core")
                .desc("Number of row misses for read requests per channel per core")
                .precision(0);
            read_row_conflicts
                .init(configs.get_core_num())
                .name("read_row_conflicts_channel_" + to_string(channel->id) + "_core")
                .desc("Number of row conflicts for read requests per channel per core")
                .precision(0);

            write_row_hits
                .init(configs.get_core_num())
                .name("write_row_hits_channel_" + to_string(channel->id) + "_core")
                .desc("Number of row hits for write requests per channel per core")
                .precision(0);
            write_row_misses
                .init(configs.get_core_num())
                .name("write_row_misses_channel_" + to_string(channel->id) + "_core")
                .desc("Number of row misses for write requests per channel per core")
                .precision(0);
            write_row_conflicts
                .init(configs.get_core_num())
                .name("write_row_conflicts_channel_" + to_string(channel->id) + "_core")
                .desc("Number of row conflicts for write requests per channel per core")
                .precision(0);

            useless_activates
                .name("useless_activates_" + to_string(channel->id) + "_core")
                .desc("Number of useless activations. E.g, ACT -> PRE w/o RD or WR")
                .precision(0);

            read_transaction_bytes
                .name("read_transaction_bytes_" + to_string(channel->id))
                .desc("The total byte of read transaction per channel")
                .precision(0);
            write_transaction_bytes
                .name("write_transaction_bytes_" + to_string(channel->id))
                .desc("The total byte of write transaction per channel")
                .precision(0);

            read_latency_sum
                .name("read_latency_sum_" + to_string(channel->id))
                .desc("The memory latency cycles (in memory time domain) sum for all read requests in this channel")
                .precision(0);
            read_latency_avg
                .name("read_latency_avg_" + to_string(channel->id))
                .desc("The average memory latency cycles (in memory time domain) per request for all read requests in this channel")
                .precision(6); 

            req_queue_length_sum
                .name("req_queue_length_sum_" + to_string(channel->id))
                .desc("Sum of read and write queue length per memory cycle per channel.")
                .precision(0);
            req_queue_length_avg
                .name("req_queue_length_avg_" + to_string(channel->id))
                .desc("Average of read and write queue length per memory cycle per channel.")
                .precision(6);

            read_req_queue_length_sum
                .name("read_req_queue_length_sum_" + to_string(channel->id))
                .desc("Read queue length sum per memory cycle per channel.")
                .precision(0);
            read_req_queue_length_avg
                .name("read_req_queue_length_avg_" + to_string(channel->id))
                .desc("Read queue length average per memory cycle per channel.")
                .precision(6);

            write_req_queue_length_sum
                .name("write_req_queue_length_sum_" + to_string(channel->id))
                .desc("Write queue length sum per memory cycle per channel.")
                .precision(0);
            write_req_queue_length_avg
                .name("write_req_queue_length_avg_" + to_string(channel->id))
                .desc("Write queue length average per memory cycle per channel.")
                .precision(6);

            write_req_queue_full_times
                .name("write_req_queue_full_times_" + to_string(channel->id))
                .desc("Number of times where write queue gets full.")
                .precision(0);

            actq_req_queue_full_times
                .name("actq_req_queue_full_times_" + to_string(channel->id))
                .desc("Number of times where act queue gets full.")
                .precision(0);                

            others_req_queue_full_times
                .name("others_req_queue_full_times_" + to_string(channel->id))
                .desc("Number of times where others queue gets full.")
                .precision(0);      

            read_req_queue_full_times
                .name("read_req_queue_full_times_" + to_string(channel->id))
                .desc("Number of times where read queue gets full.")
                .precision(0);      

            totalDirStreamRequests
                .name("Total_Direct_Stream_Requests_" + to_string(channel->id))
                .desc("Total number of direct stream requests.")
                .precision(0);

            totalInDirStreamRequests
                .name("Total_Indirect_Stream_Requests_" + to_string(channel->id))
                .desc("Total number of Indirect stream requests.")
                .precision(0);

            totalDirStreamMisPredictions
                .name("Total_Direct_Stream_policy_mispredictions_" + to_string(channel->id))
                .desc("Total number of direct stream policy mispredictions.")
                .precision(0);


            totalInDirStreamMisPredictions
                .name("Total_Indirect_Stream_policy_mispredictions_" + to_string(channel->id))
                .desc("Total number of Indirect stream policy mispredictions.")
                .precision(0);

            ///*******************************************************************************//
            ///////////////////////// InterStellarRT   RealTime Stats  /////////////////////////
            read_latency_sum_per_core  
                .init(configs.get_core_num())
                .name("read_latency_sum_per_core"+to_string(channel->id))
                .desc("The memory latency cycles (in memory time domain) per core")
                .precision(0)
                ;
            read_latency_max_per_core   
                .init(configs.get_core_num())
                .name("read_latency_max_per_core"+to_string(channel->id))
                .desc("The worst memory latency cycles (in memory time domain) per request per core")
                .precision(0)
                ;
            read_latency_avg_per_core   
                .init(configs.get_core_num())
                .name("read_latency_avg_per_core"+to_string(channel->id))
                .desc("The average memory latency cycles (in memory time domain) per request per core")
                .precision(0)
                ;             

            totRReq__rtbh   
                .init(configs.get_core_num())
                .name("totRReq__rtbh"+to_string(channel->id))
                .desc("Total read direct stream requests that hit in rtBatch and hit in rtBuffer per core")
                .precision(0)
                ;         
            totRReq__rtbw   
                .init(configs.get_core_num())
                .name("totRReq__rtbw"+to_string(channel->id))
                .desc("Total read direct stream requests that hit in rtBatch but waits for data from rtBuffer per core")
                .precision(0)
                ; 
            totRReq__rtbs   
                .init(configs.get_core_num())
                .name("totRReq__rtbs"+to_string(channel->id))
                .desc("Total read direct stream requests that start an rtBatch per core")
                .precision(0)
                ; 
            totRReq__rtbs_conf 
                .init(configs.get_core_num())
                .name("totRReq__rtbs_conf"+to_string(channel->id))
                .desc("Total read direct stream requests that start an rtBatch per core and conflict")
                .precision(0)
                ;             
            totRReq__rtbs_miss 
                .init(configs.get_core_num())
                .name("totRReq__rtbs_miss"+to_string(channel->id))
                .desc("Total read direct stream requests that start an rtBatch per core and miss")
                .precision(0)
                ;                  
            totRReq__rtbs_hit 
                .init(configs.get_core_num())
                .name("totRReq__rtbs_hit"+to_string(channel->id))
                .desc("Total read direct stream requests that start an rtBatch per core and hit")
                .precision(0)
                ;                  
            totRReq__dir   
                .init(configs.get_core_num())
                .name("totRReq__dir"+to_string(channel->id))
                .desc("Total read direct stream requests per core")
                .precision(0)
                ;                      
            totRReq__none   
                .init(configs.get_core_num())
                .name("totRReq__none"+to_string(channel->id))
                .desc("Total read not-direct stream requests per core")
                .precision(0)
                ;   
                //#############   WCL ############
            PerRequest_WCL                     
                .init(configs.get_core_num())
                .name("PerRequest_WCL"+to_string(channel->id))
                .desc("WCL per request for all requests per core")
                .precision(0)
                ;   
            PerRequest_WCL_dir
                .init(configs.get_core_num())
                .name("PerRequest_WCL_dir"+to_string(channel->id))
                .desc("WCL per request for direct stream request type per core")
                .precision(0)
                ;   
            PerRequest_WCL_None                      
                .init(configs.get_core_num())
                .name("PerRequest_WCL_None"+to_string(channel->id))
                .desc("WCL per request for not-direct stream requests per core")
                .precision(0)
                ;   
            PerRequest_WCL_dir_rtbh            
                .init(configs.get_core_num())
                .name("PerRequest_WCL_dir_rtbh"+to_string(channel->id))
                .desc("WCL per request for direct stream requests that hit in rtBatch and rtBuffer per core")
                .precision(0)
                ;   
            PerRequest_WCL_dir_rtbw            
                .init(configs.get_core_num())
                .name("PerRequest_WCL_dir_rtbw"+to_string(channel->id))
                .desc("WCL per request for direct stream requests that hit in rtBatch and wait for data per core")
                .precision(0)
                ;   
            PerRequest_WCL_dir_rtbs
                .init(configs.get_core_num())
                .name("PerRequest_WCL_dir_rtbs"+to_string(channel->id))
                .desc("WCL per request for direct stream requests that starts the rtBatch per core")
                .precision(0)
                ;   
            PerRequest_WCL_dir_rtbs_hit        
                .init(configs.get_core_num())
                .name("PerRequest_WCL_dir_rtbs_hit"+to_string(channel->id))
                .desc("WCL per request for direct stream requests that starts the rtBatch and hit per core")
                .precision(0)
                ;   
            PerRequest_WCL_dir_rtbs_miss        
                .init(configs.get_core_num())
                .name("PerRequest_WCL_dir_rtbs_miss"+to_string(channel->id))
                .desc("WCL per request for direct stream requests that starts the rtBatch and miss per core")
                .precision(0)
                ;   
            PerRequest_WCL_dir_rtbs_conf         
                .init(configs.get_core_num())
                .name("PerRequest_WCL_dir_rtbs_conf"+to_string(channel->id))
                .desc("WCL per request for direct stream requests that starts the rtBatch and conflict per core")
                .precision(0)
                ;
                //#############   Avergae latency  ############
            PerRequest_AvgLat                     
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat"+to_string(channel->id))
                .desc("Average latency per request for all requests per core")
                .precision(0)
                ;   
            PerRequest_AvgLat_dir
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat_dir"+to_string(channel->id))
                .desc("Average latency per request for direct stream request type per core")
                .precision(0)
                ;   
            PerRequest_AvgLat_None                      
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat_None"+to_string(channel->id))
                .desc("Average latency per request for not-direct stream requests per core")
                .precision(0)
                ;   
            PerRequest_AvgLat_dir_rtbh            
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat_dir_rtbh"+to_string(channel->id))
                .desc("Average latency per request for direct stream requests that hit in rtBatch and rtBuffer per core")
                .precision(0)
                ;   
            PerRequest_AvgLat_dir_rtbw            
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat_dir_rtbw"+to_string(channel->id))
                .desc("Average latency per request for direct stream requests that hit in rtBatch and wait for data per core")
                .precision(0)
                ;   
            PerRequest_AvgLat_dir_rtbs
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat_dir_rtbs"+to_string(channel->id))
                .desc("Average latency per request for direct stream requests that starts the rtBatch per core")
                .precision(0)
                ;   
            PerRequest_AvgLat_dir_rtbs_hit        
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat_dir_rtbs_hit"+to_string(channel->id))
                .desc("Average latency per request for direct stream requests that starts the rtBatch and hit per core")
                .precision(0)
                ;   
            PerRequest_AvgLat_dir_rtbs_miss        
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat_dir_rtbs_miss"+to_string(channel->id))
                .desc("Average latency per request for direct stream requests that starts the rtBatch and miss per core")
                .precision(0)
                ;   
            PerRequest_AvgLat_dir_rtbs_conf         
                .init(configs.get_core_num())
                .name("PerRequest_AvgLat_dir_rtbs_conf"+to_string(channel->id))
                .desc("Average latency per request for direct stream requests that starts the rtBatch and conflict per core")
                .precision(0)
                ;   
                //#############   Sum latency  ############
            AllRequests_SumLat                     
                .init(configs.get_core_num())
                .name("AllRequests_SumLat"+to_string(channel->id))
                .desc("Sum latency for all requests for all requests per core")
                .precision(0)
                ;   
            AllRequests_SumLat_dir
                .init(configs.get_core_num())
                .name("AllRequests_SumLat_dir"+to_string(channel->id))
                .desc("Sum latency for all requests for direct stream request type per core")
                .precision(0)
                ;   
            AllRequests_SumLat_None                      
                .init(configs.get_core_num())
                .name("AllRequests_SumLat_None"+to_string(channel->id))
                .desc("Sum latency for all requests for not-direct stream requests per core")
                .precision(0)
                ;   
            AllRequests_SumLat_dir_rtbh            
                .init(configs.get_core_num())
                .name("AllRequests_SumLat_dir_rtbh"+to_string(channel->id))
                .desc("Sum latency for all requests for direct stream requests that hit in rtBatch and rtBuffer per core")
                .precision(0)
                ;   
            AllRequests_SumLat_dir_rtbw            
                .init(configs.get_core_num())
                .name("AllRequests_SumLat_dir_rtbw"+to_string(channel->id))
                .desc("Sum latency for all requests for direct stream requests that hit in rtBatch and wait for data per core")
                .precision(0)
                ;   
            AllRequests_SumLat_dir_rtbs
                .init(configs.get_core_num())
                .name("AllRequests_SumLat_dir_rtbs"+to_string(channel->id))
                .desc("Sum latency for all requests for direct stream requests that starts the rtBatch per core")
                .precision(0)
                ;   
            AllRequests_SumLat_dir_rtbs_hit        
                .init(configs.get_core_num())
                .name("AllRequests_SumLat_dir_rtbs_hit"+to_string(channel->id))
                .desc("Sum latency for all requests for direct stream requests that starts the rtBatch and hit per core")
                .precision(0)
                ;   
            AllRequests_SumLat_dir_rtbs_miss        
                .init(configs.get_core_num())
                .name("AllRequests_SumLat_dir_rtbs_miss"+to_string(channel->id))
                .desc("Sum latency for all requests for direct stream requests that starts the rtBatch and miss per core")
                .precision(0)
                ;   
            AllRequests_SumLat_dir_rtbs_conf         
                .init(configs.get_core_num())
                .name("AllRequests_SumLat_dir_rtbs_conf"+to_string(channel->id))
                .desc("Sum latency for all requests for direct stream requests that starts the rtBatch and conflict per core")
                .precision(0)
                ;   

            ResponseTimeSum         
                .init(configs.get_core_num())
                .name("ResponseTimeSum"+to_string(channel->id))
                .desc("Response Time Sum per core")
                .precision(0)
                ;   
 
            ///////////////////////// InterStellarRT   RealTime Stats  /////////////////////////
            ///*******************************************************************************//











            //Abotaleb  - [iBatch Stats Total for all streams]
            Tot_Req_Hit_In_iBatch               
                .name("Total_Requests_Hit_in_iBatch_" + to_string(channel->id))
                .desc("Total Requests Hit in iBatch.")
                .precision(0);

            Tot_Req_wait_iBatch                
                .name("Total_Requests_Wait_for_iBatch_" + to_string(channel->id))
                .desc("Total Requests Wait for iBatch.")
                .precision(0);

            Tot_Req_From_Readq_to_iBatch      
                .name("Total_Requests_enqueued_in_readq_then_iBatched" + to_string(channel->id))
                .desc("Total Requests enqueued in readq then iBatched.")
                .precision(0);

            Tot_Req_deferred_In_Future_iBatch  
                .name("Total_Requests_scheduled_but_deferred_because_they_are_in_future_iBatch_" + to_string(channel->id))
                .desc("Total Requests scheduled but deferred because they are in future iBatch.")
                .precision(0);

            Tot_Req_iBatched
                .name("Total_Requests_iBatched_" + to_string(channel->id))
                .desc("Total Requests iBatched.")
                .precision(0);

            Tot_Req_issue_iBatch
                .name("Total_Requests_issue_iBatches_" + to_string(channel->id))
                .desc("Total Requests issue iBatch.")
                .precision(0);
            //
            read_to_write_transitions          
                .name("Total_read_batch_to_write_batch_transitions_" + to_string(channel->id))
                .desc("Total read to write transitions.")
                .precision(0);

            write_to_read_transitions           
                .name("Total_write_batch_to_read_batch_transitions_" + to_string(channel->id))
                .desc("Total write to read transitions.")
                .precision(0);

            read_to_write_transitions_on_bank_level          
                .name("Total_read_to_write_transitions_measured_on_bank_level_" + to_string(channel->id))
                .desc("Total read to write transitions.")
                .precision(0);

            write_to_read_transitions_on_bank_level           
                .name("Total_write_to_read_transitions_measured_on_bank_level_" + to_string(channel->id))
                .desc("Total write to read transitions.")
                .precision(0);


            
            //Stream_Transition_Read_State0
            read_stream_tot_transitions_on_bank_level          
                .name("Total_read_stream_tot_transitions_on_bank_level_" + to_string(channel->id))
                .desc("Total read stream transitions on bank level.")
                .precision(0);            
            //Stream_Transition_Read_State1
            read_to_read_stream_transitions_on_bank_level          
                .name("Total_read_to_read_stream_transitions_on_bank_level_" + to_string(channel->id))
                .desc("Total read to read transitions with the different streams in both on bank level.")
                .precision(0);
            //Stream_Transition_Read_State2
            write_to_read_diff_stream_transitions_on_bank_level          
                .name("Total_write_to_read_diff_stream_transitions_on_bank_level_" + to_string(channel->id))
                .desc("Total write to read transitions with the different streams in both on bank level.")
                .precision(0);
            //Stream_Transition_Read_State3
            write_to_read_same_stream_transitions_on_bank_level          
                .name("Total_write_to_read_same_stream_transitions_on_bank_level_" + to_string(channel->id))
                .desc("Total write to read transitions with the same stream in both on bank level.")
                .precision(0);


            //Stream_Transition_Write_State1
            write_stream_tot_transitions_on_bank_level          
                .name("Total_write_stream_tot_transitions_on_bank_level_" + to_string(channel->id))
                .desc("Total write stream transitions on bank level.")
                .precision(0);             
            //Stream_Transition_Write_State1
            write_to_write_stream_transitions_on_bank_level          
                .name("Total_write_to_write_stream_transitions_on_bank_level_" + to_string(channel->id))
                .desc("Total write to write transitions with the different streams in both.")
                .precision(0);
            //Stream_Transition_Write_State2
            read_to_write_diff_stream_transitions_on_bank_level          
                .name("Total_read_to_write_diff_stream_transitions_on_bank_level_" + to_string(channel->id))
                .desc("Total read to write transitions with the different streams in both.")
                .precision(0);


            /************** Abotaleb:  PRE   and ACT Reasons    ****************************/    
            //Stream_Transition_Write_State3
            read_conflicts_by_an_interrupting_write_to_hit_batch
                .name("Total_read_conflicts_by_an_interrupting_write_to_hit_batch_" + to_string(channel->id))
                .desc("Total read conflicts by an interrupting write to hit batch.")
                .precision(0);
            read_conflicts_by_inter_stream
                .name("Total_read_conflicts_by_inter_stream_" + to_string(channel->id))
                .desc("Total read_conflicts by inter stream interference (Same Bank But different stream ID and/or requestor ID).")
                .precision(0);
            read_conflicts_intra_stream
                .name("Total_read_conflicts_intra_stream_" + to_string(channel->id))
                .desc("Total read_conflicts by intra stream interference (Same Bank, same stream ID and requestor ID).")
                .precision(0);
            read_conflict_precedded_by_no_read_conflict                
                .name("Total_read_conflict_precedded_by_no_read_conflict_" + to_string(channel->id))
                .desc("Total read_conflicts while previous access was conflict without a read done!.")
                .precision(0);
            read_conflict_preceeded_by_non_useful_conflict_or_miss                
                .name("Total_read_conflict_preceeded_by_non_useful_conflict_or_miss_" + to_string(channel->id))
                .desc("Total read_conflicts while same row has previous non useful PRE or ACT!.")
                .precision(0);
            read_conflicts_by_others
                .name("Total_read_conflicts_by_others_" + to_string(channel->id))
                .desc("Total read_conflicts by other reasons.")
                .precision(0);

            write_misses_unnecassary_by_useless_wra
                .name("Total_write_misses_by_useless_WRAs_" + to_string(channel->id))
                .desc("Total write missses that caused by useless WRAs.")
                .precision(0);
            write_misses_preceeded_by_non_useful_conflict_or_miss
                .name("Total_write_misses_preceeded_by_non_useful_conflict_or_miss_" + to_string(channel->id))
                .desc("Total write  while same row has previous non useful PRE or ACT!.")
                .precision(0);                
            write_misses_by_useful_wra 
                .name("Total_write_misses_by_useful_WRAs_" + to_string(channel->id))
                .desc("Total write missses that caused by useful WRAs.")
                .precision(0);
            write_misses_by_others
                .name("Total_write_misses_by_others_" + to_string(channel->id))
                .desc("Total write missses by other reasons.")
                .precision(0);
            read_misses_unnecassary_by_useless_rda
                .name("Total_read_misses_by_useless_RDAs_" + to_string(channel->id))
                .desc("Total read missses that caused by useless RDAs.")
                .precision(0);
            read_misses_by_useful_rda
                .name("Total_read_misses_by_useful_RDAs_" + to_string(channel->id))
                .desc("Total read missses that caused by useful RDAs.")
                .precision(0);
            read_misses_preceeded_by_non_useful_conflict_or_miss            
                .name("Total_read_misses_preceeded_by_non_useful_conflict_or_miss_" + to_string(channel->id))
                .desc("Total read missses while same row has previous non useful PRE or ACT!.")
                .precision(0);            
            read_misses_by_others
                .name("Total_read_misses_by_others_" + to_string(channel->id))
                .desc("Total read missses by other reasons.")
                .precision(0);

            /**************************************************************************/

            read_to_write_same_stream_transitions_on_bank_level          
                .name("Total_read_to_write_same_stream_transitions_on_bank_level_" + to_string(channel->id))
                .desc("Total read to write transitions with the same stream in both.")
                .precision(0);
            
            read_conflicts_by_an_interrupting_other_stream_per_stream
                    .init(TOT_STREAMS)
                    .name("read_conflicts_by_an_interrupting_other_stream_per_stream_channel" + to_string(channel->id) )
                    .desc("Total read conflicts by an interrupting write to hit batch per stream")
                    .precision(0);


            rda_count_reached       
                .name("Total_rda_count_reached_" + to_string(channel->id))
                .desc("Total RDAs because direct stream reached hit counts on a row buffer.")
                .precision(0);

            rda_nxt_addr_conflict   
                .name("Total_rda_nxt_addr_conflict_" + to_string(channel->id))
                .desc("Total RDAs because direct stream has next expected address outside row buffer.")
                .precision(0);

            rda_misses_above_high   
                .name("Total_rda_misses_above_high_" + to_string(channel->id))
                .desc("Total RDAs because misses counter above high threshold.")
                .precision(0);

            rda_gray_prev_closed    
                .name("Total_rda_gray_prev_closed_" + to_string(channel->id))
                .desc("Total RDAs because misses counter in gray area but previous cmd was close.")
                .precision(0);       

            rda_inter_stream        
                .name("Total_rda_inter_stream_" + to_string(channel->id))
                .desc("Total RDAs in iBatch and IPP because this is last iBatched and no same stream iBatch request exist now in iBatch buffer.")
                .precision(0);


            //Abotaleb : Stream CMD Stats 
            pre_cnt_per_Stream
                    .init(TOT_STREAMS_CORES)
                    .name("PRE_Count_per_stream_per_core_per_processor_channel_" + to_string(channel->id) )
                    .desc("Number of PRE commands per channel per stream")
                    .precision(0);
            act_cnt_per_Stream
                    .init(TOT_STREAMS_CORES)
                    .name("ACT_Count_per_stream_per_core_per_processor_channel_" + to_string(channel->id) )
                    .desc("Number of ACT commands per channel per stream")
                    .precision(0);
            rd_cnt_per_Stream
                    .init(TOT_STREAMS_CORES)
                    .name("RD_Count_per_stream_per_core_per_processor_channel_" + to_string(channel->id) )
                    .desc("Number of RD commands per channel per stream")
                    .precision(0);
            wr_cnt_per_Stream
                    .init(TOT_STREAMS_CORES)
                    .name("WR_Count_per_stream_per_core_per_processor_channel_" + to_string(channel->id) )
                    .desc("Number of WR commands per channel per stream")
                    .precision(0);
            rda_cnt_per_Stream
                    .init(TOT_STREAMS_CORES)
                    .name("RDA_Count_per_stream_per_core_per_processor_channel_" + to_string(channel->id) )
                    .desc("Number of RDA commands per channel per stream")
                    .precision(0);
            wra_cnt_per_Stream
                    .init(TOT_STREAMS_CORES)
                    .name("WRA_Count_per_stream_per_core_per_processor_channel_" + to_string(channel->id) )
                    .desc("Number of WRA commands per channel per stream")
                    .precision(0);
            
            iBatches_cnt_per_Stream
                    .init(TOT_STREAMS_CORES)
                    .name("iBatches_Issued_Count_per_stream_per_core_channel_" + to_string(channel->id) )
                    .desc("Number of intelligent batch commands issued per channel per stream")
                    .precision(0);
            iBatches_completed_cnt_per_stream
                    .init(TOT_STREAMS_CORES)
                    .name("iBatches_Completed_Count_per_stream_per_core_channel_" + to_string(channel->id) )
                    .desc("Number of intelligent batch commands completed per channel per stream")
                    .precision(0);

            iBatches_cnt_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("iBatches_Issued_Count_per_bank_" + to_string(channel->id) )
                    .desc("Number of intelligent batch commands issued per channel per bank")
                    .precision(0);

            iprefetches_completed_cnt_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("iBatches_Completed_Count_per_bank_" + to_string(channel->id) )
                    .desc("Number of intelligent batch commands completed per channel per bank")
                    .precision(0);

            iprefetches_waiting_cycles_per_Stream
                    .init(TOT_STREAMS)
                    .name("iBatches_Waiting_Time_per_stream__channel_" + to_string(channel->id) )
                    .desc("Number of intelligent batch waiting cycles from accepted as iBatched but waiting data till being serviced per stream")
                    .precision(0);

            normal_waiting_cycles_per_Stream
                    .init(TOT_STREAMS)
                    .name("Normal_Waiting_Time_per_stream__channel_" + to_string(channel->id) )
                    .desc("Number of cycles from request enetered queue till being serviced per stream")
                    .precision(0);
            /*********               ********/
            misses_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Misses_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Misses per bank per channel")
                    .precision(0);
            rd_misses_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Read_misses_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Read Misses per bank per channel")
                    .precision(0);
            wr_misses_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Write_misses_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Write Misses per bank per channel")
                    .precision(0);


            conflicts_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Conflicts_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Conflicts per bank per channel")
                    .precision(0);
            rd_conflicts_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Read_conflicts_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Read Conflicts per bank per channel")
                    .precision(0);
            wr_conflicts_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Write_conflicts_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Write Conflicts per bank per channel")
                    .precision(0);


             hits_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Hits_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Hits per bank per channel")
                    .precision(0);                   
            rd_hits_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Read_hits_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Read hits per bank per channel")
                    .precision(0);                   
            wr_hits_per_bank
                    .init(MAX_BANKS_DDR4)
                    .name("Write_hits_per_bank_per_channel_" + to_string(channel->id) )
                    .desc("Write hits per bank per channel")
                    .precision(0);                   




#ifndef INTEGRATED_WITH_GEM5
            record_read_hits
                .init(configs.get_core_num())
                .name("record_read_hits")
                .desc("record read hit count for this core when it reaches request limit or to the end");

            record_read_misses
                .init(configs.get_core_num())
                .name("record_read_misses")
                .desc("record_read_miss count for this core when it reaches request limit or to the end");

            record_read_conflicts
                .init(configs.get_core_num())
                .name("record_read_conflicts")
                .desc("record read conflict count for this core when it reaches request limit or to the end");

            record_write_hits
                .init(configs.get_core_num())
                .name("record_write_hits")
                .desc("record write hit count for this core when it reaches request limit or to the end");

            record_write_misses
                .init(configs.get_core_num())
                .name("record_write_misses")
                .desc("record write miss count for this core when it reaches request limit or to the end");

            record_write_conflicts
                .init(configs.get_core_num())
                .name("record_write_conflicts")
                .desc("record write conflict for this core when it reaches request limit or to the end");
#endif
        }

        ~Controller()
        {
            delete scheduler;
            delete rowpolicy;
            delete rowtable;
            delete channel;
            delete refresh;
            // for (auto &file : cmd_trace_files)
            //     file.close();
            // cmd_trace_files.clear();
            for (auto &filev4 : cmd_tracev4_files)
                filev4.close();
            cmd_tracev4_files.clear();            
        }

        void finish(long read_req, long * num_read_requests_per_cor_arr,int coresNum , long dram_cycles)
        {
            read_latency_avg                        = read_latency_sum.value() / read_req   ;
            for(int i = 0 ; i <coresNum ; i++)
            {
                read_latency_avg_per_core[i]   = read_latency_sum_per_core[i].value() / num_read_requests_per_cor_arr[i] ;
            
                PerRequest_AvgLat    [i]       = AllRequests_SumLat[i] .value()         /  read_req              ;
                PerRequest_AvgLat_dir[i]       = AllRequests_SumLat_dir[i] .value()     / totRReq__dir[i].value()   ;
                PerRequest_AvgLat_None[i]      = AllRequests_SumLat_None[i] .value()    / totRReq__none[i].value()  ;
                PerRequest_AvgLat_dir_rtbh[i]  = AllRequests_SumLat_dir_rtbh[i] .value()/ totRReq__rtbh[i].value()  ;
                PerRequest_AvgLat_dir_rtbw[i]  = AllRequests_SumLat_dir_rtbw[i] .value()/ totRReq__rtbw[i].value()  ;
                PerRequest_AvgLat_dir_rtbs[i]  = AllRequests_SumLat_dir_rtbs[i] .value()/ totRReq__rtbs[i].value()  ;

                PerRequest_AvgLat_dir_rtbs_hit[i]  = AllRequests_SumLat_dir_rtbs_hit[i] .value()/ totRReq__rtbs_hit[i].value()  ;
                PerRequest_AvgLat_dir_rtbs_conf[i] = AllRequests_SumLat_dir_rtbs_conf[i] .value()/ totRReq__rtbs_conf[i].value()  ;
                PerRequest_AvgLat_dir_rtbs_miss[i] = AllRequests_SumLat_dir_rtbs_miss[i] .value()/ totRReq__rtbs_miss[i].value()  ;
            }






            
            req_queue_length_avg = req_queue_length_sum.value() / dram_cycles;
            read_req_queue_length_avg = read_req_queue_length_sum.value() / dram_cycles;
            write_req_queue_length_avg = write_req_queue_length_sum.value() / dram_cycles;
            // call finish function of each channel
            channel->finish(dram_cycles);
        }

        /* Member Functions */
        Queue &get_queue(Request::Type type)
        {
            switch (int(type))
            {
            case int(Request::Type::READ):
                return readq;
            case int(Request::Type::WRITE):
                return writeq;
            default:
                return otherq;
            }
        }


        void invalidateStream(Request &req,uint64_t  metaisa_winkey,int win_index)
        {
            // Add early return if IPP not configured to prevent accessing uninitialized metaisa_window_counter
            if(!isConfigIPP)
            {
                return;
            }

            // if an entry for this stream exist in Misses Tracking WIndow - Remove it
            if (metaisa_window_counter[win_index].find(metaisa_winkey) != metaisa_window_counter[win_index].end())
            {
                metaisa_window_counter[win_index].erase(metaisa_winkey);
            }
            // For direct stream , if entry exist in DIrSTreamTable , remove it
            uint64_t iDirStreamTableKey;
            iDirStreamTableKey = (( (uint64_t)req.metaISAStreamID << METAISA_REQID_BITS)     +
                                    req.metaISARequestorID);
            if(iDirStreamBuffer!=NULL)
                if(iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey)!=iDirStreamBuffer->IDirStreamTable.end())
                {
                    iDirStreamBuffer->IDirStreamTable.erase(iDirStreamTableKey);
                }
            printf("Invalidate Stream P:%d S:%d\n",req.coreid,req.metaISAStreamID);
        }

        void deactivateAllStreams(uint32_t requestorID)
        {
            uint64_t iDirStreamTableKey;
            if(iDirStreamBuffer!=NULL)
                for(int i = 0 ; i <TOT_STREAMS-1 ; i++)
                {
                    iDirStreamTableKey = (( (uint64_t)i << METAISA_REQID_BITS)     +
                                        requestorID); 
                    if( iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey)!=iDirStreamBuffer->IDirStreamTable.end())
                        iDirStreamBuffer->IDirStreamTable.erase(iDirStreamTableKey);
                }

            //Clear Misses Tracking Tables for both direct and indrreect stream , for all banks 
            // Iterate over both stream Types , over all banks , over all streams 

            for(int i = 0 ; i <2 ; i++)
                for(int j=0 ; j<MAX_BANKS; j++)
                    for(int k=0; k <TOT_STREAMS-1;k++)
                      {
                        uint64_t  metaisa_winkey = ( 
                          ( (uint64_t)j              << METAISA_REQID_STRID_BITS)+
                          ( (uint64_t)k << METAISA_REQID_BITS) 
                          + requestorID);
                         if(metaisa_window_counter[i].find(metaisa_winkey)!=metaisa_window_counter[i].end())
                         {
                             this->metaisa_window_counter[i].erase(metaisa_winkey);
                         }
                      }
        }

        bool enqueue(Request &req)
        {
            
            Queue &queue = get_queue(req.type);
            arrivalStatus = ArrivalSTATUS::enqueueQ;
            int bank_id = req.addr_vec[int(T::Level::Bank)];
            if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                bank_id += req.addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
            ramulator::iBufferKeyClass iBufferKeyObj(req.addr, req.metaISAStreamID, req.metaISARequestorID);
            
            if(!(isConfigIPP||enableIPrefetcher) &&req.deactive_stream)
            {
                //Invalidate Stream Recivied But Not in IPP Mode + iBatch
                return true;
            }
            
            if(isConfigIPP || enableIPrefetcher)
            {
       
                uint64_t  metaisa_winkey = ( 
                          ( (uint64_t)bank_id              << METAISA_REQID_STRID_BITS)+
                          ( (uint64_t)req.metaISAStreamID << METAISA_REQID_BITS) 
                          + req.metaISARequestorID);
                int win_index = WINDOW_TYPE::REG_WINDOW ;
                if (req.metaISAStreamType==INDIR_STREAM || req.metaISAStreamType == PTR_CHASE)
                    win_index = WINDOW_TYPE::IRREG_WINDOW;
                                    
                /************* Check if special deactivate request  from MetaISA *********/
                if(req.deactive_stream)
                {
                    // Only call invalidateStream if IPP is configured to prevent accessing uninitialized maps
                    if(isConfigIPP)
                    {
                        invalidateStream(req,metaisa_winkey,win_index);
                    }
                    //deactivateAllStreams(req.metaISARequestorID);
                    return true;
                }
                // Updating Sliding Window is done for IPP+iBatch only s
                if( enableIPrefetcher && req.type==Request::Type::READ)
                {
                    /** ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ **/
                    /**************  Check if  iBatched Request **************/
                    // If iBatched then respond
                    bool inIPDQ               = false; 
                    bool waitingDataInRTBatch = true ;//This condition is needed for requests that hit in rtBatch but are not start of rtBatch to avoid designating them as rtbw wrongly. 
                    if(iBuffer[bank_id]!=NULL)
                        inIPDQ = (this-> iBuffer[bank_id]->find(iBufferKeyObj)  );
                    if (inIPDQ)
                    {      

                        /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
                        /****************  Special  Case   [This is a base address of another stream request]   *************/
                        // as "find" uses the address to do the comparison , 
                        // so if the address is feteched by other stream it will hit in iBatch
                        if(req.is_base_addr)
                        {
                            uint64_t ownerDirStreamTableKey;
                            ownerDirStreamTableKey = (( (uint64_t)req.metaISAStreamID << METAISA_REQID_BITS)     +
                                    req.metaISARequestorID);
                            ramulator::iBufferKeyClass  otherStreamKey = 
                                              (this-> iBuffer[bank_id]->getDirStreamTableID(iBufferKeyObj));
                            uint64_t   otherStreamTableKey = otherStreamKey.getUniqueStreamID();
                            // The other stream feteched requests beoynd its bound so safely the last iBatched address will be the last in iBatch contains current request
                            long lastiBatchedAddr = iDirStreamBuffer->IDirStreamTable[otherStreamTableKey   ].previBatchLastAddr;
                            iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].previBatchLastAddr = lastiBatchedAddr;
                            iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].active                 = true;
                            iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].stride                 = req.stride;
                            printf(" [Req.Is_base_addr] \n");
                            printf(" ( A:%#16lx ) ( T:%10ld ) (V:",  req.addr, clk);
                            for (int lev = 0; lev < int(T::Level::MAX); lev++)
                            {
                                if(lev!=0) 
                                    printf(",");
                                printf(" %5x ", req.addr_vec[lev]);
                            }

                            /***** Inter-Stream Improvement */
                            if(enableInterStreamKnowledge==1)
                            {
                                uint8_t  nextID = 1;
                                if(req.metaISAStreamID==1)
                                    nextID = 2;

                                iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].nextMetaISAStreamID   = nextID ; 
                                iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].incorrect_Predictions = 0      ;

                                printf("iDirStreamBuffer->IDirStreamTable[%lu].nextMetaISAStreamID   = %d\n" , ownerDirStreamTableKey , nextID); 
                            }



                            for(int i= 0 ; i <MAX_BANKS ;i++)
                            {
                                iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].crntRowReqsCount[i][0]   = 0; // First Request in row buffer  ( Read  Tracking Counters)                          
                                iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].crntRowReqsCount[i][1]   = 0; // First Request in row buffer  ( Write Tracking Counters)                          
                            }    
                            if (print_iprefetcher_trace_verbose)
                            {
                                printf("InterStream iBatch Interference Resolved (A:%#lx)(Owner ID:%lx)(Interf ID:%#lx)",req.addr,ownerDirStreamTableKey,otherStreamTableKey);
                                printf("\t\tInterStream iBatch Interference Update_last_ibatch[Owner ID:%lx]=%#lx",ownerDirStreamTableKey,lastiBatchedAddr);
                            }
                            printf("\n");

                        }       
                        /****************  Special  Case   [This is a base address of another stream request]   *************/
                        /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

                        if (print_iprefetcher_trace)
                            printf(" iBatched  RD to 0x%lx accepted [PID =%d] .\n", req.addr, req.metaISARequestorID);
                        // Mark the entry as requested
                        (*this->iBuffer[bank_id])[iBufferKeyObj].R = 1;
        
                        //  *************  A hit in iPrefetcher happens ******************/
                        //  (Corresponding window should be updated !)
                        //Hit is considered once an actula iPrefetch arrives regardless its corresponding data is ready or not yet
                        if(enableIPrefetcherWinUpdate)
                        {

                                // Update the crntDirWinLen
                                int maxWinSize = Regular_Window_Size;
                                if(win_index==WINDOW_TYPE::IRREG_WINDOW)
                                    maxWinSize = Irregular_Window_Size;
                                if(maxWinSize!=0)
                                {
                                    /* Debug_Detailed
                                    if (print_iprefetcher_trace)
                                        printf(" iBatch Win[%d][%lx] , addr = %lx\n",win_index, metaisa_winkey,req.addr);
                                        */
                                    //Window should be created for at least the first request that issued the iPrefteches
                                    // This can be violated at the stream edges 
                                    if (metaisa_window_counter[win_index].find(metaisa_winkey) == metaisa_window_counter[win_index].end())
                                    {
                                            // Initalize The counter
                                            metaisa_window_counter[win_index][metaisa_winkey] = new Window_Info;
                                            metaisa_window_counter[win_index][metaisa_winkey]->crnt_window_length = 0;
                                            metaisa_window_counter[win_index][metaisa_winkey]->num_misses = 0;
                                            metaisa_window_counter[win_index][metaisa_winkey]->misses_shift_reg = bitset<UPPER_WIN_SIZE>(0);
                                    }
                                    else 
                                    {
                                        uint8_t crntWinLen = metaisa_window_counter[win_index][metaisa_winkey]->crnt_window_length;

                                        if (crntWinLen     < maxWinSize)
                                            metaisa_window_counter[win_index][metaisa_winkey]->crnt_window_length++;
                                        // metaisa_window_counter[win_index][metaisa_dir_winkey]->misses_shift_reg<<1;
                                        uint8_t isCrntMiss = 0; // iPrefetched means Hit !                            
                                        // Fast GEM5 Sliding Window
                                        if (crntWinLen == maxWinSize)
                                        {
                                                metaisa_window_counter[win_index][metaisa_winkey]->misses_shift_reg >>= 1;
                                                metaisa_window_counter[win_index][metaisa_winkey]->misses_shift_reg[maxWinSize-1] =  0;    
                                        }
                                        else 
                                            metaisa_window_counter[win_index][metaisa_winkey]->misses_shift_reg[crntWinLen] =  0;
                                        metaisa_window_counter[win_index][metaisa_winkey]->num_misses =
                                                    metaisa_window_counter[win_index][metaisa_winkey]->misses_shift_reg.count();

                                        if (print_ipp_logic_trace)
                                        {
                                            cout << "\t\tRamulator :: DirStream Window Counter["
                                                << std::hex << metaisa_winkey
                                                << "] : Addr = " << std::hex << req.addr
                                                << "- Bank ID: = " << std::hex << bank_id
                                                << " - PrevAddr = (";
                                        for (auto i : prev_addr_vec[bank_id])
                                                std::cout << i << ' ';
                                                cout << " )- Misses = " << std::dec << metaisa_window_counter[win_index][metaisa_winkey]->num_misses
                                                << " - Window Length = " << (int)crntWinLen << " - Window : ";
                                            for (std::size_t i = 0; i < crntWinLen; ++i)
                                                std::cout << metaisa_window_counter[win_index][metaisa_winkey]->misses_shift_reg[i] << ' ';
                                            cout << "\n";

                                            printf("\t\tRamulator- IPP(Dir)[Window(%lx)Strategy]: R(%s) addr = 0x%lx \n",
                                                        metaisa_winkey, ramulator::request_name[int(req.type)].c_str(),   req.addr);
                                        }
                                    }
                                }
                        }


                        // If Data is valid respond
                        if ( (*this->iBuffer[bank_id])[iBufferKeyObj].W == 0)
                        {
                            arrivalStatus=ArrivalSTATUS::iBufferH;
                        }
                        else
                        { // Not Valid (   respond later)  reads queue should include the packet for sake of statistics
                            if( (*this->iBuffer[bank_id])[iBufferKeyObj].isStartSeg == 0)
                                arrivalStatus=ArrivalSTATUS::iBufferWS;
                            else
                                arrivalStatus=ArrivalSTATUS::iBufferW;
                        }

                        //Check if the request is in the pending queue (Progressing now due to iBatch) 
                        for (int i=0;i<pending.size();i++) {
                            if(pending[i].addr==req.addr)
                                arrivalStatus=ArrivalSTATUS::iBatchInPending;
                        }       

                    }

 

              }


            }

            switch(arrivalStatus)
            {
                case(ArrivalSTATUS::enqueueQ):
                {
                    if (queue.max == queue.size())
                    { 
                            arrivalStatus=ArrivalSTATUS::qneueueQFull;
                            if(req.type==Request::Type::READ)
                            {
                               // printf("[ReadQ");
                                read_req_queue_full_times++;
                            }
                            else 
                                if(req.type==Request::Type::WRITE)
                                {
                                //    printf("[WriteQ");
                                    write_req_queue_full_times++;
                                }
                                else 
                                {
                                //    printf("[OthersQ");
                                    others_req_queue_full_times++;
                                }
                            // printf("is Full @T:%lu,Inside:mem_ctrl] [%d/%d]\n ",gem5::curTick(),queue.size(),queue.max);
                            //  debugQueuesDetailed();
                            return false;                                     
                    }
                    else 
                    {
                        req.arrive = clk;
                        queue.q.push_back(req);
                        // shortcut for read requests, if a write to same addr exists necessary for coherence
                        if (req.type == Request::Type::READ && find_if(writeq.q.begin(), writeq.q.end(),
                                                                    [req](Request &wreq)
                                                                    { return req.addr == wreq.addr; }) != writeq.q.end())
                        {
                            req.depart = clk + 1;
                            pending.push_back(req);
                            readq.q.pop_back();
                        }
                    }
                }
                break; 
                case(ArrivalSTATUS::iBufferH):
                {
                    if (print_iprefetcher_trace)
                        printf("Request %#lx in iBatch and complete now!\n", req.addr);                           
                    req.req_interstellarRT_type = DIRREQ_RT_TYPE::rtbh;
                    ++Tot_Req_Hit_In_iBatch;
                    totRReq__rtbh[req.coreid]++;
                    ++iprefetches_completed_cnt_per_bank[bank_id];
                    int idx_per_stream_per_core = req.metaISAStreamID + ( MAX_STREAMS*req.metaISARequestorID) ; 
                    ++iBatches_completed_cnt_per_stream[idx_per_stream_per_core];
                    long lat = 1 ;//       req.arrive not set , the request just arrive; 
                    last_finish_time_per_core[req.coreid]  = clk ; 
                    AllRequests_SumLat_dir_rtbh[req.coreid]+=lat;
                    AllRequests_SumLat_dir[req.coreid]     += lat  ;
                    if(lat > PerRequest_WCL_dir_rtbh[req.coreid].value())
                        PerRequest_WCL_dir_rtbh[req.coreid]  = lat;
                    if(print_departures)
                    {
                        std::string reqType = (int(req.type)==0)?"R":"W";
                        printf("\t Hit_in_rtBuffer  [C:%d][S:%d]  ( A:%#16lx ) ( T:%10ld ) (V:",req.metaISARequestorID,req.metaISAStreamID  ,req.addr, clk);
                        for (int lev = 0; lev < int(T::Level::MAX); lev++)
                        {
                            if(lev!=0) 
                                printf(",");
                            printf(" %5x ", req.addr_vec[lev]);
                        }
                        printf(") ");
                        std::string rtTypeStr = rtTypeStrMap[req.req_interstellarRT_type];
                        printf("(RT:%s)"    , rtTypeStr.c_str());
                        printf(" (Type:%s) (Lat=%ld)(WCL=%f)\n", reqType.c_str() ,  lat , (*(pPerRequest_WCL[(int)(req.req_interstellarRT_type)]))[req.coreid].value());                            
                    }
                    //Now delete entryin iBuffer
                    (*this->iBuffer[bank_id]).delete_data(iBufferKeyObj);
                    if(print_departures)
                    {
                        //delete entry from iBuffer 
                        printf("\tDetele entry from iBuffer[BA:%d] , Len = (%d/%d)\n",bank_id,(*this->iBuffer[bank_id]).getLength(),(*this->iBuffer[bank_id]).getMaxSize());
                    }
                    req.callback(req,bank_id);
                }  
                break; 
                case(ArrivalSTATUS::iBufferW):
                {
                    if (print_iprefetcher_trace)
                        printf("\t A= %lx iBatch issued , wait data\n",req.addr);
                    req.req_interstellarRT_type = DIRREQ_RT_TYPE::rtbw;
                    ++Tot_Req_wait_iBatch;
                    totRReq__rtbw[req.coreid]++;
                    (*this->iBuffer[bank_id])[iBufferKeyObj].actualReqArrive = clk;   
                }  
                break; 
                case(ArrivalSTATUS::iBufferWS):
                {
                    if (print_iprefetcher_trace)
                        printf("\t A= %lx iBatch issued , start segment in rtBatch but wait data\n",req.addr);
                }  
                break;                 
                case(ArrivalSTATUS::iBatchInPending):
                {
                    if (print_iprefetcher_trace)
                        printf("\t A= %lx iBatch issued , wait data in pending\n",req.addr);
                     req.req_interstellarRT_type = DIRREQ_RT_TYPE::rtbw;
                    ++Tot_Req_wait_iBatch;
                    totRReq__rtbw[req.coreid]++;
                    (*this->iBuffer[bank_id])[iBufferKeyObj].actualReqArrive = clk; 
                }  
                break; 
                default:
                break; 
            }


            std::string arrivalStatusStr =  arrivalStatusMap[arrivalStatus];
            if (print_arrivals)
            {
                //printf("\t Enqueue  (D:%d) (S:%d) (A:%#lx) (T:%10ld)  l=%u\n",req.is_demand , req.metaISAStreamID, req.addr ,clk,  queue.size());
                string reqType = (int(req.type)==0)?"R":"W";
                printf("\t %s  [C:%d][S:%d]  ( A:%#16lx ) ( T:%10ld ) (V:", arrivalStatusStr.c_str() ,req.metaISARequestorID,req.metaISAStreamID  ,req.addr, clk);
                            for (int lev = 0; lev < int(T::Level::MAX); lev++)
                            {
                                if(lev!=0) 
                                    printf(",");
                                printf(" %5x ", req.addr_vec[lev]);
                            }
                            printf(") ");
                printf("(Type:%s) l=%u\n", reqType.c_str(),  queue.size());
            
            }

           
            return true;
        }

        void updatePrefetcher( circBuffer<ramulator::iBufferKeyClass, ramulator::IpreftecherQueueEntry> * iBatchBuffer[]  )
        {
            for(int ba_ind = 0 ;ba_ind < MAX_BANKS ; ba_ind++ )
            {
                if( this->iBuffer[ba_ind] == NULL )
                {
                    this->iBuffer[ba_ind] =  iBatchBuffer[ba_ind];
                    this->iBuffer[ba_ind]->setCircBufferSize( iBufferSize   );
                    this->iBuffer[ba_ind]->preftechStreamDepth = this->iBatchWidth; 
                }
            }
        }


        int getCoresNum()
        {
            return coresNum;
        }

        /***************************************************************
         * @param  NonModifiable  : req , bank_id , metaisa_window_counter_key
         * @param  ByReference    : queue - actq - readq -  isDemandIbatchInActq
         * @return Should current request be inserted into command queue ?
        */
        void doiBatchHWPAware(list<Request>::iterator req,typename T::Command cmd,
                      const int & bank_id, const uint64_t & metaisa_window_counter_key,
                      Queue*   queue , Queue & actq , Queue & readq,
                      bool & isDemandIbatchInActq , bool & ret  )
        {
            /********^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*********************/
            /************ Intelligent  Prefetcher Logic Start ***************/
            // Apply  IPreftecher Issues only only if the request is not already IPrefteched

            demandiBatchStatus   isDemandIniBatchRange = FUTURE       ;
            int                  coreid                = req->coreid  ;
            // If not exist , create it

            //if (channel->spec->is_accessing(cmd))
            string SpecialBatchMsg="";
            uint64_t iDirStreamTableKey;
            iDirStreamTableKey = (( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                                    req->metaISARequestorID);
            int iPrefetchStep = 0;
            iPrefetchStep = req->stride;// if req->stride is zero -> infinite loop
            if(req->stride>0)
                while (iPrefetchStep < ramulator::CACHE_LINE_SIZE)
                      iPrefetchStep += req->stride;
              
            //            if(req->metaISAStreamType == DIR_STREAM && req->is_demand && !req->is_iprefetched)
          
            if(req->metaISAStreamType == DIR_STREAM   && !req->is_iprefetched)
            {
 
                // only add new key if old one not exist (avoid overwrite last prefetch address by subsequent PRE , ACT , RD of base address)
                if(iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey)==iDirStreamBuffer->IDirStreamTable.end())
                {
                    //Create Direct Stream Table at all channels 
                    for(int chan_id = 0 ; chan_id<channels ; chan_id++)
                    {
                        //printf("(T:%ld) Base address %#lx of requestor %d received\n",this->clk, req->addr , req->metaISARequestorID);
                        SpecialBatchMsg+="/ActivateStream";
                        ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].active             = true;
                        ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].stride             = req->stride;
                        for(int i= 0 ; i <MAX_BANKS ;i++)
                        {
                            ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[i][0]   = 0; // First Request in row buffer  ( Read  Tracking Counters)                          
                            ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[i][1]   = 0; // First Request in row buffer  ( Write Tracking Counters)                          
                            /***** Inter-Stream Improvement */                    
                        }
                        if(enableInterStreamKnowledge==1)
                        {
                            uint8_t  nextID = 1;
                            if(req->metaISAStreamID==1)
                                        nextID = 2;

                            ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].nextMetaISAStreamID   = nextID ; 
                            ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].incorrect_Predictions = 0      ;
                            printf("ctrls[%d]->iDirStreamBuffer->IDirStreamTable[%lu].nextMetaISAStreamID   = %d\n" , chan_id,iDirStreamTableKey , nextID); 
                        }   

                        ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr = -1;

                    }
                }
                if(this->isConfigiBatch)
                {
                    if (req->is_base_addr)
                    {
                        //Reset Direct Stream Table at all channels 
                        for(int chan_id = 0 ; chan_id<channels ; chan_id++)
                        {
                            
                            SpecialBatchMsg+="/BaseAddress";
                            //printf("(T:%ld) Base address %#lx of requestor %d received\n",this->clk, req->addr , req->metaISARequestorID);
                            ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].active             = true;
                            ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].stride             = req->stride;
                            for(int i= 0 ; i <MAX_BANKS ;i++)
                            {
                                    ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[i][0]   = 0; // First Request in row buffer  ( Read  Tracking Counters)                          
                                    ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[i][1]   = 0; // First Request in row buffer  ( Write Tracking Counters)                          
                            }
                            /***** Inter-Stream Improvement */
                            if(enableInterStreamKnowledge==1)
                            {
                                    uint8_t  nextID = 1;
                                    if(req->metaISAStreamID==1)
                                        nextID = 2;
                                    ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].nextMetaISAStreamID   = nextID ; 
                                    ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].incorrect_Predictions = 0      ;
                                    printf("ctrls[%d]->iDirStreamBuffer->IDirStreamTable[%lu].nextMetaISAStreamID   = %d\n" ,chan_id, iDirStreamTableKey , nextID); 
                            }                             
                            ctrls[chan_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr = req->addr-iPrefetchStep ; // as if the last iBatch just before the base address
                        }
                    }
                    else
                    {
                        if(iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr != -1)
                        {

                        // if(iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr==-1)
                        // {
                        //     iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr = req->addr  ;
                        //     iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].active            = true       ;   
                        // }
                        // else 
                        // {
                            //if Demand request with the address greater than previBatchLastAddr
                            //    This is helpful in case of some addresses is cached in middle of direct stream 

                            if(req->addr   >  (200*req->stride) + iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr)
                            {
                                iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].FarFutureAccessCount++;
                            }
                            if( iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].FarFutureAccessCount > 10)
                            {
                                iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].FarFutureAccessCount = 0;
                                //For this assumption , the distance should be large enough , otherwise may be small reordering happen (@todo : can cached intervals happen in narrower range?)
                                SpecialBatchMsg+="/JumpCachedRegion";
                                //printf("iBatch(Change PreviBatchAddr)::Possibly Jump Cached Region(OldA:%#lx)(NewA:%#lx)\n",
                                // iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr,req->addr-iPrefetchStep);
                                iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr=req->addr-iPrefetchStep;
                            }
                            // Another iteration of the stream [start the stream again] (Another clean way is to change it if teh first address is rexed)
                            /*if(req->addr+(100*req->stride)   <   iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr)
                            {
                                iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].FarFutureAccessCount = 0;
                                //For this assumption , the distance should be large enough , otherwise may be small reordering happen (@todo : can cached intervals happen in narrower range?)
                                printf("iBatch(Change PreviBatchAddr)::Possibly Jump Back[New Iteration](OldA:%#lx)(NewA:%#lx)\n",
                                iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr,req->addr-iPrefetchStep);
                                iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr=req->addr-iPrefetchStep;
                            }*/
                        }
                    }
                }
            }

            if(enableHwpAwareiBatch)//For HWP Outliers Aware Only
            {
                // If base addr didn't come yet - skip the current req  
                if(req->metaISAStreamType == DIR_STREAM)
                {
                    // First condition means that first iteration and current req is HWP before base addr 
                    // Second contion means subsequent iterations and current request is HWP before base address
                    if(
                    iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey) == iDirStreamBuffer->IDirStreamTable.end()
                    ||
                    ! iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].active
                    ||
                    iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr == -1
                    )
                    {
                        // For early requests , remove them from readq ! (They will respond later on)
                        //queue->q.erase(req);
                        //printf("erase  req for address A:%#lx\n",req->addr);
                        if (print_iprefetcher_trace)
                            printf(" iBatch [S:%d][C:%d][T:%ld](A:%#lx )(B:%5x %5x %5x %5x %5x %5x)(D:%d)(H:%d)(Last_iBatch:%#lx)(|iBuff[%d]|=%d)(Early HWP won't issue iBatch)\n ",req->metaISAStreamID,req->metaISARequestorID,this->clk,req->addr,
                                    req->addr_vec[0], req->addr_vec[1], req->addr_vec[2], req->addr_vec[3], req->addr_vec[4], req->addr_vec[5]
                                    , req->is_demand , req->is_hwp , iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr ,bank_id , 
                                    iBuffer[bank_id]->getLength());
                        
                        // @todo (If we process this req - it will cost extra few cycles ) -> but maybe useful to avoid making waiting cache ?
                         return; 
                    }
                }
            }

            if (!req->is_iprefetched)
            {

                if (enableIPrefetcher == 1 && req->type == Request::Type::READ)
                {

                    // Indirect Stream but of high locality
                    bool irregularStrHighLocality = false;
                   if (req->metaISAStreamType == INDIR_STREAM || req->metaISAStreamType == PTR_CHASE)
                        if(
                        metaisa_window_counter[WINDOW_TYPE::IRREG_WINDOW].find(metaisa_window_counter_key)
                        != 
                        metaisa_window_counter[WINDOW_TYPE::IRREG_WINDOW].end()
                        ) 
                             irregularStrHighLocality = ((float)metaisa_window_counter[WINDOW_TYPE::IRREG_WINDOW][metaisa_window_counter_key]->num_misses < (float)(metaisa_window_counter[WINDOW_TYPE::IRREG_WINDOW][metaisa_window_counter_key]->crnt_window_length) * (float)metaisa_indir_window_threshold);

                    bool regularStr = false; 
                    if(req->metaISAStreamType == DIR_STREAM)
                    {
                        if(enableHwpAwareiBatch)
                        {
                            if(iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey) != iDirStreamBuffer->IDirStreamTable.end())
                                if(iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].active)
                                    regularStr = true;
                        }
                        else 
                                regularStr = true;
                    }


                    if (irregularStrHighLocality || regularStr)
                    {
                        // Generate New CAS Commands (That result in hit only)
                        // Assuming N is homogenous across all banks (The same) , so no need to get it from iBatcher_bank_id -> Better make it as a single global constant
                        int N = iBuffer[bank_id]->preftechStreamDepth;
                        // Add N CASs to the command queue

                        int *iBatchReqsCount = new int[channels];
                        int *iBatchLen = new int[channels];
                        int *iBatchCrntCnt = new int[channels];

                        
                        for(int chan=0;chan<channels;chan++)
                        {
                            iBatchReqsCount[chan]=0;
                            iBatchLen[chan]=0;
                            iBatchCrntCnt[chan]=0;
                        }
                            
                        
                        int *iBatchesBGarr [4];
                        for(int bg_id=0;bg_id<4;bg_id++)
                        {
                            iBatchesBGarr[bg_id] =  new int[channels];
                            for(int chan=0;chan<channels;chan++)
                                iBatchesBGarr[bg_id][chan]=0;
                        }

                        uint16_t stride = req->stride;


                        if (print_iprefetcher_trace)
                            printf("\tiBatch [%#lx]: \n",req->addr);
                        // Debug_Detailed
                        if (print_iprefetcher_trace_verbose)
                        {
                                if (req->metaISAStreamType == DIR_STREAM)
                                    printf("\tiBatch Reason: Direct Stream");
                                else
                                    printf("\tiBatch Reason: InDir Stream and counter misses = %d , while counter window length*Threshold = %f",
                                           metaisa_window_counter[WINDOW_TYPE::IRREG_WINDOW][metaisa_window_counter_key]->num_misses,
                                           (metaisa_window_counter[WINDOW_TYPE::IRREG_WINDOW][metaisa_window_counter_key]->crnt_window_length * (float)metaisa_indir_window_threshold));
                                printf("\t\nCrntAddr = %#lx (%d %d %d %d %d %d) \t Stride = %d - iBatchStep = %d\n ",
                                       req->addr,
                                       req->addr_vec[0], req->addr_vec[1], req->addr_vec[2], req->addr_vec[3], req->addr_vec[4], req->addr_vec[5],
                                       stride, iPrefetchStep);
                        }
                        


                        uint64_t             issuingiBatchAddr      = req->addr;
                        vector<int>          startiBatchAddr_vec    = req->addr_vec; //It will be the req in case of NoSchediBatch or (lastiPref+iPrefStep) in case of SchediBatch                                                                   
                        //For InterStellarRT       
                        // generate address vectors for the rtBatch start 
                        vector<int>          startiBatchBGArrAddr_vec [4];
                        long                 startiBatchBGArrAdd[4];
                        for(int bgCnt = 0 ; bgCnt<bgNumPeriBatch ; bgCnt ++)
                        {
                            startiBatchBGArrAdd[bgCnt]      =  (uint64_t)req->addr+bgCnt*iPrefetchStep                              ;     
                            startiBatchBGArrAddr_vec[bgCnt] =  addr_to_addr_vec  (coreid,(uint64_t)req->addr+bgCnt*iPrefetchStep,req->metaISAStreamID)   ;
                                                        // Assign type for direct stream rtBatch start , there are 2 starts if map-2 and 4 starts if map-4 
                        }
                        int IGNORE_CHANNEL = 1 ; // InterStellarMC To ignore channel in comparison to check if consecutive commands in iBatch are hit                      
                        
                        if (enableHwpAwareiBatch && regularStr)
                        {
                            
                            issuingiBatchAddr      = iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr;
                            issuingiBatchAddr      = issuingiBatchAddr+iPrefetchStep;
                            
                            //printf("Issuing Address = %#lx\n",issuingiBatchAddr);
                            startiBatchAddr_vec    = addr_to_addr_vec  (coreid,issuingiBatchAddr,req->metaISAStreamID);
                            /*****  Check if demand req is in iBatch Range  *******/   
                            // a- It should hit with the issuingiBatchAddr 
                            auto beginFirstiBatchReq   = startiBatchAddr_vec.begin();
                            vector<int> rowgroupFiBatch   (beginFirstiBatchReq+IGNORE_CHANNEL, beginFirstiBatchReq + int(T::Level::Row));
                            auto beginDemandReq        = req->addr_vec.begin();
                            vector<int> rowgroupDemand (beginDemandReq+IGNORE_CHANNEL     , beginDemandReq      + int(T::Level::Row));
                            int r = int(T::Level::Row);
                            if(rowgroupFiBatch == rowgroupDemand && req->addr_vec[r] == startiBatchAddr_vec[r])
                                if (req->addr < ( issuingiBatchAddr + N * iPrefetchStep))
                                     isDemandIniBatchRange = IN_BATCH;
                            if(req->addr  < issuingiBatchAddr)
                                isDemandIniBatchRange = PAST;
                            
                            
                            if (req->is_base_addr)//First Batch 
                            {
                                // Check with the first demand request , if iBatch must be disabled (i.e. stride >= ROW BUFFER)
                                if(!(rowgroupFiBatch == rowgroupDemand && req->addr_vec[r] == startiBatchAddr_vec[r]))
                                    N  = 0 ;                            
                            }

                        }
                        if(iPrefetchStep >= ROW_BUFFER_SIZE)
                            N = 0 ;//Don't do any prefetches 
                        if(N==0)
                            return;

                        /********* (1) Put Issuing Request at the front ot actq   ***********/
                        // For the demand request this should happen at the start (Demand is pushed at front) , while iPrefetch is pushed at back to make it first in actq
                        // for isDemandBatchRange this means that the demand will be priotrized over the remaining iBatch requests in the iBatch
                        
                        if(  irregularStrHighLocality 
                        || (!enableHwpAwareiBatch) 
                        || (enableHwpAwareiBatch&&(isDemandIniBatchRange==IN_BATCH ||  isDemandIniBatchRange==PAST)))
                            if (cmd != channel->spec->translate[int(req->type)]) // if cmd not  RD , WR (i.e. PRE or ACT] becuase translate returns RD,WR
                            {
                                totRReq__rtbs[req->coreid]++;
                                switch(cmd)
                                {
                                    case (T::Command::PRE):
                                        req->req_interstellarRT_type = DIRREQ_RT_TYPE::rtbs_conf;
                                        totRReq__rtbs_conf[req->coreid]++;
                                        break;
                                    case (T::Command::ACT):
                                        req->req_interstellarRT_type = DIRREQ_RT_TYPE::rtbs_miss;
                                        totRReq__rtbs_miss[req->coreid]++;
                                        break;
                                    case (T::Command::RD):
                                        req->req_interstellarRT_type = DIRREQ_RT_TYPE::rtbs_hit;
                                        totRReq__rtbs_hit[req->coreid]++;
                                        break;
                                    case (T::Command::RDA):
                                        req->req_interstellarRT_type = DIRREQ_RT_TYPE::rtbs_hit;
                                        totRReq__rtbs_hit[req->coreid]++;
                                        break;
                                    default:
                                        break;
                                }
                                Request   crntReq            = *req;
                                crntReq.is_iprefetched       = true;
                                crntReq.is_first_command     = false;
                                // promote the request that caused issuing activation to actq
                                actq.q.push_back  (crntReq);
                                isDemandIbatchInActq = true;

                            }



                        string prefReqTrace=""; 
                        // Allocate N Requests 
                        Request   iBatchReqsArr [MAX_IBATCH_SIZE];
                        bool      valid_iBatch  [MAX_IBATCH_SIZE];
                        int       tgt_bank_id    [MAX_IBATCH_SIZE];  // InterStellarRT
                        for(int i = 0; i<MAX_IBATCH_SIZE;i++)
                            valid_iBatch[i] = false;
                        long  lastOldAddr = req->addr ; 
                        if(isDemandIniBatchRange== PAST)
                        {
                            // In case of past request , This means that maybe iBatched requests not fetched before so return back to this iBatch
                            issuingiBatchAddr       = req->addr                              ;
                            startiBatchAddr_vec     = addr_to_addr_vec  (coreid,issuingiBatchAddr,req->metaISAStreamID)  ;
                        }
                        // In InterStellar, we have 2/4 bank Groups in the iBatch
                        int iBatcher_tgt_bank_id;
                        int iBatcher_BGi_bank_id [4];
                        if(this->memoryMapping==MemMapping::InterStellarRT)
                        {  
                            for(int bgCnt = 0 ; bgCnt<bgNumPeriBatch ; bgCnt ++)
                            {
                                iBatcher_BGi_bank_id[bgCnt] = startiBatchBGArrAddr_vec[bgCnt][int(T::Level::Bank)];
                                if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                                    iBatcher_BGi_bank_id[bgCnt] += startiBatchBGArrAddr_vec[bgCnt][int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
                            }
                        }

                        int iBatcher_BG1_bank_id = startiBatchAddr_vec[int(T::Level::Bank)];
                        if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                            iBatcher_BG1_bank_id += startiBatchAddr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
                        iBatcher_tgt_bank_id = iBatcher_BG1_bank_id;

                        /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
                        /**********************************************************************************************
                        **                                                                                           **
                        **  iBatch Loop1 :                                                                           **
                        **  Generate Temporary Addresses                                                             **
                        **  Check all conditions if iBatch requests can be inserted into iBuffer.                    **
                        **        (Break the iBatch-loop if the iBuffer is full)                                     **
                        **********************************************************************************************/
                        /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

                        for (int prefCount = 0; prefCount < N; prefCount++)
                        {
                            int iBatchIdx          = prefCount ;
                                                        
                            // Special rtBatch segments forming
                            // @TODO Enforce with map2/map4 BG as following :
                            //        Map-2: BG0 , BG1  ;  Map-4: BG0 , BG1 , BG2 , BG4
                            if(rtAddrMaping==RTAddrMaping::map4)
                            {
                                if (prefCount < N / 2) {
                                    // First half of iterations: Handles %4 == 0 or 1
                                    iBatchIdx = (prefCount / 2) * 4 + (prefCount % 2);
                                } else {
                                    // Second half of iterations: Handles %4 == 2 or 3
                                    iBatchIdx = ((prefCount - N / 2) / 2) * 4 + (prefCount % 2) + 2;
                                }
                            }
                            /*
                                Add the Demand request itself to the iBuffer
                                This is helpful under 2 reasons:
                                1- Indirect stream with high locality -> The request address may be needed in near future
                                2- Direct stream                      ->  with HWP , Some reordering may happen
                                i.e. call 8000,8C00,8400,8200 Then no sense to prefetch 8C00 while access 8400 !
                            */

                            iBatchReqsArr[iBatchIdx] = *req;
                            iBatchReqsArr[iBatchIdx].addr               = issuingiBatchAddr + iBatchIdx * iPrefetchStep;
                            iBatchReqsArr[iBatchIdx].metaISARequestorID = req->metaISARequestorID;
                            iBatchReqsArr[iBatchIdx].metaISAStreamID    = req->metaISAStreamID; // Don't issue a prefetch if it is issued before

                            if(regularStr) // Direct Stream Start rtBatch 
                            {
                                if (
                                    ( iBatchIdx==0 && (rtAddrMaping==RTAddrMaping::map1) )
                                    ||
                                    ( iBatchIdx<=1 && (rtAddrMaping==RTAddrMaping::map2) )
                                    ||
                                    ( iBatchIdx<=3 && (rtAddrMaping==RTAddrMaping::map4) )
                                     )
                                        iBatchReqsArr[iBatchIdx].is_start_inIBatch = true;
                            }
                                                      
                            
                            iBufferKeyClass iBufferKeyObj(iBatchReqsArr[iBatchIdx].addr, iBatchReqsArr[iBatchIdx].metaISAStreamID, iBatchReqsArr[iBatchIdx].metaISARequestorID);

                            std::stringstream stream;
                            stream << std::hex <<iBatchReqsArr[iBatchIdx].addr;
                            std::string addrHexStr( stream.str() ); 

                            // Because of reordering happens in HWP , and use of PreScheduled Prefteches , It happens that a prefetch generated is the same address as a demand request
                            /*if (iBatchReqsArr[iBatchIdx].addr == req->addr)
                                continue;*/
                            iBatchReqsArr[iBatchIdx].is_base_addr  = false;
                            iBatchReqsArr[iBatchIdx].addr_vec      = addr_to_addr_vec(coreid,iBatchReqsArr[iBatchIdx].addr,iBatchReqsArr[iBatchIdx].metaISAStreamID);
                            //Phase_2 Modify: iBatchReqsArr[iBatchIdx].nextAddr      = req->nextAddr + iBatchIdx * iPrefetchStep;
                            // InterStellarMC (To account for multichannel in interstellar)
                            int ch_idx = int(T::Level::Channel);
                            int ch_id  = iBatchReqsArr[iBatchIdx].addr_vec[ch_idx];  

                            iBatchReqsArr[iBatchIdx].nextAddr      = issuingiBatchAddr + (iBatchIdx+1) * iPrefetchStep;       
                            iBatchReqsArr[iBatchIdx].next_addr_vec = addr_to_addr_vec(coreid,iBatchReqsArr[iBatchIdx].nextAddr,iBatchReqsArr[iBatchIdx].metaISAStreamID);
                            // It will be the first command (Active open row)
                            if (iBatchReqsArr[iBatchIdx].addr != req->addr) // Not the demand request  
                                iBatchReqsArr[iBatchIdx].is_first_command = true;
                            iBatchReqsArr[iBatchIdx].is_iprefetched = true;
                            iBatchReqsArr[iBatchIdx].metaISAStreamType = req->metaISAStreamType;

                            // check if the last iPrefetched request and new prefetched request are on same row or not !
                            auto beginReq    = startiBatchAddr_vec.begin();
                            vector<int> rowgroupReq(beginReq+IGNORE_CHANNEL, beginReq + int(T::Level::Row));
                            vector<int>::iterator    beignReqBG2    , beignReqBG3    , beignReqBG4    ;
                            vector<int>              rowgroupReqBG2 , rowgroupReqBG3 , rowgroupReqBG4 ; 
                            if(rtAddrMaping==RTAddrMaping::map2||  rtAddrMaping==RTAddrMaping::map4 )
                            {   beignReqBG2  = startiBatchBGArrAddr_vec[1].begin();
                                rowgroupReqBG2.assign(beignReqBG2+IGNORE_CHANNEL,beignReqBG2+ int(T::Level::Row));
                            }
                            if(rtAddrMaping==RTAddrMaping::map4 )
                            {
                                beignReqBG3 = startiBatchBGArrAddr_vec[2].begin();
                                rowgroupReqBG3.assign(beignReqBG3+IGNORE_CHANNEL,beignReqBG3+ int(T::Level::Row));
                                beignReqBG4 = startiBatchBGArrAddr_vec[3].begin();
                                rowgroupReqBG4.assign(beignReqBG4+IGNORE_CHANNEL,beignReqBG4+ int(T::Level::Row));
                            }
                            auto beginPrefetch = iBatchReqsArr[iBatchIdx].addr_vec.begin();
                            vector<int> rowgroupPrefetch(beginPrefetch+IGNORE_CHANNEL, beginPrefetch + int(T::Level::Row));
                            int r = int(T::Level::Row);
                            // Only fetch subset requests out of N requests that will hit (Same rowgroup and row)
                            if(this->memoryMapping == MemMapping::RoCoBaRaCh)
                            {
                                //Batch requests that belongs to different banks[Large stride case] / bank group 
                                if(! (startiBatchAddr_vec[r] == iBatchReqsArr[iBatchIdx].addr_vec[r]))
                                      break;
                            }
                            else
                            {
                                if(this->memoryMapping == MemMapping::RoBaBg1CoBg0RaCh || this->memoryMapping == MemMapping::StrmPartRoBaBg1CoBg0RaCh)
                                {
                                    //Batch requests that belongs to different banks[Large stride case] / bank group 
                                    if(! (startiBatchAddr_vec[r] == iBatchReqsArr[iBatchIdx].addr_vec[r]))
                                        break;                                   
                                }
                                else
                                {
                                    if(this->memoryMapping == MemMapping::InterStellarRT)
                                    {
                                        //Either hit in Same BG
                                        if(rtAddrMaping==RTAddrMaping::map2 )
                                        {
                                            if(!(
                                                (rowgroupReqBG2 == rowgroupPrefetch && startiBatchBGArrAddr_vec[1][r] == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                                ||
                                                (rowgroupReq    == rowgroupPrefetch && startiBatchAddr_vec[r]    == iBatchReqsArr[iBatchIdx].addr_vec[r])))
                                                break;
                                            if(rowgroupReqBG2 == rowgroupPrefetch && startiBatchBGArrAddr_vec[1][r] == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                            {
                                                iBatcher_tgt_bank_id = iBatcher_BGi_bank_id[1];
                                                iBatchReqsArr[iBatchIdx].rt_segment_id = 1; 
                                            }
                                            else 
                                            {
                                                iBatcher_tgt_bank_id = iBatcher_BG1_bank_id;
                                                iBatchReqsArr[iBatchIdx].rt_segment_id = 0; 
                                            }

                                        }
                                        if(rtAddrMaping==RTAddrMaping::map4)
                                        {
                                            if(!(
                                                (rowgroupReqBG2  == rowgroupPrefetch && startiBatchBGArrAddr_vec[1][r] == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                                ||
                                                (rowgroupReqBG3  == rowgroupPrefetch && startiBatchBGArrAddr_vec[2][r]  == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                                ||
                                                (rowgroupReqBG4  == rowgroupPrefetch && startiBatchBGArrAddr_vec[3][r]  == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                                ||
                                                (rowgroupReq    == rowgroupPrefetch && startiBatchAddr_vec[r]      == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                                ))
                                                break;
                                            if(rowgroupReqBG2 == rowgroupPrefetch && startiBatchBGArrAddr_vec[1][r] == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                            {
                                                iBatcher_tgt_bank_id = iBatcher_BGi_bank_id[1];
                                                iBatchReqsArr[iBatchIdx].rt_segment_id    = 1; 
                                            }
                                            else 
                                                if(rowgroupReqBG3 == rowgroupPrefetch && startiBatchBGArrAddr_vec[2][r] == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                                {
                                                    iBatcher_tgt_bank_id = iBatcher_BGi_bank_id[2];
                                                    iBatchReqsArr[iBatchIdx].rt_segment_id    = 2; 
                                                }    
                                                else 
                                                    if(rowgroupReqBG4 == rowgroupPrefetch && startiBatchBGArrAddr_vec[3][r] == iBatchReqsArr[iBatchIdx].addr_vec[r])
                                                    {
                                                        iBatcher_tgt_bank_id = iBatcher_BGi_bank_id[3]; 
                                                        iBatchReqsArr[iBatchIdx].rt_segment_id    = 3; 
                                                    }                                       
                                                    else
                                                    {
                                                        iBatcher_tgt_bank_id = iBatcher_BG1_bank_id;
                                                        iBatchReqsArr[iBatchIdx].rt_segment_id    = 0; 
                                                    }    
                                                        

                                        }
                                        if(rtAddrMaping==RTAddrMaping::map1 )
                                        { if(!(rowgroupReq == rowgroupPrefetch && startiBatchAddr_vec[r] == iBatchReqsArr[iBatchIdx].addr_vec[r]))
                                            break; // @todo In case of -N to N (Indirect tream you may not want to break)
                                        }

                                    }                             
                                    else 
                                        if(!(rowgroupReq == rowgroupPrefetch && startiBatchAddr_vec[r] == iBatchReqsArr[iBatchIdx].addr_vec[r]))
                                            break; // @todo In case of -N to N (Indirect tream you may not want to break)
                                }
                            }
                            tgt_bank_id[iBatchIdx] = iBatcher_tgt_bank_id;
                              // If iBatched Before , Break (i.e. Possibility that because of re-order demand request added to iBatch and now it is part of new iBatch)
                            if (ctrls[ch_id]->iBuffer[tgt_bank_id[iBatchIdx]]->find(iBufferKeyObj)) // If exist before then continue
                            {
                                prefReqTrace = prefReqTrace+" iBatch [S:"+to_string(iBatchReqsArr[iBatchIdx].metaISAStreamID)+"][C:"+to_string(iBatchReqsArr[iBatchIdx].metaISARequestorID)+"][T:"+to_string(this->clk)+"](A:"+addrHexStr+" )(B:"+  to_string(iBatchReqsArr[iBatchIdx].addr_vec[0])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[1])+" "+ to_string(iBatchReqsArr[iBatchIdx].addr_vec[2])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[3])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[4])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[5])+")(D:-)(H:-)(Last_iBatch:"+addrHexStr+")(Exist Nearly Before)\n ";
                                if(isDemandIniBatchRange != PAST)
                                {
                                    ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr = iBatchReqsArr[iBatchIdx].addr; 
                                }
                                continue;
                            }

                            if(ctrls[ch_id]->iBatchHistory->find(iBatchReqsArr[iBatchIdx].addr))//To account for HWP -> Only fo iPrefetch if not in history of iPrefteches 
                            {
                               
                                //iBuffer[iBatcher_BG1_bank_id]->DisplayIprefetchFull();
                                //iBuffer[iBatcher_BG2_bank_id]->DisplayIprefetchFull();
                                prefReqTrace = prefReqTrace+" iBatch [S:"+to_string(iBatchReqsArr[iBatchIdx].metaISAStreamID)+"][C:"+to_string(iBatchReqsArr[iBatchIdx].metaISARequestorID)+"][T:"+to_string(this->clk)+"](A:"+ addrHexStr +" )(B:"+ to_string(iBatchReqsArr[iBatchIdx].addr_vec[0])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[1])+" "+ to_string(iBatchReqsArr[iBatchIdx].addr_vec[2])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[3])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[4])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[5])+")(D:-)(H:-)(Last_iBatch:"+addrHexStr+")(Exist Old Before)\n ";
                                if(isDemandIniBatchRange != PAST)
                                {
                                    ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr = iBatchReqsArr[iBatchIdx].addr; 
                                }
                                continue;
                            }   
                            
                            /****************************************************************
                             *                                                              *
                             *  Dealing with iBuffer Full Scenarios:                        *
                             *  iBuffer_Repl_Policy =                                       *
                             *     a. None     : Don't overwrite item in iBuffer.           *
                             *        (Break the iBatch loop if the iBuffer is full)        *
                             *                                                              *
                             *     b. LRU      : Replace the least recently entered.        *
                             *        (Always iBatch requests can be generated )            *
                             *                                                              *
                             ****************************************************************/
                            if (iBatchReqsArr[iBatchIdx].addr != req->addr)
                            {
                                if(this->iBuffer_Repl_Policy==0)//Don't overwrite the iBuffer
                                {
                                    // as First iBatch-loop don't push the iBatch requests , so inspect length+iBatch requests count expected till now
                                    if(ctrls[ch_id]->iBuffer[tgt_bank_id[iBatchIdx]]->getLength()+iBatchReqsCount[ch_id] ==ctrls[ch_id]->iBuffer[tgt_bank_id[iBatchIdx]]->getMaxSize())
                                        break;
                                    //Debug iBuffer when it is expected to be full                                
                                    /*if(iBuffer->getLength()+iBatchReqsCount ==iBuffer->getMaxSize())
                                    {
                                        printf("Pop iprefetcher \n");
                                        iBuffer->DisplayIprefetchFull();
                                    }*/                                        
                                }// otherwise pushing into full iBuffer will do LRU replacement (circular buffer)
                            }

                            // total iBatched requestes should increment 
                            if (iBatchReqsArr[iBatchIdx].addr != req->addr)
                            {
                                iBatchReqsCount[ch_id]++; 
                                (tgt_bank_id[iBatchIdx] ==iBatcher_BG1_bank_id)?iBatchesBGarr[0][ch_id]++:
                                (tgt_bank_id[iBatchIdx] ==iBatcher_BGi_bank_id[1])?iBatchesBGarr[1][ch_id]++:
                                (tgt_bank_id[iBatchIdx] ==iBatcher_BGi_bank_id[2])?iBatchesBGarr[2][ch_id]++:iBatchesBGarr[3][ch_id]++;
                            }
 
                            ctrls[ch_id]->iBatchHistory    -> Push(std::make_pair(iBatchReqsArr[iBatchIdx].addr, 0)           )  ;
                            //iBatchHistory->DisplayIprefetchAddr();
                            valid_iBatch[iBatchIdx]=true;
                            if(isDemandIniBatchRange != PAST)
                            {
                                ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr = iBatchReqsArr[iBatchIdx].addr; 
                            }
                            iBatchLen[ch_id]++;
                        }

                        bool  leadFound    = false;

                        /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
                        /**********************************************************************************************
                        **                                                                                           **
                        **  iBatch Loop2 :                                                                           **
                        **  Do actual insertions of iBatch requests in iBuffer                                       **                       **
                        **  Insert iBatch requests into the actq if feasible                                         **
                        **                                                                                           **
                        **********************************************************************************************/
                        /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
                        
                        for(int prefCount = 0 ; prefCount < N ; prefCount++ )
                        {
                            int iBatchIdx          = prefCount ;

                            /// With special address mapping map4 : First hlaf (Bank Groups 00,01) , then second half (Bank groups 10,11)
                            if(rtAddrMaping==RTAddrMaping::map4)
                            {
                                if (prefCount < N / 2) {
                                    // First half of iterations: Handles %4 == 0 or 1
                                    iBatchIdx = (prefCount / 2) * 4 + (prefCount % 2);
                                } else {
                                    // Second half of iterations: Handles %4 == 2 or 3
                                    iBatchIdx = ((prefCount - N / 2) / 2) * 4 + (prefCount % 2) + 2;
                                }
                            }
                            if(!valid_iBatch[iBatchIdx])
                                continue;

                            // InterStellarMC (To account for multichannel in interstellar)
                            int ch_idx = int(T::Level::Channel)                     ;
                            int ch_id  = iBatchReqsArr[iBatchIdx].addr_vec[ch_idx]  ;   
                                
                            if(!leadFound && isDemandIniBatchRange== FUTURE )//In case of future request generates iBatch group
                            {    
                                iBatchReqsArr[iBatchIdx].lead_iBatch = leadFound = true;
                            }
                            iBatchCrntCnt[ch_id]++;
                            iBufferKeyClass iBufferKeyObj(iBatchReqsArr[iBatchIdx].addr, iBatchReqsArr[iBatchIdx].metaISAStreamID, iBatchReqsArr[iBatchIdx].metaISARequestorID);
                            
                            if(this->memoryMapping==MemMapping::InterStellarRT)
                            {  
                                switch(int(rtAddrMaping)){
                                    case int(RTAddrMaping::map1):
                                        if(iBatchCrntCnt[ch_id]==iBatchLen[ch_id])
                                        iBatchReqsArr[iBatchIdx].is_last_inIBatch = true;
                                    break;
                                    case int(RTAddrMaping::map2):
                                        if(iBatchCrntCnt[ch_id]==iBatchLen[ch_id] || iBatchCrntCnt[ch_id]==iBatchLen[ch_id]-1)
                                            iBatchReqsArr[iBatchIdx].is_last_inIBatch = true; 
                                    break;
                                    case int(RTAddrMaping::map4):
                                        if(iBatchCrntCnt[ch_id]==iBatchLen[ch_id] || iBatchCrntCnt[ch_id]==iBatchLen[ch_id]-1 || iBatchCrntCnt[ch_id]==(iBatchLen[ch_id]/2) || iBatchCrntCnt[ch_id]==(iBatchLen[ch_id]/2)-1)
                                            iBatchReqsArr[iBatchIdx].is_last_inIBatch = true;
                                    break;                                    
                                    default:
                                        assert(false);
                                }
                              }
                            else{
                                if( iBatchCrntCnt[ch_id]==(iBatchLen[ch_id]))
                                    iBatchReqsArr[iBatchIdx].is_last_inIBatch = true;
                                }

                            /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
                            std::stringstream stream;
                            stream << std::hex <<iBatchReqsArr[iBatchIdx].addr;
                            std::string addrHexStr( stream.str() ); 
                            prefReqTrace = prefReqTrace+" iBatch [S:"+to_string(iBatchReqsArr[iBatchIdx].metaISAStreamID)+"][C:"+to_string(iBatchReqsArr[iBatchIdx].metaISARequestorID)+"][T:"+to_string(this->clk)+"](A:"+addrHexStr+" )(B:"+ to_string(iBatchReqsArr[iBatchIdx].addr_vec[0])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[1])+" "+ to_string(iBatchReqsArr[iBatchIdx].addr_vec[2])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[3])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[4])+" "+to_string(iBatchReqsArr[iBatchIdx].addr_vec[5])+")(D:-)(H:-)(Last_iBatch:"+addrHexStr+")(is_last_in_ibatch:"+to_string(iBatchReqsArr[iBatchIdx].is_last_inIBatch)+")(lead_iBatch:"+to_string(iBatchReqsArr[iBatchIdx].lead_iBatch)+")(iBatched)\n ";
                               
                            // If enqueued in readq (i.e. rquested before iPrefetched) 
                            // remove it from rqueue or wqueue
                            int readqC = 0, iBatchRequested = 0;
                            long actualReqArrive = req->arrive;
                            uint64_t   ownerDirStreamTableKey ; bool ownerStreamFound = false ; //To check if another stream is the owner of part of the iBatch
                            if (iBatchReqsArr[iBatchIdx].addr != req->addr)
                            {
                                for (list<Request>::iterator itr = ctrls[ch_id]->readq.q.begin(); itr != ctrls[ch_id]->readq.q.end() && readqC < ctrls[ch_id]->readq.q.size(); ++itr)
                                {
                                    // Demand Request exists in readq for request in iBatch 
                                    if (itr->addr == iBatchReqsArr[iBatchIdx].addr)
                                    {
                                        if(itr->is_base_addr)
                                        {   
                                            //Used for Inter-Stream iBatch Interference
                                            ownerDirStreamTableKey = (( (uint64_t)itr->metaISAStreamID << METAISA_REQID_BITS)     +
                                                    itr->metaISARequestorID);
                                            ownerStreamFound = true; 
                                            if (print_iprefetcher_trace_verbose)
                                                printf("InterStream iBatch Interference Resolved (A:%#lx)(Owner ID:%lx)(Interf ID:%#lx)\n",itr->addr,ownerDirStreamTableKey,iDirStreamTableKey);
                                            ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].active    = true;
                                            ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].stride    = req->stride;
                                            for(int i= 0 ; i <MAX_BANKS ;i++)
                                            {
                                                ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].crntRowReqsCount[i][0]   = 0; // First Request in row buffer  ( Read  Tracking Counters)                          
                                                ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].crntRowReqsCount[i][1]   = 0; // First Request in row buffer  ( Write Tracking Counters)                          
                                            }       
                                            ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[ownerDirStreamTableKey].previBatchLastAddr =
                                                      ctrls[ch_id]->iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr ;
                                            printf("\t\tInterStream iBatch Interference Update_last_ibatch[Owner ID:%lx]=%#lx\n",ownerDirStreamTableKey,iBatchReqsArr[iBatchIdx].addr);
                                        }
                                        iBatchRequested = 1;
                                        iBatchReqsArr[iBatchIdx].do_winUpdate = true;
                                        actualReqArrive = itr->arrive;
                                        itr = ctrls[ch_id]->readq.q.erase(itr);
                                        Tot_Req_From_Readq_to_iBatch++;
                                        if (print_iprefetcher_trace_verbose)                                           
                                            printf("Issue iBatch  to Already enqueued Request (A:%#lx) (T:%ld) (Ch=%d)!\n",iBatchReqsArr[iBatchIdx].addr,clk,ch_id);
                                        if(iBatchReqsArr[iBatchIdx].is_start_inIBatch==false)
                                        {
                                            iBatchReqsArr[iBatchIdx].req_interstellarRT_type = DIRREQ_RT_TYPE::rtbw;
                                            ++ctrls[ch_id]->Tot_Req_wait_iBatch;
                                            ctrls[ch_id]->totRReq__rtbw[req->coreid]++;                                      
                                        }  
                                    }
                                    readqC++;
                                }
                            }
                            if (iBatchReqsArr[iBatchIdx].addr == req->addr)
                                iBatchRequested = 1;

                            //Only insert requests to the actq if there is a space (Remaining will be retried to be inserted every next tick)    
                            uint8_t in_actq= 0;
                            
                            if(iBatchReqsArr[iBatchIdx].addr != req->addr)
                            {
                                // (InterStellarMC) Check the channel corresponds to iBatched request

                                    
                                if (ctrls[ch_id]->actq.size()  < ctrls[ch_id]->actq.max)
                                {
                                    in_actq = 1;
                                    
                                    ctrls[ch_id]->actq.q.push_back(iBatchReqsArr[iBatchIdx]);
                                }
                                else
                                    ctrls[ch_id]->pendingActq.q.push_back(iBatchReqsArr[iBatchIdx]);
 

                                if (!ctrls[ch_id]->iBuffer[tgt_bank_id[iBatchIdx]]->find(iBufferKeyObj))
                                {
                                    // Insert Key
                                    IPreftecherQueueEntry iBatchEntry;
                                    // In Ramulator, No actual data ; GEM5 inject data in the memory interface
                                    // for (int i = 0; i < CACHE_LINE_SIZE; i++)
                                    //     iBatchEntry.data[i] = 0x0;
                                    iBatchEntry.W             = 1       ; // InValid Data (Waiting)
                                    iBatchEntry.I             = 0       ; // Corresponding CMD issued
                                    iBatchEntry.isStartSeg    = iBatchReqsArr[iBatchIdx].is_start_inIBatch;
                                    iBatchEntry.iBatchRequest = &iBatchReqsArr[iBatchIdx]; 
                                    if (iBatchRequested == 1)
                                    {
                                        iBatchEntry.R = 1;
                                        iBatchEntry.actualReqArrive = actualReqArrive;
                                    }
                                    else
                                    {
                                        
                                        iBatchEntry.R = 0; // Not requested Yet
                                    }
                                    ++ctrls[ch_id]->Tot_Req_iBatched; 
                                    //@todo , uncomment for ibuffer optimization 
                                    //if(!(iBatchRequested==1 && in_actq==1)) // If it is requested and can be inserted in actq ; don't insert it into iBuffer.
                                    {
                                        ctrls[ch_id]->iBuffer[tgt_bank_id[iBatchIdx]]->Push(std::make_pair(iBufferKeyObj, iBatchEntry));
                                    }
                                    /*if (print_iprefetcher_trace)
                                            printf(" Add entry for iBatch Addr = %lx   \n", iBatchReqsArr[iBatchIdx].addr);*/

                                }
                            }
                        }
                        
                        // iBatch Per Stream Per Core
                        int idx_per_stream_per_core = req->metaISAStreamID + ( MAX_STREAMS*req->metaISARequestorID) ; 
                        for(int ch_id=0 ; ch_id<channels ; ch_id++)
                        {
                            ctrls[ch_id]->iBatches_cnt_per_Stream[idx_per_stream_per_core] += iBatchReqsCount[ch_id];
                            //To account for InterStellarRT 
                            ctrls[ch_id]->iBatches_cnt_per_bank  [iBatcher_BG1_bank_id] += iBatchesBGarr[0][ch_id];
                            for(int bgCnt = 1 ; bgCnt<bgNumPeriBatch ; bgCnt ++)
                            {
                                ctrls[ch_id]->iBatches_cnt_per_bank  [iBatcher_BGi_bank_id[bgCnt]] += iBatchesBGarr[bgCnt][ch_id];
                            }
                        }
                        // If due to the HWP reordering , the current demand request outside the iPreftech expected range
                        int ch_idx = int(T::Level::Channel)                     ;
                        int crnt_ch  = req->addr_vec[ch_idx]  ;   
 
                        
                        if(this->enableHwpAwareiBatch)
                            if( iBatchReqsCount[crnt_ch] > 0 && regularStr  && (isDemandIniBatchRange== FUTURE))
                            //if( iBatchReqsCount > 0 && regularStr && this->enableHwpAwareiBatch && (isDemandIniBatchRange==FUTURE))
                            {

                                // if req->addr stride not in the stream stride granularity (Remove the request - don't respond to it !)
                                // @todo You may want to proceed with past requets 
                                // if(isDemandIniBatchRange==FUTURE)  -> erase from actq and put in readq
                                //  else -> respond 

                                //if(isDemandIniBatchRange==PAST &&  ((req->addr-issuingiBatchAddr)% iPrefetchStep)!=0) // In Past and not in iPrefetch Granularity 
                                /*
                                if(isDemandIniBatchRange==PAST) // &&  ((req->addr-issuingiBatchAddr)% iPrefetchStep)!=0) // In Past and not in iPrefetch Granularity 
                                {
                                    printf(" iBatch Past Request:: A:%#lx\n" , req->addr);
                                    //queue->q.erase(req);
                                    // Past request should be ignored
                                    // It is out of stride granularity and generated by wrong speculation from HWP
                                    // So we can respond with Invalid data (To avoid make Cache stall) but this won't affect the program critical path
                                    //printf("updat e_serving _requests for Past Req A:%#lx\n",req->addr);

                                    channel->update _serving _requests(req->addr_vec.data(), -1, clk);
                                    req->callback(*req,iBatcher_bank_id);
                                    actq.q.push_back(req);
                                }
                                else
                                */
                                {
                                    /* Debug_Detailed
                                    if (print_iprefetcher_trace)
                                        printf(" iBatch Future Request:: A:%#lx\n",req->addr);
                                    */
                                    // If not in readq , put it back there
                                    ++Tot_Req_deferred_In_Future_iBatch;
                                    if (print_iprefetcher_trace_verbose)
                                        printf(" iBatch [S:%d][C:%d][T:%ld](A:%#lx )(B:%d %d %d %d %d %d)(D:%d)(H:%d)(Last_iBatch:%#lx)(lead_iBatch:%d)(|iBuff[%d]|=%d)(Future Request ",req->metaISAStreamID,req->metaISARequestorID,this->clk,req->addr,
                                            req->addr_vec[0], req->addr_vec[1], req->addr_vec[2], req->addr_vec[3], req->addr_vec[4], req->addr_vec[5]
                                            ,req->is_demand,req->is_hwp, iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr ,req->lead_iBatch,
                                            bank_id , iBuffer[bank_id]->getLength());
                                    // Here we are checking for current channel = current demand request 
                                    list<Request>::iterator isReqInReadq = std::find(readq.q.begin(), readq.q.end(), req);
                                    if (isReqInReadq ==readq.q.end())
                                    {

                                        if (print_iprefetcher_trace_verbose)
                                            printf("-> Back into readq");                                        
                                        readq.q.push_back(*req);
                                        // If in actq , remove it
                                        list<Request>::iterator isReqInActq = std::find(actq.q.begin(),actq.q.end(), req);
                                        if (isReqInActq != actq.q.end())
                                            actq.q.erase(req);
                                    }
                                    if (print_iprefetcher_trace_verbose)
                                        printf(")(N:%d)\n",iBatchReqsCount);                                        

                                }

                                if (print_iprefetcher_trace_verbose)
                                    if(!prefReqTrace.empty())
                                        printf("%s",prefReqTrace.c_str());
                                ret = true;
                                return;
                            }
                        
                        if(this->enableHwpAwareiBatch &&  regularStr&&(isDemandIniBatchRange== PAST))
                        {
                            if (print_iprefetcher_trace_verbose)
                               printf(" iBatch [S:%d][C:%d][T:%ld](A:%#lx )(B:%d %d %d %d %d %d)(D:%d)(H:%d)(Last_iBatch:%#lx)(lead_iBatch:%d)(|iBuff[%d]|=%d)(Back Request/Cont.)\n ",req->metaISAStreamID,req->metaISARequestorID,this->clk,req->addr,
                                 req->addr_vec[0], req->addr_vec[1], req->addr_vec[2], req->addr_vec[3], req->addr_vec[4], req->addr_vec[5]
                                 , req->is_demand , req->is_hwp , iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr ,req->lead_iBatch,
                                 bank_id , iBuffer[bank_id]->getLength());

                        }
                        if(iBatchReqsCount[crnt_ch] > 0)
                        {    
                            // Issuing the batch is from current channel , no need for ++ctrls[ch_id]->Tot_Req_issue_iBatch
                            ++Tot_Req_issue_iBatch;
                            if(forceRT_batch_mode)
                            {
                                // Notify the schduler that a batch is started and the stream+requestor ID (i.e. the batch ID)
                                scheduler->setCrntBatchStatus(true,req->metaISAStreamID,req->metaISARequestorID);
                                this->rtBatchStarted = true; 
                            }
                        }
                        // Special Scenario , if a request doesn't ibatch at all = means all requests in iBatch are fetched before -> last iBatch address must be advanced otherwise it will stuck
                        if(  iBatchReqsCount==0 && isDemandIniBatchRange != PAST )
                        {
                            // iBatchReqsCount ==0 Means No Batching , So no need for ctrls[ch_id]->
                            iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr =  lastOldAddr ; 
                        }
                        string BatchMsg =  (iBatchReqsCount[crnt_ch] > 0)?"Issue Batch":"Can't Batch" ;
                        BatchMsg       += SpecialBatchMsg                                ;
                        if (print_iprefetcher_trace_verbose)
                        {
                            printf(" iBatch [S:%d][C:%d][T:%ld](A:%#lx )(B:%d %d %d %d %d %d)(D:%d)(H:%d)(Last_iBatch:%#lx)(lead_iBatch:%d)(is_last_in_ibatch:%d)(|iBuff[%d]|=%d)(%s)(N:%d)\n ",req->metaISAStreamID,req->metaISARequestorID,this->clk,req->addr,
                                req->addr_vec[0], req->addr_vec[1], req->addr_vec[2], req->addr_vec[3], req->addr_vec[4], req->addr_vec[5]
                                , req->is_demand , req->is_hwp , iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr, req->lead_iBatch, req->is_last_inIBatch
                                , bank_id , iBuffer[bank_id]->getLength(),BatchMsg.c_str(),iBatchReqsCount[crnt_ch] );
                            if(!prefReqTrace.empty())
                                printf("%s",prefReqTrace.c_str());
                        }
                    }
                }
            }

            
            
            /************ Intelligent  Prefetcher Logic End  ********************/
            /***********vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*********/
    }


        // ipp
        void update_misses_sliding_window(list<Request>::iterator req,typename T::Command cmd)
        {
            int bank_id = req->addr_vec[int(T::Level::Bank)];
            if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                bank_id += req->addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];                                  
            
            // Updating the Policy Window Tracking should happen with RD,WR,RDA,WRA not with all commands
            if (channel->spec->is_accessing(cmd))
            {

                                   
                if (  isConfigIPP)
                {
                    
                    uint64_t metaisa_dir_winkey        ;//Direct Stream Window Counter Map's Key
                    uint64_t metaisa_window_counter_key;//Indirect Stream Window Counter Map's Key

                    // Apply window Update in the following cases:
                    // 1- Not iBatch issued 
                    // 2- iBatch issued but Request arrive early before put iPrefetch is issued
                    if (req->metaISAStreamType == DIR_STREAM)
                    {
                        /************************************************************************/
                        /********  Add Entry to DirSTreamBuffer (if not exist) O.W Update Entry */
                        uint64_t iDirStreamTableKey = ( ( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                                                        req->metaISARequestorID);

                        if (iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey) == iDirStreamBuffer->IDirStreamTable.end())
                        {
                                iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].stride                    = req->stride;
                                for(int i = 0 ; i< MAX_BANKS ; i++)
                                {
                                       iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[i][0]    = 0        ; // First Request in row buffer
                                       iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[i][1]    = 0        ; // First Request in row buffer
                                }
                                // If req is direct stream not found in iDirStreamBuffer (FIrst Demand Not come yet), then it means that in HWP Aware it can't start 
                                iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr        = req->addr;
                                if(print_ipp_logic_trace)
                                    printf("At req A:%#lx the crntRowReqsCount for all banks is reset to 0 ! \n",req->addr);
                        }
                        else
                        {
                                vector<int> dirStrPrevAddr =
                                    iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].lastAddrVec[bank_id];
                                vector<int> dirStrPrevAddrPerType =
                                    iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].lastAddrVecPerType[bank_id][int(req->type)];

                                uint8_t isCrntDirMiss = 0;
                                if (dirStrPrevAddrPerType.size() != 0) // First time (it is empty)
                                {
                                    // If Same bank and different rows -> Conflict
                                    // better than comparing row groups
                                    auto begin = req->addr_vec.begin();
                                    vector<int> rowgroup(begin, begin + int(T::Level::Row));
                                    auto begin2 = dirStrPrevAddrPerType.begin();
                                    vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row));
                                    int r = int(T::Level::Row);
                                    if ((rowgroup != rowgroup2) || (rowgroup == rowgroup2 && req->addr_vec[r] != dirStrPrevAddrPerType[r]))
                                        isCrntDirMiss = 1;
                                    //printf("row1 = %d - row2 = %d\n", req->addr_vec[r], dirStrPrevAddr[r]);
                                }

                                if (isCrntDirMiss==0)
                                    // //if Hit , increment counter
                                    (iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[bank_id][int(req->type)])+=1;
                                else // otherwise reset to 1
                                    iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[bank_id][int(req->type)] = 1;

                                /* Debug_Detailed
                                if (dirStrPrevAddr.size() != 0)
                                    printf("isCrntDirMiss =%d (Req A:%#lX) (Prev A Vec:%5d %5d %5d %5d %5d %5d ) [%d][%d] crntRowReqsCount[%d] = %d\n",
                                    isCrntDirMiss , req->addr ,                            
                                     dirStrPrevAddr[0],dirStrPrevAddr[1],dirStrPrevAddr[2],dirStrPrevAddr[3],dirStrPrevAddr[4],dirStrPrevAddr[5],
                                     req->metaISAStreamID, req->metaISARequestorID,bank_id, iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[bank_id]);
                                else 
                                    printf("(Req A:%#lX)  crntRowReqsCount [%d] =%d\n",req->addr,bank_id, iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[bank_id]);

                                */

                        }
                        iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].lastAddrVec[bank_id]                        = req->addr_vec;
                        iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].lastAddrVecPerType[bank_id][int(req->type)] = req->addr_vec;

                    }

                    if(!req->is_iprefetched || req->do_winUpdate )
                    {

                        if (req->metaISAStreamType == INDIR_STREAM || req->metaISAStreamType == PTR_CHASE)
                        {

                            /**************************************************************/
                            /*^^^^^^^^^^^^^  Cumulative Counter Method   ^^^^^^^^^^^^^^^^^*/
                            /*long metaisa_counter_key = ((req->metaISAStreamID<<METAISA_REQID_BITS)+req->metaISARequestorID);
                            if(metaisa_counter.find( metaisa_counter_key ) != metaisa_counter.end())
                            {
                                //Initalize The counter
                                metaisa_counter[metaisa_counter_key] = 0 ;
                            }

                            if(print_ipp_logic_trace)
                                printf("\t\tRamulator :: Counter[%ld] = %lld\n",metaisa_counter_key,metaisa_counter[metaisa_counter_key]);
                            //Increment or Decrement the counter
                            if (is_row_hit(req))
                            {
                                metaisa_counter[metaisa_counter_key]++;
                            }
                            else
                            {
                                metaisa_counter[metaisa_counter_key]--;
                            }
                            if(print_ipp_logic_trace)
                                printf("\t\tRamulator- IPP(InDir)[Single Threhold Strategy]: R(%s),C(%s) addr = 0x%lx \n",ramulator::request_name[int(req->type)].c_str(),channel->spec->command_name[int(cmd)].c_str(),req->addr);
                            */
                            /**************************************************************/
                            /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

                            /**************************************************************/
                            /*^^^^^^^^^^^^^  Window Based Counter        ^^^^^^^^^^^^^^^^^*/

                            //
                            int win_index = WINDOW_TYPE::IRREG_WINDOW ;
                            metaisa_window_counter_key = (
                            ((uint64_t)bank_id             << METAISA_REQID_STRID_BITS) +
                            ((uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)      + 
                            req->metaISARequestorID);
                            // If not exist , create it

                            if (metaisa_window_counter[win_index].find(metaisa_window_counter_key) == metaisa_window_counter[win_index].end())
                            {
                                // Initalize The counter
                                metaisa_window_counter[win_index][metaisa_window_counter_key] = new Window_Info;
                                metaisa_window_counter[win_index][metaisa_window_counter_key]->crnt_window_length = 0;
                                metaisa_window_counter[win_index][metaisa_window_counter_key]->num_misses = 0;
                                metaisa_window_counter[win_index][metaisa_window_counter_key]->misses_shift_reg = bitset<UPPER_WIN_SIZE>(0);
                            }

                            uint8_t crntWinLen = metaisa_window_counter[win_index][metaisa_window_counter_key]->crnt_window_length;

                            // Update the crntWinLen
                            if (crntWinLen < Irregular_Window_Size)
                                metaisa_window_counter[win_index][metaisa_window_counter_key]->crnt_window_length++;

                            // metaisa_window_counter[win_index][metaisa_window_counter_key]->misses_shift_reg<<1;
                            uint8_t isCrntMiss = 0;
                            // Increment or Decrement the counter
                            if (prev_addr_vec[bank_id].size() != 0) // First time (it is empty)
                            {
                                // If Same bank and different rows -> Conflict
                                // better than comparing row groups
                                auto begin = req->addr_vec.begin();
                                vector<int> rowgroup(begin, begin + int(T::Level::Row)  );
                                auto begin2 = prev_addr_vec[bank_id].begin();
                                vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row)  );
                                int r = int(T::Level::Row);
                                if( (rowgroup!=rowgroup2) ||(rowgroup == rowgroup2  && req->addr_vec[r]!=prev_addr_vec[bank_id][r] ))
                                    isCrntMiss = 1;
                            }

                            // Fast GEM5 Sliding Window
                            if (crntWinLen == Irregular_Window_Size )
                            {
                                    metaisa_window_counter[win_index][metaisa_window_counter_key]->misses_shift_reg >>= 1;
                                    metaisa_window_counter[win_index][metaisa_window_counter_key]->misses_shift_reg[Irregular_Window_Size-1] =
                                        isCrntMiss;
                            }
                            else 
                                metaisa_window_counter[win_index][metaisa_window_counter_key]->misses_shift_reg[crntWinLen] =
                                    isCrntMiss;
                            metaisa_window_counter[win_index][metaisa_window_counter_key]->num_misses =
                                metaisa_window_counter[win_index][metaisa_window_counter_key]->misses_shift_reg.count();

                            /* Debug_Detailed
                            if (print_ipp_logic_trace)
                            {
                                cout << "\t\tRamulator :: Window Counter["
                                    << std::hex << metaisa_window_counter_key
                                    << "] : Addr = " << std::hex << req->addr
                                    << "- Bank ID: = " << std::hex << bank_id
                                    << " - PrevAddr = (";
                                for (auto i : prev_addr_vec[bank_id])
                                    std::cout << i << ' ';
                                cout << ") - Misses = " << std::dec << metaisa_window_counter[win_index][metaisa_window_counter_key]->num_misses
                                    << " - Window Length = " << (int)crntWinLen << " - Window : ";
                                for (std::size_t i = 0; i < crntWinLen; ++i)
                                    std::cout << metaisa_window_counter[win_index][metaisa_window_counter_key]->misses_shift_reg[i] << ' ';
                                cout << "\n";

                                if (print_ipp_logic_trace)
                                    printf("\t\tRamulator- IPP(InDir)[Window(%lx)Strategy]: R(%s),C(%s) addr = 0x%lx \n",
                                        metaisa_window_counter_key, ramulator::request_name[int(req->type)].c_str(), channel->spec->command_name[int(cmd)].c_str(), req->addr);
                            }

                            */
                            /**************************************************************/
                            /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
                        }


                        /**************************************************************/
                        /*^^^^^^^^^^^^^  Window Based Counter        ^^^^^^^^^^^^^^^^^*/
                        if (req->metaISAStreamType == DIR_STREAM && Regular_Window_Size!=0)
                        {
                            /************  Dir Stream Buffer Table Done *************************/


                            /**************************************************************/
                            /*^^^^^^^^^^^^^  Window Based Counter        ^^^^^^^^^^^^^^^^^*/
                            int win_index = WINDOW_TYPE::REG_WINDOW ;

                            // If not exist , create it
                            metaisa_dir_winkey = ( 
                                ( (uint64_t)bank_id              << METAISA_REQID_STRID_BITS)+
                                ( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS) 
                                + req->metaISARequestorID);

                            if (metaisa_window_counter[win_index].find(metaisa_dir_winkey) == metaisa_window_counter[win_index].end())
                            {
                                // Initalize The counter
                                metaisa_window_counter[win_index][metaisa_dir_winkey] = new Window_Info;
                                metaisa_window_counter[win_index][metaisa_dir_winkey]->crnt_window_length = 0;
                                metaisa_window_counter[win_index][metaisa_dir_winkey]->num_misses = 0;
                                metaisa_window_counter[win_index][metaisa_dir_winkey]->misses_shift_reg = bitset<UPPER_WIN_SIZE>(0);
                            }

                            uint8_t crntDirWinLen = metaisa_window_counter[win_index][metaisa_dir_winkey]->crnt_window_length;

                            // Update the crntDirWinLen
                            if (crntDirWinLen < Regular_Window_Size )
                                metaisa_window_counter[win_index][metaisa_dir_winkey]->crnt_window_length++;


                            // metaisa_window_counter[win_index][metaisa_dir_winkey]->misses_shift_reg<<1;
                            uint8_t isCrntMiss = 0;
                            // Increment or Decrement the counter
                            if (prev_addr_vec[bank_id].size() != 0) // First time (it is empty)
                            {
                                // If Same bank and different rows -> Conflict
                                // better than comparing row groups
                                auto begin = req->addr_vec.begin();
                                vector<int> rowgroup(begin, begin + int(T::Level::Row)  );
                                auto begin2 = prev_addr_vec[bank_id].begin();
                                vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row)  );
                                int r = int(T::Level::Row);
                                if (print_ipp_logic_trace)
                                {
                                    printf("\tDir CrntAddr=%lx (%d %d %d %d %d %d) , PrevAddr=  (%d %d %d %d %d %d)\n",
                                            req->addr,req->addr_vec[0],req->addr_vec[1],req->addr_vec[2],req->addr_vec[3],req->addr_vec[4],req->addr_vec[5],
                                            prev_addr_vec[bank_id][0],prev_addr_vec[bank_id][1],prev_addr_vec[bank_id][2],prev_addr_vec[bank_id][3],prev_addr_vec[bank_id][4],prev_addr_vec[bank_id][5]);
                                }

                                if( (rowgroup!=rowgroup2) ||(rowgroup == rowgroup2  && req->addr_vec[r]!=prev_addr_vec[bank_id][r] ))                                
                                    isCrntMiss = 1;
                            }

                            // Fast GEM5 Sliding Window
                            if (crntDirWinLen == Regular_Window_Size  )
                            {    
                                metaisa_window_counter[win_index][metaisa_dir_winkey]->misses_shift_reg >>= 1;
                                metaisa_window_counter[win_index][metaisa_dir_winkey]->misses_shift_reg[Regular_Window_Size-1] =
                                    isCrntMiss;        
                            }
                            else 
                               metaisa_window_counter[win_index][metaisa_dir_winkey]->misses_shift_reg[crntDirWinLen] =
                                    isCrntMiss;
                            metaisa_window_counter[win_index][metaisa_dir_winkey]->num_misses =
                                metaisa_window_counter[win_index][metaisa_dir_winkey]->misses_shift_reg.count();

                            /*  Debug_Detailed
                            if (print_ipp_logic_trace)
                            {
                                cout << "\t\tRamulator :: DirStream Window Counter["
                                    << std::hex << metaisa_dir_winkey
                                    << "] : Addr = " << std::hex << req->addr
                                    << "- Bank ID: = " << std::hex << bank_id
                                    << " - PrevAddr = (";
                                for (auto i : prev_addr_vec[bank_id])
                                    std::cout << i << ' ';
                                cout << " )- Misses = " << std::dec << metaisa_window_counter[win_index][metaisa_dir_winkey]->num_misses
                                    << " - Window Length = " << (int)crntDirWinLen << " - Window : ";
                                for (std::size_t i = 0; i < crntDirWinLen; ++i)
                                    std::cout << metaisa_window_counter[win_index][metaisa_dir_winkey]->misses_shift_reg[i] << ' ';
                                cout << "\n";

                                printf("\t\tRamulator- IPP(Dir)[Window(%lx)Strategy]: R(%s),C(%s) addr = 0x%lx \n",
                                    metaisa_dir_winkey, ramulator::request_name[int(req->type)].c_str(), channel->spec->command_name[int(cmd)].c_str(), req->addr);
                            }
                            */
                            /**************************************************************/
                            /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
                    }

                        if (req->metaISAStreamType == NONE       && NoStream_Window_Size!=0)
                        {
                            /**************************************************************/
                            /*^^^^^^^^^^^^^  Window Based Counter        ^^^^^^^^^^^^^^^^^*/

                            // metaisa_window_counter[win_index]

                            //@TODO : Replace NONE_STR_ID by req->metaISAStreamID if None stream can be assigned different IDs
                            uint64_t metaisa_none_winkey = (
                                ((uint64_t)bank_id      << METAISA_REQID_STRID_BITS) +
                                ((uint64_t)NONE_STR_ID  << METAISA_REQID_BITS)       + 
                                req->metaISARequestorID);
                            // If not exist , create it
                            int win_index = WINDOW_TYPE::NONE_WINDOW ;
                            if (metaisa_window_counter[win_index].find(metaisa_none_winkey) == metaisa_window_counter[win_index].end())
                            {
                                // Initalize The counter
                                metaisa_window_counter[win_index][metaisa_none_winkey] = new Window_Info;
                                metaisa_window_counter[win_index][metaisa_none_winkey]->crnt_window_length = 0;
                                metaisa_window_counter[win_index][metaisa_none_winkey]->num_misses = 0;
                                metaisa_window_counter[win_index][metaisa_none_winkey]->misses_shift_reg = bitset<UPPER_WIN_SIZE>(0);
                            }

                            uint8_t crntNoneWinLen = metaisa_window_counter[win_index][metaisa_none_winkey]->crnt_window_length;

                            // Update the crntDirWinLen
                            if (crntNoneWinLen < NoStream_Window_Size)
                                metaisa_window_counter[win_index][metaisa_none_winkey]->crnt_window_length++;
                            uint8_t isCrntMiss = 0;
                            if (prev_addr_vec[bank_id].size() != 0) // First time (it is empty)
                            {
                                // Increment or Decrement the counter
                                // If Same bank and different rows -> Conflict
                                // better than comparing row groups
                                auto begin = req->addr_vec.begin();
                                vector<int> rowgroup(begin, begin + int(T::Level::Row)  );
                                auto begin2 = prev_addr_vec[bank_id].begin();
                                vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row)  );
                                int r = int(T::Level::Row);
                                if( (rowgroup!=rowgroup2) ||(rowgroup == rowgroup2  && req->addr_vec[r]!=prev_addr_vec[bank_id][r] ))                                
                                    isCrntMiss = 1;
                            }

                            // Fast GEM5 Sliding Window
                            if (crntNoneWinLen == NoStream_Window_Size )
                            {
                                metaisa_window_counter[win_index][metaisa_none_winkey]->misses_shift_reg >>= 1;
                                metaisa_window_counter[win_index][metaisa_none_winkey]->misses_shift_reg[NoStream_Window_Size-1] =
                                        isCrntMiss;
                            }
                            else 
                                metaisa_window_counter[win_index][metaisa_none_winkey]->misses_shift_reg[crntNoneWinLen] =
                                    isCrntMiss;
                            metaisa_window_counter[win_index][metaisa_none_winkey]->num_misses =
                                metaisa_window_counter[win_index][metaisa_none_winkey]->misses_shift_reg.count();

                            /* Debug Detialed 
                            if (print_ipp_logic_trace)
                            {
                                cout << "\t\tRamulator :: None Stream Window Counter["
                                    << std::hex << metaisa_none_winkey
                                    << "] : Addr = "   << std::hex << req->addr
                                    << "- Bank ID: = " << std::hex << bank_id
                                    << " - PrevAddr = (";
                                for (auto i : prev_addr_vec[bank_id])
                                    std::cout << i << ' ';
                                cout << ") - Misses = " << std::dec << metaisa_window_counter[win_index][metaisa_none_winkey]->num_misses
                                    << " - Window Length = " << (int)crntNoneWinLen << " - Window : ";
                                for (std::size_t i = 0; i < crntNoneWinLen; ++i)
                                    std::cout << metaisa_window_counter[win_index][metaisa_none_winkey]->misses_shift_reg[i] << ' ';
                                cout << "\n";

                                printf("\t\tRamulator- IPP(None)[Window(%lx)Strategy]: R(%s),C(%s) addr = 0x%lx \n",
                                    metaisa_none_winkey, ramulator::request_name[int(req->type)].c_str(), channel->spec->command_name[int(cmd)].c_str(), req->addr);
                            }
                            */

                            /**************************************************************/
                            /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
                        }
                    }
                
                

                }
                else if (  isAdaptive && Unified_Window_Size)
                {
                    uint64_t unified_winkey = (bank_id  );
                    if (adaptive_Policy_Counter.find(unified_winkey) == adaptive_Policy_Counter.end())
                    {
                        // Initalize The counter
                        adaptive_Policy_Counter[unified_winkey] = new Unified_Counter_Info;
                        adaptive_Policy_Counter[unified_winkey]->crnt_window_length = 0;
                        adaptive_Policy_Counter[unified_winkey]->num_misses = 0;
                        adaptive_Policy_Counter[unified_winkey]->misses_shift_reg = bitset<UPPER_WIN_SIZE>(0);
                    }
                    uint8_t crntUnifiedWinLen = adaptive_Policy_Counter[unified_winkey]->crnt_window_length;

                    // Update the crntDirWinLen
                    if (crntUnifiedWinLen < Unified_Window_Size )
                        adaptive_Policy_Counter[unified_winkey]->crnt_window_length++;
                    uint8_t isCrntMiss = 0;
                    if (prev_addr_vec[bank_id].size() != 0) // First time (it is empty)
                    {
                                // Increment or Decrement the counter
                                // If Same bank and different rows -> Conflict
                                // better than comparing row groups
                                auto begin = req->addr_vec.begin();
                                vector<int> rowgroup(begin, begin + int(T::Level::Row)  );
                                auto begin2 = prev_addr_vec[bank_id].begin();
                                vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row)  );
                                int r = int(T::Level::Row);
                                if( (rowgroup!=rowgroup2) ||(rowgroup == rowgroup2  && req->addr_vec[r]!=prev_addr_vec[bank_id][r] ))                                
                                    isCrntMiss = 1;
                    }

                    // Fast GEM5 Sliding Window
                    if (crntUnifiedWinLen == Unified_Window_Size )
                    {
                         adaptive_Policy_Counter[unified_winkey]->misses_shift_reg >>= 1;
                         adaptive_Policy_Counter[unified_winkey]->misses_shift_reg[Unified_Window_Size-1] =isCrntMiss;
                    }
                    else
                        adaptive_Policy_Counter[unified_winkey]->misses_shift_reg[crntUnifiedWinLen] =
                                isCrntMiss;
                    adaptive_Policy_Counter[unified_winkey]->num_misses =
                                adaptive_Policy_Counter[unified_winkey]->misses_shift_reg.count();

                    /* Debug_Detailed
                    if (print_adaptive_logic_trace)
                    {
                        cout << "\t\tRamulator :: None Stream Window Counter["
                            << std::hex << unified_winkey
                            << "] : Addr = " << std::hex << req->addr
                            << "- Bank ID: = " << std::hex << bank_id
                            << " - PrevAddr = (";
                        for (auto i : prev_addr_vec[bank_id])
                             std::cout << i << ' ';
                        cout << ") - Misses = " << std::dec << adaptive_Policy_Counter[unified_winkey]->num_misses
                            << " - Window Length = " << (int)crntUnifiedWinLen << " - Window : ";
                        for (std::size_t i = 0; i < crntUnifiedWinLen; ++i)
                            std::cout << adaptive_Policy_Counter[unified_winkey]->misses_shift_reg[i] << ' ';
                        cout << "\n";

                        printf("\t\tRamulator-COTS [Window(%lx)Strategy]: R(%s),C(%s) addr = 0x%lx \n",
                                    unified_winkey, ramulator::request_name[int(req->type)].c_str(), channel->spec->command_name[int(cmd)].c_str(), req->addr);
                    }
                    */

                }
            
                prev_addr_vec[bank_id] = req->addr_vec;
            }
            
        }

        bool upgrade_prefetch_req(const Request& req) {
            assert(req.type == Request::Type::READ);

            Queue& queue = get_queue(req.type);

            // the prefetch request could be in readq, actq, or pending
            if (upgrade_prefetch_req(queue, req))
                return true;

            if (upgrade_prefetch_req(actq, req))
                return true;

            if (upgrade_prefetch_req(pending, req))
                return true;

            return false;
        } 



        void tick()
        {
            clk++;    

            //********************************************************************************************** */
            // for interstellar , check if a request in iBuffer with pendAct status can be inserted into actq
            // From iBuffer (Pending Actq Requests) to Actq  
            if (isConfigIntelligent)
            {
                while(actq.size()<actq.max && pendingActq.size()>0)
                {
                    Request  iBatchedReq = pendingActq.q.front();
                    actq.q.push_back  (iBatchedReq);
                    pendingActq.q.pop_front();
                }
            }
            //********************************************************************************************** */


			if(isQoS)
				scheduler->sched_base->Tick();
            
            req_queue_length_sum      += readq.size() + writeq.size() + pending.size();
            read_req_queue_length_sum += readq.size() + pending.size();
            write_req_queue_length_sum += writeq.size();


            /*** 1. Serve completed reads ***/
            if (pending.size())
            {
                Request &req = pending[0];
                int bank_id = req.addr_vec[int(T::Level::Bank)]                                                            ;
                if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                    bank_id += req.addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)] ;                                  
            
                //printf("clk=%ld A:%lx Arive = %d  Depart = %ld ",clk,req.addr,req.arrive,req.depart);
                if (req.depart <= clk)
                {
                    if (req.depart - req.arrive > 1)
                    { 
                        long lat                         = 0           ; // overlapping_latency 
                        long non_overlappinglat          = 0           ; // overlapping_latency 
                        long arrival_without_overlapping = last_finish_time_per_core[req.coreid] ; 
                        long demand_req_arrival          = req.arrive                            ;
                        if(isConfigiBatch)//Only do this check for intelligenet policy and ibatch enabled
                        {
                            ramulator::iBufferKeyClass iBufferKeyObj(req.addr, req.metaISAStreamID, req.metaISARequestorID);
                            if( req.is_iprefetched && (this->iBuffer[bank_id]->find(iBufferKeyObj)))
                            {
                                if((*this->iBuffer[bank_id])[iBufferKeyObj].R ==1)//requested - so waiting time is req.depart - actual LLC request arrive  
                                {    
                                    demand_req_arrival= (*this->iBuffer[bank_id])[iBufferKeyObj].actualReqArrive;
                                }
                                else //, no waiting time (hit in ibatch Buffer)
                                {
                                        demand_req_arrival = -1  ; // Demand request doesn't come yet -> it will hit in iBatch                               
                                }
                            }
                            // else 
                            // Either not iBatched , or the demand request that issued the other iBatches                       
                        }
                        if(demand_req_arrival>last_finish_time_per_core[req.coreid]) 
                            arrival_without_overlapping  =   demand_req_arrival        ; 
                        // if it is an iBatch execurtion without demand request yet 
                        // , don't update last finish time , lat = non_overlapping latency = 0  
                        if(demand_req_arrival!=-1)
                        {
                            last_finish_time_per_core[req.coreid] = req.depart               ; 
                            lat                 = req.depart - demand_req_arrival            ;    
                            non_overlappinglat  = req.depart - arrival_without_overlapping   ;   
                        }
                        // this request really accessed a row
                        //printf("clk = %ld req.addr=%lx , req.depart = %ld , req.arrive = %ld \n",clk,req.addr,req.depart,req.arrive);
                        /***************  Overlapping Latency Stats   *****************/
                        read_latency_sum                      += lat                   ;    
                        read_latency_sum_per_core[req.coreid] += lat                   ;    
                        if(lat > read_latency_max_per_core[req.coreid].value())           
                            read_latency_max_per_core[req.coreid] = lat                ;                          
                        /***************** Overlapping latency Stats ******************/
                        ResponseTimeSum[req.coreid] += non_overlappinglat                     ;
                        AllRequests_SumLat[req.coreid] += non_overlappinglat                  ;
                        if(non_overlappinglat > PerRequest_WCL[req.coreid].value())
                            PerRequest_WCL[req.coreid] =   non_overlappinglat;

                        // This includes AllRequests_SumLat_None and PerRequest_WCL_None
                        (*(pAllRequests_SumLat[(int)(req.req_interstellarRT_type)]))[req.coreid]  += non_overlappinglat;                             
                        if(non_overlappinglat> (*(pPerRequest_WCL[(int)(req.req_interstellarRT_type)]))[req.coreid].value())
                            (*(pPerRequest_WCL[(int)(req.req_interstellarRT_type)]))[req.coreid]  = non_overlappinglat;  

                        if(req.metaISAStreamType==DIR_STREAM )
                        {
                            AllRequests_SumLat_dir[req.coreid] += non_overlappinglat  ;

                            if(non_overlappinglat> PerRequest_WCL_dir[req.coreid].value())
                               PerRequest_WCL_dir[req.coreid]  =   non_overlappinglat;  
                            
                            if(req.req_interstellarRT_type==DIRREQ_RT_TYPE::rtbh)
                            {
                                    printf("error , can't have rtbh for pending request , demand musat come later");
                            } 
                        }

                        //printf("update _serving _requests for Pending Req A:%#lx\n",req.addr);
                        channel->update_serving_requests(
                           req.addr, req.addr_vec.data(), -1, clk);
                        if(print_departures)
                        {
                            string reqType = (int(req.type)==0)?"R":"W";
                            printf("\t Departure  [C:%d][S:%d]  ( A:%#16lx ) ( T:%10ld ) (V:",req.metaISARequestorID,req.metaISAStreamID  ,req.addr, clk);
                            for (int lev = 0; lev < int(T::Level::MAX); lev++)
                            {
                                if(lev!=0) 
                                    printf(",");
                                printf(" %5x ", req.addr_vec[lev]);
                            }
                            printf(") ");
                            std::string rtTypeStr = rtTypeStrMap[req.req_interstellarRT_type];
                            if(demand_req_arrival!=-1)
                                printf("(RT:%s)"    , rtTypeStr.c_str());
                            else 
                                printf("(RT:Possible Future rtbh)");

                            printf(" (Type:%s) (Lat=%ld)(WCL=%f)\n", reqType.c_str(), non_overlappinglat , (*(pPerRequest_WCL[(int)(req.req_interstellarRT_type)]))[req.coreid].value());                            

                        }
                    }
 					if(isQoS)
					{
                        //printf("Call dequeue!\n");
						scheduler->sched_base->dequeue_req(req);
					}    

                    ramulator::iBufferKeyClass iBufferKeyObj(req.addr, req.metaISAStreamID, req.metaISARequestorID);
                    bool inIPDQ =  this->iBuffer[bank_id]->find(iBufferKeyObj)   ;
                    int idx_per_stream_per_core = req.metaISAStreamID + ( MAX_STREAMS*req.metaISARequestorID) ; 

                    if (inIPDQ) 
                    {              
                         if ((*this->iBuffer[bank_id])[iBufferKeyObj].R == 1)
                         {
                                 ++iprefetches_completed_cnt_per_bank[bank_id];
                                 ++iBatches_completed_cnt_per_stream[idx_per_stream_per_core];
                         }
                    }

                    req.callback(req,bank_id);
                    pending.pop_front();
                }
            }

            /*** 2. Refresh scheduler ***/
            if (!refresh_disabled)
                refresh->tick_ref();

            /*** 3. Should we schedule writes? ***/
            if (!write_mode)
            {
                // yes -- write queue is almost full or read queue is empty
                if (writeq.size() >= int(wr_high_watermark * writeq.max))// || readq.size() == 0 )
                {
                    write_mode = true;
                    //Zero the transition checked and write batch accessed banks 
                    for(int ba_ind = 0 ; ba_ind < MAX_BANKS ;ba_ind++)
                    {
                        transition_checked[ba_ind]         = false;
                        write_batch_accessed_banks[ba_ind] = false;
                        // we were in read -> all write should be zeroed 
                        for(int stream_large_id=0 ; stream_large_id < TOT_STREAMS_CORES ;  stream_large_id++)
                        {
                            write_batch_accessed_banks_per_stream[stream_large_id] [ba_ind] = false;
                            transition_checked_per_stream        [stream_large_id] [ba_ind] = false;
                        }

                    }
                    read_to_write_transitions++;
                }
            }
            else
            {
                // no -- write queue is almost empty and read queue is not empty
                if (writeq.size() <= int(wr_low_watermark * writeq.max) && readq.size() != 0)
                {
                    write_mode = false;
                    write_to_read_transitions++;                
                    //Zero the transition checked and read batch accessed banks 
                    for(int ba_ind = 0 ; ba_ind < MAX_BANKS ;ba_ind++)
                    {
                        transition_checked[ba_ind]         = false;
                        read_batch_accessed_banks[ba_ind]  = false;
                        for(int stream_large_id=0 ; stream_large_id < TOT_STREAMS_CORES ;  stream_large_id++)
                        {
                            read_batch_accessed_banks_per_stream[stream_large_id] [ba_ind] = false;
                            transition_checked_per_stream       [stream_large_id] [ba_ind] = false;
                        }
                    }
                }
            }

            /*** 4. Find the best command to schedule, if any ***/

            //@TODO --->  Undo to  Fix the actq in interstellar hack (The hack is done for fast results)
            /*
            // In InterStellar , The highest priority is the iBuffer 
            if (isConfigIntelligent && req->type == Request::Type::READ )
            {

                // If there is any request not pushed to actq (i.e. in iBuffer and  not scheduled yet). 
                // Insert the request from iBuffer that is not scheduled into actq
                // Iterate over all banks iBuffer
                // For Faster implementation , software buffer that stores non-scheduled iBatchable FIFO requests is iterated 
                for( int iBatch_FIFO_ind = 0 ; iBatch_FIFO_ind < iBatchable_FIFO.size() ; iBatch_FIFO_ind++)
                {
                     if(actq.size()<actq.max)
                     {
                         list<Request>::iterator req  =  iBatchable_FIFO[iBatch_FIFO_ind];
                         actq.q.push_back  (req);
                     }
                 }

                // schedule them if there is any empty place in actq.
            
            }
            */


            // First check the actq (which has higher priority) to see if there
            // are requests available to service in this cycle
            Queue *queue = &actq;
            typename T::Command cmd;            
            auto req = scheduler->get_head(queue->q);

            // Debug_Detailed [REQ_READY] Trace selected actq command 
            /*if(clk==25926)
            {
                printf("ACTQ_REQ ( A:%#16lx ) (T:%ld) (Arrival:%10ld ) (V:",  req->addr,clk, req->arrive);
                for (int lev = 0; lev < int(T::Level::MAX); lev++)
                {
                                if(lev!=0) 
                                    printf(",");
                                printf(" %5x ", req->addr_vec[lev]);
                }
            }*/
            bool is_valid_req = (req != queue->q.end()) , is_valid_ready_req=false;
            if (is_valid_req)
            {
                cmd                = get_first_cmd(req);
                is_valid_ready_req = is_ready(cmd, req->addr_vec);
            }
            /*********************   Important iBatching update    ***********************/
            // Don't issue Conflict requests for the opened row buffer if the iBatch is not done yet. 
            // This works good with bank paritioning (InterStellarRT) because without bank partiotning it is not good idea to make other cores suffer because of current core iBatch |(Is it ?)
            bool  is_valid_not_ready_iBatch     = false; 
            if( is_valid_req && !is_valid_ready_req) // Valid but not ready 
                is_valid_not_ready_iBatch = req->is_iprefetched ; // and part of iBatch 
            int   coreID_not_ready_iBatch = req->coreid         ; 




            // If nothing can be scheduled from actq. check readq/writeq
            if (!is_valid_req || !is_valid_ready_req)
            {

                if(forceRT_batch_mode && this->rtBatchStarted)
                {
                    return; // if force un-interrupted rtBatch and can't issue rtBatch belong to the same rtBatch id , return
                }            
    
                queue = !write_mode ? &readq : &writeq;

                if (otherq.size())
                    queue = &otherq; // "other" requests are rare, so we give them precedence over reads/writes

                req = scheduler->get_head(queue->q);
                //Debug_Detailed [REQ_READY] Trace selected readq/writeq command
                /*if(clk==25926)
                {
                    printf("%s ",write_mode==0?"READQ_REQ":"WRITEQ_REQ"); 
                    printf("( A:%#16lx ) (T:%ld) (Arrival:%10ld ) (V:",  req->addr,clk, req->arrive);
                    for (int lev = 0; lev < int(T::Level::MAX); lev++)
                    {
                        if(lev!=0) 
                            printf(",");
                        printf(" %5x ", req->addr_vec[lev]);
                    }
                }*/
                is_valid_req = (req != queue->q.end());
                if (is_valid_req)
                {
                    cmd                = get_first_cmd(req);
                    is_valid_ready_req = is_ready(cmd, req->addr_vec);
                }
                //Debug_Detailed
                /*if(clk==25926)
                {
                    printf(") (is_valid_req:%d) (cmd:%d) (is_ready=%d)\n",is_valid_req,cmd,is_valid_ready_req);  
                }*/
            }

            //Debug_Detailed - iterate Readq , Writeq   , Actq 
            /*          
            if(clk==25926)
            {
                Queue *debugQueue[3]     = { &readq  , &writeq  , &actq  };
                string debugQueueName[3] = {"readq " , "writeq ", "actq "};
                for(int qInd = 0 ; qInd < 3 ; qInd++)
                {

                    for (auto const& itr : debugQueue[qInd]->q) 
                    {
                        printf("%s",debugQueueName[qInd].c_str());
                        printf(" ( A:%#16lx ) (T:%ld) (Arrival:%10ld ) (V:",  itr.addr,clk, itr.arrive);
                        for (int lev = 0; lev < int(T::Level::MAX); lev++)
                        {
                            if(lev!=0) 
                                printf(",");
                            printf(" %5x ", itr.addr_vec[lev]);
                        }
                        printf(") ");    
                        printf("\n");    
                    }
                    
                }
            }
            */



            // Do speculative PRE 
            if (!is_valid_req || !is_valid_ready_req) 
            {
                // we couldn't find a command to schedule -- let's try to be speculative
                //if(IPP)
                if (  (rowpolicy->type != RowPolicy<T>::Type::Adaptive))//Exclude un necassry PREs in case of IPP
                {
                    auto cmd = T::Command::PRE;
                    vector<int> victim = rowpolicy->get_victim(cmd);
                    if (!victim.empty())
                    {
                        issue_cmd(cmd, victim);
                    }

                }
                // Abotaleb : Increment the number of cycles at the end : i.e. start from cycle 0 [be consistent with GEM5 Timing]
                return; // nothing more to be done this cycle
            }

            int coreid = req->coreid;
            int bank_id = req->addr_vec[int(T::Level::Bank)];
            if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                bank_id += req->addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];                                  
            string cmd_trace_dbg_msg ="";

            // Stream Transition 
            int crnt_stream_large_id = (req->metaISAStreamID    +  req->metaISARequestorID *NONE_STR_ID  );//None_Str_ID Represents number of streams per core 
            //Count only any transitions including NONE
            //if(last_stream_id_requested[bank_id]!=NONE_STR_ID && crnt_stream_large_id!=NONE_STR_ID )
                if(last_stream_id_requested[bank_id]!=crnt_stream_large_id)
                {
                    if (!write_mode)
                    {
                        read_batch_accessed_banks_per_stream[crnt_stream_large_id] [bank_id] = false;
                    }
                    else 
                    {
                        write_batch_accessed_banks_per_stream[crnt_stream_large_id] [bank_id] = false;
                    }
                    transition_checked_per_stream       [crnt_stream_large_id] [bank_id] = false;
                }


            /*Debug_Detailed 
            if (print_ipp_logic_trace)
                printf("(T:%ld) [A:%#lx] Stream type inside controller = %d\n", clk, req->addr , req->metaISAStreamType);
            */





            uint64_t metaisa_window_counter_key;//Indirect Stream Window Counter Map's Key
            metaisa_window_counter_key = (
                            ((uint64_t)bank_id             << METAISA_REQID_STRID_BITS) +
                            ((uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)      + 
                            req->metaISARequestorID);



            /*****************   Intelligent Batching  *********************/
            // ipp
            bool isDemandIbatchInActq = false , ret = false; 
            if (isConfigIntelligent && req->type == Request::Type::READ )
            {        
                if(iBuffer[bank_id]!=NULL && !req->is_iprefetched)
                {
                    doiBatchHWPAware(req,cmd,bank_id,metaisa_window_counter_key,queue,actq,readq,isDemandIbatchInActq,ret);
                }

                if(forceRT_batch_mode)
                {
                    if(req->is_last_inIBatch==true)
                    {
                        // Notify the schduler that a batch is ended and the stream+requestor ID (i.e. the batch ID)
                        scheduler->setCrntBatchStatus(false,req->metaISAStreamID,req->metaISARequestorID);  
                        this->rtBatchStarted = false; 
                    }
                }
                if(this->memoryMapping ==MemMapping::InterStellarRT )
                {
                    // check if readq is conflict an iBatch request - in actq - opened row. for the selected ready core. 
                    /******* @todo  : this logic needs seperate iBatchq not actq , and logic to return if_hit not is_valid 
                    if(is_valid_not_ready_iBatch) 
                        if(req->coreid == coreID_not_ready_iBatch)
                        {    
                            queue->push_back(req);
                            //Don't interrupt the iBatch by a conflict request from the same core 
                            return; 
                        }
                        ************/
                    // Set request type 
                    if(req->is_first_command) // to count req once even if it consists of several commands
                    {
                        if (req->metaISAStreamType==DIR_STREAM)
                        {  
                            totRReq__dir[req->coreid]++;
                        }
                        else 
                            totRReq__none[req->coreid]++;
                    }

                }
    
            }


           

            if( req->type == Request::Type::READ )



            if(isConfigIntelligent && ret==true) // skip futre requests command generation (will close opened row)
            {
                    if (req->is_first_command)
                    {
                        {
                            if (req->type == Request::Type::READ)
                            {
                                if (is_row_hit(req))
                                {
                                    cmd_trace_dbg_msg+="(RB_Status:RD_Hit)";                           
                                }
                                else if (is_row_open(req))
                                {
                                    //Conflict Happens because of Interrupting Read Hit Batch
                                    cmd_trace_dbg_msg+="(RB_Status:RD_Conflict)";
                                    if((prev_rd_metaisa_id[bank_id] == (( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                                                        req->metaISARequestorID))       &&
                                        (prev_rd_cmd[bank_id]        == T::Command::RD || prev_rd_cmd[bank_id] == T::Command::RDA)  &&
                                        addresses_hit_same_row(coreid,prev_rd_addr[bank_id] ,req->addr,prev_rd_stream_id[bank_id],req->metaISAStreamID)  &&
                                        prev_access_type[bank_id]    == Request::Type::WRITE )
                                    {
                                        cmd_trace_dbg_msg+="[PRE_REASON:RD_Batch_Interrupted_By_Write]";
                                    }
                                }
                                else
                                {
                                    cmd_trace_dbg_msg+="(RB_Status:RD_Miss)";
                                }
                            }
                            else if (req->type == Request::Type::WRITE)
                            {
                                if (is_row_hit(req))
                                {
                                    cmd_trace_dbg_msg+="(RB_Status:WR_Hit)";                           
                                }
                                else if (is_row_open(req))
                                {
                                    cmd_trace_dbg_msg+="(RB_Status:WR_Conflict)";
                                }
                                else
                                {
                                    cmd_trace_dbg_msg+="(RB_Status:WR_Miss)";
                                }
                            }
                        }
                    }
                    else
                        cmd_trace_dbg_msg+="(is_first_time:0)";
                if (print_iprefetcher_trace_verbose)
                {
                    printf("\t Skip_FutureReq_%s ", channel->spec->command_name[int(cmd)].c_str());
                    if(isConfigIntelligent)
                        printf("[%d][%d]",req->metaISAStreamID ,req->metaISARequestorID);
                    printf(" ( A:%#16lx ) ( T:%10ld ) (V:",  req->addr, clk);
                    for (int lev = 0; lev < int(T::Level::MAX); lev++)
                    {
                        if(lev!=0) 
                            printf(",");
                        printf(" %5x ", req->addr_vec[lev]);
                    }
                    printf(") ");
                    printf("(WMode:%d)", write_mode);
                    printf("(WQ:%d)"   , writeq.size());
                    printf("(RQ:%d)"   , readq.size());  
                    if(this->enableIPrefetcher)
                        printf(" (iBatched:%d ) ",req->is_iprefetched);
                    printf("(RQ_Type:%d)%s\n",req->type,    cmd_trace_dbg_msg.c_str());
                }
                return;
            }
            /**************************************************************/
            if (req->is_first_command)
            {
                cmd_trace_dbg_msg+="(is_first_time:1)";
                req->is_first_command = false;
                if (req->type == Request::Type::READ || req->type == Request::Type::WRITE)
                {
                    //printf("update _serving_ requests for FIrst Time Req A:%#lx\n",req->addr);
                    channel->update_serving_requests(req->addr,req->addr_vec.data(), 1, clk);
                }
                int tx = (channel->spec->prefetch_size * channel->spec->channel_width / 8);
                //if(!req->is_iprefetched || req->lead_iBatch)// If Current Req is in Future iBatch , It will issue N iBatches , while first request in this iBatch won't count its conflict/hits/misses
                {
                    if (req->type == Request::Type::READ)
                    {
                        if (is_row_hit(req))
                        {
                            cmd_trace_dbg_msg+="(RB_Status:RD_Hit)";
                            prev_rd_rb_status[bank_id] = HIT ;
                            ++read_row_hits[coreid]          ;
                            ++row_hits                       ;
                            ++hits_per_bank[bank_id]         ;
                            ++rd_hits_per_bank[bank_id]      ;
                        }
                        else if (is_row_open(req))//Conflict , row is opened and current command doesn't hit , 
                        {
                            //Conflict Happens because of Interrupting Read Hit Batch
                            cmd_trace_dbg_msg+="(RB_Status:RD_Conflict)";
                            if((prev_rd_metaisa_id[bank_id] == (( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                                                req->metaISARequestorID))       &&
                                (prev_rd_cmd[bank_id]        == T::Command::RD || prev_rd_cmd[bank_id] == T::Command::RDA)  &&
                                addresses_hit_same_row(coreid,prev_rd_addr[bank_id] ,req->addr,prev_rd_stream_id[bank_id],req->metaISAStreamID)  &&
                                prev_access_type[bank_id]    == Request::Type::WRITE )
                            {
                                cmd_trace_dbg_msg+="[REASON:RD_Batch_Interrupted_By_Write]";
                                read_conflicts_by_an_interrupting_write_to_hit_batch++;
                            }
                            else
                            {
                                if((prev_rd_metaisa_id[bank_id] != (( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                                                    req->metaISARequestorID))       &&
                                    (prev_rd_cmd[bank_id]        == T::Command::RD  ) )
                                {
                                    cmd_trace_dbg_msg+="[REASON:PRE_RD_Inter_Stream]";
                                    read_conflicts_by_inter_stream++;
                                }
                                else
                                {
                                    if((prev_rd_metaisa_id[bank_id] == (( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                                                        req->metaISARequestorID))       &&
                                        (prev_rd_cmd[bank_id]        == T::Command::RD  ) )
                                    {
                                        cmd_trace_dbg_msg+="[REASON:PRE_RD_Intra_Stream]";
                                        read_conflicts_intra_stream++;
                                    }
                                    else
                                    {
                                        //Previous command neither raead or rda , and it was conflict 
                                        if( (  !(prev_rd_cmd[bank_id]        == T::Command::RD || prev_rd_cmd[bank_id] == T::Command::RDA))   &&
                                                (prev_rd_rb_status[bank_id]        == CONFLICT  ) )
                                        {
                                            cmd_trace_dbg_msg+="[REASON:PRE_RD_CONF_No_Read_CONF]"  ;
                                            read_conflict_precedded_by_no_read_conflict++           ;
                                        }
                                        else
                                        {
                                            cmd_trace_dbg_msg+="[REASON:PRE_RD_Others]";
                                            read_conflicts_by_others++;                
                                        }                        
                                    }

                                }
                            }

                            // Read Conflict Happens
                            prev_rd_rb_status[bank_id] = CONFLICT ;
                            ++read_row_conflicts[coreid]          ;
                            ++row_conflicts                       ;
                            ++conflicts_per_bank[bank_id]         ;
                            ++rd_conflicts_per_bank[bank_id]      ;
                        }
                        else //Miss , row is closed and current command doesn't hit , 
                        {
                            if(prev_cmd[bank_id]        != T::Command::PRE ) 
                            {
                                cmd_trace_dbg_msg+="(RB_Status:RD_Miss)";
                                
                                if( (prev_rd_cmd[bank_id]        == T::Command::RDA )  &&
                                    addresses_hit_same_row(coreid,prev_rd_addr[bank_id] ,req->addr,prev_rd_stream_id[bank_id],req->metaISAStreamID) 
                                )
                                {
                                    cmd_trace_dbg_msg+="[REASON:ACT_RD_Useless_RDA]";
                                    read_misses_unnecassary_by_useless_rda++;
                                }
                                else
                                {
                                    if((prev_rd_cmd[bank_id]        == T::Command::RDA )  &&
                                    !addresses_hit_same_row(coreid,prev_rd_addr[bank_id] ,req->addr,prev_rd_stream_id[bank_id],req->metaISAStreamID) )
                                    {
                                        cmd_trace_dbg_msg+="[REASON:ACT_RD_UseFul_RDA]";
                                        read_misses_by_useful_rda++;
                                    }
                                    else
                                    {
                                        cmd_trace_dbg_msg+="[REASON:ACT_RD_Others]";
                                        read_misses_by_others++;
                                    }
                                                                
                                }
                            
                                prev_rd_rb_status[bank_id] = MISS ;
                                ++read_row_misses[coreid]         ;
                                ++row_misses                      ;
                                ++misses_per_bank[bank_id]        ;
                                ++rd_misses_per_bank[bank_id]     ;

                            }
                        }
                        read_transaction_bytes += tx;
                    }
                    else if (req->type == Request::Type::WRITE)
                    {
                        if (is_row_hit(req))
                        {
                            cmd_trace_dbg_msg+="(RB_Status:WR_Hit)";                           
                            ++write_row_hits[coreid];
                            ++row_hits;
                            ++hits_per_bank[bank_id];
                            ++wr_hits_per_bank[bank_id];                            
                        }
                        else if (is_row_open(req))
                        {
                            cmd_trace_dbg_msg+="(RB_Status:WR_Conflict)";



                            ++write_row_conflicts[coreid];
                            ++row_conflicts;
                            ++conflicts_per_bank[bank_id];
                            ++wr_conflicts_per_bank[bank_id];
                        }
                        else
                        {
                            if(prev_cmd[bank_id]        != T::Command::PRE ) 
                            {
                                //Check a special type of misses that can happen here 
                                cmd_trace_dbg_msg+="(RB_Status:WR_Miss)";
                                if( (prev_wr_cmd[bank_id]        == T::Command::WRA )  &&
                                    addresses_hit_same_row(coreid,prev_wr_addr[bank_id] ,req->addr,prev_wr_stream_id[bank_id],req->metaISAStreamID) 
                                )
                                {
                                    cmd_trace_dbg_msg+="[REASON:ACT_Useless_WRA]";
                                    write_misses_unnecassary_by_useless_wra++;
                                }
                                else
                                {
                                    if((prev_wr_cmd[bank_id]        == T::Command::WRA )  &&
                                    //hahaha
                                    !addresses_hit_same_row(coreid,prev_wr_addr[bank_id] ,req->addr,prev_wr_stream_id[bank_id],req->metaISAStreamID) )
                                    {
                                        cmd_trace_dbg_msg+="[REASON:ACT_Useful_WRA]";
                                        write_misses_by_useful_wra++;
                                    }
                                    else
                                    {
                                        cmd_trace_dbg_msg+="[REASON:ACT_WR_Others]";
                                        read_misses_by_others++;
                                    }
                                }                            
                                ++write_row_misses[coreid];
                                ++row_misses;
                                ++misses_per_bank[bank_id];
                                ++wr_misses_per_bank[bank_id];                            

                            }                           
                        }
                        write_transaction_bytes += tx;
                    }
                }
            }
            else
            {
                if (req->type == Request::Type::READ)
                {
                    if(!is_row_hit(req))
                    {
                        if (is_row_open(req))//Conflict
                        {
                            cmd_trace_dbg_msg+="(RB_Status:RD_Conflict)";
                            // to know the previous non useful command , track per address needed (can be done for nearby history)
                            cmd_trace_dbg_msg+="[REASON:PRE_RD_PRECEED_NON_USEFUL_PRE/ACT]" ;
                            read_conflict_preceeded_by_non_useful_conflict_or_miss ++       ;
                            prev_rd_rb_status[bank_id] = CONFLICT ;
                            ++read_row_conflicts[coreid]          ;
                            ++row_conflicts                       ;
                            ++conflicts_per_bank[bank_id]         ;
                            ++rd_conflicts_per_bank[bank_id]      ;
                        }
                        else
                        {  //To be a miss for the second time , previous command on this bank mustn't be a PRE.
                            if(prev_cmd[bank_id]        != T::Command::PRE )    
                            {                        
                                cmd_trace_dbg_msg+="(RB_Status:RD_Miss)";
                                cmd_trace_dbg_msg+="[REASON:ACT_RD_PRECEED_NON_USEFUL_PRE/ACT]" ;
                                read_misses_preceeded_by_non_useful_conflict_or_miss++          ;
                                prev_rd_rb_status[bank_id] = MISS ;
                                ++read_row_misses[coreid]         ;
                                ++row_misses                      ;
                                ++misses_per_bank[bank_id]        ;
                                ++rd_misses_per_bank[bank_id]     ;
                            }
                        }
                    }


                }
                else if (req->type == Request::Type::WRITE)
                {

                    if(!is_row_hit(req))
                    {
                        if (is_row_open(req))//Conflict
                        {
                            cmd_trace_dbg_msg+="(RB_Status:WR_Conflict)";
                            // to know the previous non useful command , track per address needed (can be done for nearby history)
                            cmd_trace_dbg_msg+="[REASON:PRE_WR_PRECEED_NON_USEFUL_PRE/ACT]" ;
                            ++write_row_conflicts[coreid];
                            ++row_conflicts;
                            ++conflicts_per_bank[bank_id];
                            ++wr_conflicts_per_bank[bank_id];
                        }
                        else
                        {  //To be a miss for the second time , previous command on this bank mustn't be a PRE.
                            if(prev_cmd[bank_id]        != T::Command::PRE )
                            {
                                cmd_trace_dbg_msg+="(RB_Status:WR_Miss)";
                                cmd_trace_dbg_msg+="[REASON:ACT_WR_PRECEED_NON_USEFUL_PRE/ACT]" ;
                                write_misses_preceeded_by_non_useful_conflict_or_miss ++        ;
                                ++write_row_misses[coreid];
                                ++row_misses;
                                ++misses_per_bank[bank_id];
                                ++wr_misses_per_bank[bank_id];   
                            }
                        }
                    }



                }
                cmd_trace_dbg_msg+="(is_first_time:0)";

            }

            /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^**/
            /************************* Transition Stat Update   ***************************/
            // Sometimes a RD can happen in write batch (if it is put in ACTQ , so it mustn't affect the transision calculation i.e. make sure that we are in read mode)
            if(!write_mode && req->type==Request::Type::READ)
            {
                if(transition_checked[bank_id]==false)
                {
                    read_batch_accessed_banks[bank_id]   = true ;
                    transition_checked[bank_id]          = true ;
                    if(write_batch_accessed_banks[bank_id]==true)
                    {
                        write_batch_accessed_banks[bank_id] = false;
                        write_to_read_transitions_on_bank_level++                           ;
                        cmd_trace_dbg_msg+="(Transitiion[WR->RD][BA:"+to_string(bank_id)+"])";
                    }
                }
                //Count transitions including None
                //if(last_stream_id_requested[bank_id]!=-1 && last_stream_id_requested[bank_id]!=NONE_STR_ID && crnt_stream_large_id!=NONE_STR_ID )
                
                {
                    if(transition_checked_per_stream[crnt_stream_large_id][bank_id]==false)
                    {

                        if(write_batch_accessed_banks_per_stream[last_stream_id_requested[bank_id]][bank_id]==true)
                        {
                            write_batch_accessed_banks_per_stream[last_stream_id_requested[bank_id]][bank_id]=false;
                            read_stream_tot_transitions_on_bank_level++                                   ;
                            //Mode changed Only
                            if(last_stream_id_requested[bank_id]==crnt_stream_large_id)
                            {
                                write_to_read_same_stream_transitions_on_bank_level++                     ;
                                cmd_trace_dbg_msg+="(Transitiion[WR->RD(SameStream:"+to_string(crnt_stream_large_id)+")][BA:"+to_string(bank_id)+"])";
                            }
                            else//Mode and Stream changed 
                            {
                            write_to_read_diff_stream_transitions_on_bank_level++                         ;
                            cmd_trace_dbg_msg+="(Transitiion[WR->RD(DiffStream:"+to_string(crnt_stream_large_id)+"->"  
                            +to_string(last_stream_id_requested[bank_id])+")][BA:"+to_string(bank_id)+"])"                  ;
                            }
                        }           
                        //Stream changed   Only       
                        if(read_batch_accessed_banks_per_stream[last_stream_id_requested[bank_id]][bank_id]==true)
                        {
                            read_batch_accessed_banks_per_stream[last_stream_id_requested[bank_id]][bank_id]=false;
                            assert((last_stream_id_requested[bank_id]!=crnt_stream_large_id));
                            read_to_read_stream_transitions_on_bank_level++                                                                         ;
                            read_stream_tot_transitions_on_bank_level++                                                                             ;
                            cmd_trace_dbg_msg+="(Transitiion[RD->RD(DiffStream:"+to_string(crnt_stream_large_id)+"->"  
                                +to_string(last_stream_id_requested[bank_id])+")][BA:"+to_string(bank_id)+"])"                  ;                    
                            
                        }
                        transition_checked_per_stream[crnt_stream_large_id][bank_id]        = true;
                        read_batch_accessed_banks_per_stream[crnt_stream_large_id][bank_id] = true;
                                                
                    }
                }
                last_stream_id_requested[bank_id] = crnt_stream_large_id ;


            }
            else
            {
                // In write mode a read can occur (Out of the actq of previously activated row in previous write mode)
                // so we want to count the transition rd->wr when an actual write happens in teh write mode only 
                 if(write_mode && req->type==Request::Type::WRITE)
                 {
                    if(transition_checked[bank_id]==false)
                    {
                        write_batch_accessed_banks[bank_id]   = true ;
                        transition_checked[bank_id]           = true;
                        if(read_batch_accessed_banks[bank_id]==true)
                        {                         
                            read_batch_accessed_banks[bank_id]  = false;  
                            read_to_write_transitions_on_bank_level++                           ;
                            cmd_trace_dbg_msg+="(Transitiion[RD->WR][BA:"+to_string(bank_id)+"])";
                        }
                    }
                    
                    if(last_stream_id_requested[bank_id]!=-1) //&& last_stream_id_requested[bank_id]!=NONE_STR_ID && crnt_stream_large_id!=NONE_STR_ID )
                    {
                        if(transition_checked_per_stream[crnt_stream_large_id][bank_id]==false)
                        {

                            if(read_batch_accessed_banks_per_stream[last_stream_id_requested[bank_id]][bank_id]==true)
                            {
                                write_stream_tot_transitions_on_bank_level++                               ;
                                read_batch_accessed_banks_per_stream[last_stream_id_requested[bank_id]][bank_id]=false;
                                //Mode changed Only 
                                if(last_stream_id_requested[bank_id]==crnt_stream_large_id)
                                {
                                    read_to_write_same_stream_transitions_on_bank_level++                     ;
                                    cmd_trace_dbg_msg+="(Transitiion[RD->WR(SameStream:"+to_string(crnt_stream_large_id)+")][BA:"+to_string(bank_id)+"])";

                                }
                                else     //Mode and Stream changed 
                                {
                                    read_to_write_diff_stream_transitions_on_bank_level++                      ;
                                    cmd_trace_dbg_msg+="(Transitiion[RD->WR(DiffStream:"+to_string(crnt_stream_large_id)+"->"  
                                    +to_string(last_stream_id_requested[bank_id])+")][BA:"+to_string(bank_id)+"])"                  ;
                                }
                            }           
                            //Stream changed  Only       
                            if(write_batch_accessed_banks_per_stream[last_stream_id_requested[bank_id]][bank_id]==true)
                            {
                                write_batch_accessed_banks_per_stream[last_stream_id_requested[bank_id]][bank_id]=false;
                                assert((last_stream_id_requested[bank_id]!=crnt_stream_large_id));
                                write_to_write_stream_transitions_on_bank_level++                                                                         ;
                                write_stream_tot_transitions_on_bank_level++                                                                             ;
                                cmd_trace_dbg_msg+="(Transitiion[WR->WR(DiffStream:"+to_string(crnt_stream_large_id)+"->"  
                                +to_string(last_stream_id_requested[bank_id])+")][BA:"+to_string(bank_id)+"])"                  ;                    
                            }
                            transition_checked_per_stream[crnt_stream_large_id][bank_id]         = true;
                            write_batch_accessed_banks_per_stream[crnt_stream_large_id][bank_id] = true;
                        }
                    }
                    last_stream_id_requested[bank_id] = crnt_stream_large_id ;

                 }
            }
            //last_stream_id_requested[bank_id] = crnt_stream_large_id ;

            //Check stream to stream transition 

            /****************************** Transition Status Update End *****************************/
            update_misses_sliding_window(req,cmd);

            //issue_req for BLISS in case of CAS commands only 
            if (isQoS && (cmd == channel->spec->translate[int(req->type)]))
                scheduler->sched_base->issue_req(*req);

            typename T::Command cmd_AP=cmd;
            issue_cmd(cmd, req->addr_vec, req,cmd_trace_dbg_msg,cmd_AP);
            //Update the last read access address per bank 
            if(req->type == (Request::Type::READ))
            {
                prev_rd_addr       [bank_id] = req->addr             ;
                prev_rd_stream_id  [bank_id] = req->metaISAStreamID  ; 
                prev_rd_cmd        [bank_id] = cmd_AP                ; 
                prev_rd_metaisa_id [bank_id] = (( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                                                req->metaISARequestorID);
            }
            else{
                    if(req->type == (Request::Type::WRITE))
                    {
                        prev_wr_addr       [bank_id] = req->addr            ;
                        prev_wr_stream_id  [bank_id] = req->metaISAStreamID ;
                        prev_wr_cmd        [bank_id] = cmd_AP               ; 
                    }
            }
            prev_cmd[bank_id]                =  cmd_AP    ; 
            prev_access_type[bank_id]        = (req->type); 



            // check whether this is the last command (which finishes the request)
            // if (cmd != channel->spec->translate[int(req->type)]){
            // Abotaleb : add isDemandIbatchInActq condition ; to move req to actq if it is "PRE" or "ACT" and the request is first in iBatch
            if(!isDemandIbatchInActq) // isDemandIbatchInActq will be false if WRITE Request
                if (cmd != channel->spec->translate[int(req->type)]) // if cmd not  RD , WR (i.e. PRE or ACT] becuase translate returns RD,WR
                {
                    if (channel->spec->is_opening(cmd))//ACT in case DDR4
                    {
                        // promote the request that caused issuing activation to actq
                        if(queue!=&actq)
                        {
                            if(actq.size()==actq.max) // if actq is Full , Wait 
                            {
                                actq_req_queue_full_times++;
                                return;
                            }
                            actq.q.push_back(*req);
                            queue->q.erase(req);
                        }
                    }

                    return;
                }

            // set a future completion time for read requests
            // if demand is PRE or ACT , and start of iBatch don't move to demand
            if(!( isDemandIbatchInActq && cmd != channel->spec->translate[int(req->type)]))
                if (req->type == Request::Type::READ)
                {
                    req->depart = clk + channel->spec->read_latency;
                    //printf("Add to the pending req A:%lx - clk = %d , add latency = %d\n",req->addr,clk,channel->spec->read_latency);
                    pending.push_back(*req);
                }

            if (req->type == Request::Type::WRITE)
            {
                //printf("update _serving_requests for Write Req A:%#lx\n",req->addr);
                channel->update_serving_requests(req->addr,req->addr_vec.data(), -1, clk);
                // req->callback(*req);
            }

            // remove request from queue
            //Abotaleb :: important to avoid double erase 
            list<Request>::iterator isReqInQItr = std::find(queue->q.begin(), queue->q.end(), req);
            if(isReqInQItr!=queue->q.end()) 
            {               
                 queue->q.erase(req);
                 //printf("Erase from queue(%p) A:%#lx\n",&queue,req->addr);
            }

        }



        // Helper Function to the scheduler to determine if a req is in iPrefetch range 
        bool is_in_expected_iPrefteches(list<Request>::iterator req)
        {

            uint64_t issuingiBatchAddr = req->addr ;
            uint64_t iDirStreamTableKey         ;
            int bank_id = req->addr_vec[int(T::Level::Bank)];
            if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                bank_id += req->addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];                                  
            
            if(enableHwpAwareiBatch)
            {
                    iDirStreamTableKey = (
                    ( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                    req->metaISARequestorID);
                    issuingiBatchAddr = iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].previBatchLastAddr;

                    
                    uint16_t stride        = req->stride            ;   
                    int      iPrefetchStep = 0                      ;
                    iPrefetchStep = stride                          ;
                    while (iPrefetchStep < ramulator::CACHE_LINE_SIZE)
                            iPrefetchStep += stride;

                    //@todo:  Not necassary all N next requests will be iPrefeteched (they must hit)
                    if(req->addr >= issuingiBatchAddr && req->addr <=issuingiBatchAddr+iBuffer[bank_id]->preftechStreamDepth*iPrefetchStep)
                        return true; 
                    else 
                        return false; 

            }
            
            return true;

        }

        bool is_ready(list<Request>::iterator req)
        {
            bool isRT = (this->isConfigiBatch && this->memoryMapping == MemMapping::InterStellarRT &&( ( this->rtAddrMaping == ramulator::RTAddrMaping::map2) || ( this->rtAddrMaping == ramulator::RTAddrMaping::map4)));
            bool cond_isRT_ready  =  true ;
            if(isRT)
            {
                bool cond1_isRT_demand_dir_start_batch = (!req->is_iprefetched && req->metaISAStreamType == DIR_STREAM && req->is_demand ) ;                    
                if(cond1_isRT_demand_dir_start_batch)
                {
                    int  seg_num = ( this->rtAddrMaping == ramulator::RTAddrMaping::map4)?4:2;
                    //Start of other segments = Current address + i* segment length*stride.
                    int iPrefetchStep = InterStellarRT_STEP; // InterStellarRT_STEP assumes 1 stride for the whole appication = 1 cacheline 
                    int readyCause=-0 ; 
                    if(print_cmd_trace)
                        printf("Raddr: %16lx  - ",req->addr);                        
                    
                    for(int seg_idx = 1 ; seg_idx <seg_num ; seg_idx++)
                    {   
                        std::list<Request> copy_list;
                        copy_list.push_back(*req);  // Copy the element into the new list
                        std::list<Request>::iterator req_seg1 = copy_list.begin();  // Iterator pointing to the copy in the new list
                        req_seg1->addr           = req->addr + (seg_idx)*iPrefetchStep   ; 
                        req_seg1->addr_vec       = this->addr_to_addr_vec(req->coreid,req_seg1->addr,req->metaISAStreamID); 
                        req_seg1->is_iprefetched = true ; // to stop the recursion on is_ready(req_seg1); [This is not a demand request to recurse in its ready InterStellarRT logic]
                        cond_isRT_ready  = cond_isRT_ready && is_ready(req_seg1);
                        if(print_cmd_trace)
                        {
                            printf("S[%d]_addr: %16lx  ; ",seg_idx , req_seg1->addr);
                        }
                        if(cond_isRT_ready==false)
                        {
                            readyCause =    seg_idx;                    
                            break;
                        }
                    }
                    //std::string readyStatus = (cond_isRT_ready==true)?"RT_Ready":"RT-Unready";
                    if(print_cmd_trace)
                    {
                        printf("\n readyStatus:%d  [C:%d][S:%d]  ( A:%#16lx ) ( T:%10ld ) (V:",readyCause,req->metaISARequestorID,req->metaISAStreamID  ,req->addr, this->clk);
                        for (int lev = 0; lev < int(T::Level::MAX); lev++)
                        {
                            if(lev!=0) 
                                printf(",");
                            printf(" %5x ", req->addr_vec[lev]);
                        }
                        printf(")\n ");
                    }
                }
            }
            //@todo ready1 = cond1_isRT_ready[0] && this->ctrl->is_ready(req1) && this->ctrl->is_row_hit(req1);


            typename T::Command cmd = get_first_cmd(req);
            //return channel->check_iteratively(cmd, req->addr_vec.data(), clk);
            return cond_isRT_ready&&channel->check(cmd, req->addr_vec.data(), clk);
        }

        bool is_ready(typename T::Command cmd, const vector<int> &addr_vec)
        {
            //return channel->check_iteratively(cmd, addr_vec.data(), clk);
            return channel->check(cmd, addr_vec.data(), clk);
        }

        bool is_row_hit(list<Request>::iterator req)
        {

            // cmd must be decided by the request type, not the first cmd
            typename T::Command cmd = channel->spec->translate[int(req->type)];
            return  channel->check_row_hit(cmd, req->addr_vec.data());
        }

        bool is_row_hit(typename T::Command cmd, const vector<int> &addr_vec)
        {
            return channel->check_row_hit(cmd, addr_vec.data());
        }

        bool addresses_hit_same_row(int coreid, long req1_addr,long req2_addr,int stream_id_1,int stream_id_2)
        {
            vector<int>  req1_addr_vec = addr_to_addr_vec(coreid,req1_addr,stream_id_1);
            vector<int>  req2_addr_vec = addr_to_addr_vec(coreid,req2_addr,stream_id_2);

             auto beginReq1   = req1_addr_vec.begin();
             vector<int> rowgroup1(beginReq1, beginReq1 + int(T::Level::Row));
             auto beginReq2   = req2_addr_vec.begin();
             vector<int> rowgroup2(beginReq2, beginReq2 + int(T::Level::Row));
             int r = int(T::Level::Row);
             if(rowgroup1 == rowgroup2 && req1_addr_vec[r] == req2_addr_vec[r])
                return true;
            return false;

        }



        bool is_row_open(list<Request>::iterator req)
        {
            // cmd must be decided by the request type, not the first cmd
            typename T::Command cmd = channel->spec->translate[int(req->type)];
            return channel->check_row_open(cmd, req->addr_vec.data());
        }

        bool is_row_open(typename T::Command cmd, const vector<int> &addr_vec)
        {
            return channel->check_row_open(cmd, addr_vec.data());
        }

        void update_temp(ALDRAM::Temp current_temperature)
        {
        }

        // For telling whether this channel is busying in processing read or write
        bool is_active()
        {
            return (channel->cur_serving_requests > 0);
        }

        // For telling whether this channel is under refresh
        bool is_refresh()
        {
            return clk <= channel->end_of_refreshing;
        }

        void set_high_writeq_watermark(const float watermark)
        {
            wr_high_watermark = watermark;
        }

        void set_low_writeq_watermark(const float watermark)
        {
            wr_low_watermark = watermark;
        }

        void record_core(int coreid)
        {
#ifndef INTEGRATED_WITH_GEM5
            record_read_hits[coreid] = read_row_hits[coreid];
            record_read_misses[coreid] = read_row_misses[coreid];
            record_read_conflicts[coreid] = read_row_conflicts[coreid];
            record_write_hits[coreid] = write_row_hits[coreid];
            record_write_misses[coreid] = write_row_misses[coreid];
            record_write_conflicts[coreid] = write_row_conflicts[coreid];
#endif
        }


        void clear_lower_bits(long& addr, int bits)
        {
            addr >>= bits;
        }
        void set_addr_bits(vector<int> addr_bits_in)
        {
            this->addr_bits = addr_bits_in;
        }

        void set_tx_bits(int tx_bits_in)
        {
            this->tx_bits = tx_bits_in;
        }
        void set_interstellarRT_mapping_req(long partition_mask,int partbits,int shift_bits_ba_bg)
        {
            this->partition_mask    = partition_mask;
            this->partbits          = partbits;
            this->shift_bits_ba_bg  = shift_bits_ba_bg;
        }
        int slice_lower_bits(long& addr, int bits)
        {
            int lbits = addr & ((1<<bits) - 1);
            addr >>= bits;
            return lbits;
        }
        
        vector<int> addr_to_addr_vec(int coreid, long addr_in,int metaISAStreamID)
        {
            int interleaveBGbits = 0 ; 
            vector<int>  addr_vec;
            int mapped_channel;
            clear_lower_bits(addr_in, this->tx_bits);
            addr_vec.resize(addr_bits.size())     ;  
            switch(int(this->memoryMapping)){
                case int(MemMapping::ChRaBaRoCo):
                {
                    for (int i = addr_bits.size() - 1; i >= 0; i--)
                        addr_vec[i] = slice_lower_bits(addr_in, addr_bits[i]);
                    break;
                }
                case int(MemMapping::RoBaRaCoCh):
                {
                       addr_vec[0] = slice_lower_bits(addr_in, addr_bits[0]);
                       addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr_in, addr_bits[addr_bits.size() - 1]);
                       for (int i = 1; i <= int(T::Level::Row); i++)
                           addr_vec[i] = slice_lower_bits(addr_in, addr_bits[i]);
                    break;
                }
                //InterStellar InterStreamMultiChannelSched (ISMS)
                case int(MemMapping::RoCoBaRaCh):
                {
                    //string DDR4::level_str [int(Level::MAX)] = {"Ch", "Ra", "Bg", "Ba", "Ro", "Co"};
                    //Least bits represents the highest parallelism possible [Should be baseline of Streaming]
                    addr_vec[int(T::Level::Channel)] = slice_lower_bits(addr_in,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                    addr_vec[int(T::Level::Rank)]    = slice_lower_bits(addr_in,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                    addr_vec[2] = slice_lower_bits(addr_in,  addr_bits[2] ) ;   // Bank Group
                    addr_vec[3] = slice_lower_bits(addr_in,  addr_bits[3] ) ;   // Bank 
                    addr_vec[5] = slice_lower_bits(addr_in,  addr_bits[5] ) ;   // Column 
                    addr_vec[int(T::Level::Row)]     = slice_lower_bits(addr_in,  addr_bits[int(T::Level::Row)] ) ;   // Row 
                    break;
                }
                case int(MemMapping::RoBaBg1CoBg0RaCh):
                {

                    addr_vec[int(T::Level::Channel)] =  slice_lower_bits(addr_in,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                    addr_vec[int(T::Level::Rank)]    =  slice_lower_bits(addr_in,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                    addr_vec[2]                      =  slice_lower_bits(addr_in,  1 )                                 ;   // Bank Group0 BG0
                    addr_vec[int(T::Level::Column)]  =  slice_lower_bits(addr_in,  addr_bits[int(T::Level::Column)])   ;  //Column                  
                    addr_vec[2]                      += (slice_lower_bits(addr_in,  addr_bits[2]-1)<<1)                ;   // Remaining Bank Groups 
                    addr_vec[3]                      =  slice_lower_bits(addr_in,  addr_bits[3]  )                     ;     // Bank bits 
                    addr_vec[int(T::Level::Row)]     =  slice_lower_bits(addr_in,  addr_bits[int(T::Level::Row)] )     ;   // Row                     
                    break;
                }
                case int(MemMapping::StrmPartRoBaBg1CoBg0RaCh):
                {

                    addr_vec[int(T::Level::Channel)] =  slice_lower_bits(addr_in,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                    addr_vec[int(T::Level::Rank)]    =  slice_lower_bits(addr_in,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                    addr_vec[2]                      =  slice_lower_bits(addr_in,  1 )                                 ;   // Bank Group0 BG0
                    addr_vec[int(T::Level::Column)]  =  slice_lower_bits(addr_in,  addr_bits[int(T::Level::Column)])   ;  //Column                  
                    // Do Bank Partitioning based on stream ID
                    //@todo  Better bank utilization if number of streams less than BA+BG0 bits                     
                    addr_vec[2]                      +=  ((metaISAStreamID&0x1)<<1)  ; 
                    addr_vec[3]                      =  slice_lower_bits(addr_in,  addr_bits[3]  )                     ;     // Bank bits 
                    //@todo make it generic to support any number of banks (in DDR4 they are 4)
                    addr_vec[3] &= ((metaISAStreamID>>1)&0x3) ; 
                    addr_vec[int(T::Level::Row)]     =  slice_lower_bits(addr_in,  addr_bits[int(T::Level::Row)] )     ;   // Row                     
                    break;
                }
                case int(MemMapping::InterStreamChRoCoBaRa):
                {
                    addr_vec[int(T::Level::Rank)]    = slice_lower_bits(addr_in,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                    addr_vec[2] = slice_lower_bits(addr_in,  addr_bits[2] ) ;   // Bank Group
                    addr_vec[3] = slice_lower_bits(addr_in,  addr_bits[3] ) ;   // Bank 
                    addr_vec[5] = slice_lower_bits(addr_in,  addr_bits[5] ) ;   // Column 
                    addr_vec[int(T::Level::Row)]     = slice_lower_bits(addr_in,  addr_bits[int(T::Level::Row)] ) ;   // Row 
                    mapped_channel = metaISAStreamID%this->channels;
                    addr_vec[0] =  slice_lower_bits(addr_in,mapped_channel);
                    break;
                }
                case int(MemMapping::InterStellarRT)://Special Address Mapping BA1-0 BG1 ROW COL BG0 Burst
                {
                    //overwrite MSB addr                 
                        addr_in  &= partition_mask; 
                        addr_in  |= (coreid<<(shift_bits_ba_bg-partbits)); 
                        addr_vec[0] = slice_lower_bits(addr_in,  addr_bits[0] ) ;  // Channel 
                        addr_vec[1] = slice_lower_bits(addr_in,  addr_bits[1] ) ;   // Rank
                        if(rtAddrMaping==RTAddrMaping::map2)//interleave pages if InterStellar Design 
                            interleaveBGbits = 1 ; 
                        if(rtAddrMaping==RTAddrMaping::map4)//interleave pages if InterStellar Design 
                            interleaveBGbits = 2 ;                             
                        addr_vec[2] = slice_lower_bits(addr_in,  interleaveBGbits   ) ;  // Least bit of Bang Group 0  
                        // Do Paritioning. 
                        addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr_in, addr_bits[addr_bits.size() - 1]);  //Column                  
                        addr_vec[2] += (slice_lower_bits(addr_in,  addr_bits[2]-interleaveBGbits)<<interleaveBGbits) ;   // Remaining Bank Groups 
                        addr_vec[3] += slice_lower_bits(addr_in,  addr_bits[3]  ) ;   // Bank bits 
                        addr_vec[4]  = slice_lower_bits(addr_in,  addr_bits[4]  ) ;   // Row
                        break;    
                }
                default:
                    assert(false);
            }
            return addr_vec;
        }
    private:
        typename T::Command get_first_cmd(list<Request>::iterator req)
        {
            typename T::Command cmd = channel->spec->translate[int(req->type)];
            return channel->decode(cmd, req->addr_vec.data());
        }

        void cmd_issue_autoprecharge(typename T::Command &cmd,
                                     const vector<int> &addr_vec)
        {

            // currently, autoprecharge is only used with closed row policy
            if (channel->spec->is_accessing(cmd) && rowpolicy->type == RowPolicy<T>::Type::ClosedAP)
            {
                // check if it is the last request to the opened row
                Queue *queue = write_mode ? &writeq : &readq;

                auto begin = addr_vec.begin();
                vector<int> rowgroup(begin, begin + int(T::Level::Row) + 1);

                int num_row_hits = 0;

                for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr)
                {
                    if (is_row_hit(itr))
                    {
                        auto begin2 = itr->addr_vec.begin();
                        vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                        if (rowgroup == rowgroup2)
                            num_row_hits++;
                    }
                }

                if (num_row_hits == 0)
                {
                    Queue *queue = &actq;
                    for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr)
                    {
                        if (is_row_hit(itr))
                        {
                            auto begin2 = itr->addr_vec.begin();
                            vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                            if (rowgroup == rowgroup2)
                                num_row_hits++;
                        }
                    }
                }

                assert(num_row_hits > 0); // The current request should be a hit,
                                          // so there should be at least one request
                                          // that hits in the current open row
                if (num_row_hits == 1)
                {
                    if (cmd == T::Command::RD)
                        cmd = T::Command::RDA;
                    else if (cmd == T::Command::WR)
                        cmd = T::Command::WRA;
                    else if (!(cmd == T::Command::WRA || cmd == T::Command::RDA))
                        assert(false && "Unimplemented command type.");
                }
            }
        }

        // upgrade to an autoprecharge command
        // return number of misses within the Sliding misses window (COTS/IPP)
        int  cmd_issue_autoprecharge(typename T::Command &cmd,
                                     const vector<int> &addr_vec,
                                     list<Request>::iterator req,
                                     uint64_t & reqWinID        ,
                                     CLOSE_CAUSE &policy_cause  )

        {
            vector<int> next_addr_vec = get_next_addr_vec(cmd, req);
            policy_cause = NOT_CLOSED;
            /* Condition to apply Direct stream IPP logic is to have next expected miss address */
            bool applyIPPLogicDir = false;
            bool applyIPPLogicInDir = false;
            bool applyIPPLogicNone = false;
            int numMisses = -1 ; 
            applyIPPLogicDir = (req->metaISAStreamType == DIR_STREAM);
            applyIPPLogicInDir = (req->metaISAStreamType == INDIR_STREAM) || (req->metaISAStreamType == PTR_CHASE);
            applyIPPLogicNone = (req->metaISAStreamType == NONE);

            bool isAdaptive   = rowpolicy->type == RowPolicy<T>::Type::Adaptive;
            bool isTrueClosed = rowpolicy->type == RowPolicy<T>::Type::TrueClosed;
            bool isIPPInDir   = (static_cast<int>(rowpolicy->ipp_type) & RowPolicy<T>::IPP_Type::IPP_InDir)    ;
            bool isIPPNone    = (static_cast<int>(rowpolicy->ipp_type) & RowPolicy<T>::IPP_Type::IPP_NoStream) ;
            int  coreid       = req->coreid ; 
            // printf("isConfigIPP=%d\n",isConfigIPP);

            
            //If Inteillgent prefetched requests sent - only apply policy with the last request
            //if(actq.size()>1)//There are other requests to the activated row buffer
            //    return;
            
            int bank_id = req->addr_vec[int(T::Level::Bank)];
            if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                bank_id += req->addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
    
            if (channel->spec->is_accessing(cmd) && rowpolicy->type == RowPolicy<T>::Type::ClosedAP)
            {
                // check if it is the last request to the opened row
                Queue *queue = write_mode ? &writeq : &readq;

                auto begin = req->addr_vec.begin();
                vector<int> rowgroup(begin, begin + int(T::Level::Row) + 1);

                int num_row_hits = 0;

                for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr)
                {
                    if (is_row_hit(itr))
                    {
                        auto begin2 = itr->addr_vec.begin();
                        vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                        if (rowgroup == rowgroup2)
                            num_row_hits++;
                    }
                }

                if (num_row_hits == 0)
                {
                    Queue *queue = &actq;
                    for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr)
                    {
                        if (is_row_hit(itr))
                        {
                            auto begin2 = itr->addr_vec.begin();
                            vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                            if (rowgroup == rowgroup2)
                                num_row_hits++;
                        }
                    }
                }

                assert(num_row_hits > 0); // The current request should be a hit,
                                          // so there should be at least one request
                                          // that hits in the current open row
                if (num_row_hits == 1)
                {
                    if (cmd == T::Command::RD)
                        cmd = T::Command::RDA;
                    else if (cmd == T::Command::WR)
                        cmd = T::Command::WRA;
                    else if (!(cmd == T::Command::WRA || cmd == T::Command::RDA))
                        assert(false && "Unimplemented command type.");
                }
            }

            if (channel->spec->is_accessing(cmd) && isConfigIPP && req->type == Request::Type::WRITE) //&& req->type == Request::Type::WRITE -> IPP_AP Do it with Write or Read
            {
                // check if it is the last request to the opened row
                /*
                Sometimes it happens that write_mode = 0 and req is write because it is scheduled from actq not writeq (example: when writeq size is exactly <0.2* max and all previous commands related to write)
                assert(write_mode==1);
                Queue *queue = &writeq ;
                */
                // check if it is the last request to the opened row
                Queue *queue = write_mode ? &writeq : &readq;


                auto begin = req->addr_vec.begin();
                vector<int> rowgroup(begin, begin + int(T::Level::Row) + 1);

                int num_row_hits = 0;

                for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr)
                {
                    // Debug_Detailed IPP_AP
                    /*
                    printf("APQueueCheck ");
                    printf(" ( A:%#16lx ) ( T:%10ld ) (V:",  itr->addr, clk);
                    for (int lev = 0; lev < int(T::Level::MAX); lev++)
                    {
                        if(lev!=0) 
                            printf(",");
                        printf(" %5x ", itr->addr_vec[lev]);
                    }
                    // Debug_Detailed IPP_AP
                    printf(") ");
                    */
                    if (is_row_hit(itr))
                    {
                        auto begin2 = itr->addr_vec.begin();
                        vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                        if (rowgroup == rowgroup2)
                        {    
                            num_row_hits++;
                        }
                    }
                    // Debug_Detailed IPP_AP
                    // printf("(Hits=%d)\n",num_row_hits);

                }

               // if (num_row_hits == 0)
                {
                    Queue *queue = &actq;
                    
                    for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr)
                    {
                        // Debug_Detailed IPP_AP
                        /*
                        printf("APActqCheck ");
                        printf(" ( A:%#16lx ) ( T:%10ld ) (V:",  itr->addr, clk);
                        for (int lev = 0; lev < int(T::Level::MAX); lev++)
                        {
                            if(lev!=0) 
                                printf(",");
                            printf(" %5x ", itr->addr_vec[lev]);
                        }
                        printf(") ");
                        */
                        if (is_row_hit(itr))
                        {
                            auto begin2 = itr->addr_vec.begin();
                            vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                            if (rowgroup == rowgroup2)
                                num_row_hits++;
                        }
                        // Debug_Detailed IPP_AP
                        // printf("(Hits=%d)\n",num_row_hits);
                    }
                }

                assert(num_row_hits > 0); // The current request should be a hit,
                                          // so there should be at least one request
                                          // that hits in the current open row
                if (num_row_hits == 1)
                {
                    if (cmd == T::Command::RD)
                        cmd = T::Command::RDA;
                    else if (cmd == T::Command::WR)
                        cmd = T::Command::WRA;
                    else if (!(cmd == T::Command::WRA || cmd == T::Command::RDA))
                        assert(false && "Unimplemented command type.");
                }
            }            
            if (channel->spec->is_accessing(cmd) && isConfigIPP  && req->type == Request::Type::READ)
            {
                //if (print_ipp_logic_trace)
                //    printf("Start Checking If to execute IPP Logic\n");

                uint64_t metaisa_sliding_window_key= ( 
                    ( (uint64_t)bank_id              << METAISA_REQID_STRID_BITS)+
                    ( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS) 
                      + req->metaISARequestorID);
                reqWinID = metaisa_sliding_window_key;
                int win_index ;
 
                /*^^^^^^^^^^^^^ If row policy is IPP_Dir or All_IPP ^^^^^^^^^^^^^^^*/
                if ((static_cast<int>(rowpolicy->ipp_type) & RowPolicy<T>::IPP_Type::IPP_Dir) && applyIPPLogicDir)
                {
                    win_index = WINDOW_TYPE::REG_WINDOW;                    
                    if (metaisa_window_counter[win_index].find(metaisa_sliding_window_key) == metaisa_window_counter[win_index].end())
                    {
                                // Initalize The counter
                                metaisa_window_counter[win_index][metaisa_sliding_window_key] = new Window_Info;
                                metaisa_window_counter[win_index][metaisa_sliding_window_key]->crnt_window_length = 0;
                                metaisa_window_counter[win_index][metaisa_sliding_window_key]->num_misses = 0;
                                metaisa_window_counter[win_index][metaisa_sliding_window_key]->misses_shift_reg = bitset<UPPER_WIN_SIZE>(0);
                    }
                    numMisses = metaisa_window_counter[win_index][metaisa_sliding_window_key]->num_misses;

                    /* Hybrid Policy:
                    1- If next LLC expected address causes a miss , then close the page
                    2- If a regular pattern can be found , follow it 
                    3- else:
                       Apply window based policy
                    */
                    //\*************  Abotaleb:  Debugging Messages  : *****************\/


                    //\***************************************************************************\/
                    int policy1WorkOnCount = this->dirStr_CntEn; //true ; 
                    // IPP
                    if(policy1WorkOnCount==1)
                    {
                        /* Update policy based on count of hits in the opened row buffer*/
                        uint64_t iDirStreamTableKey = (
                            ( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS) 
                                + req->metaISARequestorID);
                        if(iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey)!=iDirStreamBuffer->IDirStreamTable.end())
                        {
                               uint16_t   crntReqStride   = iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].stride            ;
                               uint16_t crntRowReqsCount = iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[bank_id][int(req->type)]  ; 
                                //@todo compute row buffer size
					            while (crntReqStride < ramulator::CACHE_LINE_SIZE)
						                crntReqStride += iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].stride ; 
                                 // ippbg - aware              
                                int bank_div_factor = 1 ;  
                                if(this->memoryMapping == MemMapping::RoBaBg1CoBg0RaCh || this->memoryMapping == MemMapping::StrmPartRoBaBg1CoBg0RaCh)                         
                                {   
                                    //@todo make it more generic for any DRAM device 
                                    //if(channel->spec->standard_name == "DDR4" )
                                    bank_div_factor = 2;
                                }
                                else 
                                {
                                    if(this->memoryMapping == MemMapping::RoCoBaRaCh)                         
                                    {   
                                        //@todo make it more generic for any DRAM device 
                                        //if(channel->spec->standard_name == "DDR4" )
                                        bank_div_factor = 16;
                                    }
                                }
                                if(crntRowReqsCount==((ROW_BUFFER_SIZE*bank_div_factor)/crntReqStride) 
                                                || crntReqStride>=(ROW_BUFFER_SIZE*bank_div_factor))
                                { 
                                    policy_cause = COUNT_REACHED;
                                    rda_count_reached++; 
                                }

                         }


                    }
                    else 
                    {
                        /*
                        if(print_cmd_trace&IPP)
                        {
                            if(addr_vec.size()==6)
                                 printf("\t\t Crnt Addr => Ch = %x , Ra=%x , BG=%x, Ba=%x, Ro=%x, Co=%x\n",addr_vec[0],addr_vec[1],addr_vec[2],addr_vec[3],addr_vec[4],addr_vec[5]);
                            if(next_addr_vec.size()==6)
                                printf("\t\t Next Addr => Ch = %x , Ra=%x , BG=%x, Ba=%x, Ro=%x, Co=%x\n",next_addr_vec[0],next_addr_vec[1],next_addr_vec[2],next_addr_vec[3],next_addr_vec[4],next_addr_vec[5]);
                        }
                        */

                        auto begin = addr_vec.begin();
                        vector<int> rowgroup(begin, begin + int(T::Level::Row) );
                        auto begin2 = next_addr_vec.begin();
                        vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) );
                        int r = int(T::Level::Row);

                        //\**************  Abotaleb : Debugging Messages for rowgroup *****************\/
                        /* Debug_Detailed
                        if (print_ipp_logic_trace)
                        {
                            printf("Ramulator-IPP-Logic : Compare rowgroups:\n");
                            // printf("\trowgroup 1 = ");
                            for (const auto &value : rowgroup)
                                printf(" %d ", value);
                            printf(" %d\n",addr_vec[r]);
                            // printf("\trowgroup 2 = ");
                            for (const auto &value : rowgroup2)
                                printf(" %d ", value);
                            printf(" %d\n",next_addr_vec[r]);
                        }
                        */
                        //\***************************************************************************\/

                        //Miss when same "Ch,Ra,BG,Ba" but different rows!
                        //old condition was wrong rowgroup != rowgroup2 [rowgroup extended to row]
                        //cout<<"r value :"<<r<<endl; 
                        if(rowgroup == rowgroup2 && addr_vec[r]!=next_addr_vec[r])
                        {
                            policy_cause=NEXT_ADDR_CONFLICT ;
                            rda_nxt_addr_conflict++         ;        
                        }
                        if(rowgroup != rowgroup2)
                        {
                            policy_cause=NEXT_ADDR_CONFLICT ;
                            rda_nxt_addr_conflict++         ;
                        }
                            
                    }

                                            
                    if(policy_cause==NOT_CLOSED)
                    {

                            // If below threshold , this means large misses -> close the row
                            //if (print_ipp_logic_trace)
                            //    printf("Policy 2 : Addr = %lx Bank ID =%x key=%lx - \n",req->addr ,    bank_id, metaisa_sliding_window_key);
                            
                            // req->metaISAStreamID 
                            if(IPP_Last_Policy.find(metaisa_sliding_window_key)==IPP_Last_Policy.end())
                            {
                                IPP_Last_Policy[metaisa_sliding_window_key] = OPEN_POLICY;
                            }
                            if(Regular_Window_Size!=0)//Regular_Window_Size = 0 Disables the Window , But STill IPP can be applied from the next address
                            {
                                    // Apply WaterMark
                                    bool WaterMark_Cond_H =   ((float)numMisses  >=
                                    metaisa_dir_window_threshold * (float) metaisa_window_counter[win_index][metaisa_sliding_window_key]->crnt_window_length  );
                                    bool WaterMark_Cond_H_L = (
                                        ((float)numMisses <
                                        metaisa_dir_window_threshold * (float) metaisa_window_counter[win_index][metaisa_sliding_window_key]->crnt_window_length  )
                                            &&
                                        ((float) numMisses        
                                            >=
                                            metaisa_dir_window_threshold_Low * (float) metaisa_window_counter[win_index][metaisa_sliding_window_key]->crnt_window_length  ));
                                    bool  WaterMark_Cond_H_L_CL = WaterMark_Cond_H_L &&   (IPP_Last_Policy[metaisa_sliding_window_key]==CLOSE_POLICY);
                                    //printf("Dir_IPP Watermark = [U:%d,G:%d] , numMisses = %d - Prev Policy = %d\n",WaterMark_Cond_H,WaterMark_Cond_H_L,numMisses, IPP_Last_Policy[metaisa_sliding_window_key]);                
                                    IPP_Last_Policy[metaisa_sliding_window_key]=OPEN_POLICY;
                                    
                                    bool dir_ipp_cond;
                                    if(enable_ipp_dir_watermark)
                                        dir_ipp_cond = WaterMark_Cond_H || WaterMark_Cond_H_L_CL;
                                    else 
                                        dir_ipp_cond = WaterMark_Cond_H;

                                    if(dir_ipp_cond )                 
                                    {
                                        policy_cause = WaterMark_Cond_H  ? MISSES_ABOVE_HIGH : GRAY_PREV_CLOSED;
                                        if(WaterMark_Cond_H)
                                            rda_misses_above_high++  ;
                                        else
                                            rda_gray_prev_closed ++  ;

                                        IPP_Last_Policy[metaisa_sliding_window_key]=CLOSE_POLICY;
                                    }

                            }

                       
                    }// end of 2nd strategy for direct stream
                    

                    //printf("IPP_Decision A:%#lx - policy_cause = %d\n",req->addr,policy_cause);
                    if (policy_cause!=NOT_CLOSED)
                    {
                            if (cmd == T::Command::RD)
                            {
                                cmd = T::Command::RDA;
                            }
                            else if (cmd == T::Command::WR)
                            {
                                cmd = T::Command::WRA;
                            }
                            else if (!(cmd == T::Command::WRA || cmd == T::Command::RDA))
                                assert(false && "Unimplemented command type.");
                        }

                 }

                /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
                if ( (isIPPInDir && applyIPPLogicInDir) || (isIPPNone && applyIPPLogicNone) )
                {
                          win_index      = applyIPPLogicInDir  ? WINDOW_TYPE::IRREG_WINDOW          : WINDOW_TYPE::NONE_WINDOW          ;
                    float threshold_high = applyIPPLogicInDir  ? metaisa_indir_window_threshold     : metaisa_none_window_threshold     ;
                    float threshold_low  = applyIPPLogicInDir  ? metaisa_indir_window_threshold_Low : metaisa_none_window_threshold_Low ;
                    int   winSize        = applyIPPLogicInDir  ? Irregular_Window_Size              : NoStream_Window_Size              ;     
                    int  enableWaterMark = applyIPPLogicInDir  ? enable_ipp_indir_watermark          : enable_ipp_none_watermark;
                    if (metaisa_window_counter[win_index].find(metaisa_sliding_window_key) == metaisa_window_counter[win_index].end())
                    {
                                // Initalize The counter
                                metaisa_window_counter[win_index][metaisa_sliding_window_key] = new Window_Info;
                                metaisa_window_counter[win_index][metaisa_sliding_window_key]->crnt_window_length = 0;
                                metaisa_window_counter[win_index][metaisa_sliding_window_key]->num_misses = 0;
                                metaisa_window_counter[win_index][metaisa_sliding_window_key]->misses_shift_reg = bitset<UPPER_WIN_SIZE>(0);
                    }
                    numMisses = metaisa_window_counter[win_index][metaisa_sliding_window_key]->num_misses;
                    if(IPP_Last_Policy.find(metaisa_sliding_window_key)==IPP_Last_Policy.end())
                    {
                                IPP_Last_Policy[metaisa_sliding_window_key] = OPEN_POLICY;
                    }

                    if (winSize!=0)
                    {

                        // Apply WaterMark
                        bool WaterMark_Cond_H =   ((float)numMisses  >=
                        threshold_high * (float) metaisa_window_counter[win_index][metaisa_sliding_window_key]->crnt_window_length  );
                        bool WaterMark_Cond_H_L = (
                            ((float)numMisses <
                            threshold_high * (float) metaisa_window_counter[win_index][metaisa_sliding_window_key]->crnt_window_length  )
                                &&
                            ((float) numMisses        
                                >=
                                threshold_low * (float) metaisa_window_counter[win_index][metaisa_sliding_window_key]->crnt_window_length  ));
                        bool  WaterMark_Cond_H_L_CL = WaterMark_Cond_H_L &&   (IPP_Last_Policy[metaisa_sliding_window_key]==CLOSE_POLICY);
                        //printf("Dir_IPP Watermark = [U:%d,G:%d] , numMisses = %d - Prev Policy = %d\n",WaterMark_Cond_H,WaterMark_Cond_H_L,numMisses, IPP_Last_Policy[metaisa_sliding_window_key]);                
                        IPP_Last_Policy[metaisa_sliding_window_key]=OPEN_POLICY;
                        
                        bool ipp_cond;
                        if(enableWaterMark)
                            ipp_cond = WaterMark_Cond_H || WaterMark_Cond_H_L_CL;
                        else 
                            ipp_cond = WaterMark_Cond_H;
                        if(ipp_cond )                 
                        {
                            policy_cause = WaterMark_Cond_H  ? MISSES_ABOVE_HIGH : GRAY_PREV_CLOSED;
                            if(WaterMark_Cond_H)
                                rda_misses_above_high++  ;
                            else
                                rda_gray_prev_closed++   ;
                            
                            IPP_Last_Policy[metaisa_sliding_window_key]=CLOSE_POLICY;

                                if (cmd == T::Command::RD)
                                {
                                    cmd = T::Command::RDA;
                                }
                                else if (cmd == T::Command::WR)
                                {
                                    cmd = T::Command::WRA;
                                }
                                else if (!(cmd == T::Command::WRA || cmd == T::Command::RDA))
                                    assert(false && "Unimplemented command type.");
                        }

                        }


                    }
                
                /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
                if (metaisa_window_counter[win_index].find(metaisa_sliding_window_key) != metaisa_window_counter[win_index].end())
                    numMisses = metaisa_window_counter[win_index][metaisa_sliding_window_key]->num_misses;
                        
            }
            else if (channel->spec->is_accessing(cmd) && isAdaptive)
            {
                uint64_t unified_winkey = (bank_id  );
                reqWinID                =  bank_id   ;
                numMisses = adaptive_Policy_Counter[unified_winkey]->num_misses;
                // Apply WaterMark
                bool WaterMark_Cond_H =   ((float)numMisses  >=
                unified_window_threshold * (float) adaptive_Policy_Counter[unified_winkey]->crnt_window_length  );
                bool WaterMark_Cond_H_L = (
                    ((float)numMisses <
                      unified_window_threshold * (float) adaptive_Policy_Counter[unified_winkey]->crnt_window_length  )
                        &&
                    ((float) adaptive_Policy_Counter[unified_winkey]->num_misses        
                         >=
                        unified_window_threshold_Low * (float) adaptive_Policy_Counter[unified_winkey]->crnt_window_length  ));
                bool  WaterMark_Cond_H_L_CL = WaterMark_Cond_H_L &&   (COTS_Last_Policy[unified_winkey]==CLOSE_POLICY);
                //printf("Unified Watermark = [U:%d,G:%d] , numMisses = %d - Prev Policy = %d\n",WaterMark_Cond_H,WaterMark_Cond_H_L,numMisses, COTS_Last_Policy[unified_winkey]);                
                COTS_Last_Policy[unified_winkey]=OPEN_POLICY;
                
                bool unified_Cond;
                if(enable_unified_watermark)
                    unified_Cond = WaterMark_Cond_H || WaterMark_Cond_H_L_CL;
                else 
                    unified_Cond = WaterMark_Cond_H;

                if(unified_Cond )                 
                {
                    policy_cause = WaterMark_Cond_H  ? MISSES_ABOVE_HIGH : GRAY_PREV_CLOSED;
                    if(WaterMark_Cond_H)
                        rda_misses_above_high++  ;
                    else
                        rda_gray_prev_closed++   ;                    
                    
                    COTS_Last_Policy[unified_winkey]=CLOSE_POLICY;
                    if (cmd == T::Command::RD)
                    {
                        cmd = T::Command::RDA;
                    }
                    else if (cmd == T::Command::WR)
                        {
                            cmd = T::Command::WRA;
                        }
                    else if (!(cmd == T::Command::WRA || cmd == T::Command::RDA))
                            assert(false && "Unimplemented command type.");
                }                 
            }
            else if (channel->spec->is_accessing(cmd) && isTrueClosed)
            {
                        if (cmd == T::Command::RD)
                        {
                            cmd = T::Command::RDA;   
                        }
                        else if (cmd == T::Command::WR)
                        {
                            cmd = T::Command::WRA;
                        }
                        policy_cause = TRUE_CLOSED;
            } 

             

            /************        Inter-Stream IPP+iBatch ************************/
            // Phase 2 
            if (channel->spec->is_accessing(cmd)  && isConfigiBatch )
            {
                if(req->is_last_inIBatch)//That means this is read request 
                {
                    //assert(  req->type == Request::Type::READ )                     ;
                    iBufferKeyClass reqKey ( req->addr , req->metaISAStreamID , req->metaISARequestorID) ;
                    bool inIPDQ =  this->iBuffer[bank_id]->find(reqKey)   ;
                    if(inIPDQ) 
                    {
                        (*this->iBuffer[bank_id])[reqKey].I = 1  ;
                    }
                    /*
                    // if not in iBatch , This means that it has been overwritten 
                    else 
                    {
                        printf("iBatched req is overwritten!\n");
                    }
                    */
                   
                    if (isConfigIPP )
                    {
                        //Check if there exist a different stream in the iBatch access the same bank 
                        //If there is a request in iBatch 
                        //isNotCMDIssuedOfSameStreamIDExists return true if a request with the same stream ID exists in iBatch but has no CMD issued yet 
                        //Check if no next access for current stream in the iBatch Buffer: 
                        bool cond1 = iBuffer[bank_id]->isNotCMDIssuedOfSameStreamIDExists(reqKey);
                        //Check if no next access for current stream in the read queue: 
                        bool cond2 = false;
                        for (auto itr = readq.q.begin(); itr != readq.q.end(); ++itr)
                        {
                            if( itr->metaISAStreamID    == req->metaISAStreamID &&
                                itr->metaISARequestorID == req->metaISARequestorID)
                            {
                                cond2 = true; 
                            } 
                        }                        
                        //Blindless Close the page if no more requests related to the same stream
                        // This deisison will be rectified if next stream ID can be found 
                        if(cond1==false && cond2 ==false)
                        {
                            cmd = T::Command::RDA       ;
                            rda_inter_stream++          ;
                            policy_cause = INTER_STREAM ;  
                        }                        
                        // Next Stream ID Targeting the current req bank (?)
                        //Phase_2 : for direct stream only till now
                        if((this->enableInterStreamKnowledge==1) && req->metaISAStreamType==DIR_STREAM)
                        {
                            printf("[Knowledge] ") ; 
                            int iNxtStreamStep = 0                                  ;
                            uint16_t nxtStreamStride                                ;
                            uint64_t iDirStreamTableKey,iDirNextStreamTableKey      ;
                            uint64_t issuingiBatchAddr                              ;
                            Request   nextStreamRequest                             ;

                            iDirStreamTableKey = (( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS)     +
                                        req->metaISARequestorID);
                            uint8_t  nextStreamID   =  iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].nextMetaISAStreamID; 
                            printf(" (nSID=%d) ",nextStreamID);
                            iDirNextStreamTableKey = (( (uint64_t)nextStreamID<< METAISA_REQID_BITS)     +
                                        req->metaISARequestorID);//Must be of the same requestor 

                            //If the next Stream is already added to the stream table 
                            if(iDirStreamBuffer->IDirStreamTable.find(iDirNextStreamTableKey)!=iDirStreamBuffer->IDirStreamTable.end())
                            {
                                nxtStreamStride = iDirStreamBuffer->IDirStreamTable[iDirNextStreamTableKey].stride;
                                iNxtStreamStep = nxtStreamStride;// if nxtStreamStrideis zero -> infinite loop
                                if(nxtStreamStride>0)
                                    while (iNxtStreamStep < ramulator::CACHE_LINE_SIZE)
                                        iNxtStreamStep += nxtStreamStride;
                        
                                issuingiBatchAddr      = iDirStreamBuffer->IDirStreamTable[iDirNextStreamTableKey].previBatchLastAddr ;
                                issuingiBatchAddr      = issuingiBatchAddr + iNxtStreamStep                                           ;
 
                                nextStreamRequest                    =  * req                                       ; 
                                nextStreamRequest.addr               = issuingiBatchAddr                            ; 
                                nextStreamRequest.metaISARequestorID = req->metaISARequestorID                      ; 
                                nextStreamRequest.metaISAStreamID    = nextStreamID                                 ;   
                                nextStreamRequest.metaISAStreamType  = DIR_STREAM                                   ;
                                nextStreamRequest.is_base_addr       = false                                        ; 
                                nextStreamRequest.addr_vec           = addr_to_addr_vec(coreid , nextStreamRequest.addr     , nextStreamRequest.metaISAStreamID) ; 
                                nextStreamRequest.nextAddr           = issuingiBatchAddr +  2*iNxtStreamStep             ; 
                                nextStreamRequest.next_addr_vec      = addr_to_addr_vec(coreid , nextStreamRequest.nextAddr , nextStreamRequest.metaISAStreamID) ; 
                                nextStreamRequest.is_first_command   = true   ;
                                nextStreamRequest.is_iprefetched     = false  ;
                                nextStreamRequest.is_last_inIBatch   = false  ; 


                                /// Only put in actq if not exist in readq or actq 
                                //phase_3
                                /// If it is already in actq or readq the scheduler will handl it.
                                list<Request>::iterator isReqInActq  = std::find(actq.q.begin() , actq.q.end()   , nextStreamRequest);
                                list<Request>::iterator isReqInReadq = std::find(readq.q.begin(), readq.q.end()  , nextStreamRequest);

                                if (isReqInReadq == readq.q.end() &&  isReqInActq == actq.q.end())
                                {

                                    iBufferKeyClass nxtStrQkey(nextStreamRequest.addr, nextStreamRequest.metaISAStreamID, nextStreamRequest.metaISARequestorID);
                                    vector<int> nxtStrAddr_vec     = addr_to_addr_vec  (coreid,issuingiBatchAddr,nextStreamRequest.metaISAStreamID)  ;
                                    int nxtStr_bank_id = nxtStrAddr_vec[int(T::Level::Bank)];
                                    if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                                        nxtStr_bank_id += nxtStrAddr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];


                                    if (!iBuffer[nxtStr_bank_id]->find(nxtStrQkey))
                                    {
                                        IPreftecherQueueEntry nxtStrEnt;
                                        //UnComment following line if actual data is used! (Not Ramulator Case)
                                        // for (int i = 0; i < CACHE_LINE_SIZE; i++)
                                        //     nxtStrEnt.data[i] = 0x0;
                                        nxtStrEnt.W = 1 ; // InValid Data (Waiting)
                                        nxtStrEnt.I = 0 ; // Corresponding CMD issued
                                        nxtStrEnt.R = 0 ; // Not requested Yet 
                                        iBuffer[nxtStr_bank_id]->Push(std::make_pair(nxtStrQkey, nxtStrEnt));
                                    }
                                }
                                actq.q.push_back  (nextStreamRequest);



                                printf(" ( A:%#16lx ) ( T:%10ld ) (V:",  nextStreamRequest.addr, clk);
                                for (int lev = 0; lev < int(T::Level::MAX); lev++)
                                {
                                    if(lev!=0) 
                                        printf(",");
                                    printf(" %5x ", nextStreamRequest.addr_vec[lev]);
                                }
                                printf(")\n");


                                // check if the last nextStreamRequest request and crnt request are on same row or not !
                                auto beginReq      = req->addr_vec             .begin();
                                vector<int> rowgroupReq(beginReq, beginReq + int(T::Level::Row));
                                auto beginPrefetch = nextStreamRequest.addr_vec.begin();
                                vector<int> rowgroupNextStream(beginPrefetch, beginPrefetch + int(T::Level::Row));
                                int r = int(T::Level::Row);
                                // Only fetch subset requests out of N requests that will hit (Same rowgroup and row)
                                if( (rowgroupReq != rowgroupNextStream) || //either target other bank 
                                    ( (rowgroupReq == rowgroupNextStream)  && req->addr_vec[r] ==nextStreamRequest.addr_vec[r]))
                                { 
                                    // Not Conflict  
                                    cmd = T::Command::RD       ;
                                    if(cond1==false && cond2 ==false)
                                        rda_inter_stream--         ;
                                    policy_cause = NOT_CLOSED ;  
                                }

                            }


                        }


                    }
                }
            }


            /************        Finally Don't close if there is a hit !    ************/
            //In isConfigIPP , isAdaptive -> we must close the row if there is no other hits in readq , writeq , or actq 
            //Issue Autoprecharge in case of reads as well is there is no access to the opened row in readq
            
            if ( (cmd == T::Command::RDA) ||   (cmd == T::Command::WRA) )
                if (
                    (channel->spec->is_accessing(cmd) && isConfigIPP)
                    ||
                     (channel->spec->is_accessing(cmd) && isAdaptive))
                {
                    // check if it is the last request to the opened row
                    Queue *queue = write_mode ? &writeq : &readq;

                    auto begin = req->addr_vec.begin();
                    vector<int> rowgroup(begin, begin + int(T::Level::Row) + 1);

                    int num_row_hits = 0;

                    for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr)
                    {
                        if (is_row_hit(itr))
                        {
                            auto begin2 = itr->addr_vec.begin();
                            vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                            if (rowgroup == rowgroup2)
                                num_row_hits++;
                        }
                    }

                    //if (num_row_hits == 0)
                    {
                        Queue *queue = &actq;
                        for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr)
                        {
                            if (is_row_hit(itr))
                            {
                                auto begin2 = itr->addr_vec.begin();
                                vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                                if (rowgroup == rowgroup2)
                                    num_row_hits++;
                            }
                        }
                    }

                    assert(num_row_hits > 0); // The current request should be a hit,
                                            // so there should be at least one request
                                            // that hits in the current open row
                    if (num_row_hits > 1)
                    {
                        if (cmd == T::Command::RDA)
                            cmd = T::Command::RD;
                        else if (cmd == T::Command::WRA)
                            cmd = T::Command::WR;
                        else if (!(cmd == T::Command::WR || cmd == T::Command::RD))
                            assert(false && "Unimplemented command type.");
                        if (print_ipp_logic_trace)
                            printf("Rvert back to Open , There is still more hits in the current row! \n");
                    }
                }
            
            




            //In IPP + Direct Stream -> we must close the row at the Read/Write or write/read swithing 
            /**************************************** Switching Logic *********************************/
            // 1- Track switch to read 
            //    if IPP + current req type is Write 
            //    if no further access to current bank in writeq && writeq size > wr_low_watermark*max  (iterate for size difference)
            //               or current writeq size == wr_low_watermark*max
            //               or  current wire mode = 0 
            /* 
            if (channel->spec->is_accessing(cmd) && isConfigIPP && req->type == Request::Type::WRITE)
            {
                // Switch to read condition 
                if (writeq.size() <= int(wr_low_watermark * writeq.max)+1  && readq.size() != 0)
                {
                        cmd = T::Command::WRA;
                        policy_cause = SWITCH_TO_READ1;
                }
                else 
                {
                    if(write_mode==0)
                    {
                        cmd = T::Command::WRA;
                        policy_cause = SWITCH_TO_READ2;
                    }
                    
                    else 
                    {
                        if(writeq.size() > int(wr_low_watermark))
                        {
                            bool NoFutureAccess = true;//Assume first that there is no future acces to same rowgroup before switching 
                            auto begin = req->addr_vec.begin();
                            vector<int> rowgroup(begin, begin + int(T::Level::Row) + 1);
                            int i=writeq.size();
                            for (auto itr = writeq.q.begin(); 
                                    (itr != writeq.q.end() )  && (i >=int(wr_low_watermark * writeq.max)) ; ++itr)
                            {

                                auto begin2 = itr->addr_vec.begin();
                                vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                                if (rowgroup == rowgroup2) // Future access to same bank and bank group 
                                    NoFutureAccess = false;
                                i--;

                             }
                             if(NoFutureAccess)
                             {
                                cmd = T::Command::WRA;
                                policy_cause = SWITCH_TO_READ3;
                             }

                        }
                    }
                    
           
                }

                    

            }  
           
            // 2- Track switch to write
            //    if IPP + current req type is read
            //    if no further access to current bank in readq && writeq size < wr_high_watermark*max  (iterate for size difference)
            //               or current writeq size == wr_high_watermark*max-1
 

           
            if (channel->spec->is_accessing(cmd) && isConfigIPP && req->type == Request::Type::READ)
            {
                if( ( (req->is_last_inIBatch)&& isConfigiBatch ) || !isConfigiBatch)
                if (writeq.size() >= int(wr_high_watermark * writeq.max)-1  )
                {
                    cmd = T::Command::RDA ;
                    policy_cause = SWITCH_TO_WRITE1;

                }
            } 
             */
            return numMisses;
        }

        void issue_cmd(typename T::Command   cmd, const vector<int> &addr_vec)
        {
            cmd_issue_autoprecharge(cmd, addr_vec);
            assert(is_ready(cmd, addr_vec));

            channel->update(cmd, addr_vec.data(), clk);

            if (cmd == T::Command::PRE)
            {
                if (rowtable->get_hits(addr_vec, true) == 0)
                {
                    useless_activates++;
                }
            }

            rowtable->update(cmd, addr_vec, clk);
            if (record_cmd_trace)
            {
                // Format should be :
                // timestamp, command, rank, bank_group, bank, row, column
                auto &filev4 = cmd_tracev4_files[addr_vec[1]];

                string &cmd_name = channel->spec->command_name[int(cmd)];
                //# For DRAMPower v5 
                bool enDRAMPowerv5 = false; 
                if(enDRAMPowerv5)
                {
                    auto &file   = cmd_trace_files[addr_vec[1]];
                    file << clk << ',' << cmd_name << ',' ;
                    file << addr_vec[int(T::Level::Rank)]    << ','  << addr_vec[int(T::Level::Bank) - 1] << ',';
                    file << addr_vec[int(T::Level::Bank)]    << ','  << addr_vec[int(T::Level::Row) ]     << ',';
                    file << addr_vec[int(T::Level::Column)]  << endl;
                }

                filev4 <<clk<<','<<cmd_name;
                // TODO bad coding here
                if (cmd_name == "PREA" || cmd_name == "REF")
                    filev4<<endl;
                else{
                    int bank_id = addr_vec[int(T::Level::Bank)];
                    if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                        bank_id += addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
                    filev4<<','<<bank_id<<endl;
                }


            }
            if (print_cmd_trace & NOT_IPP)
            {
                //printf("issue command of two parameters\n");
                printf("\t %5s (%10ld):", channel->spec->command_name[int(cmd)].c_str(), clk);
                for (int lev = 0; lev < int(T::Level::MAX); lev++)
                    printf(" %5x", addr_vec[lev]);
                printf("\n");
            }
        }

        // Don't pass cmd by reference , it is used in comparison in controller and expected to be PRE , ACT , RD or WR
        // so to return actual command incluse precharge cases , we use a fifth parameter 
        void issue_cmd(typename T::Command   cmd, const vector<int> &addr_vec, list<Request>::iterator req,string cmd_trace_dbg_msg,typename T::Command  &  cmd_AP)
        {

            uint64_t        reqWinID   ;
            CLOSE_CAUSE   policy_cause ;
            int numMisses = cmd_issue_autoprecharge(cmd, req->addr_vec, req , reqWinID,policy_cause);
            cmd_AP        = cmd ; 
            assert(is_ready(cmd, req->addr_vec));
            //Abotaleb : Update state and timing is done now 
            //Debug_Detailed [TIMING_UPDATE]
            bool debug_enable=false;
            /*if(req->addr==0xa0040000)       
            {
                printf("\tTiming_Update:");
                debug_enable=true;

            }*/ 
            channel->update(cmd, req->addr_vec.data(), clk,debug_enable);

            if (cmd == T::Command::PRE)
            {
                if (rowtable->get_hits(req->addr_vec, true) == 0)
                {
                    useless_activates++;
                }
            }

            rowtable->update(cmd, req->addr_vec, clk);
            int bank_id = req->addr_vec[int(T::Level::Bank)];
            if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                bank_id += req->addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
                    
            if (record_cmd_trace)
            {
                // Format should be :
                // timestamp, command, rank, bank_group, bank, row, column
                auto &filev4 = cmd_tracev4_files[addr_vec[1]];

                string &cmd_name = channel->spec->command_name[int(cmd)];
                 //# For DRAMPower v5 
                bool enDRAMPowerv5 = false; 
                if(enDRAMPowerv5)
                {
                    auto &file   = cmd_trace_files[addr_vec[1]];
                    file << clk << ',' << cmd_name << ',' ;
                    file << addr_vec[int(T::Level::Rank)]    << ','  << addr_vec[int(T::Level::Bank) - 1] << ',';
                    file << addr_vec[int(T::Level::Bank)]    << ','  << addr_vec[int(T::Level::Row) ]     << ',';
                    file << addr_vec[int(T::Level::Column)]  << endl;
                }
                filev4 <<clk<<','<<cmd_name;
                // TODO bad coding here
                if (cmd_name == "PREA" || cmd_name == "REF")
                    filev4<<endl;
                else{
                    int bank_id = addr_vec[int(T::Level::Bank)];
                    if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                        bank_id += addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
                    filev4<<','<<bank_id<<endl;
                }


            }
            

            //Debug Command Trace [Core Interstellar DRAM Debugging]
            if (print_cmd_trace )
            {
                printf("\t %5s ", channel->spec->command_name[int(cmd)].c_str());
                printf("[%d][%d]",req->metaISAStreamID ,req->metaISARequestorID);
                printf(" ( A:%#16lx ) ( T:%10ld ) (V:",  req->addr, clk);
                for (int lev = 0; lev < int(T::Level::MAX); lev++)
                {
                    if(lev!=0) 
                        printf(",");
                    printf(" %5x ", req->addr_vec[lev]);
                }
                printf(") ");
                std::string rtTypeStr = rtTypeStrMap[req->req_interstellarRT_type];
                printf("(RT:%s)"    , rtTypeStr.c_str());
                printf("(WMode:%d)(WQ:%d)(RQ:%d)", write_mode,writeq.size(),readq.size());           
                if(this->enableIPrefetcher)
                    printf(" (iBatched:%d ) ",req->is_iprefetched);
                if(numMisses!=-1)
                {
                    int winID=-1;
                    if(rowpolicy->type == RowPolicy<T>::Type::Adaptive)
                    {
                        printf(" ( Win[ %lx ] = \'",reqWinID);
                        uint8_t crntUnifiedWinLen = adaptive_Policy_Counter[reqWinID]->crnt_window_length;
                        for (std::size_t i = 0; i < crntUnifiedWinLen; ++i)
                            std::cout << adaptive_Policy_Counter[reqWinID]->misses_shift_reg[i];
                    }
                    else 
                    {
                        if(rowpolicy->type == RowPolicy<T>::Type::IPP)
                        {
                            switch(req->metaISAStreamType)
                            {
                                case(DIR_STREAM):
                                    winID = 0;
                                    break;
                                case(INDIR_STREAM):
                                    winID = 1;
                                    break;
                                default:
                                    winID = 2; 
                                    break;
                            }
                        }

                        printf(" ( Win[%d][%#lx] =  \'",winID,reqWinID);
                        uint8_t crntWinLen = metaisa_window_counter[winID][reqWinID]->crnt_window_length;
                        for (std::size_t i = 0; i < crntWinLen; ++i)
                            std::cout << metaisa_window_counter[winID][reqWinID]->misses_shift_reg[i];

                    }


                    printf(" ) M = %d ",numMisses);                            
                    if(policy_cause!=NOT_CLOSED && policy_cause!=TRUE_CLOSED)
                    {
                        string policy_cause_str= POLICY_CAUSE_STR[int(policy_cause)];
                        printf(" PL:%s",policy_cause_str.c_str());
                    }

                }
                // Print crntRowReqCount if IPPC Policy 
                if(this->isConfigIPP && (this->dirStr_CntEn==1))
                {
                    uint64_t iDirStreamTableKey = (
                            ( (uint64_t)req->metaISAStreamID << METAISA_REQID_BITS) 
                                + req->metaISARequestorID);

                    if(iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey)!=iDirStreamBuffer->IDirStreamTable.end())
                    {
                        uint16_t crntRowReqsCount = iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].crntRowReqsCount[bank_id][int(req->type)]  ; 
                        printf(" Count = %d ",crntRowReqsCount);
                    }
                }
                // To add any extra debugging messages 
                printf("(RQ_Type:%d)%s\n",req->type,    cmd_trace_dbg_msg.c_str());
            }
        
        /*ACT, PRE, PREA,
        ,  ,  ,  
                 ;
         wr_cnt_per_Stream;
         rda_cnt_per_Stream;
         wra_cnt_per_Stream;
                                 ;

        */
            int idx_per_stream_per_core = req->metaISAStreamID + ( MAX_STREAMS*req->metaISARequestorID) ; 

            switch(cmd)
            {
                case T::Command::ACT:
                    act_cnt_per_Stream[idx_per_stream_per_core]++;
                    break;
                case T::Command::PRE:
                    pre_cnt_per_Stream[idx_per_stream_per_core]++;
                    break;
                case T::Command::RD:
                    rd_cnt_per_Stream[idx_per_stream_per_core]++;
                    break;  
                case T::Command::WR:
                    wr_cnt_per_Stream[idx_per_stream_per_core]++;
                    break; 
                case T::Command::RDA:
                    rda_cnt_per_Stream[idx_per_stream_per_core]++;
                    break; 
                case T::Command::WRA:
                    wra_cnt_per_Stream[idx_per_stream_per_core]++;
                    break;
                default:
                    break;
            }

        }

        vector<int> get_addr_vec(typename T::Command cmd, list<Request>::iterator req)
        {
            return req->addr_vec;
        }

        vector<int> get_next_addr_vec(typename T::Command cmd, list<Request>::iterator req)
        {
            return req->next_addr_vec;
        }

    // Debug all queues : readq , writeq , actq 
    void debugQueuesDetailed()
    {
        Queue *debugQueue[3]     = { &readq  , &writeq  , &actq  };
        string debugQueueName[3] = {"readq " , "writeq ", "actq "};
        for(int qInd = 0 ; qInd < 3 ; qInd++)
        {

            for (auto const& itr : debugQueue[qInd]->q) 
            {
                printf("%s",debugQueueName[qInd].c_str());
                printf(" ( A:%#16lx ) (T:%ld) (Arrival:%10ld ) (V:",  itr.addr,clk, itr.arrive);
                for (int lev = 0; lev < int(T::Level::MAX); lev++)
                {
                    if(lev!=0) 
                        printf(",");
                    printf(" %5x ", itr.addr_vec[lev]);
                }
                printf(") ");    
                printf("\n");    
            }
                            
        }
    }


    bool upgrade_prefetch_req (Queue& q, const Request& req) {
        if(q.size() == 0)
            return false;

        auto pref_req = find_if(q.q.begin(), q.q.end(), [req](Request& preq) {
                                                return req.addr == preq.addr;});

        if (pref_req != q.q.end()) {
            pref_req->type = Request::Type::READ;
            pref_req->callback = pref_req->proc_callback; // FIXME: proc_callback is an ugly workaround
            return true;
        }
            
        return false;
    }

    // FIXME: ugly
    bool upgrade_prefetch_req (deque<Request>& p, const Request& req) {
        if (p.size() == 0)
            return false;

        auto pref_req = find_if(p.begin(), p.end(), [req](Request& preq) {
                                                return req.addr == preq.addr;});

        if (pref_req != p.end()) {
            pref_req->type = Request::Type::READ;
            pref_req->callback = pref_req->proc_callback; // FIXME: proc_callback is an ugly workaround
            return true;
        }
            
        return false;
    }
      

       

    };

    template <>
    vector<int> Controller<SALP>::get_addr_vec(
        SALP::Command cmd, list<Request>::iterator req);

    template <>
    bool Controller<SALP>::is_ready(list<Request>::iterator req);

    template <>
    void Controller<ALDRAM>::update_temp(ALDRAM::Temp current_temperature);

    template <>
    void Controller<TLDRAM>::tick();

    template <>
    void Controller<TLDRAM>::cmd_issue_autoprecharge(typename TLDRAM::Command &cmd,
                                                     const vector<int> &addr_vec);

} /*namespace ramulator*/

#endif /*__CONTROLLER_H*/
