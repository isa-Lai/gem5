#ifndef __MEMORY_H
#define __MEMORY_H

#include "Config.h"
#include "DRAM.h"
#include "Request.h"
#include "Controller.h"
#include "SpeedyController.h"
#include "Statistics.h"
#include "GDDR5.h"
#include "HBM.h"
#include "LPDDR3.h"
#include "LPDDR4.h"
#include "WideIO2.h"
#include "DSARP.h"
#include <vector>
#include <functional>
#include <cmath>
#include <cassert>
#include <tuple>


using namespace std;

typedef vector<unsigned int> MapSrcVector;
typedef map<unsigned int, MapSrcVector > MapSchemeEntry;
typedef map<unsigned int, MapSchemeEntry> MapScheme;

namespace ramulator
{

class MemoryBase{
public:
    MemoryBase() {}
    virtual ~MemoryBase() {}
    virtual double clk_ns() = 0;
    virtual void tick() = 0;
    virtual bool send(Request req) = 0;
    virtual bool updatePrefetcher(int ctrlNum ,  circBuffer<ramulator::iBufferKeyClass, ramulator::IpreftecherQueueEntry> * iBatchBuffer[]) = 0;
    virtual int  get_num_cores(int ctrlNum) = 0;
    virtual int  get_ctrls_num() = 0;
    virtual bool upgrade_prefetch_req(long addr) = 0;
    virtual int pending_requests() = 0;
    virtual void finish(void) = 0;
    virtual long page_allocator(long addr, int coreid) = 0;
    virtual void record_core(int coreid) = 0;
    virtual void set_high_writeq_watermark(const float watermark) = 0;
    virtual void set_low_writeq_watermark(const float watermark) = 0;
};

template <class T, template<typename> class Controller = Controller >
class Memory : public MemoryBase
{
protected:
  ScalarStat dram_capacity;
  ScalarStat num_dram_cycles;
  ScalarStat num_incoming_requests;
  VectorStat num_read_requests;
  VectorStat num_write_requests;
  VectorStat num_read_requests_dir_stream;
  VectorStat num_read_requests_indir_stream;
  VectorStat num_read_requests_none_streams;
  ScalarStat ramulator_active_cycles;
  VectorStat incoming_requests_per_channel;
  VectorStat incoming_read_reqs_per_channel;

  ScalarStat physical_page_replacement;
  ScalarStat maximum_bandwidth;
  ScalarStat in_queue_req_num_sum;
  ScalarStat in_queue_read_req_num_sum;
  ScalarStat in_queue_write_req_num_sum;
  ScalarStat in_queue_req_num_avg;
  ScalarStat in_queue_read_req_num_avg;
  ScalarStat in_queue_write_req_num_avg;

#ifndef INTEGRATED_WITH_GEM5
  VectorStat record_read_requests;
  VectorStat record_write_requests;
#endif

  long max_address;
  bool memory_trace_flag;
  MapScheme mapping_scheme;
  
public:
    enum class Type {
        ChRaBaRoCo                ,
        RoBaRaCoCh                ,
        RoCoBaRaCh                ,
        RoBaBg1CoBg0RaCh          , 
        StrmPartRoBaBg1CoBg0RaCh  , 
        InterStreamChRoCoBaRa     ,
        InterStellarRT            , 
        MAX,
    } type = Type::ChRaBaRoCo;
    int channels ; 

    enum class RTAddrMaping {
        map1     ,
        map2     ,
        map4     , 
    } rtAddrMaping = RTAddrMaping::map1;


    enum class Translation {
      None,
      Random,
      MAX,
    } translation = Translation::None;

    std::map<string, Translation> name_to_translation = {
      {"None", Translation::None},
      {"Random", Translation::Random},
    };

    vector<int> free_physical_pages;
    long free_physical_pages_remaining;
    map<pair<int, long>, long> page_translation;

    vector<Controller<T>*> ctrls;
    T * spec;
    vector<int> addr_bits;
    int         coresNum                            ;
    int         num_banks, num_bankgroups           ;
    int         banksPerPartition                   ;
    float       grpsPerPartition,bank_part          ; //multicore can share same bank group 
    int         partbits                                                  ;
    int         tot_addr_bits,tot_addr_bits_except_rows,shift_bits_ba_bg  ;
    long        partition_mask          ;
    string mapping_file;
    bool use_mapping_file;
    bool dump_mapping;
    
    int tx_bits;

