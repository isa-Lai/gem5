#ifndef __IDIRSTREAMBUFFER_H
#define __IDIRSTREAMBUFFER_H

/**
 * Intelligent Prefetcher Iprefetcher.h 
 * 
 * @file    iDirStreamBuffer.h
 * @brief   Intelligent Direct STream Buffer
 * @author  Abdelrhman Mohamed Abotaleb  
 * @contact abotaleb@mcmaster.ca  
 *           
 */



#include <vector>
#include <functional>
#include <map>

using namespace std;
 
namespace ramulator
{

    //Maxuimum allowed size for tracking direct stream stats 
    const int DEF_IDIRSTREAM_BUFF_LEN   = 64*2048; //Maximum iDirStream Table length 
    
    /****** To Unify Changes -> Related to Header :
     * /home/abotaleb/arena/Gem5/gem5/src/arch/riscv/page_size.hh
     *                                                         *****/

    const   uint64_t           PageShift = 26;//RiscvISA::PageShift
    #define PAGE_OFFSET_MASK   (( 1uL << PageShift)-1)  
    #define MAX_BANKS          16           // Maximum number of banks in DRAM Channel -> may be better to have crntRowReqsCount as pointer and allocate the size inside controller constructor
    #define MAX_BANKS_DDR4     16 
    #define TOT_STREAMS_CORES  1600+1  //# assumes max 16  cores (16*100)

    typedef struct iDirStreamEntry
    {
        uint16_t      stride             ; 
        vector<int>   lastAddrVec        [MAX_BANKS]; //  @todo : not needed - can be removed 
        bool          active             ; // Started or not  
        uint64_t      previBatchLastAddr ; // Last Address in Previous iBatch
        vector<int>   lastAddrVecPerType [MAX_BANKS][2]; // Number of requests of this stream per current row (Per Bank per Type)
        uint16_t      crntRowReqsCount   [MAX_BANKS][2]; // Number of requests of this stream per current row (Per Bank per Type)
        int           FarFutureAccessCount = 0; //To Change previBatchLastAddr in case of very far future access
        // 
        uint8_t  nextMetaISAStreamID     ;
        uint8_t  actualMetaISAStreamID   ;
        int      incorrect_Predictions   ;
    }IDirStreamEntry;

    class IDirStreamBuffer
    {
        public: 
            
            map< uint64_t , IDirStreamEntry  > IDirStreamTable;
            int iDirStreamTableLength ; 
            IDirStreamBuffer()
            {
                iDirStreamTableLength = DEF_IDIRSTREAM_BUFF_LEN;
             }

    };


};




#endif 
