/***************************** SCHEDULER.H ***********************************
- SAFARI GROUP

This file contains the different scheduling policies and row policies that the 
memory controller can use to schedule requests.

Current Memory Scheduling Policies:

1) FCFS - First Come First Serve
        This scheduling policy schedules memory requests chronologically

2) FRFCFS - Frist Ready First Come First Serve
        This scheduling policy first checks if a request is READY(meets all 
        timing parameters), if yes then it is prioritized. If multiple requests
        are ready, they they are scheduled chronologically. Otherwise, it 
        behaves the same way as FCFS. 

3) FRFCFS_Cap - First Ready First Come First Serve Cap
       This scheduling policy behaves the same way as FRFCS, except that it has
       a cap on the number of hits you can get in a certain row. The CAP VALUE
       can be altered by changing the number for the "cap" variable in 
       line number 76. 

4) FRFCFS_PriorHit - First Ready First Come First Serve Prioritize Hits
       This scheduling policy behaves the same way as FRFCFS, except that it
       prioritizes row hits more than readiness. 

You can select which scheduler you want to use by changing the value of 
"type" variable on line number 74.

                _______________________________________

Current Row Policies:

1) Closed   - Precharges a row as soon as there are no pending references to 
              the active row.
2) ClosedAP - Closed Auto Precharge
3) Opened   - Precharges a row only if there are pending references to 
              other rows.
[Abotaleb - Huawei Project Addition:]
4) IPP      - Precharge a row :
                  1) if there are pending refereces to other rows.
                  2) If from MetaISA we expects that next stream access will
                        reference other rows. 
5) Timeout  - Precharges a row after X time if there are no pending references.
              'X' time can be changed by changing the variable timeout 
              on line number 221
Abotaleb :: Row Policy can be modified now through configuration file.

*****************************************************************************/

#ifndef __SCHEDULER_H
#define __SCHEDULER_H

#include "DRAM.h"
#include "Request.h"
#include "Controller.h"
#include <vector>
#include <map>
#include <list>
#include <functional>
#include <cassert>
#include "IDirStreamBuffer.h"
#include "interstellar/base_metaisa.hpp"
//PARBS+BLISS Support
#include "SchedulerBase.h"
#include "BLISS.h"
#include "PARBS.h"


using namespace std;
#define MASK_CORES        0xF
#define MASK_THREADS      0xF
#define CORE_BITS         16 

namespace ramulator
{
    const int InterStellarRT_STEP = 64; 
    //InterStellar
    enum class RTAddrMaping {
            map1     ,
            map2     ,
            map4     , 
    } ;
template <typename T>
class Controller;

template <typename T>
class Scheduler
{
public:
    Controller<T>* ctrl    ;
    uint*     PARBS_rank   ; 

    enum class Type {
        FCFS, FRFCFS, FRFCFS_Cap, FRFCFS_PriorHit, BLISS, PARBS, MAX
    } type = Type::FRFCFS; //Change this line to change scheduling policy
    std::map<std::string, Type> configs_scheduling_map = 
    { {"FCFS"                   , Type::FCFS     }, 
      {"FRFCFS"                 , Type::FRFCFS   }, 
      {"FRFCFS_Cap"             , Type::FRFCFS_Cap }, 
      {"FRFCFS_PriorHit"        , Type::FRFCFS_PriorHit     }, 
      {"BLISS"                  , Type::BLISS   },
      {"PARBS"                  , Type::PARBS   }};

    long cap = 16; //Change this line to change cap
    SchedulerBase<T>* sched_base = NULL;
    IDirStreamBuffer * iDirStreamBuffer;
    int               enablePreSchedIPrefetch;
    /****************   rtBatch , force rtBatch  Mode  Start       ***********************/
    // in rtBatch : Force un-interrupted rtBatch
    std::map<std::string, bool> forceRT_batch_mode_map = {{"off", false}, {"on", true}};
    bool              forceRT_batch_mode;
    bool              rtBatchStarted    ; // The flag is set by the first request in the rtBatch and reset by last request in rtbatch 
    uint64_t          crntBatchID       ; // Identidy the batch with core(thread id) + (stream ID)


