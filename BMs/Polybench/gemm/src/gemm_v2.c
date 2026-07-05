/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* gemm.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "gemm.h"


#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"
#define NI  NP
#define NJ  NQ
#define NK  NR
/* Array initialization. */
static
void init_array(int ni, int nj, int nk,
		DATA_TYPE *alpha,
		DATA_TYPE *beta,
		DATA_TYPE POLYBENCH_2D(C,NI,NJ,ni,nj),
		DATA_TYPE POLYBENCH_2D(A,NI,NK,ni,nk),
		DATA_TYPE POLYBENCH_2D(B,NK,NJ,nk,nj))
{
  int i, j;

  *alpha = 1.5;
  *beta = 1.2;
  for (i = 0; i < ni; i++)
    for (j = 0; j < nj; j++)
      C[i][j] = (DATA_TYPE) ((i*j+1) % ni) / ni;
  for (i = 0; i < ni; i++)
    for (j = 0; j < nk; j++)
      A[i][j] = (DATA_TYPE) (i*(j+1) % nk) / nk;
  for (i = 0; i < nk; i++)
    for (j = 0; j < nj; j++)
      B[i][j] = (DATA_TYPE) (i*(j+2) % nj) / nj;
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int ni, int nj,
		 DATA_TYPE POLYBENCH_2D(C,NI,NJ,ni,nj))
{
  int i, j;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("C");
  for (i = 0; i < ni; i++)
    for (j = 0; j < nj; j++) {
	if ((i * ni + j) % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
	fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, C[i][j]);
    }
  POLYBENCH_DUMP_END("C");
  POLYBENCH_DUMP_FINISH;
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
static
void kernel_gemm(int ni, int nj, int nk,
		 DATA_TYPE alpha,
		 DATA_TYPE beta,
		 DATA_TYPE POLYBENCH_2D(C,NI,NJ,ni,nj),
		 DATA_TYPE POLYBENCH_2D(A,NI,NK,ni,nk),
		 DATA_TYPE POLYBENCH_2D(B,NK,NJ,nk,nj))
{
  int i, j, k;
   /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop A
    int LOOP1_START=1, LOOP1_END = NI*NK , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loopA_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopA_LoopDesc.type   = LOOP    ;
    loopA_LoopDesc.valid  = VALID   ;
    loopA_LoopDesc.active =  ACTIVE ;
    loopA_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loopADesc = (unsigned long long * ) &loopA_LoopDesc;

    // 1- The loop B
    int LOOP2_START=1, LOOP2_END = NK*NJ , LOOP2_INC = 1 ;
    MISA_LoopDesc_t   loop2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC_OFFSET };
    MISA_Desc_t       loopB_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopB_LoopDesc.type   = LOOP    ;
    loopB_LoopDesc.valid  = VALID   ;
    loopB_LoopDesc.active =  ACTIVE ;
    loopB_LoopDesc.descInfo.loopDesc=loop2_metaData;
    unsigned long long * loopBDesc = (unsigned long long * ) &loopB_LoopDesc;


    // 1- The loop C
    int LOOP4_START=1, LOOP4_END = NI*NJ , LOOP4_INC = 1 ;
    MISA_LoopDesc_t   loop4_metaData       = { LOOP3_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP4_START, LOOP4_END , LOOP4_INC  , PC_OFFSET };
    MISA_Desc_t       loopC_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopC_LoopDesc.type   = LOOP    ;
    loopC_LoopDesc.valid  = VALID   ;
    loopC_LoopDesc.active =  ACTIVE ;
    loopC_LoopDesc.descInfo.loopDesc=loop4_metaData;
    unsigned long long * loopCDesc = (unsigned long long * ) &loopC_LoopDesc;


    // 2- A[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrAStride;
#ifdef DATA_TYPE_IS_DOUBLE
    arrAStride = 8 ;
#else
    arrAStride = 4 ;
#endif

    MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&A[0][0],
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


    MISA_DirStreamDesc_t  arrB_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&B[0][0],
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB_DirStreamDesc.type   = DIR_STREAM;
    arrB_DirStreamDesc.valid  = VALID;
    arrB_DirStreamDesc.active =  ACTIVE ;
    arrB_DirStreamDesc.descInfo.streamDesc=arrB_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_DirStreamDesc;


    // 3- ARRc[i-1][j-1] -> Stride = 2*MaxSize+2
#ifdef DATA_TYPE_IS_DOUBLE
    int arrCStride = 8 ;
#else
    int arrCStride = 4 ;
#endif


    MISA_DirStreamDesc_t  arrC_metaData      = { LOOP3_ID , 0 , RESERVED , (uint64_t)&C[0][0],
											     arrCStride , 0 , RESERVED };
    MISA_Desc_t           arrC_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrC_DirStreamDesc.type   = DIR_STREAM;
    arrC_DirStreamDesc.valid  = VALID;
    arrC_DirStreamDesc.active =  ACTIVE ;
    arrC_DirStreamDesc.descInfo.streamDesc=arrC_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrCDesc = (unsigned long long * ) &arrC_DirStreamDesc;

    printf("_PB_NI = %d  _PB_NJ =%d _PB_NK = %d \n",_PB_NI ,_PB_NJ ,_PB_NK );
    printf("&A[0][0]=%p , A Descriptor = %#llx %llx  \n",&A[0][0],*(arrADesc),*(arrADesc+1));
    printf("&B[0][0]=%p , B Descriptor = %#llx %llx  \n",&B[0][0],*(arrBDesc),*(arrBDesc+1));
    printf("&C[0][0]=%p , C Descriptor = %#llx %llx  \n",&C[0][0],*(arrCDesc),*(arrCDesc+1));
    printf("&A[I-1][K-1]=%p    \n",&A[_PB_NI-1][_PB_NK-1] );
    printf("&B[K-1][J-1]=%p    \n",&B[_PB_NK-1][_PB_NJ-1] );
    printf("&C[I-1][J-1]=%p    \n",&C[_PB_NI-1][_PB_NJ-1] );

    write_csr(0x800, *loopADesc    ); // LOOP A
    write_csr(0x801, *(loopADesc+1)); //

    write_csr(0x802, *loopBDesc    ); // LOOP B
    write_csr(0x803, *(loopBDesc+1)); //

    write_csr(0x804, *(loopCDesc)  ); // LOOP C
    write_csr(0x805, *(loopCDesc+1)); //

    write_csr(0x806, *arrADesc    ); // A
    write_csr(0x807, *(arrADesc+1)); //

    write_csr(0x808, *arrBDesc    ); // B
    write_csr(0x809, *(arrBDesc+1)); //

    write_csr(0x80A, *(arrCDesc)  ); // C
    write_csr(0x80B, *(arrCDesc+1)); //





    m5_reset_stats(0,0);
//BLAS PARAMS
//TRANSA = 'N'
//TRANSB = 'N'
// => Form C := alpha*A*B + beta*C,
//A is NIxNK
//B is NKxNJ
//C is NIxNJ
#pragma scop
  for (i = 0; i < _PB_NI; i++) {
    for (j = 0; j < _PB_NJ; j++)
	C[i][j] *= beta;
    for (k = 0; k < _PB_NK; k++) {
       for (j = 0; j < _PB_NJ; j++)
	  C[i][j] += alpha * A[i][k] * B[k][j];
    }
  }
#pragma endscop

}