    Memory(const Config& configs, vector<Controller<T>*> ctrls)
        : ctrls(ctrls),
          spec(ctrls[0]->channel->spec),
          addr_bits(int(T::Level::MAX))
    {
        // make sure 2^N channels/ranks
        // TODO support channel number that is not powers of 2
        channels = stoi(configs["channels"], NULL, 0);
        int *sz = spec->org_entry.count    ; 
        assert((sz[0] & (sz[0] - 1)) == 0) ; 
        assert((sz[1] & (sz[1] - 1)) == 0) ; 
        // validate size of one transaction
        int tx = (spec->prefetch_size * spec->channel_width / 8);
        tx_bits = calc_log2(tx);
        assert((1<<tx_bits) == tx);
        
        // Parsing mapping file and initialize mapping table
        use_mapping_file = false;
        dump_mapping = false;
        if (spec->standard_name.substr(0, 4) == "DDR3"){
            if (configs["mapping"] != "defaultmapping"){
              init_mapping_with_file(configs["mapping"]);
              // dump_mapping = true;
              use_mapping_file = true;
            }
        }
        //Abotaleb : enable configuring address mapping from the config file 
        if(configs.contains("address_mapping"))
        {
            assert(configs["address_mapping"]=="ChRaBaRoCo" 
                           || configs["address_mapping"]=="RoBaRaCoCh"
                           || configs["address_mapping"]=="InterStellarRT"   || configs["address_mapping"]=="RoCoBaRaCh"
                           || configs["address_mapping"]=="RoBaBg1CoBg0RaCh" || configs["address_mapping"]=="StrmPartRoBaBg1CoBg0RaCh" 
                           || configs["address_mapping"]=="InterStreamChRoCoBaRa" ); 

            type =  (  configs["address_mapping"]=="ChRaBaRoCo"                ? Type::ChRaBaRoCo               :
                    ( (configs["address_mapping"]=="RoBaRaCoCh")               ? Type::RoBaRaCoCh               :
                    ( (configs["address_mapping"]=="InterStellarRT")           ? Type::InterStellarRT           :
                    ( (configs["address_mapping"]=="InterStreamChRoCoBaRa")    ? Type::InterStreamChRoCoBaRa    :
                    ( (configs["address_mapping"]=="RoBaBg1CoBg0RaCh")         ? Type::RoBaBg1CoBg0RaCh         :
                    ( (configs["address_mapping"]=="StrmPartRoBaBg1CoBg0RaCh") ? Type::StrmPartRoBaBg1CoBg0RaCh :
                    Type::RoCoBaRaCh)))))
                  );
        }

        if(configs.contains("rtAddrMaping"))
        {
            assert(configs["rtAddrMaping"]=="map1" || configs["rtAddrMaping"]=="map2"
                           || configs["rtAddrMaping"]=="map4" );
            rtAddrMaping =( configs["rtAddrMaping"]=="map1"?RTAddrMaping::map1:
            ( (configs["rtAddrMaping"]=="map2")?RTAddrMaping::map2:RTAddrMaping::map4));
        }
        



        // enable trace 
        memory_trace_flag = configs.print_memory_trace();
         
        // If hi address bits will not be assigned to Rows
        // then the chips must not be LPDDRx 6Gb, 12Gb etc.
        if (type != Type::RoBaRaCoCh && spec->standard_name.substr(0, 5) == "LPDDR")
            assert((sz[int(T::Level::Row)] & (sz[int(T::Level::Row)] - 1)) == 0);
        
        max_address = spec->channel_width / 8;
        tot_addr_bits=0;
        for (unsigned int lev = 0; lev < addr_bits.size(); lev++) {
          addr_bits[lev] = calc_log2(sz[lev]);
          tot_addr_bits+= addr_bits[lev]; 
            max_address *= sz[lev];
        }

        addr_bits[int(T::Level::MAX) - 1] -= calc_log2(spec->prefetch_size); // Reduce column bits by burst length bits 
        tot_addr_bits                     -= calc_log2(spec->prefetch_size);
        tot_addr_bits_except_rows          = tot_addr_bits - addr_bits[int(T::Level::Row)] ;
        //%%% If Addres Mapping : BA BG ROW COL BURST             
        //shift_bits_ba_bg                   = tot_addr_bits;
        //%%% If Addres Mapping : ROW BA BG COL BURST         
        shift_bits_ba_bg                     = tot_addr_bits_except_rows;

        printf("The memory configuration : \n");
        num_banks           = sz[int(T::Level::Bank)];        
        if (spec->standard_name.substr(0, 4) == "DDR4")
        {
            num_bankgroups  = sz[2];
            printf("# Bank Groups = %d, Bank Group bits = %d \n", num_bankgroups , addr_bits[2]   );
        }        
        printf("# Banks = %d , Bank bits = %d \n",num_banks                ,addr_bits[int(T::Level::Bank)]   );
        printf("# Rows  = %d , Row  bits = %d \n",sz[int(T::Level::Row)]   ,addr_bits[int(T::Level::Row)]    );
        printf("# Cols  = %d , Col  bits = %d \n",sz[int(T::Level::Column)],addr_bits[int(T::Level::Column)] );
        //// Check if we can do bank partitioning 
        bank_part = 0                       ;
        coresNum       = stoi(configs["cores"])  ;
        partbits       = calc_log2(coresNum)     ;
        partition_mask = ~( ((0x1ULL <<partbits)-1) <<(shift_bits_ba_bg -partbits));

        string addressMapping =
                 ( type==Type::RoBaRaCoCh                    ? "RoBaRaCoCh"                :
               ((  type==Type::ChRaBaRoCo                 )  ? "ChRaBaRoCo"                :
               ((  type==Type::InterStellarRT             )  ? "InterStellarRT"            :
               ((  type==Type::InterStreamChRoCoBaRa      )  ? "InterStreamChRoCoBaRa"     :
               ((  type==Type::RoBaBg1CoBg0RaCh           )  ? "RoBaBg1CoBg0RaCh"          :
               ((  type==Type::StrmPartRoBaBg1CoBg0RaCh   )  ? "StrmPartRoBaBg1CoBg0RaCh"  :               
                "RoCoBaRaCh")))))
              );
        printf("AddressMapping =   %s\n",  addressMapping.c_str());
        if(type==Type::InterStellarRT)
        {
            printf("Partition Bits  = %d , Partition Mask = %lx, shift_bits_ba_bg =%d\n" , partbits,partition_mask,shift_bits_ba_bg);
            printf("  tot_addr_bits = =%d, tot_addr_bits_ecept_rows =%d\n"               , tot_addr_bits,tot_addr_bits_except_rows);
        }
        // Initiating translation
        if (configs.contains("translation")) {
          translation = name_to_translation[configs["translation"]];
        }
        if (translation != Translation::None) {
          // construct a list of available pages
          // TODO: this should not assume a 4KB page!
          free_physical_pages_remaining = max_address >> 12;
          free_physical_pages.resize(free_physical_pages_remaining, -1);
        }
        //Set the addr_bits inside each channel's controller
        for(typename std::vector<Controller<T>*>::iterator it = ctrls.begin(); it != ctrls.end(); ++it)
        {
            (*it)->set_addr_bits(addr_bits);
            (*it)->set_tx_bits(tx_bits);
            (*it)->set_interstellarRT_mapping_req(partition_mask,partbits,shift_bits_ba_bg);


        }
        dram_capacity
            .name("dram_capacity")
            .desc("Number of bytes in simulated DRAM")
            .precision(0)
            ;
        dram_capacity = max_address;

        num_dram_cycles
            .name("dram_cycles")
            .desc("Number of DRAM cycles simulated")
            .precision(0)
            ;
        num_incoming_requests
            .name("incoming_requests")
            .desc("Number of incoming requests to DRAM")
            .precision(0)
            ;
        num_read_requests
            .init(configs.get_core_num())
            .name("read_requests")
            .desc("Number of incoming read requests to DRAM per core")
            .precision(0)
            ;
        num_write_requests
            .init(configs.get_core_num())
            .name("write_requests")
            .desc("Number of incoming write requests to DRAM per core")
            .precision(0)
            ;
        num_read_requests_dir_stream
            .init(configs.get_core_num())
            .name("read_requests_dir_stream")
            .desc("Number of incoming direct stream read requests to DRAM per core")
            .precision(0)
            ;
        num_read_requests_indir_stream
            .init(configs.get_core_num())
            .name("read_requests_indir_stream")
            .desc("Number of incoming indirect stream read requests to DRAM per core")
            .precision(0)
            ;            
        num_read_requests_none_streams
            .init(configs.get_core_num())
            .name("read_requests_none_streams")
            .desc("Number of incoming none streams read requests to DRAM per core")
            .precision(0)
            ;
        incoming_requests_per_channel
            .init(sz[int(T::Level::Channel)])
            .name("incoming_requests_per_channel")
            .desc("Number of incoming requests to each DRAM channel")
            ;
        incoming_read_reqs_per_channel
            .init(sz[int(T::Level::Channel)])
            .name("incoming_read_reqs_per_channel")
            .desc("Number of incoming read requests to each DRAM channel")
            ;

        ramulator_active_cycles
            .name("ramulator_active_cycles")
            .desc("The total number of cycles that the DRAM part is active (serving R/W)")
            .precision(0)
            ;
        physical_page_replacement
            .name("physical_page_replacement")
            .desc("The number of times that physical page replacement happens.")
            .precision(0)
            ;
        maximum_bandwidth
            .name("maximum_bandwidth")
            .desc("The theoretical maximum bandwidth (Bps)")
            .precision(0)
            ;
        in_queue_req_num_sum
            .name("in_queue_req_num_sum")
            .desc("Sum of read/write queue length")
            .precision(0)
            ;
        in_queue_read_req_num_sum
            .name("in_queue_read_req_num_sum")
            .desc("Sum of read queue length")
            .precision(0)
            ;
        in_queue_write_req_num_sum
            .name("in_queue_write_req_num_sum")
            .desc("Sum of write queue length")
            .precision(0)
            ;
        in_queue_req_num_avg
            .name("in_queue_req_num_avg")
            .desc("Average of read/write queue length per memory cycle")
            .precision(6)
            ;
        in_queue_read_req_num_avg
            .name("in_queue_read_req_num_avg")
            .desc("Average of read queue length per memory cycle")
            .precision(6)
            ;
        in_queue_write_req_num_avg
            .name("in_queue_write_req_num_avg")
            .desc("Average of write queue length per memory cycle")
            .precision(6)
            ;
#ifndef INTEGRATED_WITH_GEM5
        record_read_requests
            .init(configs.get_core_num())
            .name("record_read_requests")
            .desc("record read requests for this core when it reaches request limit or to the end")
            ;

        record_write_requests
            .init(configs.get_core_num())
            .name("record_write_requests")
            .desc("record write requests for this core when it reaches request limit or to the end")
            ;
#endif

    }

