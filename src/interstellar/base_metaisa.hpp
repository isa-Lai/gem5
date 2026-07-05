//
// Summary:
// Each descriptor is 128 bit, so we need to assign two CSR registers per descriptor (TBD: naming convention of the CSRs)

#ifndef BASE_METAISA_ENGINE_METAISA_HPP
#define  BASE_METAISA_ENGINE_METAISA_HPP

#include <stdint.h>
#include <string>
using namespace std;

#define RESERVED_MetaISA 0    //Another RESERVED Defined in  "dev/net/ns_gige_reg.h"
#define MAX_LOOP_DESC_NUM 64
#define MAX_DESC_COUNT   100
enum initValStatus    { INIT_NOT_LINKED = 0 , INIT_LINKED = 1};
enum endValStatus     { END_NOT_LINKED  = 0  , END_LINKED = 1};
enum ParentloopsIDs   { NO_PARENT = 63,PARENT_IS_LOOP1=0 };

#define NONE_STR_ID              MAX_DESC_COUNT
#define TOT_STREAMS              MAX_DESC_COUNT+1
#define INVALID_REQUESTOR_ID     255

enum descType         { LOOP = 0 , DIR_STREAM , INDIR_STREAM ,PTR_CHASE, BRANCH , CODE_SLICE,INTERSTREAM_PATTERN,NONE };
enum isValid          { INAVLID = 0  , VALID};
enum isActive         { INACTIVE = 0 , ACTIVE};
enum arrBaseAddresses { LOOP1_BASE_PA = 0x80004000 , LOOP2_BASE_PA =0x80005000 };


/**********************************************************************
 ********  MISA_LoopDesc_t     Loop Descriptor               **********
 **********************************************************************/
typedef struct  __attribute__((__packed__)){
  uint8_t   parentLoopId:6;    // descriptor id of the parent loop
  uint8_t   initValLinked:1;   // 0: not linked, 1: the init value is linked to another stream
  uint8_t   endValLinked:1;    // 0: not linked, 1: the end value is linked to another stream
  uint32_t  initVal;           // initValLinked == 0 : the initial value of the loop induction variable,
                               // initVallinked == 1 : the descriptor id of the master stream

  uint32_t  endVal;            // endValLinked == 0 : the end value of the loop induction variable,
                               // endVallinked == 1 : the descriptor id of the master stream

  uint16_t  inc;               // increment to the loop induction variable at each iteration
  uint32_t  headerPCOffset;    // offset of the pc of the loop header instruction (relative to the csrrw)
} MISA_LoopDesc_t;


/**********************************************************************
 ********  MISA_DirStreamDesc_t    Direct Stream Descriptor  **********
 **********************************************************************/
typedef struct  __attribute__((__packed__)){
  uint8_t  loopDescId:6;        // the descriptor id of the loop descriptor
  uint8_t  linked:1;            // 0: not linked, 1: the init value is linked to another stream
  uint8_t  reserved:1;
  uint64_t baseAddr;           // linked == 0: the base address of the stream
                               // linked == 1: the decriptor id of the master stream from which the baseAddr is taken
  uint16_t stride;             // stride between two subsequent elements in the stream in the linear address space
  uint16_t streamAttr;         // reserved
  uint16_t reserved_;           // reserved
} MISA_DirStreamDesc_t;



/**********************************************************************
 ********  MISA_DirStreamDesc_t    InDirect Stream Descriptor  ********
 **********************************************************************/
typedef struct  __attribute__((__packed__)){
  uint8_t parentStreamId:6;    // the descriptor id of the parent stream
  uint8_t linked:1;            // 0: not linked, 1: the init value is linked to another stream
  uint8_t reserved:1;
  uint64_t baseAddr;           // linked == 0: the base address of the stream
                               // linked == 1: the decriptor id of the master stream from which the baseAddr is taken
  uint16_t stride;             // stride between two subsequent elements in the stream in the linear address space
  uint16_t streamAttr;          // reserved
  uint16_t reserved_;           // reserved
} MISA_IndirStreamDesc_t;


