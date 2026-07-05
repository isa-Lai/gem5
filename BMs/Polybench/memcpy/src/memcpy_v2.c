#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"
#define cacheSize 65536*4

void core_memcpy(void * dest, void * src, uint64_t size){
	// // Memcpy version
	// memcpy(dest, src, size);

	// For version
	uint8_t * _dest = (uint8_t*)dest;
	uint8_t * _src = (uint8_t*)src;

    /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop 1
    int LOOP1_START=1, LOOP1_END = size , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;


   int  unitSize = 1 ; /* Double */
     //X2
    MISA_DirStreamDesc_t  X_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&_dest[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           X_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    X_DirStreamDesc.type   = DIR_STREAM;
    X_DirStreamDesc.valid  = VALID;
    X_DirStreamDesc.active =  ACTIVE ;
    X_DirStreamDesc.descInfo.streamDesc=X_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * XDesc = (unsigned long long * ) &X_DirStreamDesc;
    //Y
    MISA_DirStreamDesc_t  Y_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&_src[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           Y_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    Y_DirStreamDesc.type   = DIR_STREAM;
    Y_DirStreamDesc.valid  = VALID;
    Y_DirStreamDesc.active =  ACTIVE ;
    Y_DirStreamDesc.descInfo.streamDesc=Y_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * YDesc = (unsigned long long * ) &Y_DirStreamDesc;

    printf("x and y of size %d  \n",N);
    printf("&_dest[0]=%p , _dest Descriptor = %#llx %llx  \n",&_dest[0],*(XDesc),*(XDesc+1));
    printf("&_src[0]=%p , Y Descriptor = %#llx %llx  \n",&_src[0],*(YDesc),*(YDesc+1));
    printf("&_dest[N-1]=%p    \n",&_dest[N-1] );
    printf("&_src[N-1]=%p    \n",&_src[N-1] );

    write_csr(0x800, *loop1Desc    ); // LOOP 1
    write_csr(0x801, *(loop1Desc+1)); //

    write_csr(0x802, *XDesc    ); // X
    write_csr(0x803, *(XDesc+1)); //

    write_csr(0x804, *(YDesc)  ); // Y
    write_csr(0x805, *(YDesc+1)); //

    //m5_reset_stats(0,0);

	for(uint64_t i = 0; i<size; i++){
		_dest[i] = _src[i];
	}
}



int main(void)
{

   char cacher[cacheSize], cacher2[cacheSize];

   uint8_t  *x=(uint8_t*)malloc(N*sizeof(uint8_t));
   uint8_t  *y=(uint8_t*)malloc(N*sizeof(uint8_t));
   const uint8_t XVAL = rand() % 256;
   const uint8_t YVAL = rand() % 256;
   for (size_t i = 0; i < N; i++) {
      x[i] = XVAL;
      y[i] = YVAL;
   }


   //CLear the cache
   printf("Clearing cache (%d)...\n", cacheSize);
   //Clear Cache
   for(int j=0; j<cacheSize; j++){
      cacher[j] = 4;
      cacher2[j] = cacher[j];
   }


   core_memcpy(x,y,N);

   return 0;
}