    ~Memory()
    {
        for (auto ctrl: ctrls)
            delete ctrl;
        delete spec;
    }

    double clk_ns()
    {
        return spec->speed_entry.tCK;
    }

    void record_core(int coreid) {
#ifndef INTEGRATED_WITH_GEM5
      record_read_requests[coreid] = num_read_requests[coreid];
      record_write_requests[coreid] = num_write_requests[coreid];
#endif
      for (auto ctrl : ctrls) {
        ctrl->record_core(coreid);
      }
    }

    void tick()
    {
        ++num_dram_cycles;

        //printf("\tRamulator Memory.h tick :: %f\n",num_dram_cycles.value());
        int cur_que_req_num = 0;
        int cur_que_readreq_num = 0;
        int cur_que_writereq_num = 0;
        for (auto ctrl : ctrls) {
          cur_que_req_num += ctrl->readq.size() + ctrl->writeq.size() + ctrl->pending.size();
          cur_que_readreq_num += ctrl->readq.size() + ctrl->pending.size();
          cur_que_writereq_num += ctrl->writeq.size();
        }
        in_queue_req_num_sum += cur_que_req_num;
        in_queue_read_req_num_sum += cur_que_readreq_num;
        in_queue_write_req_num_sum += cur_que_writereq_num;

        bool is_active = false;
        for (auto ctrl : ctrls) {
          is_active = is_active || ctrl->is_active();
          ctrl->tick();
         // printf("(T:%ld) ramulator_active_cycles = %f\n",ctrl->clk,ramulator_active_cycles.value());
        }
        if (is_active) {
          ramulator_active_cycles++;
        }

    }