int main(int argc, char** argv)
{
  /* Retrieve problem size. */


  int ni = NI;
  int nj = NJ;
  int nk = NK;

  /* Variable declaration/allocation. */
  DATA_TYPE alpha;
  DATA_TYPE beta;
  POLYBENCH_2D_ARRAY_DECL(C,DATA_TYPE,NI,NJ,ni,nj);
  POLYBENCH_2D_ARRAY_DECL(A,DATA_TYPE,NI,NK,ni,nk);
  POLYBENCH_2D_ARRAY_DECL(B,DATA_TYPE,NK,NJ,nk,nj);

  /* Initialize array(s). */
  init_array (ni, nj, nk, &alpha, &beta,
	      POLYBENCH_ARRAY(C),
	      POLYBENCH_ARRAY(A),
	      POLYBENCH_ARRAY(B));

  /* Start timer. */
  polybench_start_instruments;

  /* Run kernel. */
  kernel_gemm (ni, nj, nk,
	       alpha, beta,
	       POLYBENCH_ARRAY(C),
	       POLYBENCH_ARRAY(A),
	       POLYBENCH_ARRAY(B));

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(ni, nj,  POLYBENCH_ARRAY(C)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(C);
  POLYBENCH_FREE_ARRAY(A);
  POLYBENCH_FREE_ARRAY(B);

  return 0;
}
