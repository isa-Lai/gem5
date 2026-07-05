/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* mvt.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "mvt.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"


/* Array initialization. */
static
void init_array(int n,
		DATA_TYPE POLYBENCH_1D(x1,N,n),
		DATA_TYPE POLYBENCH_1D(x2,N,n),
		DATA_TYPE POLYBENCH_1D(y_1,N,n),
		DATA_TYPE POLYBENCH_1D(y_2,N,n),
		DATA_TYPE POLYBENCH_2D(A,N,N,n,n))
{
  int i, j;

  for (i = 0; i < n; i++)
    {
      x1[i] = (DATA_TYPE) (i % n) / n;
      x2[i] = (DATA_TYPE) ((i + 1) % n) / n;
      y_1[i] = (DATA_TYPE) ((i + 3) % n) / n;
      y_2[i] = (DATA_TYPE) ((i + 4) % n) / n;
      for (j = 0; j < n; j++)
	A[i][j] = (DATA_TYPE) (i*j % n) / n;
    }
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int n,
		 DATA_TYPE POLYBENCH_1D(x1,N,n),
		 DATA_TYPE POLYBENCH_1D(x2,N,n))

{
  int i;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("x1");
  for (i = 0; i < n; i++) {
    if (i % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
    fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, x1[i]);
  }
  POLYBENCH_DUMP_END("x1");

  POLYBENCH_DUMP_BEGIN("x2");
  for (i = 0; i < n; i++) {
    if (i % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
    fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, x2[i]);
  }
  POLYBENCH_DUMP_END("x2");
  POLYBENCH_DUMP_FINISH;
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
static
void kernel_mvt(int n,
		DATA_TYPE POLYBENCH_1D(x1,N,n),
		DATA_TYPE POLYBENCH_1D(x2,N,n),
		DATA_TYPE POLYBENCH_1D(y_1,N,n),
		DATA_TYPE POLYBENCH_1D(y_2,N,n),
		DATA_TYPE POLYBENCH_2D(A,N,N,n,n))
{
  //    m5_checkpoint(0,0);

   volatile int i, j;
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
    int LOOP2_START=1, LOOP2_END = _PB_N*_PB_N , LOOP2_INC = 1 , PC2_OFFSET=01000;
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
    MISA_DirStreamDesc_t  X1_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&x1[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           X1_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    X1_DirStreamDesc.type   = DIR_STREAM;
    X1_DirStreamDesc.valid  = VALID;
    X1_DirStreamDesc.active =  ACTIVE ;
    X1_DirStreamDesc.descInfo.streamDesc=X1_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * X1Desc = (unsigned long long * ) &X1_DirStreamDesc;

    //X2
    MISA_DirStreamDesc_t  X2_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&x2[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           X2_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    X2_DirStreamDesc.type   = DIR_STREAM;
    X2_DirStreamDesc.valid  = VALID;
    X2_DirStreamDesc.active =  ACTIVE ;
    X2_DirStreamDesc.descInfo.streamDesc=X2_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * X2Desc = (unsigned long long * ) &X2_DirStreamDesc;

    //Y1
    MISA_DirStreamDesc_t  Y1_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&y_1[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           Y1_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    Y1_DirStreamDesc.type   = DIR_STREAM;
    Y1_DirStreamDesc.valid  = VALID;
    Y1_DirStreamDesc.active =  ACTIVE ;
    Y1_DirStreamDesc.descInfo.streamDesc=Y1_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * Y1Desc = (unsigned long long * ) &Y1_DirStreamDesc;

    //Y2
    MISA_DirStreamDesc_t  Y2_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&y_2[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           Y2_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    Y2_DirStreamDesc.type   = DIR_STREAM;
    Y2_DirStreamDesc.valid  = VALID;
    Y2_DirStreamDesc.active =  ACTIVE ;
    Y2_DirStreamDesc.descInfo.streamDesc=Y2_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * Y2Desc = (unsigned long long * ) &Y2_DirStreamDesc;




    write_csr(0x800, *loop1Desc    ); // LOOP 1
    write_csr(0x801, *(loop1Desc+1)); //

    write_csr(0x802, *loop2Desc    ); // LOOP 2
    write_csr(0x803, *(loop2Desc+1)); //

    write_csr(0x804, *(matADesc)  ); // Matrix A
    write_csr(0x805, *(matADesc+1)); //

    write_csr(0x806, *(X1Desc)  ); // X1
    write_csr(0x807, *(X1Desc+1)); //

    write_csr(0x808, *(X2Desc)  ); // X2
    write_csr(0x809, *(X2Desc+1)); //

    write_csr(0x80A, *(Y1Desc)  ); // y_1
    write_csr(0x80B, *(Y1Desc+1)); //

    write_csr(0x80C, *(Y2Desc)  ); // y_2
    write_csr(0x80D, *(Y2Desc+1)); //

    printf("A   of size %d*%d \n",_PB_N,_PB_N);
    printf("&A[0][0]=%p , A Descriptor = %#llx %llx  \n",&A[0][0],*(matADesc),*(matADesc+1));
    printf("&A[_PB_N-1][_PB_N-1]=%p    \n",&A[_PB_N-1][_PB_N-1] );
    printf("&X1[0]=%p , X1 Descriptor = %#llx %llx  \n",&x1[0],*(X1Desc),*(X1Desc+1));
    printf("&X2[0]=%p , X2 Descriptor = %#llx %llx  \n",&x2[0],*(X2Desc),*(X2Desc+1));
    printf("&Y1[0]=%p , Y1 Descriptor = %#llx %llx  \n",&y_1[0],*(Y1Desc),*(Y1Desc+1));
    printf("&Y2[0]=%p , Y2 Descriptor = %#llx %llx  \n",&y_2[0],*(Y2Desc),*(Y2Desc+1));
    printf("&X1[_PB_N-1]=%p    \n",&x1[_PB_N-1] );
    printf("&X2[_PB_N-1]=%p    \n",&x2[_PB_N-1] );
    printf("&Y1[_PB_N-1]=%p    \n",&y_1[_PB_N-1] );
    printf("&Y2[_PB_N-1]=%p    \n",&y_2[_PB_N-1] );


    //m5_reset_stats(0,0);

#pragma scop
  for (i = 0; i < _PB_N; i++)
    for (j = 0; j < _PB_N; j++)
      x1[i] = x1[i] + A[i][j] * y_1[j];

    // Change the loop size + stride size for Matrix A


    A_DirStreamDesc.descInfo.streamDesc.stride = unitSize*_PB_N ;
    A_DirStreamDesc.descInfo.streamDesc.loopDescId = LOOP1_ID   ;
    write_csr(0x804, *(matADesc)  ); // Matrix A
    write_csr(0x805, *(matADesc+1)); //

  for (i = 0; i < _PB_N; i++)
    for (j = 0; j < _PB_N; j++)
      x2[i] = x2[i] + A[j][i] * y_2[j];
#pragma endscop

}


int main(int argc, char** argv)
{
  /* Retrieve problem size. */
  int n = N;

  /* Variable declaration/allocation. */
  POLYBENCH_2D_ARRAY_DECL(A, DATA_TYPE, N, N, n, n);
  POLYBENCH_1D_ARRAY_DECL(x1, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(x2, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(y_1, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(y_2, DATA_TYPE, N, n);


  /* Initialize array(s). */
  init_array (n,
	      POLYBENCH_ARRAY(x1),
	      POLYBENCH_ARRAY(x2),
	      POLYBENCH_ARRAY(y_1),
	      POLYBENCH_ARRAY(y_2),
	      POLYBENCH_ARRAY(A));

  /* Start timer. */
  polybench_start_instruments;

  /* Run kernel. */
  kernel_mvt (n,
	      POLYBENCH_ARRAY(x1),
	      POLYBENCH_ARRAY(x2),
	      POLYBENCH_ARRAY(y_1),
	      POLYBENCH_ARRAY(y_2),
	      POLYBENCH_ARRAY(A));
  return 0;

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(n, POLYBENCH_ARRAY(x1), POLYBENCH_ARRAY(x2)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(A);
  POLYBENCH_FREE_ARRAY(x1);
  POLYBENCH_FREE_ARRAY(x2);
  POLYBENCH_FREE_ARRAY(y_1);
  POLYBENCH_FREE_ARRAY(y_2);

  return 0;
}