    virtual bool send(Request req)
    {
        int coreid = req.coreid;

        /*****  With Bank Partiotioning : Special Address Mapping , Over-write the bank bits */
        /****   InterStellar-RTSS   ******************/
        //Abotaleb [Important RTAS ] 
        // Do Bank Paritioning 
        //DDR3 :  Channel, Rank, Bank, Row, Column, MAX
        //DDR4 :  Channel, Rank, BankGroup, Bank, Row, Column, MAX
        // How address vector is assigned to col , rank , bank, bank group , channel !
        //r[ addr_vec[1]] , bg[addr_vec[2]] , b[  addr_vec[3]] , row[addr_vec[4]] , ch[addr_vec[0]]  , col[ addr_vec[5] ]"
        if(bank_part==1)
        {

        }
        update_addr_vec(req);
        // The following is done inside update_addr_vec function : 
        //     switch(int(type)){
        //         case int(Type::ChRaBaRoCo):
        //             for (int i = addr_bits.size() - 1; i >= 0; i--)
        //                 req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
        //             break;
        //         case int(Type::RoBaRaCoCh):
        //             req.addr_vec[0] = slice_lower_bits(addr, addr_bits[0]);
        //             req.addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr, addr_bits[addr_bits.size() - 1]);
        //             for (int i = 1; i <= int(T::Level::Row); i++)
        //                 req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
        //             break;

        /**************************************************************
         *  Abotaleb : Modify the send to init next_addr_vec to account
         *   for the MetaISA Logic (Next expected LLC Miss)
         * 
         * ************************************************************/
        req.next_addr_vec.resize(addr_bits.size()); 
        if(req.metaISAStreamType==DIR_STREAM) // This means a direct stream next LLC miss addr is sent 
        {
            long next_addr = req.nextAddr;
            // Each transaction size is 2^tx_bits, so first clear the lowest tx_bits bits
            clear_lower_bits(next_addr, tx_bits);
            int interleaveBGbits = 0 ; 

            if (use_mapping_file){
                apply_mapping(next_addr, req.next_addr_vec);
            }
            else {
                switch(int(type)){
                    //InterStellar InterStreamMultiChannelSched (ISMS)
                    case int(Type::RoCoBaRaCh):
                    {
                        //string DDR4::level_str [int(Level::MAX)] = {"Ch", "Ra", "Bg", "Ba", "Ro", "Co"};
                        //Least bits represents the highest parallelism possible [Should be baseline of Streaming]
                        req.next_addr_vec[int(T::Level::Channel)] = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                        req.next_addr_vec[int(T::Level::Rank)]    = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                        req.next_addr_vec[2] = slice_lower_bits(next_addr,  addr_bits[2] ) ;   // Bank Group
                        req.next_addr_vec[3] = slice_lower_bits(next_addr,  addr_bits[3] ) ;   // Bank 
                        req.next_addr_vec[5] = slice_lower_bits(next_addr,  addr_bits[5] ) ;   // Column 
                        req.next_addr_vec[int(T::Level::Row)]     = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Row)] ) ;   // Row 
                        break;
                    }
                    case int(Type::RoBaBg1CoBg0RaCh):
                    {
                        //string DDR4::level_str [int(Level::MAX)] = {"Ch", "Ra", "Bg", "Ba", "Ro", "Co"};
                        //Least bits represents the highest parallelism possible [Should be baseline of Streaming]
                        req.next_addr_vec[int(T::Level::Channel)] = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                        req.next_addr_vec[int(T::Level::Rank)]    = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                        req.next_addr_vec[2]                      = slice_lower_bits(next_addr,  1 )                                 ;   // Bank Group0 BG0
                        req.next_addr_vec[int(T::Level::Column)]  = slice_lower_bits(next_addr, addr_bits[int(T::Level::Column)])    ;  //Column                  
                        req.next_addr_vec[2]                     += (slice_lower_bits(next_addr,  addr_bits[2]-1)<<1)                ;   // Remaining Bank Groups 
                        req.next_addr_vec[3]                      = slice_lower_bits(next_addr,  addr_bits[3]  )                     ;     // Bank bits 
                        req.next_addr_vec[int(T::Level::Row)]     = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Row)] )     ;   // Row 