    /****************   rtBatch , force rtBatch  Mode   End        **********************/
    Scheduler(const Config& configs,Controller<T>* ctrl) : ctrl(ctrl) 
    {
        if (configs.contains("scheduling_policy")) 
            this->type =  configs_scheduling_map[configs["scheduling_policy"]];

        /****************************** */
        // InterStellarRT
        forceRT_batch_mode = false; 
        rtBatchStarted     = false;    
        if (configs.contains("forceRT_batch_mode")) 
            this->forceRT_batch_mode =  forceRT_batch_mode_map[configs["forceRT_batch_mode"]];
        
            
        /****************************** */

        if (this->type == Type::BLISS) {
            sched_base = new BLISS<T>(ctrl);
            sched_base->set_num_cores(configs.get_int("cores"));
        } else if (this->type == Type::PARBS) {
			int       cores_num     = configs.get_int("cores")     ; 
			unsigned  global_bcount = configs.get_int("channels")  ; 
			unsigned  local_bcount  = configs.get_int("ranks")*ctrl->channel->spec->org_entry.count[int(T::Level::Bank)]  ; 
			if (ctrl->channel->spec->standard_name == "DDR4" || ctrl->channel->spec->standard_name == "GDDR5")
                local_bcount *=  ctrl->channel->spec->org_entry.count[int(T::Level::Bank)-1];                 				             
			printf("Init PARBS with num_cores = %d , channels = %d , banks = %d \n",cores_num,global_bcount,local_bcount);
            sched_base = new PARBS<T>(ctrl);
			sched_base->set_num_cores(cores_num);
            sched_base->set_localBcount(local_bcount);
            sched_base->set_globalBcount(global_bcount);
            sched_base->initialize();
        }
        PARBS_rank = NULL;
    }

    void initIDirStreamTable(IDirStreamBuffer* pDirStreamBuffer)
    {
        iDirStreamBuffer = pDirStreamBuffer;
    }

    void enablePreSchedIPrefetcher(int flag)
    {
        enablePreSchedIPrefetch = flag;
    }

