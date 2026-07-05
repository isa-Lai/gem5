#ifndef __IPREFECTCHER_H
#define __IPREFECTCHER_H

/**
 * Intelligent Prefetcher Iprefetcher.h
 *
 * @file    Iprefetcher.h
 * @brief   IPDQ Intelligenet Prefetcher Data Queue
 * @author  Abdelrhman Mohamed Abotaleb
 * @contact abotaleb@mcmaster.ca
 *          aabotaleb@eng.cu.edu.eg
 *
 */


#include <vector>
#include <functional>
#include <map>


using namespace std;

namespace ramulator
{

    const int CACHE_LINE_SIZE           = 64;
    const int DEF_PREFTECH_STREAM_DEPTH = 16; //Maximum Preftech Depth Per Stream

    class IpreftecherQueueKey
    {
        public:
            uint64_t  addr;
	    	uint8_t  metaISAStreamID;
		    uint32_t metaISARequestorID;
            //Constructor
            IpreftecherQueueKey(uint64_t addrIn,uint8_t  metaISAStreamIDIn,uint32_t metaISARequestorIDIn )
            {
                addr                 =   addrIn;
                metaISAStreamID      =   metaISAStreamIDIn;
                metaISARequestorID   =   metaISARequestorIDIn;

            }
            //Overload Comparator ==
            bool operator== (  const IpreftecherQueueKey& k2)
            {

                return (
                    this->addr               == k2.addr            &&
                    this->metaISAStreamID    == k2.metaISAStreamID &&
                    this->metaISARequestorID == k2.metaISARequestorID);
            }
            //Overload Operator <
            bool operator<(  const IpreftecherQueueKey& k2) const
            {
                //Order based on streamID
                return (
                        //Unique field to do comparison (used in find !?)
                        this->addr    < k2.addr
                     );
            }
    } ;

    typedef struct IPreftecherQueueEntry
    {
        uint8_t  data[CACHE_LINE_SIZE];
        uint8_t  W                    ;//Waiting for Data
        uint8_t  R                    ;//Requested by LLC
        //uint16_t numPrefetches        ;//How many times its prefetch is issued       (If More than one prefetch is allowed (?))
    } IpreftecherQueueEntry;

	class Iprefetcher
	{
    	public:

   	        map<IpreftecherQueueKey , IpreftecherQueueEntry  > iPrefetcherDataQueue;
            int preftechStreamDepth ;
            Iprefetcher()
            {
                preftechStreamDepth = DEF_PREFTECH_STREAM_DEPTH;
            }

	};



};


#endif
