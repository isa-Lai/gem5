#ifndef  IPREFETCHER_CIRC_BUFFER_H
#define  IPREFETCHER_CIRC_BUFFER_H
#include <stdio.h>
#include <iostream>
#include <map>
using namespace std;

#include "Request.h"


namespace ramulator
{

    const int CACHE_LINE_SIZE              = 64;
    const int DEF_PREFTECH_STREAM_DEPTH    = 16; //Maximum Preftech Depth Per Stream
    const int DEF_IPREFETCHER_BUF_SIZE  = (1024*4)+1; // Size of iPrefetcher Per Bank 16 = 1 kB
    class iBufferKeyClass
    {
        public:
            uint64_t  addr;
	    	uint8_t  metaISAStreamID;
		    uint32_t metaISARequestorID;   
            //Constructor  
            iBufferKeyClass()
            {
                addr                 = 0x0;
            }  
            iBufferKeyClass(uint64_t addrIn,uint8_t  metaISAStreamIDIn,uint32_t metaISARequestorIDIn )  
            {
                addr                 =   addrIn;
                metaISAStreamID      =   metaISAStreamIDIn;
                metaISARequestorID   =   metaISARequestorIDIn;
            }
            bool sameStream( const iBufferKeyClass& k2 ) 
            {
                return ( this->metaISAStreamID    == k2.metaISAStreamID &&
                         this->metaISARequestorID == k2.metaISARequestorID); 
            }
            //Overload Comparator ==
            bool operator== (  const iBufferKeyClass& k2)
            {
                return (
                    this->addr               == k2.addr             );
                
                /*return (
                    this->addr               == k2.addr            && 
                    this->metaISAStreamID    == k2.metaISAStreamID &&
                    this->metaISARequestorID == k2.metaISARequestorID);*/
            }
            //Overload Operator < 
            bool operator<(  const iBufferKeyClass& k2) const
            {
                //Order based on streamID
                return (
                        //Unique field to do comparison (used in find !?)
                        this->addr    < k2.addr  
                     );
            }

            uint64_t getUniqueStreamID()
            {
                return      (( (uint64_t)this->metaISAStreamID << METAISA_REQID_BITS)     +
                                                        this->metaISARequestorID);
            }
    } ;
    
    
    
    
    typedef struct IPreftecherQueueEntry
    {
        uint8_t  data[CACHE_LINE_SIZE];     
        Request  *iBatchRequest       ;   
        uint8_t  W                    ;//Waiting for Data
        uint8_t  R                    ;//Requested by LLC  
        uint8_t  I                    ;//Has its command issued  
        uint8_t  pendAct               ;//in iBuffer but not in actq
        uint8_t  isStartSeg           ;//InterStellarRT ; Is it start request in rtBatch segment ? 
        long actualReqArrive = -1     ;//Used in iBatch to differentiate between actual request arrive and time when the iBatch is issued

        //uint16_t numPrefetches        ;//How many times its prefetch is issued       (If More than one prefetch is allowed (?))
    } IpreftecherQueueEntry;


    template<typename T1, typename T2>
    class circBuffer {
    public:
        circBuffer() : head_(0), tail_(0),pendActStartPos_(-1)
        {
            this->iPrefetcherBufferSize = DEF_IPREFETCHER_BUF_SIZE ; 
            keys_                       = NULL ;
            _length                     = 0    ;
        }
        void setCircBufferSize(uint32_t new_size)
        {
            this->iPrefetcherBufferSize = new_size;
            keys_  = new T1[iPrefetcherBufferSize];
        }
        bool IsFull() const {
            int next = (tail_ + 1) % iPrefetcherBufferSize;
            return next == head_;
        }
        void Push(const std::pair<T1, T2>& item) {
            int next = (tail_ + 1) % iPrefetcherBufferSize;
            auto it = data_.find(item.first);
            if (it != data_.end()) {
                // Key already exists, update its value
                it->second = item.second;
                return;
            }

            if (next == head_) {
                T1 head_key = keys_[head_];
                data_.erase(head_key);
                 _length-- ; // If Full then decrease the buffer length first when erase then increase after add 
                head_ = (head_ + 1) % iPrefetcherBufferSize;
            }
            data_[item.first] = item.second;
            keys_[tail_] = item.first;
            //Track first pendAct=1 entry (pendActStartPos_ is the tail if pendActStartPos_ = -1 (all pevious are not pendAct)
            // if (item.second.pendAct == 1 && pendActStartPos_ == -1) {
            //     pendActStartPos_ = tail_;
            // }
            if (pendActStartPos_ == -1) {
                auto ptr = reinterpret_cast<const IpreftecherQueueEntry*>(&item.second);
                if (ptr->pendAct == 1) {
                    pendActStartPos_ = tail_;
                }
            }
            tail_ = (tail_ + 1) % iPrefetcherBufferSize;
            _length++ ;
        }

