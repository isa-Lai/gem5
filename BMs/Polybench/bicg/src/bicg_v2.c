/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* bicg.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "bicg.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"


/* Array initialization. */
static
void init_array (int m, int n,
		 DATA_TYPE POLYBENCH_2D(A,N,M,n,m),
		 DATA_TYPE POLYBENCH_1D(r,N,n),
		 DATA_TYPE POLYBENCH_1D(p,M,m))
{
  int i, j;

  for (i = 0; i < m; i++)
    p[i] = (DATA_TYPE)(i % m) / m;
  for (i = 0; i < n; i++) {
    r[i] = (DATA_TYPE)(i % n) / n;
    for (j = 0; j < m; j++)
      A[i][j] = (DATA_TYPE) (i*(j+1) % n)/n;
  }
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int m, int n,
		 DATA_TYPE POLYBENCH_1D(s,M,m),
		 DATA_TYPE POLYBENCH_1D(q,N,n))

{
  int i;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("s");
  for (i = 0; i < m; i++) {
    if (i % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
    fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, s[i]);
  }
  POLYBENCH_DUMP_END("s");
  POLYBENCH_DUMP_BEGIN("q");
  for (i = 0; i < n; i++) {
    if (i % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
    fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, q[i]);
  }
  POLYBENCH_DUMP_END("q");
  POLYBENCH_DUMP_FINISH;
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
static
void kernel_bicg(int m, int n,
		 DATA_TYPE POLYBENCH_2D(A,N,M,n,m),
		 DATA_TYPE POLYBENCH_1D(s,M,m),
		 DATA_TYPE POLYBENCH_1D(q,N,n),
		 DATA_TYPE POLYBENCH_1D(p,M,m),
		 DATA_TYPE POLYBENCH_1D(r,N,n))
{
  int i, j;

   /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop 1
    int LOOP1_START=1, LOOP1_END = _PB_M , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;

    // 2- The loop 2
    int LOOP2_START=1, LOOP2_END = _PB_N*_PB_M , LOOP2_INC = 1 , PC2_OFFSET=01000;
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
    //S
    MISA_DirStreamDesc_t  S_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&s[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           S_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    S_DirStreamDesc.type   = DIR_STREAM;
    S_DirStreamDesc.valid  = VALID;
    S_DirStreamDesc.active =  ACTIVE ;
    S_DirStreamDesc.descInfo.streamDesc=S_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * SDesc = (unsigned long long * ) &S_DirStreamDesc;

    //P
    MISA_DirStreamDesc_t  P_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&p[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           P_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    P_DirStreamDesc.type   = DIR_STREAM;
    P_DirStreamDesc.valid  = VALID;
    P_DirStreamDesc.active =  ACTIVE ;
    P_DirStreamDesc.descInfo.streamDesc=P_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * PDesc = (unsigned long long * ) &P_DirStreamDesc;

    write_csr(0x800, *loop1Desc    ); // LOOP 1
    write_csr(0x801, *(loop1Desc+1)); //

    write_csr(0x802, *loop2Desc    ); // LOOP 2
    write_csr(0x803, *(loop2Desc+1)); //

    write_csr(0x804, *(matADesc)  ); // Matrix A
    write_csr(0x805, *(matADesc+1)); //

    write_csr(0x806, *(SDesc)  ); // S
    write_csr(0x807, *(SDesc+1)); //

    write_csr(0x808, *(PDesc)  ); // P
    write_csr(0x809, *(PDesc+1)); //


    printf("&A[0][0]=%p , A Descriptor = %#llx %llx  \n",&A[0][0],*(matADesc),*(matADesc+1));
    printf("&A[_PB_N-1][_PB_M-1]=%p    \n",&A[_PB_N-1][_PB_M-1] );
    printf("&s[0]=%p , S Descriptor = %#llx %llx  \n",&s[0],*(SDesc),*(SDesc+1));
    printf("&s[_PB_M-1]=%p    \n",&s[_PB_M-1] );
    printf("&p[0]=%p , P Descriptor = %#llx %llx  \n",&p[0],*(PDesc),*(PDesc+1));
    printf("&p[_PB_M-1]=%p    \n",&p[_PB_M-1] );

   // m5_reset_stats(0,0);
#pragma scop
  for (i = 0; i < _PB_M; i++)
    s[i] = 0;
  for (i = 0; i < _PB_N; i++)
  {
      q[i] = SCALAR_VAL(0.0);
      for (j = 0; j < _PB_M; j++)
	    {
        s[j] = s[j] + r[i] * A[i][j];
        q[i] = q[i] + A[i][j] * p[j];
	    }
  }
#pragma endscop

}


int main(int argc, char** argv)
{
  /* Retrieve problem size. */
  int n = N;
  int m = M;

  /* Variable declaration/allocation. */
  POLYBENCH_2D_ARRAY_DECL(A, DATA_TYPE, N, M, n, m);
  POLYBENCH_1D_ARRAY_DECL(s, DATA_TYPE, M, m);
  POLYBENCH_1D_ARRAY_DECL(q, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(p, DATA_TYPE, M, m);
  POLYBENCH_1D_ARRAY_DECL(r, DATA_TYPE, N, n);

  /* Initialize array(s). */
  init_array (m, n,
	      POLYBENCH_ARRAY(A),
	      POLYBENCH_ARRAY(r),
	      POLYBENCH_ARRAY(p));

  /* Start timer. */
  polybench_start_instruments;

  /* Run kernel. */
  kernel_bicg (m, n,
	       POLYBENCH_ARRAY(A),
	       POLYBENCH_ARRAY(s),
	       POLYBENCH_ARRAY(q),
	       POLYBENCH_ARRAY(p),
	       POLYBENCH_ARRAY(r));

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(m, n, POLYBENCH_ARRAY(s), POLYBENCH_ARRAY(q)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(A);
  POLYBENCH_FREE_ARRAY(s);
  POLYBENCH_FREE_ARRAY(q);
  POLYBENCH_FREE_ARRAY(p);
  POLYBENCH_FREE_ARRAY(r);

  return 0;
}
