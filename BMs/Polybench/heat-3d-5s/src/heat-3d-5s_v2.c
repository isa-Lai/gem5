/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* heat-3d.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "heat-3d.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"

/* Array initialization. */
static
void init_array (int n,
		 DATA_TYPE POLYBENCH_3D(A,N,N,N,n,n,n),
		 DATA_TYPE POLYBENCH_3D(B,N,N,N,n,n,n))
{
  int i, j, k;

  for (i = 0; i < n; i++)
    for (j = 0; j < n; j++)
      for (k = 0; k < n; k++)
        A[i][j][k] = B[i][j][k] = (DATA_TYPE) (i + j + (n-k))* 10 / (n);
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int n,
		 DATA_TYPE POLYBENCH_3D(A,N,N,N,n,n,n))

{
  int i, j, k;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("A");
  for (i = 0; i < n; i++)
    for (j = 0; j < n; j++)
      for (k = 0; k < n; k++) {
         if ((i * n * n + j * n + k) % 20 == 0) fprintf(POLYBENCH_DUMP_TARGET, "\n");
         fprintf(POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, A[i][j][k]);
      }
  POLYBENCH_DUMP_END("A");
  POLYBENCH_DUMP_FINISH;
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
static
void kernel_heat_3d(int tsteps,
		      int n,
		      DATA_TYPE POLYBENCH_3D(A,N,N,N,n,n,n),
		      DATA_TYPE POLYBENCH_3D(B,N,N,N,n,n,n))
{
  int t, i, j, k;
  /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- Stream 1
    // (_PB_N*_PB_N)-(_PB_N) = True difference
    // then decrement by 1   -> backward one palce
    // then decrement by 8   -> backward by a cache line

    int LOOP1_START=1, LOOP1_END = (_PB_N*_PB_N)-(_PB_N)-1-8 , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;

    // 2- Stream 2
    // (_PB_N*_PB_N)+(_PB_N) = True difference
    // then decrement by 1   -> backward one palce
    // then decrement by 8   -> backward by a cache line
    int LOOP2_START=1, LOOP2_END =  (_PB_N)-1-8, LOOP2_INC = 1 ; PC_OFFSET=01000;
    MISA_LoopDesc_t   LOOP2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC_OFFSET };
    MISA_Desc_t       LOOP2_LoopDesc; //      ={LOOP,VALID,ACTIVE ,LOOP2_metaData};
    LOOP2_LoopDesc.type   = LOOP    ;
    LOOP2_LoopDesc.valid  = VALID   ;
    LOOP2_LoopDesc.active =  ACTIVE ;
    LOOP2_LoopDesc.descInfo.loopDesc=LOOP2_metaData;
    unsigned long long * loop2Desc = (unsigned long long * ) &LOOP2_LoopDesc;

    // 3- Stream 3
    // (_PB_N*_PB_N*_PB_N)-(2*_PB_N*_PB_N+_PB_N) = True difference
    // then decrement by 1 -> backward one palce
    int LOOP3_START=1, LOOP3_END = (_PB_N*_PB_N*_PB_N)-(2*_PB_N*_PB_N+_PB_N)-1-8 , LOOP3_INC = 1 ; PC_OFFSET=01000;
    MISA_LoopDesc_t   LOOP3_metaData       = { LOOP3_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP3_START, LOOP3_END , LOOP3_INC  , PC_OFFSET };
    MISA_Desc_t       LOOP3_LoopDesc; //      ={LOOP,VALID,ACTIVE ,LOOP3_metaData};
    LOOP3_LoopDesc.type   = LOOP    ;
    LOOP3_LoopDesc.valid  = VALID   ;
    LOOP3_LoopDesc.active =  ACTIVE ;
    LOOP3_LoopDesc.descInfo.loopDesc=LOOP3_metaData;
    unsigned long long * loop3Desc = (unsigned long long * ) &LOOP3_LoopDesc;


    // 4- Stream A1 ; A[0][1][1]
    int arrAStride;
#ifdef DATA_TYPE_IS_DOUBLE
    arrAStride = 8 ;
#else
    arrAStride = 4 ;
#endif
    MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&A[0][1][1]  ,
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrA_DirStreamDesc.type   = DIR_STREAM;
    arrA_DirStreamDesc.valid  = VALID;
    arrA_DirStreamDesc.active =  ACTIVE ;
    arrA_DirStreamDesc.descInfo.streamDesc=arrA_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrADesc = (unsigned long long * ) &arrA_DirStreamDesc;

    //5- Stream A2 ; A[1][0][1]
    MISA_DirStreamDesc_t  arrA2_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&A[1][0][1]    ,
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA2_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA2_metaData};
    arrA2_DirStreamDesc.type   = DIR_STREAM;
    arrA2_DirStreamDesc.valid  = VALID;
    arrA2_DirStreamDesc.active =  ACTIVE ;
    arrA2_DirStreamDesc.descInfo.streamDesc=arrA2_metaData;
    // Copy from arrA2y descriptor struct to CSRs
    unsigned long long * arrA2Desc = (unsigned long long * ) &arrA2_DirStreamDesc;

    //6- Stream A3 ; A[1][1][1]
    MISA_DirStreamDesc_t  arrA3_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&A[1][1][1]    ,
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA3_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA3_metaData};
    arrA3_DirStreamDesc.type   = DIR_STREAM;
    arrA3_DirStreamDesc.valid  = VALID;
    arrA3_DirStreamDesc.active =  ACTIVE ;
    arrA3_DirStreamDesc.descInfo.streamDesc=arrA3_metaData;
    // Copy from arrA3y descriptor struct to CSRs
    unsigned long long * arrA3Desc = (unsigned long long * ) &arrA3_DirStreamDesc;


    //7- Stream A4 ; A[1][2][1]
    MISA_DirStreamDesc_t  arrA4_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&A[1][2][1]    ,
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA4_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA4_metaData};
    arrA4_DirStreamDesc.type   = DIR_STREAM;
    arrA4_DirStreamDesc.valid  = VALID;
    arrA4_DirStreamDesc.active =  ACTIVE ;
    arrA4_DirStreamDesc.descInfo.streamDesc=arrA4_metaData;
    // Copy from arrA4y descriptor struct to CSRs
    unsigned long long * arrA4Desc = (unsigned long long * ) &arrA4_DirStreamDesc;


    //8- Stream A5 ;  A[2][1][1]
    MISA_DirStreamDesc_t  arrA5_metaData      = { LOOP3_ID , 0 , RESERVED , (uint64_t)&A[2][1][1]    ,
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA5_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA5_metaData};
    arrA5_DirStreamDesc.type   = DIR_STREAM;
    arrA5_DirStreamDesc.valid  = VALID;
    arrA5_DirStreamDesc.active =  ACTIVE ;
    arrA5_DirStreamDesc.descInfo.streamDesc=arrA5_metaData;
    // Copy from arrA5y descriptor struct to CSRs
    unsigned long long * arrA5Desc = (unsigned long long * ) &arrA5_DirStreamDesc;



    // 9- B Stream B1 ; B[0][1][1]