                        break;
                    }                   
                    case int(Type::StrmPartRoBaBg1CoBg0RaCh):
                    {
                        //string DDR4::level_str [int(Level::MAX)] = {"Ch", "Ra", "Bg", "Ba", "Ro", "Co"};
                        //Least bits represents the highest parallelism possible [Should be baseline of Streaming]
                        req.next_addr_vec[int(T::Level::Channel)] = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                        req.next_addr_vec[int(T::Level::Rank)]    = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                        req.next_addr_vec[2]                      = slice_lower_bits(next_addr,  1 )                                 ;   // Bank Group0 BG0
                        req.next_addr_vec[int(T::Level::Column)]  = slice_lower_bits(next_addr, addr_bits[int(T::Level::Column)])    ;  //Column                  
                        //req.next_addr_vec[2]                     += (slice_lower_bits(next_addr,  addr_bits[2]-1)<<1)                ;   // Remaining Bank Groups 
                        //@todo  Better bank utilization if number of streams less than BA+BG0 bits                     
                        req.next_addr_vec[2]                      +=  ((req.metaISAStreamID&0x1)<<1)  ; 
                        //@todo make it generic to support any number of banks (in DDR4 they are 4)
                        req.next_addr_vec[3]                      = slice_lower_bits(next_addr,  addr_bits[3]  )                     ;     // Bank bits 
                        req.next_addr_vec[3] &= ((req.metaISAStreamID>>1)&0x3) ; 
                        req.next_addr_vec[int(T::Level::Row)]     = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Row)] )     ;   // Row 

                        break;
                    }                 
                    
                    case int(Type::InterStreamChRoCoBaRa):
                    {
                        //Different Streams -> Different Channels  then  Different Bank Groups. 

                        req.next_addr_vec[int(T::Level::Rank)]    = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                        req.next_addr_vec[2] = slice_lower_bits(next_addr,  addr_bits[2] ) ;   // Bank Group
                        req.next_addr_vec[3] = slice_lower_bits(next_addr,  addr_bits[3] ) ;   // Bank 
                        req.next_addr_vec[5] = slice_lower_bits(next_addr,  addr_bits[5] ) ;   // Column 
                        req.next_addr_vec[int(T::Level::Row)]     = slice_lower_bits(next_addr,  addr_bits[int(T::Level::Row)] ) ;   // Row 
                        int mapped_channel = (req.metaISAStreamID )%this->channels;
                        req.next_addr_vec[0] =  mapped_channel; //slice_lower_bits(next_addr,mapped_channel);
                        break;
                    }
                    case int(Type::ChRaBaRoCo):
                    {
                           for (int i = addr_bits.size() - 1; i >= 0; i--)
                            req.next_addr_vec[i] = slice_lower_bits(next_addr, addr_bits[i]);
                        break;
                    }
                    case int(Type::RoBaRaCoCh):
                    {
                        req.next_addr_vec[0] = slice_lower_bits(next_addr, addr_bits[0]);
                        req.next_addr_vec[addr_bits.size() - 1] = slice_lower_bits(next_addr, addr_bits[addr_bits.size() - 1]);
                        for (int i = 1; i <= int(T::Level::Row); i++)
                            req.next_addr_vec[i] = slice_lower_bits(next_addr, addr_bits[i]);
                        break;
                    }
                    //rank[ addr_vec[1]] , bg[addr_vec[2]] , ba[  addr_vec[3]] , row[addr_vec[4]] , ch[addr_vec[0]]  , col[ addr_vec[5] ]"
                    case int(Type::InterStellarRT)://Special Address Mapping BA1-0 BG1 ROW COL BG0 Burst
                        //overwrite MSB addr                 
                        next_addr  &= partition_mask; 
                        next_addr  |= (req.coreid<<(shift_bits_ba_bg-partbits)); 
                        req.next_addr_vec[0] = slice_lower_bits(next_addr,  addr_bits[0] ) ;  // Channel 
                        req.next_addr_vec[1] = slice_lower_bits(next_addr,  addr_bits[1] ) ;   // Rank
                        if(rtAddrMaping==RTAddrMaping::map2)//interleave pages if InterStellar Design 
                            interleaveBGbits = 1 ; 
                        if(rtAddrMaping==RTAddrMaping::map4)//interleave pages if InterStellar Design 
                            interleaveBGbits = 2 ;                             
                        req.next_addr_vec[2] = slice_lower_bits(next_addr,  interleaveBGbits   ) ;  // Least bit of Bang Group 0  
                        req.next_addr_vec[addr_bits.size() - 1] = slice_lower_bits(next_addr, addr_bits[addr_bits.size() - 1]);  //Column                  
                        // Do Paritioning. 
                        req.next_addr_vec[2] += ((slice_lower_bits(next_addr,  addr_bits[2]-interleaveBGbits))<< interleaveBGbits);   // Remaining Bank Groups 
                        req.next_addr_vec[3] += slice_lower_bits(next_addr,  addr_bits[3]  ) ;     // Bank bits 
                        req.next_addr_vec[4] = slice_lower_bits(next_addr,  addr_bits[4]       ) ;   // Row

                        break;    
                        
                    default:
                        assert(false);
                }
            }


        }
        // if(bank_part==1)
        // {
        //         if (spec->standard_name.substr(0, 4) == "DDR4")
        //         {
        //             printf("Old Ba=%d,Bg=%d\n",req.addr_vec[ int(T::Level::Bank)] ,req.addr_vec[ 2 ]);
        //             int offsetReq = 0 , offsetNxtReq = 0;
        //             if(banksPerPartition>1)
        //             {
        //                 offsetReq    = req.addr_vec[ int(T::Level::Bank)]%banksPerPartition       ;
        //                 offsetNxtReq = req.next_addr_vec[ int(T::Level::Bank)]%banksPerPartition  ;                           
        //             }
        //             req.addr_vec[ int(T::Level::Bank)]      =    offsetReq    + req.coreid * banksPerPartition;  
        //             req.next_addr_vec[ int(T::Level::Bank)] =    offsetNxtReq + req.coreid * banksPerPartition;  
        //             offsetReq = 0 , offsetNxtReq = 0;
        //             if(grpsPerPartition>1)
        //             {
        //                 offsetReq    = req.addr_vec[2 ]% (int)grpsPerPartition  ;
        //                 offsetNxtReq = req.next_addr_vec[2 ]% (int)grpsPerPartition  ;
        //             }
        //             req.addr_vec[ 2]      =   (int) ( offsetReq    +  req.coreid * grpsPerPartition); 
        //             req.next_addr_vec[2] =   (int) (offsetNxtReq  +  req.coreid * grpsPerPartition); 
        //             printf("New Ba=%d,Bg=%d\n",req.addr_vec[ int(T::Level::Bank)] ,req.addr_vec[ 2]);

        //         }
        //         if (spec->standard_name.substr(0, 4) == "DDR3")
        //         {
        //             int offsetReq = 0 , offsetNxtReq = 0;
        //             if(banksPerPartition>1)
        //             {
        //                 offsetReq    = req.addr_vec[ int(T::Level::Bank)]%banksPerPartition       ;  
        //                 offsetNxtReq = req.next_addr_vec[ int(T::Level::Bank)]%banksPerPartition  ;                      
        //             }
        //             printf("old Ba=%d \n",req.addr_vec[ int(T::Level::Bank)] );
        //             req.addr_vec[ int(T::Level::Bank)]      = offsetReq     + req.coreid * banksPerPartition ;                     
        //             req.next_addr_vec[ int(T::Level::Bank)] = offsetNxtReq  + req.coreid * banksPerPartition ;                     
        //             printf("New Ba=%d \n",req.addr_vec[ int(T::Level::Bank)] );

        //         }                
        // }

        if(memory_trace_flag)
        {

            if (spec->standard_name.substr(0, 4) == "DDR4")
           {
                printf("MC-Mem : addr=%#lx , addr => Ch = %x , Ran=%x , BG=%x, Ba=%x, Ro=%x, Co=%x\n",req.addr , req.addr_vec[0]     ,req.addr_vec[1]     ,req.addr_vec[2]     ,req.addr_vec[3]     ,req.addr_vec[4]     ,req.addr_vec[5]);
                if(req.nextAddr!=0)
                    printf("       : next addr=%#lx , addr => Ch = %x , Ran=%x , BG=%x, Ba=%x, Ro=%x, Co=%x\n",req.nextAddr , req.next_addr_vec[0]     ,req.next_addr_vec[1]     ,req.next_addr_vec[2]     ,req.next_addr_vec[3]     ,req.next_addr_vec[4]     ,req.next_addr_vec[5]);
                else 
                    printf("       : next addr=%#lx\n",req.nextAddr);
            }
            if (spec->standard_name.substr(0, 4) == "DDR3")
            {
                printf("MC-Mem : addr=%#lx , addr => Ch = %x , Ran=%x , Ba=%x , Ro=%x, Co=%x\n",req.addr , req.addr_vec[0]     ,req.addr_vec[1]     ,req.addr_vec[2]     ,req.addr_vec[3]     ,req.addr_vec[4]      );
                if(req.nextAddr!=0)
                    printf("       : next addr=%#lx , addr => Ch = %x , Ran=%x , Ba=%x, Ro=%x, Co=%x\n",req.nextAddr , req.next_addr_vec[0]     ,req.next_addr_vec[1]     ,req.next_addr_vec[2]     ,req.next_addr_vec[3]     ,req.next_addr_vec[4]     );
                else 
                    printf("       : next addr=%#lx\n",req.nextAddr);
            }        
        }

        if(ctrls[req.addr_vec[0]]->enqueue(req)) {
            // tally stats here to avoid double counting for requests that aren't enqueued
            ++num_incoming_requests;
            if (req.type == Request::Type::READ) {
              ++num_read_requests[coreid];
              if(req.metaISAStreamType==DIR_STREAM)
                  ++num_read_requests_dir_stream[coreid];
              if(req.metaISAStreamType==INDIR_STREAM)
                  ++num_read_requests_indir_stream[coreid];
              if(req.metaISAStreamType==NONE)                
                  ++num_read_requests_none_streams[coreid];
              ++incoming_read_reqs_per_channel[req.addr_vec[int(T::Level::Channel)]];
            }
            if (req.type == Request::Type::WRITE) {
              ++num_write_requests[coreid];
            }
            ++incoming_requests_per_channel[req.addr_vec[int(T::Level::Channel)]];
            return true;
        }

        return false;
    }
    
    // Abotaleb -> Send Pointer to The Intelligent Prefetcher Inside MC
    virtual bool updatePrefetcher(int ctrlNum , circBuffer<ramulator::iBufferKeyClass, ramulator::IpreftecherQueueEntry> * iBatchBuffer[])
    {
        ctrls[ctrlNum]->updatePrefetcher(iBatchBuffer);
        return true;
    }
    virtual int get_num_cores(int ctrlNum)
    {
        return ctrls[ctrlNum]->getCoresNum();
    }
    virtual int get_ctrls_num()
    {
        return ctrls.size() ; 
    }



    void init_mapping_with_file(string filename){
        ifstream file(filename);
        assert(file.good() && "Bad mapping file");
        // possible line types are:
        // 0. Empty line
        // 1. Direct bit assignment   : component N   = x
        // 2. Direct range assignment : component N:M = x:y
        // 3. XOR bit assignment      : component N   = x y z ...
        // 4. Comment line            : # comment here
        string line;
        char delim[] = " \t";
        while (getline(file, line)) {
            short capture_flags = 0;
            int level = -1;
            int target_bit = -1, target_bit2 = -1;
            int source_bit = -1, source_bit2 = -1;
            // cout << "Processing: " << line << endl;
            bool is_range = false;
            while (true) { // process next word
                size_t start = line.find_first_not_of(delim);
                if (start == string::npos) // no more words
                    break;
                size_t end = line.find_first_of(delim, start);
                string word = line.substr(start, end - start);
                
                if (word.at(0) == '#')// starting a comment
                    break;
                
                size_t col_index;
                int source_min, target_min, target_max;
                switch (capture_flags){
                    case 0: // capturing the component name
                        // fetch component level from channel spec
                        for (int i = 0; i < int(T::Level::MAX); i++)
                            if (word.find(T::level_str[i]) != string::npos) {
                                level = i;
                                capture_flags ++;
                            }
                        break;

                    case 1: // capturing target bit(s)
                        col_index = word.find(":");
                        if ( col_index != string::npos ){
                            target_bit2 = stoi(word.substr(col_index+1));
                            word = word.substr(0,col_index);
                            is_range = true;
                        }
                        target_bit = stoi(word);
                        capture_flags ++;
                        break;

                    case 2: //this should be the delimiter
                        assert(word.find("=") != string::npos);
                        capture_flags ++;
                        break;

                    case 3:
                        if (is_range){
                            col_index = word.find(":");
                            source_bit  = stoi(word.substr(0,col_index));
                            source_bit2 = stoi(word.substr(col_index+1));
                            assert(source_bit2 - source_bit == target_bit2 - target_bit);
                            source_min = min(source_bit, source_bit2);
                            target_min = min(target_bit, target_bit2);
                            target_max = max(target_bit, target_bit2);
                            while (target_min <= target_max){
                                mapping_scheme[level][target_min].push_back(source_min);
                                // cout << target_min << " <- " << source_min << endl;
                                source_min ++;
                                target_min ++;
                            }
                        }
                        else {
                            source_bit = stoi(word);
                            mapping_scheme[level][target_bit].push_back(source_bit);
                        }
                }
                if (end == string::npos) { // this is the last word
                    break;
                }
                line = line.substr(end);
            }
        }
        if (dump_mapping)
            dump_mapping_scheme();
    }
    
    void dump_mapping_scheme(){
        cout << "Mapping Scheme: " << endl;
        for (MapScheme::iterator mapit = mapping_scheme.begin(); mapit != mapping_scheme.end(); mapit++)
        {
            int level = mapit->first;
            for (MapSchemeEntry::iterator entit = mapit->second.begin(); entit != mapit->second.end(); entit++){
                cout << T::level_str[level] << "[" << entit->first << "] := ";
                cout << "PhysicalAddress[" << *(entit->second.begin()) << "]";
                entit->second.erase(entit->second.begin());
                for (MapSrcVector::iterator it = entit->second.begin() ; it != entit->second.end(); it ++)
                    cout << " xor PhysicalAddress[" << *it << "]";
                cout << endl;
            }
        }
    }
    
    void apply_mapping(long addr, std::vector<int>& addr_vec){
        int *sz = spec->org_entry.count;
        int addr_total_bits = sizeof(addr_vec)*8;
        int addr_bits [int(T::Level::MAX)];
        for (int i = 0 ; i < int(T::Level::MAX) ; i ++)
        {
            if ( i != int(T::Level::Row))
            {
                addr_bits[i] = calc_log2(sz[i]);
                addr_total_bits -= addr_bits[i];
            }
        }
        // Row address is an integer.
        addr_bits[int(T::Level::Row)] = min((int)sizeof(int)*8, max(addr_total_bits, calc_log2(sz[int(T::Level::Row)])));

        // printf("Address: %lx => ",addr);
        for (unsigned int lvl = 0; lvl < int(T::Level::MAX); lvl++)
        {
            unsigned int lvl_bits = addr_bits[lvl];
            addr_vec[lvl] = 0;
            for (unsigned int bitindex = 0 ; bitindex < lvl_bits ; bitindex++){
                bool bitvalue = false;
                for (MapSrcVector::iterator it = mapping_scheme[lvl][bitindex].begin() ;
                    it != mapping_scheme[lvl][bitindex].end(); it ++)
                {
                    bitvalue = bitvalue xor get_bit_at(addr, *it);
                }
                addr_vec[lvl] |= (bitvalue << bitindex);
            }
            // printf("%s: %x, ",T::level_str[lvl].c_str(),addr_vec[lvl]);
        }
        // printf("\n");
    }

    bool upgrade_prefetch_req(long addr) {
        Request tmp_req;
        tmp_req.addr = addr;
        tmp_req.type = Request::Type::READ;
        update_addr_vec(tmp_req);

        return ctrls[tmp_req.addr_vec[0]]->upgrade_prefetch_req(tmp_req);
    }

    void update_addr_vec(Request& req) {
        req.addr_vec.resize(addr_bits.size());
        long addr = req.addr;

        // Each transaction size is 2^tx_bits, so first clear the lowest tx_bits bits
        clear_lower_bits(addr, tx_bits);
        int interleaveBGbits = 0 ; 

        switch(int(type)){
            //InterStellar InterStreamMultiChannelSched (ISMS)
            case int(Type::RoCoBaRaCh):
            {
                //string DDR4::level_str [int(Level::MAX)] = {"Ch", "Ra", "Bg", "Ba", "Ro", "Co"};
                //Least bits represents the highest parallelism possible [Should be baseline of Streaming]
                req.addr_vec[int(T::Level::Channel)] = slice_lower_bits(addr,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                req.addr_vec[int(T::Level::Rank)]    = slice_lower_bits(addr,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                req.addr_vec[2] = slice_lower_bits(addr,  addr_bits[2] ) ;   // Bank Group
                req.addr_vec[3] = slice_lower_bits(addr,  addr_bits[3] ) ;   // Bank 
                req.addr_vec[5] = slice_lower_bits(addr,  addr_bits[5] ) ;   // Column 
                req.addr_vec[int(T::Level::Row)]     = slice_lower_bits(addr,  addr_bits[int(T::Level::Row)] ) ;   // Row 
                break;
            }
            case int(Type::RoBaBg1CoBg0RaCh):
            {
                //string DDR4::level_str [int(Level::MAX)] = {"Ch", "Ra", "Bg", "Ba", "Ro", "Co"};
                //Least bits represents the highest parallelism possible [Should be baseline of Streaming]
                req.addr_vec[int(T::Level::Channel)] =  slice_lower_bits(addr,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                req.addr_vec[int(T::Level::Rank)]    =  slice_lower_bits(addr,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                req.addr_vec[2]                      =  slice_lower_bits(addr,  1 )                                 ;   // Bank Group0 BG0
                req.addr_vec[int(T::Level::Column)]  =  slice_lower_bits(addr, addr_bits[int(T::Level::Column)])    ;   //Column                  
                req.addr_vec[2]                      += (slice_lower_bits(addr,  addr_bits[2]-1)<<1)                ;   // Remaining Bank Groups 
                req.addr_vec[3]                      =  slice_lower_bits(addr,  addr_bits[3]  )                     ;   // Bank bits 
                req.addr_vec[int(T::Level::Row)]     =  slice_lower_bits(addr,  addr_bits[int(T::Level::Row)] )     ;   // Row 
                break;
            }                    
            case int(Type::StrmPartRoBaBg1CoBg0RaCh):
            {
                //string DDR4::level_str [int(Level::MAX)] = {"Ch", "Ra", "Bg", "Ba", "Ro", "Co"};
                //Least bits represents the highest parallelism possible [Should be baseline of Streaming]
                req.addr_vec[int(T::Level::Channel)] =  slice_lower_bits(addr,  addr_bits[int(T::Level::Channel)] ) ;   // Channel 
                req.addr_vec[int(T::Level::Rank)]    =  slice_lower_bits(addr,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                req.addr_vec[2]                      =  slice_lower_bits(addr,  1 )                                 ;   // Bank Group0 BG0
                req.addr_vec[int(T::Level::Column)]  =  slice_lower_bits(addr, addr_bits[int(T::Level::Column)])    ;   //Column                  
                //@todo  Better bank utilization if number of streams less than BA+BG0 bits                     
                req.addr_vec[2]                      +=  ((req.metaISAStreamID&0x1)<<1)  ;          
                req.addr_vec[3]                      =  slice_lower_bits(addr,  addr_bits[3]  )                     ;   // Bank bits 
                req.addr_vec[3] &= ((req.metaISAStreamID>>1)&0x3) ; 
                req.addr_vec[int(T::Level::Row)]     =  slice_lower_bits(addr,  addr_bits[int(T::Level::Row)] )     ;   // Row 
                break;
            }                    
            case int(Type::InterStreamChRoCoBaRa):
            {
                req.addr_vec[int(T::Level::Rank)]    = slice_lower_bits(addr,  addr_bits[int(T::Level::Rank)] )    ;   // Rank
                req.addr_vec[2] = slice_lower_bits(addr,  addr_bits[2] ) ;   // Bank Group
                req.addr_vec[3] = slice_lower_bits(addr,  addr_bits[3] ) ;   // Bank 
                req.addr_vec[5] = slice_lower_bits(addr,  addr_bits[5] ) ;   // Column 
                req.addr_vec[int(T::Level::Row)]     = slice_lower_bits(addr,  addr_bits[int(T::Level::Row)] ) ;   // Row 
                int mapped_channel = req.metaISAStreamID%this->channels;
                req.addr_vec[0] =  slice_lower_bits(addr,mapped_channel);
                break;
            }
            case int(Type::ChRaBaRoCo):
            {
                for (int i = addr_bits.size() - 1; i >= 0; i--)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                break;
            }
            case int(Type::RoBaRaCoCh):
            {
                req.addr_vec[0] = slice_lower_bits(addr, addr_bits[0]);
                req.addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr, addr_bits[addr_bits.size() - 1]);                    
                for (int i = 1; i <= int(T::Level::Row); i++)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                
                break;
            }
            // How address vector is assigned to col , rank , bank, bank group , channel !
            //r[ addr_vec[1]] , bg[addr_vec[2]] , b[  addr_vec[3]] , row[addr_vec[4]] , ch[addr_vec[0]]  , col[ addr_vec[5] ]"
            case int(Type::InterStellarRT)://Special Address Mapping BA1-0 BG1 ROW COL BG0 Burst
            {
                //overwrite MSB addr      
                //if(debug_InterStellar_Mapping)
                //    printf("Old addr = %lx\n" , addr);           
                // Do Paritioning. 
                addr  &= partition_mask; 
                addr  |= (req.coreid<<(shift_bits_ba_bg-partbits)); 
                //if(debug_InterStellar_Mapping)
                //  printf("coreid = %d , addr=%lx\n" , req.coreid , addr);           
                req.addr_vec[0] = slice_lower_bits(addr,  addr_bits[0]);  // Channel 
                req.addr_vec[1] = slice_lower_bits(addr,  addr_bits[1]);   // Rank
                if(rtAddrMaping==RTAddrMaping::map2)//interleave pages if InterStellar Design 
                    interleaveBGbits = 1 ; 
                if(rtAddrMaping==RTAddrMaping::map4)//interleave pages if InterStellar Design 
                    interleaveBGbits = 2 ;                  
                req.addr_vec[2] = slice_lower_bits(addr, interleaveBGbits );  // Least bit of Bang Group 0  
                req.addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr, addr_bits[addr_bits.size() - 1]);    //Column                  
                req.addr_vec[2] += (slice_lower_bits(addr,  addr_bits[2]-interleaveBGbits)<<interleaveBGbits);   // Remaining Bank Groups 
                req.addr_vec[3] = slice_lower_bits(addr,  addr_bits[3]);                                         // Bank bits 
                req.addr_vec[4] = slice_lower_bits(addr,  addr_bits[4]);                                         // Row

                //if(debug_InterStellar_Mapping)                                
                //   printf("\t\t Crnt Addr => Ch = %x , Ra=%x , BG=%x, Ba=%x, Ro=%x, Co=%x\n",req.addr_vec[0],req.addr_vec[1],req.addr_vec[2],req.addr_vec[3],req.addr_vec[4],req.addr_vec[5]);
                break;        
            }
            default:
                assert(false);
        }
    }

    int pending_requests()
    {
        int reqs = 0;
        for (auto ctrl: ctrls)
            reqs += ctrl->readq.size() + ctrl->writeq.size() + ctrl->otherq.size() + ctrl->actq.size() + ctrl->pending.size();
        return reqs;
    }

    void set_high_writeq_watermark(const float watermark) {
        for (auto ctrl: ctrls)
            ctrl->set_high_writeq_watermark(watermark);
    }

    void set_low_writeq_watermark(const float watermark) {
    for (auto ctrl: ctrls)
        ctrl->set_low_writeq_watermark(watermark);
    }

    void finish(void) {
      dram_capacity = max_address;
      int  *sz = spec->org_entry.count;
      long *num_read_requests_per_cor_arr,coresnum; 
      maximum_bandwidth = spec->speed_entry.rate * 1e6 * spec->channel_width * sz[int(T::Level::Channel)] / 8;
      long dram_cycles = num_dram_cycles.value();
      num_read_requests_per_cor_arr = (long*) malloc(sizeof(long)*coresNum);   
      for(int i = 0 ; i< coresNum;i++)
          num_read_requests_per_cor_arr[i]=num_read_requests[i].value();
      for (auto ctrl : ctrls) {
        long read_req = long(incoming_read_reqs_per_channel[ctrl->channel->id].value());
        ctrl->finish(read_req, num_read_requests_per_cor_arr,coresNum, dram_cycles);
      }

      // finalize average queueing requests
      in_queue_req_num_avg = in_queue_req_num_sum.value() / dram_cycles;
      in_queue_read_req_num_avg = in_queue_read_req_num_sum.value() / dram_cycles;
      in_queue_write_req_num_avg = in_queue_write_req_num_sum.value() / dram_cycles;
    }

    long page_allocator(long addr, int coreid) {
        long virtual_page_number = addr >> 12;

        switch(int(translation)) {
            case int(Translation::None): {
              return addr;
            }
            case int(Translation::Random): {
                auto target = make_pair(coreid, virtual_page_number);
                if(page_translation.find(target) == page_translation.end()) {
                    // page doesn't exist, so assign a new page
                    // make sure there are physical pages left to be assigned

                    // if physical page doesn't remain, replace a previous assigned
                    // physical page.
                    if (!free_physical_pages_remaining) {
                      physical_page_replacement++;
                      long phys_page_to_read = lrand() % free_physical_pages.size();
                      assert(free_physical_pages[phys_page_to_read] != -1);
                      page_translation[target] = phys_page_to_read;
                    } else {
                        // assign a new page
                        long phys_page_to_read = lrand() % free_physical_pages.size();
                        // if the randomly-selected page was already assigned
                        if(free_physical_pages[phys_page_to_read] != -1) {
                            long starting_page_of_search = phys_page_to_read;

                            do {
                                // iterate through the list until we find a free page
                                // TODO: does this introduce serious non-randomness?
                                ++phys_page_to_read;
                                phys_page_to_read %= free_physical_pages.size();
                            }
                            while((phys_page_to_read != starting_page_of_search) && free_physical_pages[phys_page_to_read] != -1);
                        }

                        assert(free_physical_pages[phys_page_to_read] == -1);

                        page_translation[target] = phys_page_to_read;
                        free_physical_pages[phys_page_to_read] = coreid;
                        --free_physical_pages_remaining;
                    }
                }

                // SAUGATA TODO: page size should not always be fixed to 4KB
                return (page_translation[target] << 12) | (addr & ((1 << 12) - 1));
            }
            default:
                assert(false);
        }

    }

private:

    int calc_log2(int val){
        int n = 0;
        while ((val >>= 1))
            n ++;
        return n;
    }
    int slice_lower_bits(long& addr, int bits)
    {
        int lbits = addr & ((1<<bits) - 1);
        addr >>= bits;
        return lbits;
    }

    /****************************************************************
     * @author Abdelrhman Abotaleb  abotalea@mcmaster.ca 
     * addrvec_to_addr = utility function to convert addr_vec to addr 
     *  "not tested"
     * 
     ****************************************************************/
    void addrvec_to_addr(vector<int> addr_vec,long& addr)
    {
        //Conversion for 
        if(type == Type::ChRaBaRoCo)
        {
            addr=addr_vec[addr_bits.size() - 1]; //initially addr= col
           int totShift = 0;
            for (int i = addr_bits.size() - 2; i >= 0; i--)
            {
                totShift += addr_bits[i+1];        
                addr += ( addr_vec[i]<<totShift);
            }
        }
        else 
            addr = 0 ;// @TODO Implement it 
    }    
 





    bool get_bit_at(long addr, int bit)
    {
        return (((addr >> bit) & 1) == 1);
    }

    void clear_lower_bits(long& addr, int bits)
    {
        addr >>= bits;
    }

    long lrand(void) {
        if(sizeof(int) < sizeof(long)) {
            return static_cast<long>(rand()) << (sizeof(int) * 8) | rand();
        }

        return rand();
    }
};

} /*namespace ramulator*/

#endif /*__MEMORY_H*/
