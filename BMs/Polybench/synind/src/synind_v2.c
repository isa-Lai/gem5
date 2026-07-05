#include <stdlib.h>
#include <stdio.h>

#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"

#define MAX_SIZE     N*1024
#define MAX_ITR      1
#define STRIDE       1
#define CACHE_LINE  64
const  uint32_t LOOP1_START =  1          ;
const  uint32_t LOOP1_END   =  MAX_SIZE   ;
const  uint16_t LOOP1_INC   =  1          ;
#define PC_OFFSET 0x44 //Computed after the first compilation
// STride = 32
// csrrw zero, meta_0h, a1  : @main+306  -> CSRLoop Offset Set @Address -> 102b6
// lw a1, -36(s0)           : @main+566  -> Loop Under MetaISA @Address -> 103ba
//offset = 0x103ba- 0x102b6 = 0x104

// If -O3 optimization is used the PC offset will be different:
// The csrw                    -> csrrw zero, meta_0h, a1  :  Address:0x101ac   @main+40
// The loop start will be at:  -> mv	a3, a2             :  Address:0x101f0   @main+108
//  Offset should be 0x0x101f0 - 0x101ac = 0x44

int main(int argc, char** argv)
{

  //Simple Application
   int i;
   volatile  char sum = 0;
   uint32_t  * A = (uint32_t*)malloc(MAX_SIZE*sizeof(uint32_t));
   //Seperate by A and B addresses by a Cache Line
   char   *  Seperator = (char*) malloc(4*CACHE_LINE*sizeof(char));
   char      * B = (char*)malloc(MAX_SIZE*sizeof(char));


   /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/

    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc        ;
    loop1_LoopDesc.type   = LOOP;
    loop1_LoopDesc.valid  = VALID;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    // Copy from loop descriptor struct to CSRs
    MISA_toCSR        meta_isa_obj1;
    meta_isa_obj1.MISA_Desc_obj = loop1_LoopDesc;


    /******************** Fill descriptor data structure for Array A ************************/
    //Array A is an integer array , then the stride will be 4
    int arrAStride = 4*STRIDE ; /* Here the direct stream stride is the same as loop stride */
    MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&A[0],
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA_DirStreamDesc ;
    arrA_DirStreamDesc.type   = DIR_STREAM;
    arrA_DirStreamDesc.valid  = VALID;
    arrA_DirStreamDesc.active =  ACTIVE ;
    arrA_DirStreamDesc.descInfo.streamDesc=arrA_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrADesc = (unsigned long long * ) &arrA_DirStreamDesc;
    MISA_toCSR        meta_isa_obj2;
    meta_isa_obj2.MISA_Desc_obj = arrA_DirStreamDesc;

   /*****************************************************************************************/
   /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
    int arrBStride = STRIDE ;
    MISA_IndirStreamDesc_t   arrB_metaData   = {LOOP1_ID,0 , RESERVED,  (uint64_t)&B[0],
                                                arrBStride , 0 , RESERVED   };
    MISA_Desc_t           arrB_InDirStreamDesc ;
    arrB_InDirStreamDesc.type   = INDIR_DTREAM;
    arrB_InDirStreamDesc.valid  = VALID;
    arrB_InDirStreamDesc.active =  ACTIVE ;
    arrB_InDirStreamDesc.descInfo.stream=arrB_metaData;
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_InDirStreamDesc;


    // Copy from array descriptor struct to CSRs
    //This loop should have some benefit from IPP
    //volatile uint8_t
    //-> use very low STRIDE -> exclude direct stream benefits
    for(i=0;i<MAX_SIZE;i++)
    {
        uint32_t randVal = rand();
        A[i]=randVal%MAX_SIZE; //Mix Locality + Random Accesses for Indirect in another microbenchmark
        //printf("randVal =%u  A[%d]=%d\n",randVal,i,A[i]);
    }
    /*printf("Before checkpints: \n");
    printf("Descrip #1 [Loop] = %#llx%-#llx\n",meta_isa_obj1.desc_word[0],meta_isa_obj1.desc_word[1]);
    printf("Descrip #2 [Direct STream] = %#llx%-#llx\n",*arrADesc , *(arrADesc+1));
    printf("Descrip #3 [InDirect STream] = %#llx%-#llx\n",*arrBDesc , *(arrBDesc+1));*/
    //m5_work_begin(0, 0);
    //m5_checkpoint(0,0);

    //printf("After checkpints: \n");
    printf("&A[0]=%p , &A[%d]=%p \n",&A[0],MAX_SIZE-1,&A[MAX_SIZE-1]);
    printf("&B[0]=%p , &B[%d]=%p \n",&B[0],MAX_SIZE-1,&B[MAX_SIZE-1]);

    write_csr(0x800, meta_isa_obj1.desc_word[0]); // csrrw a0 , meta_0l , meta_isa_obj1.desc_word[0];
    write_csr(0x801, meta_isa_obj1.desc_word[1]); // csrrw a0 , meta_0h , meta_isa_obj1.desc_word[1];

    write_csr(0x802, *arrADesc    );//meta_isa_obj2.desc_word[0]); // csrrw a0 , meta_1l , meta_isa_obj2.desc_word[0];
    write_csr(0x803, *(arrADesc+1));//meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

    write_csr(0x804, *arrBDesc    );//
    write_csr(0x805, *(arrBDesc+1));//


    m5_reset_stats(0,0);
    //You may need to evict the cache first (to avoid any cached A values)
    for(i=0;i<MAX_SIZE;i++)
    {
        sum+=B[A[i]];
    }


   return 0;
}