/**********************************************************************
 ********  MISA_PtrChaseDesc_t    Pointer Chasing Descriptor  ********
 *   It can be used for :
 *        1.Simple linked list               (Head)
 *        2.Doubly linked list         (Head and Tail)
 *        3.Tree                       (Head -> Root)
 *        4.Graph/Each Node's adjacency list (Head -> Neighbor's list)
 *
 **********************************************************************/
typedef struct  __attribute__((__packed__)){
  uint8_t parentStreamId:6;    // the descriptor id of the parent stream
  uint8_t linkedH:1;            // 0: not linked, 1: the init value is linked to another stream
  uint8_t linkedT:1;
  uint64_t headAdddr:40; // linkedH == 0: the head address of the pointer chasing
                         // linkedH == 1: the decriptor id of the master stream
                         //                from which the headAddr is taken
  uint64_t tailAdddr:40; // linkedT == 0: the tail address of the pointer chasing
                         // linkedT == 1: the decriptor id of the master stream
                         //                from which the tailAddr is taken
  uint8_t offsetNext  ;  // The offset of "next" pointer from start of linked list struct
  uint8_t offsetPrev  ;  // The offset of "prev" pointer from start of linked list struct
  uint16_t reserved   ;  // 120 bits - (8+40+40+16) = 16 bits

} MISA_PtrChaseDesc_t;

/**************************************************************************************
 ********  MISA_InterStreamPatternDesc_t    Inter Stream Pattern Descriptor  **********
 **************************************************************************************/
typedef struct  __attribute__((__packed__)){



} MISA_InterStreamPatternDesc_t;


/**************************************************************************************
 ************************  Whole descriptor Data Structure   **************************
 **************************************************************************************/

typedef struct   __attribute__((__packed__)){
   // type-independent section: 8 bits
   uint8_t type:6;             // descriptor types: 0..63
                               // 0: loop descriptor
							   // 1: direct data stream
							   // 2: indirect data stream
							   // 3: branch stream
							   // 4: code slice

   uint8_t valid:1;            // 1: valid descriptor (i.e., allocated entry), 0: invalid (i.e., free entry)
   uint8_t active:1;           // 1: activated descriptor, 0: inactive

   // type-specific section: 120 bits
   union DescInfo{
      MISA_LoopDesc_t        loopDesc;
	    MISA_DirStreamDesc_t   streamDesc;
	    MISA_IndirStreamDesc_t stream;
      MISA_PtrChaseDesc_t    ptrChaseDesc;
	  //MISA_CodeSliceDesc_t code;
	} descInfo;
} MISA_Desc_t;


typedef union
{
	MISA_Desc_t   MISA_Desc_obj ;
	unsigned long long   desc_word[2]  ;
} MISA_toCSR;


typedef struct  __attribute__((__packed__)){
   // type-independent section: 8 bits
   uint8_t type:6;             // descriptor types: 0..63
                               // 0: loop descriptor
							   // 1: direct data stream
							   // 2: indirect data stream
							   // 3: branch stream
							   // 4: code slice

   uint8_t valid:1;            // 1: valid descriptor (i.e., allocated entry), 0: invalid (i.e., free entry)
   uint8_t active:1;           // 1: activated descriptor, 0: inactive

  uint8_t   parentLoopId:6;    // descriptor id of the parent loop
  uint8_t   initValLinked:1;   // 0: not linked, 1: the init value is linked to another stream
  uint8_t   endValLinked:1;    // 0: not linked, 1: the end value is linked to another stream
  uint32_t  initVal;           // initValLinked == 0 : the initial value of the loop induction variable,
                               // initVallinked == 1 : the descriptor id of the master stream

  uint32_t  endVal;            // endValLinked == 0 : the end value of the loop induction variable,
                               // endVallinked == 1 : the descriptor id of the master stream

  uint16_t  inc;               // increment to the loop induction variable at each iteration
  uint32_t  headerPCOffset;    // offset of the pc of the loop header instruction (relative to the csrrw)
} MISA_LoopDesc_big_t;


// 0000001100000000  00000000 00000000 00000000 00000000 00000000 00000000 00000010 00000000 00000000 000001001
//

#endif