        Request * getNextPendingRequest() {
            if (pendActStartPos_ == -1) {
                return NULL; // No pending actq requests
            }

            int i = pendActStartPos_;
            do {
                T1 key = keys_[i];
                T2& entry = data_.at(key);

                if (entry.pendAct == 1) {
                    entry.pendAct = 0;
                    pendActStartPos_ = (i + 1) % iPrefetcherBufferSize;
                    if (pendActStartPos_ == tail_) {
                        pendActStartPos_ = -1; // No more pending
                    }
                    return entry.iBatchRequest;
                }

                i = (i + 1) % iPrefetcherBufferSize;
            } while (i != tail_);

            // If none found
            pendActStartPos_ = -1;
            return NULL;  // No more entries with pendAct = 1
        }


        void Pop() {
            if (head_ != tail_) {
                T1 head_key = keys_[head_];
                data_.erase(head_key);
                head_ = (head_ + 1) % iPrefetcherBufferSize;
                _length -- ;
            }
        }

        void DisplayIprefetchFull() const {
            printf("Head = %d  - tail = %d \n",head_,tail_);
            int i = head_;
            while (i != tail_) {
                T1 key = keys_[i];
                //T2 in this case will be IpreftecherQueueEntry 
                printf( "A: %lx Stream ID = %d Requestor ID =%d  Requested ? = %d\n",key.addr,key.metaISAStreamID,key.metaISARequestorID,data_.at(key).R ) ;
                i = (i + 1) % iPrefetcherBufferSize;
            }
        }

        void DisplayIprefetchAddr() const {
            cout<<"Address :: Head = "<<head_<<" tail = "<<tail_<<endl;
            int i = head_;
            while (i != tail_) {
                T1 key = keys_[i];
                //T2 in this case will be "long" -> The Address
                printf( "A: %lx  = %d\t",key, data_.at(key)  ) ;
                i = (i + 1) % iPrefetcherBufferSize;
            }
        }
        
        // Check if a request with same stream ID and same bank ID , with not CMD issued exists in the iBatchBuffer
        bool isNotCMDIssuedOfSameStreamIDExists(T1 inKey)
        {
            long Addr = inKey.addr;

            for (int i = head_; i != tail_; i = (i + 1) % iPrefetcherBufferSize) 
            {

               T1 key = keys_[i];
               if (key.sameStream(inKey)) 
               {
                    //If it corresponds to non-issued iBatch
                    //In future iBatch (as there may be a request in previous iBatch that is not consumed)
                    if( data_.at(key).I ==0  &&  key.addr>inKey.addr) //  key.addr!=inKey.addr ) 
                        return true;
               }
            }
            return false;

        }

        bool    find(T1 key) 
        {
            auto it = data_.find(key);
            if (it == data_.end()) {
               // cout << "Key not found in the buffer." << endl;
                return false;;
            }
            return true;
        }
        T1 getDirStreamTableID(T1 key)
        {
            auto it = data_.find(key);
            if (it == data_.end()) {
               // cout << "Key not found in the buffer." << endl;
                iBufferKeyClass       EmptyObj ;
                T1 RetEmptyObj      =   (T1)EmptyObj;
                return RetEmptyObj;
            }
            return  it->first;
        }


        T2& operator[](T1 key)
        {
            T2 targetEntr ; 
            return data_.at(key);
        }

        void delete_data(T1 key) {
            auto it = data_.find(key);
            if (it == data_.end()) {
                cout << "Key not found in the buffer reason1." << endl;
                return;
            }
            int index = -1;
            
            //cout<<"Before delete :: Head = "<<head_<<" tail = "<<tail_<<endl;
            for (int i = head_; i != tail_; i = (i + 1) % iPrefetcherBufferSize) 
            {
                
               // printf( "A: %lx Stream ID = %d Requestor ID =%d   \t",keys_[i].addr,keys_[i].metaISAStreamID,keys_[i].metaISARequestorID  )  ; 
               // printf( "A: %lx Stream ID = %d Requestor ID =%d   \t",key.addr,key.metaISAStreamID,key.metaISARequestorID  )                 ; 

                if (keys_[i] == key) {
                    index = i;
                    break;
                }
            }
            if (index == -1) {
                cout << "Key not found in the buffer reason 2." << endl;  
                return;
            }
            data_.erase(key) ;
            _length--        ;
            for (int i = index; (i != tail_ - 1)    ;  i = (i + 1) % iPrefetcherBufferSize) {
                keys_[i] = keys_[(i + 1) % iPrefetcherBufferSize];
                if(tail_ == 0 && i == iPrefetcherBufferSize - 1)
                    break;
                //printf(" keys_[%d]=%lx - keys_[(i + 1)=%lx  \n",i,keys_[i].addr, keys_[(i + 1)% iPrefetcherBufferSize].addr);

            }
            tail_ = (tail_ - 1 + iPrefetcherBufferSize) % iPrefetcherBufferSize;
            //cout<<"After delete :: Head = "<<head_<<" tail = "<<tail_<<endl;

        }
        uint32_t  getLength()
        {
            return  _length;
        }

        uint32_t  getMaxSize()
        {
            return  iPrefetcherBufferSize;
        }
        
        int preftechStreamDepth ; 

    private:
        std::map<T1, T2> data_;
        T1  *    keys_                  ;
        int      head_                  ;
        int      tail_                  ;
        int      pendActStartPos_       ;   
        uint32_t iPrefetcherBufferSize  ;
        uint32_t _length                ; // actual number of elements stored 

    };

}

#endif 
