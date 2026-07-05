/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* atax.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "atax.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"



/* Array initialization. */
static
void init_array (int m, int n,
		 DATA_TYPE POLYBENCH_2D(A,M,N,m,n),
		 DATA_TYPE POLYBENCH_1D(x,N,n))
{
  int i, j;
  DATA_TYPE fn;
  fn = (DATA_TYPE)n;

  for (i = 0; i < n; i++)
      x[i] = 1 + (i / fn);
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++)
      A[i][j] = (DATA_TYPE) ((i+j) % n) / (5*m);
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int n,
		 DATA_TYPE POLYBENCH_1D(y,N,n))

{
  int i;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("y");
  for (i = 0; i < n; i++) {
    if (i % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
    fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, y[i]);
  }
  POLYBENCH_DUMP_END("y");
  POLYBENCH_DUMP_FINISH;
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
static
void kernel_atax(int m, int n,
		 DATA_TYPE POLYBENCH_2D(A,M,N,m,n),
		 DATA_TYPE POLYBENCH_1D(x,N,n),
		 DATA_TYPE POLYBENCH_1D(y,N,n),
		 DATA_TYPE POLYBENCH_1D(tmp,M,m))
{
  int i, j;
   /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop 1
    int LOOP1_START=1, LOOP1_END = _PB_N , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;

    // 2- The loop 2
    int LOOP2_START=1, LOOP2_END = _PB_M*_PB_N , LOOP2_INC = 1 , PC2_OFFSET=01000;
    MISA_LoopDesc_t   loop2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC2_OFFSET };
    MISA_Desc_t       loop2_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop2_LoopDesc.type   = LOOP    ;
    loop2_LoopDesc.valid  = VALID   ;
    loop2_LoopDesc.active =  ACTIVE ;
    loop2_LoopDesc.descInfo.loopDesc=loop2_metaData;
    unsigned long long * loop2Desc = (unsigned long long * ) &loop2_LoopDesc;

    // 2- A[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrAStride;
#ifdef DATA_TYPE_IS_DOUBLE
    arrAStride = 8 ;
#else
    arrAStride = 4 ;
#endif
    MISA_DirStreamDesc_t  A_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&A[0][0],
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           A_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    A_DirStreamDesc.type   = DIR_STREAM;
    A_DirStreamDesc.valid  = VALID;
    A_DirStreamDesc.active =  ACTIVE ;
    A_DirStreamDesc.descInfo.streamDesc=A_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * matADesc = (unsigned long long * ) &A_DirStreamDesc;


    // 3- B[i-1][j-1] -> Stride = 2*MaxSize+2
int unitSize;
#ifdef DATA_TYPE_IS_DOUBLE
    unitSize = 8 ;
#else
    unitSize = 4 ;
#endif
    //X1
    MISA_DirStreamDesc_t  X1_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&x[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           X1_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    X1_DirStreamDesc.type   = DIR_STREAM;
    X1_DirStreamDesc.valid  = VALID;
    X1_DirStreamDesc.active =  ACTIVE ;
    X1_DirStreamDesc.descInfo.streamDesc=X1_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * X1Desc = (unsigned long long * ) &X1_DirStreamDesc;

    //Y1
    MISA_DirStreamDesc_t  Y1_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&y[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           Y1_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    Y1_DirStreamDesc.type   = DIR_STREAM;
    Y1_DirStreamDesc.valid  = VALID;
    Y1_DirStreamDesc.active =  ACTIVE ;
    Y1_DirStreamDesc.descInfo.streamDesc=Y1_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * Y1Desc = (unsigned long long * ) &Y1_DirStreamDesc;

    write_csr(0x800, *loop1Desc    ); // LOOP 1
    write_csr(0x801, *(loop1Desc+1)); //

    write_csr(0x802, *loop2Desc    ); // LOOP 2
    write_csr(0x803, *(loop2Desc+1)); //

    write_csr(0x804, *(matADesc)  ); // Matrix A
    write_csr(0x805, *(matADesc+1)); //

    write_csr(0x806, *(X1Desc)  ); // S
    write_csr(0x807, *(X1Desc+1)); //

    write_csr(0x808, *(Y1Desc)  ); // P
    write_csr(0x809, *(Y1Desc+1)); //


    printf("&A[0][0]=%p , A Descriptor = %#llx %llx  \n",&A[0][0],*(matADesc),*(matADesc+1));
    printf("&A[_PB_N-1][_PB_M-1]=%p    \n",&A[_PB_M-1][_PB_N-1] );
    printf("&x[0]=%p , x Descriptor = %#llx %llx  \n",&x[0],*(X1Desc),*(X1Desc+1));
    printf("&x[_PB_M-1]=%p    \n",&x[_PB_N-1] );
    printf("&y[0]=%p , y Descriptor = %#llx %llx  \n",&y[0],*(Y1Desc),*(Y1Desc+1));
    printf("&y[_PB_M-1]=%p    \n",&y[_PB_N-1] );

    //m5_reset_stats(0,0);
#pragma scop
  for (i = 0; i < _PB_N; i++)
    y[i] = 0;
  for (i = 0; i < _PB_M; i++)
    {
      tmp[i] = SCALAR_VAL(0.0);
      for (j = 0; j < _PB_N; j++)
	tmp[i] = tmp[i] + A[i][j] * x[j];
      for (j = 0; j < _PB_N; j++)
	y[j] = y[j] + A[i][j] * tmp[i];
    }
#pragma endscop

}


int main(int argc, char** argv)
{
  /* Retrieve problem size. */
  int m = M;
  int n = N;

  /* Variable declaration/allocation. */
  POLYBENCH_2D_ARRAY_DECL(A, DATA_TYPE, M, N, m, n);
  POLYBENCH_1D_ARRAY_DECL(x, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(y, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(tmp, DATA_TYPE, M, m);

  /* Initialize array(s). */
  init_array (m, n, POLYBENCH_ARRAY(A), POLYBENCH_ARRAY(x));

  /* Start timer. */
  polybench_start_instruments;

  /* Run kernel. */
  kernel_atax (m, n,
	       POLYBENCH_ARRAY(A),
	       POLYBENCH_ARRAY(x),
	       POLYBENCH_ARRAY(y),
	       POLYBENCH_ARRAY(tmp));

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(n, POLYBENCH_ARRAY(y)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(A);
  POLYBENCH_FREE_ARRAY(x);
  POLYBENCH_FREE_ARRAY(y);
  POLYBENCH_FREE_ARRAY(tmp);

  return 0;
}
