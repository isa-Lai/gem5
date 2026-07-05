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
    // 1- The loop
    int LOOP1_START=1, LOOP1_END = _PB_N*_PB_N*_PB_N , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loopDesc = (unsigned long long * ) &loop1_LoopDesc;

    // 2- A[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrAStride;
#ifdef DATA_TYPE_IS_DOUBLE
    arrAStride = 8 ;
#else
    arrAStride = 4 ;
#endif
    MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&A[0][0][0],
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrA_DirStreamDesc.type   = DIR_STREAM;
    arrA_DirStreamDesc.valid  = VALID;
    arrA_DirStreamDesc.active =  ACTIVE ;
    arrA_DirStreamDesc.descInfo.streamDesc=arrA_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrADesc = (unsigned long long * ) &arrA_DirStreamDesc;


    // 3- B[i-1][j-1] -> Stride = 2*MaxSize+2
#ifdef DATA_TYPE_IS_DOUBLE
    int arrBStride = 8 ;
#else
    int arrBStride = 4 ;
#endif

    MISA_DirStreamDesc_t  arrB_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&B[0][0][0],
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB_DirStreamDesc.type   = DIR_STREAM;
    arrB_DirStreamDesc.valid  = VALID;
    arrB_DirStreamDesc.active =  ACTIVE ;
    arrB_DirStreamDesc.descInfo.streamDesc=arrB_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_DirStreamDesc;


    write_csr(0x800, *loopDesc);     // csrrw a0 , meta_0l , meta_isa_obj1.desc_word[0];
    write_csr(0x801, *(loopDesc+1)); // csrrw a0 , meta_0h , meta_isa_obj1.desc_word[1];

    write_csr(0x802, *arrADesc    );//meta_isa_obj2.desc_word[0]); // csrrw a0 , meta_1l , meta_isa_obj2.desc_word[0];
    write_csr(0x803, *(arrADesc+1));//meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

    write_csr(0x804, *(arrBDesc));//meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];
    write_csr(0x805, *(arrBDesc+1));//meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

    printf("Number of iterations = %d\n",_PB_TSTEPS);
    printf("A and B of size %d*%d \n",_PB_N,_PB_N);
    printf("&A[0][0][0]=%p , A Descriptor = %#llx %llx  \n",&A[0][0][0],*(arrADesc),*(arrADesc+1));
    printf("&B[0][0][0]=%p , B Descriptor = %#llx %llx  \n",&B[0][0][0],*(arrBDesc),*(arrBDesc+1));
    printf("&A[_PB_N-1][_PB_N-1][_PB_N-1]=%p , A Descriptor = %#llx %llx  \n",&A[_PB_N-1][_PB_N-1][_PB_N-1],*(arrADesc),*(arrADesc+1));
    printf("&B[_PB_N-1][_PB_N-1][_PB_N-1]=%p , B Descriptor = %#llx %llx  \n",&B[_PB_N-1][_PB_N-1][_PB_N-1],*(arrBDesc),*(arrBDesc+1));
m5_reset_stats(0,0);
#pragma scop
    for (t = 1; t <= TSTEPS; t++) {
        printf("t:%d\n",t);
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
        printf("t:%d\n",t);
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