    void setCrntBatchStatus(bool isStarted,uint32_t   metaISAStreamID,uint32_t metaISARequestorID)
    {
        this->rtBatchStarted  = isStarted ; // true if started , false if finished 
        uint64_t crntBatchID  = (( (uint64_t) metaISAStreamID << METAISA_REQID_BITS)     +
                                     metaISARequestorID); 
    }
    list<Request>::iterator get_head(list<Request>& q)
    {
        if(forceRT_batch_mode && rtBatchStarted)
        {
            // force pick the rtBatch requests
            auto target_req = q.end();
            //First request that is ready in the act queue will be the FRFCFS
            for (auto itr = next(q.begin(), 1); itr != q.end(); itr++)
            {
                uint64_t target_req_batch_id  = (( (uint64_t) itr->metaISAStreamID << METAISA_REQID_BITS)     +
                        itr->metaISARequestorID);  
                if(target_req_batch_id==crntBatchID)
                        return itr;
                else 
                        return q.end();                               
            }   

        }
        // TODO make the decision at compile time
        if (type != Type::FRFCFS_PriorHit) {
            //If queue is empty, return end of queue
            if (!q.size())
                return q.end();

            //Else return based on the policy
            //printf("Requests exist in q:\n");
            //for (auto itr = q.begin(); itr != q.end(); itr++)
            //  printf("\t\t Q_Addr = %lx \n",itr->addr);  
            auto head = q.begin();
            for (auto itr = next(q.begin(), 1); itr != q.end(); itr++)
                head = compare[int(type)](head, itr);
            
            return head;
        } 
        else { //Code to get around edge cases for FRFCFS_PriorHit
            
            if (q.size()==0)
                return q.end();
            /*
            if (q.begin()==q.end())
                return q.end();
            */


            /****************************************************************************/
            //Else return based on FRFCFS_PriorHit Scheduling Policy
            auto head = q.begin();
            // Debug_Detailed [SCHEDULING]
            /*if(this->ctrl->clk==25926)
            {
                printf("\tFRFCFS_PriorHit\n");
            }*/
            for (auto itr = next(q.begin(), 1); itr != q.end(); itr++) 
            {   
                // Debug_Detailed [SCHEDULING]
                /*if(this->ctrl->clk==25926)
                {
                    printf("\t\t(Head_A:%#lx)(Crnt_A:%#lx)",head->addr,itr->addr);
                }*/
                head = compare[int(Type::FRFCFS_PriorHit)](head, itr);
                /*if(this->ctrl->clk==25926)
                {
                    printf("(New_Head_A:%#lx)\n",head->addr);
                }*/
            }

            /****************************************************************************/
            // Debug_Detailed [SCHEDULING]
            /*if(this->ctrl->clk==25926)
            {
                printf("\tCheck if Head is ready and Hit : ");
            }*/
            if (this->ctrl->is_ready(head) && this->ctrl->is_row_hit(head)) 
            {
                /*if(this->ctrl->clk==25926)
                {
                    printf("YES\n");
                }*/
                return head;
            }
            /*else
            {
                if(this->ctrl->clk==25926)
                {
                    printf("NO\n");
                }
            }*/

            /****************************************************************************/
            // Debug_Detailed [SCHEDULING]
            /*if(this->ctrl->clk==25926)
            {
                printf("\tPrepare List of Hit Requests : ");
            }*/
            // prepare a list of hit request
            vector<vector<int>> hit_reqs;
            for (auto itr = q.begin() ; itr != q.end() ; ++itr) {
                if (this->ctrl->is_row_hit(itr)) {
                    auto begin = itr->addr_vec.begin();
                    // TODO Here it assumes all DRAM standards use PRE to close a row
                    // It's better to make it more general.
                    auto end = begin + int(ctrl->channel->spec->scope[int(T::Command::PRE)]) + 1;
                    vector<int> rowgroup(begin, end); // bank or subarray
                    hit_reqs.push_back(rowgroup);
                    // Debug_Detailed [SCHEDULING]
                    /*if(this->ctrl->clk==25926)
                    {
                        printf("Hit_REQ ( A:%#16lx ) (T:%ld) (V:",  itr->addr,this->ctrl->clk);
                        int  lev = 0;
                        for(vector<int>::iterator it = rowgroup.begin() ; it != rowgroup.end(); it++,lev++  )
                        {
                                    if(lev!=0) 
                                        printf(",");
                                    printf(" %5x ", *it);
                        }   
                        printf(") \n");
                    }*/
                }
            }
            // if we can't find proper request, we need to return q.end(),
            // so that no command will be scheduled
            head = q.end();
            for (auto itr = q.begin(); itr != q.end(); itr++) {
                bool violate_hit = false;
                if ((!this->ctrl->is_row_hit(itr)) && this->ctrl->is_row_open(itr)) {
                    // so the next instruction to be scheduled is PRE, might violate hit
                    auto begin = itr->addr_vec.begin();
                    // TODO Here it assumes all DRAM standards use PRE to close a row
                    // It's better to make it more general.
                    auto end = begin + int(ctrl->channel->spec->scope[int(T::Command::PRE)]) + 1;
                    vector<int> rowgroup(begin, end); // bank or subarray
                    for (const auto& hit_req_rowgroup : hit_reqs) {
                        if (rowgroup == hit_req_rowgroup) {
                            violate_hit = true;
                            break;
                        }  
                    }
                }
                if (violate_hit) {
                    continue;
                }
                // If it comes here, that means it won't violate any hit request
                if (head == q.end()) {
                    head = itr;
                } else {
                    head = compare[int(Type::FRFCFS)](head, itr);
                }
            }

            return head;
        }
    }

//Compare functions for each memory schedulers
private:
    typedef list<Request>::iterator ReqIter;
    function<ReqIter(ReqIter, ReqIter)> compare[int(Type::MAX)] = {
        // FCFS
        [this] (ReqIter req1, ReqIter req2) {
            if (req1->arrive <= req2->arrive) return req1;
            return req2;},

        // FRFCFS
        [this] (ReqIter req1, ReqIter req2) {
            bool ready1 = this->ctrl->is_ready(req1);
            bool ready2 = this->ctrl->is_ready(req2);

            if (ready1 ^ ready2) {
                if (ready1) return req1;
                return req2;
            }

            if (req1->arrive <= req2->arrive) return req1;
            return req2;},

        // FRFCFS_CAP
        [this] (ReqIter req1, ReqIter req2) {
            bool ready1 = this->ctrl->is_ready(req1);
            bool ready2 = this->ctrl->is_ready(req2);

            ready1 = ready1 && (this->ctrl->rowtable->get_hits(req1->addr_vec) <= this->cap);
            ready2 = ready2 && (this->ctrl->rowtable->get_hits(req2->addr_vec) <= this->cap);

            if (ready1 ^ ready2) {
                if (ready1) return req1;
                return req2;
            }

            if (req1->arrive <= req2->arrive) return req1;
            return req2;},
        // FRFCFS_PriorHit
        [this] (ReqIter req1, ReqIter req2) {
            ////// Special Case : InterStellarRT is handled inside is_ready

            bool ready1 =  this->ctrl->is_ready(req1) && this->ctrl->is_row_hit(req1);
            bool ready2 =  this->ctrl->is_ready(req2) && this->ctrl->is_row_hit(req2);

            if (ready1 ^ ready2) {
                if (ready1) return req1;
                return req2;
            }
            bool hit1 = this->ctrl->is_row_hit(req1);
            bool hit2 = this->ctrl->is_row_hit(req2);

            if (hit1 ^ hit2) {
                if (hit1) return req1;
                return req2;
            }

            if (req1->arrive <= req2->arrive) return req1;
            return req2;},
        //BLISS
        [this] (ReqIter req1, ReqIter req2) {
            return sched_base->better_req(req1, req2);},
        //PARBS
        [this] (ReqIter req1, ReqIter req2) {
            return sched_base->better_req(req1, req2);}

    };
};


// Row Precharge Policy
template <typename T>
class RowPolicy
{
public:
    Controller<T>* ctrl;