#ifdef DATA_TYPE_IS_DOUBLE
    int arrBStride = 8 ;
#else
    int arrBStride = 4 ;
#endif

    MISA_DirStreamDesc_t  arrB_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&B[0][1][1]  ,
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB_DirStreamDesc.type   = DIR_STREAM;
    arrB_DirStreamDesc.valid  = VALID;
    arrB_DirStreamDesc.active =  ACTIVE ;
    arrB_DirStreamDesc.descInfo.streamDesc=arrB_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_DirStreamDesc;


    // 10- B Stream B2 ; B[1][0][1]
    MISA_DirStreamDesc_t  arrB2_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&B[1][0][1]   ,
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB2_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB2_DirStreamDesc.type   = DIR_STREAM;
    arrB2_DirStreamDesc.valid  = VALID;
    arrB2_DirStreamDesc.active =  ACTIVE ;
    arrB2_DirStreamDesc.descInfo.streamDesc=arrB2_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrB2Desc = (unsigned long long * ) &arrB2_DirStreamDesc;


    // 11- B Stream B3 ; B[1][1][1]
    MISA_DirStreamDesc_t  arrB3_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&B[1][1][1]   ,
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB3_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB3_DirStreamDesc.type   = DIR_STREAM;
    arrB3_DirStreamDesc.valid  = VALID;
    arrB3_DirStreamDesc.active =  ACTIVE ;
    arrB3_DirStreamDesc.descInfo.streamDesc=arrB3_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrB3Desc = (unsigned long long * ) &arrB3_DirStreamDesc;


    // 12- B Stream B4 ; B[1][2][1]
    MISA_DirStreamDesc_t  arrB4_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&B[1][2][1]    ,
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB4_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB4_DirStreamDesc.type   = DIR_STREAM;
    arrB4_DirStreamDesc.valid  = VALID;
    arrB4_DirStreamDesc.active =  ACTIVE ;
    arrB4_DirStreamDesc.descInfo.streamDesc=arrB4_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrB4Desc = (unsigned long long * ) &arrB4_DirStreamDesc;

    // 12- B Stream B5 ; B[2][1][1]
    MISA_DirStreamDesc_t  arrB5_metaData      = { LOOP3_ID , 0 , RESERVED , (uint64_t)&B[2][1][1]   ,
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB5_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB5_DirStreamDesc.type   = DIR_STREAM;
    arrB5_DirStreamDesc.valid  = VALID;
    arrB5_DirStreamDesc.active =  ACTIVE ;
    arrB5_DirStreamDesc.descInfo.streamDesc=arrB5_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrB5Desc = (unsigned long long * ) &arrB5_DirStreamDesc;


    write_csr(0x800, *loop1Desc);     // csrrw a0 , meta_0l , meta_isa_obj1.desc_word[0];
    write_csr(0x801, *(loop1Desc+1)); // csrrw a0 , meta_0h , meta_isa_obj1.desc_word[1];

    write_csr(0x802, *loop2Desc);
    write_csr(0x803, *(loop2Desc+1));

    write_csr(0x804, *loop3Desc);
    write_csr(0x805, *(loop3Desc+1));


    write_csr(0x806, *arrADesc    );//meta_isa_obj2.desc_word[0]); // csrrw a0 , meta_1l , meta_isa_obj2.desc_word[0];
    write_csr(0x807, *(arrADesc+1));//meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

    write_csr(0x808, *arrA2Desc    );
    write_csr(0x809, *(arrA2Desc+1));

    write_csr(0x80A, *arrA3Desc    );
    write_csr(0x80B, *(arrA3Desc+1));

    write_csr(0x80C, *arrA4Desc    );
    write_csr(0x80D, *(arrA4Desc+1));

    write_csr(0x80E, *arrA5Desc    );
    write_csr(0x80F, *(arrA5Desc+1));

    write_csr(0x810, *(arrBDesc))  ;
    write_csr(0x811, *(arrBDesc+1));

    write_csr(0x812, *(arrB2Desc))  ;
    write_csr(0x813, *(arrB2Desc+1));

    write_csr(0x814, *(arrB3Desc))  ;
    write_csr(0x815, *(arrB3Desc+1));

    write_csr(0x816, *(arrB4Desc))  ;
    write_csr(0x817, *(arrB4Desc+1));

    write_csr(0x818, *(arrB5Desc))  ;
    write_csr(0x819, *(arrB5Desc+1));

    printf("Number of iterations = %d\n",_PB_TSTEPS);
    printf("A and B of size %d*%d \n",_PB_N,_PB_N);
    printf("&A[0][0][0]=%p , A Descriptor = %#llx %llx  \n",&A[0][0][0],*(arrADesc),*(arrADesc+1));
    printf("&B[0][0][0]=%p , B Descriptor = %#llx %llx  \n",&B[0][0][0],*(arrBDesc),*(arrBDesc+1));
    printf("&A[_PB_N-1][_PB_N-1][_PB_N-1]=%p , A Descriptor = %#llx %llx  \n",&A[_PB_N-1][_PB_N-1][_PB_N-1],*(arrADesc),*(arrADesc+1));
    printf("&B[_PB_N-1][_PB_N-1][_PB_N-1]=%p , B Descriptor = %#llx %llx  \n",&B[_PB_N-1][_PB_N-1][_PB_N-1],*(arrBDesc),*(arrBDesc+1));
m5_reset_stats(0,0);
#pragma scop
    for (t = 1; t <= TSTEPS; t++) {
        //printf("t:%d\n",t);
        for (i = 1; i < _PB_N-1; i++) {
            for (j = 1; j < _PB_N-1; j++) {
                for (k = 1; k < _PB_N-1; k++) {
                    B[i][j][k] =   SCALAR_VAL(0.125) * (A[i+1][j][k] - SCALAR_VAL(2.0) * A[i][j][k] + A[i-1][j][k])
                                 + SCALAR_VAL(0.125) * (A[i][j+1][k] - SCALAR_VAL(2.0) * A[i][j][k] + A[i][j-1][k])
                                 + SCALAR_VAL(0.125) * (A[i][j][k+1] - SCALAR_VAL(2.0) * A[i][j][k] + A[i][j][k-1])
                                 + A[i][j][k];
                }
            }
        }
        //printf("t:%d\n",t);
        for (i = 1; i < _PB_N-1; i++) {
           for (j = 1; j < _PB_N-1; j++) {
               for (k = 1; k < _PB_N-1; k++) {
                   A[i][j][k] =   SCALAR_VAL(0.125) * (B[i+1][j][k] - SCALAR_VAL(2.0) * B[i][j][k] + B[i-1][j][k])
                                + SCALAR_VAL(0.125) * (B[i][j+1][k] - SCALAR_VAL(2.0) * B[i][j][k] + B[i][j-1][k])
                                + SCALAR_VAL(0.125) * (B[i][j][k+1] - SCALAR_VAL(2.0) * B[i][j][k] + B[i][j][k-1])
                                + B[i][j][k];
               }
           }
       }
    }
#pragma endscop

}
#define cacheSize 65536*4


int main(int argc, char** argv)
{
  char cacher[cacheSize], cacher2[cacheSize];
  /* Retrieve problem size. */
  int n = N;
  int tsteps = TSTEPS;

  /* Variable declaration/allocation. */
  POLYBENCH_3D_ARRAY_DECL(A, DATA_TYPE, N, N, N, n, n, n);
  POLYBENCH_3D_ARRAY_DECL(B, DATA_TYPE, N, N, N, n, n, n);


  /* Initialize array(s). */
  init_array (n, POLYBENCH_ARRAY(A), POLYBENCH_ARRAY(B));

  /* Start timer. */
  polybench_start_instruments;

  //CLear the cache
  printf("Clearing cache (%d)...\n", cacheSize);
  //Clear Cache
  for(int j=0; j<cacheSize; j++){
      cacher[j] = 4;
      cacher2[j] = cacher[j];
  }

 /* Run kernel. */
 // m5_checkpoint(0,0);

  kernel_heat_3d (tsteps, n, POLYBENCH_ARRAY(A), POLYBENCH_ARRAY(B));
  return 0;

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(n, POLYBENCH_ARRAY(A)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(A);

  return 0;
}