      enum  Type {
        Closed    , 
		ClosedAP  , 
        TrueClosed , 
		Opened    ,
        Adaptive  , /* Adaptive Counter Solution */
        IPP       , 
	    Timeout   ,
		MAX       /* Number of row policy algorithms */
    } type = Type::Opened;
    std::map<std::string, Type> row_policy_map = 
    { {"Closed"       , Type::Closed     }, 
      {"ClosedAP"     , Type::ClosedAP   }, 
      {"TrueClosed"   , Type::TrueClosed }, 
      {"Opened"       , Type::Opened     }, 
      {"Adaptive"     , Type::Adaptive   }, 
      {"IPP"          , Type::IPP        }, 
      {"All_IPP"      , Type::IPP        }, 
      {"IPP_Dir"      , Type::IPP        }, 
      {"IPP_InDir"    , Type::IPP        }, 
      {"IPP_NoStream" , Type::IPP        }, 
      {"Timeout"      , Type::Timeout    },};

    enum IPP_Type{
        NOT_IPP       = 0x0,
		IPP_Dir       = 0x1, /* 0x1 , 0x2 , 0x4 are mutual exclusive ; 0x7 is covering both */
		IPP_InDir     = 0x2, 
        IPP_NoStream  = 0x4, 
		All_IPP       = 0x7,
    } ipp_type =IPP_Type::NOT_IPP ;


    int timeout = 50;

    /******
     * @author  Abotaleb 
     * @todo do good code practice : use map 
     * **************/
    RowPolicy(const Config& configs,Controller<T>* ctrl) : ctrl(ctrl) 
    {   
        
        type     = row_policy_map[configs["row_policy"]];
        if(type == Type::IPP)
            ipp_type = IPP_Type::All_IPP                    ;
        printf("Row Policy Selected = %d\n",type);
        if(configs["row_policy"]=="IPP_Dir")
        {
            type     = Type::IPP;
            ipp_type = IPP_Type::IPP_Dir;
        }
        if(configs["row_policy"]=="IPP_InDir")
        {
            type     = Type::IPP;
            ipp_type = IPP_Type::IPP_InDir;
        }
        if(configs["row_policy"]=="IPP_NoStream")
        {
            type     = Type::IPP;
            ipp_type = IPP_Type::IPP_NoStream;
        }       
         
    }

    vector<int> get_victim(typename T::Command cmd)
    {
        return policy[int(type)](cmd);
    }

private:
    function<vector<int>(typename T::Command)> policy[int(Type::MAX)] = {
        // Closed
        [this] (typename T::Command cmd) -> vector<int> {
            for (auto& kv : this->ctrl->rowtable->table) {
                if (!this->ctrl->is_ready(cmd, kv.first))
                    continue;
                return kv.first;
            }
            return vector<int>();},

        // ClosedAP
        [this] (typename T::Command cmd) -> vector<int> {
            for (auto& kv : this->ctrl->rowtable->table) {
                if (!this->ctrl->is_ready(cmd, kv.first))
                    continue;
                return kv.first;
            }
            return vector<int>();},

        // True CLosed
        [this] (typename T::Command cmd) {
            return vector<int>();},
        // Opened
        [this] (typename T::Command cmd) {
            return vector<int>();},
        // Adaptive
        [this] (typename T::Command cmd) {
            return vector<int>();},
        // IPP
        [this] (typename T::Command cmd) {
            return vector<int>();},
        // Timeout
        [this] (typename T::Command cmd) -> vector<int> {
            for (auto& kv : this->ctrl->rowtable->table) {
                auto& entry = kv.second;
                if (this->ctrl->clk - entry.timestamp < timeout)
                    continue;
                if (!this->ctrl->is_ready(cmd, kv.first))
                    continue;
                return kv.first;
            }
            return vector<int>();}
    };

};


template <typename T>
class RowTable
{
public:
    Controller<T>* ctrl;

    struct Entry {
        int row;
        int hits;
        long timestamp;
    };

    map<vector<int>, Entry> table;

    RowTable(Controller<T>* ctrl) : ctrl(ctrl) {}

    void update(typename T::Command cmd, const vector<int>& addr_vec, long clk)
    {
        auto begin = addr_vec.begin();
        auto end = begin + int(T::Level::Row);
        vector<int> rowgroup(begin, end); // bank or subarray
        int row = *end;

        T* spec = ctrl->channel->spec;

        if (spec->is_opening(cmd))
            table.insert({rowgroup, {row, 0, clk}});

        if (spec->is_accessing(cmd)) {
            // we are accessing a row -- update its entry
            auto match = table.find(rowgroup);
            assert(match != table.end());
            assert(match->second.row == row);
            match->second.hits++;
            match->second.timestamp = clk;
        } /* accessing */

        if (spec->is_closing(cmd)) {
          // we are closing one or more rows -- remove their entries
          int n_rm = 0;
          int scope;
          if (spec->is_accessing(cmd))
            scope = int(T::Level::Row) - 1; //special condition for RDA and WRA
          else
            scope = int(spec->scope[int(cmd)]);

          for (auto it = table.begin(); it != table.end();) {
            if (equal(begin, begin + scope + 1, it->first.begin())) {
              n_rm++;
              it = table.erase(it);
            }
            else
              it++;
          }

          assert(n_rm > 0);
        } /* closing */
    }

    int get_hits(const vector<int>& addr_vec, const bool to_opened_row = false)
    {
        auto begin = addr_vec.begin();
        auto end = begin + int(T::Level::Row);

        vector<int> rowgroup(begin, end);
        int row = *end;

        auto itr = table.find(rowgroup);
        if (itr == table.end())
            return 0;

        if(!to_opened_row && (itr->second.row != row))
            return 0;

        return itr->second.hits;
    }

    int get_open_row(const vector<int>& addr_vec) {
        auto begin = addr_vec.begin();
        auto end = begin + int(T::Level::Row);

        vector<int> rowgroup(begin, end);

        auto itr = table.find(rowgroup);
        if(itr == table.end())
            return -1;

        return itr->second.row;
    }
};

} /*namespace ramulator*/

#endif /*__SCHEDULER_H*/
