/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* gesummv.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "gesummv.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"

/* Array initialization. */
static
void init_array(int n,
		DATA_TYPE *alpha,
		DATA_TYPE *beta,
		DATA_TYPE POLYBENCH_2D(A,N,N,n,n),
		DATA_TYPE POLYBENCH_2D(B,N,N,n,n),
		DATA_TYPE POLYBENCH_1D(x,N,n))
{
  int i, j;

  *alpha = 1.5;
  *beta = 1.2;
  for (i = 0; i < n; i++)
    {
      x[i] = (DATA_TYPE)( i % n) / n;
      for (j = 0; j < n; j++) {
	A[i][j] = (DATA_TYPE) ((i*j+1) % n) / n;
	B[i][j] = (DATA_TYPE) ((i*j+2) % n) / n;
      }
    }
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
void kernel_gesummv(int n,
		    DATA_TYPE alpha,
		    DATA_TYPE beta,
		    DATA_TYPE POLYBENCH_2D(A,N,N,n,n),
		    DATA_TYPE POLYBENCH_2D(B,N,N,n,n),
		    DATA_TYPE POLYBENCH_1D(tmp,N,n),
		    DATA_TYPE POLYBENCH_1D(x,N,n),
		    DATA_TYPE POLYBENCH_1D(y,N,n))
{
  int i, j;
   /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop 1
    int LOOP1_START=1, LOOP1_END = _PB_N*_PB_N , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;

    // 2- The loop 2
    int LOOP2_START=1, LOOP2_END = _PB_N , LOOP2_INC = 1 , PC2_OFFSET=01000;
    MISA_LoopDesc_t   loop2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC2_OFFSET };
    MISA_Desc_t       loop2_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop2_LoopDesc.type   = LOOP    ;
    loop2_LoopDesc.valid  = VALID   ;
    loop2_LoopDesc.active =  ACTIVE ;
    loop2_LoopDesc.descInfo.loopDesc=loop2_metaData;
    unsigned long long * loop2Desc = (unsigned long long * ) &loop2_LoopDesc;


        // 2- A[i-1][j-1] -> Stride = 1
    int arrAStride;
#ifdef DATA_TYPE_IS_DOUBLE
    arrAStride = 8 ;
#else
    arrAStride = 4 ;
#endif

    MISA_DirStreamDesc_t  A_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&A[0][0],
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           A_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    A_DirStreamDesc.type   = DIR_STREAM;
    A_DirStreamDesc.valid  = VALID;
    A_DirStreamDesc.active =  ACTIVE ;
    A_DirStreamDesc.descInfo.streamDesc=A_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * matADesc = (unsigned long long * ) &A_DirStreamDesc;


      // 3- B[i-1][j-1] -> Stride = 1
    int arrBStride;
#ifdef DATA_TYPE_IS_DOUBLE
    arrBStride = 8 ;
#else
    arrBStride = 4 ;
#endif

    MISA_DirStreamDesc_t  B_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&B[0][0],
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           B_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrQ_metaData};
    B_DirStreamDesc.type   = DIR_STREAM;
    B_DirStreamDesc.valid  = VALID;
    B_DirStreamDesc.active =  ACTIVE ;
    B_DirStreamDesc.descInfo.streamDesc=B_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * matBDesc = (unsigned long long * ) &B_DirStreamDesc;

int unitSize;
#ifdef DATA_TYPE_IS_DOUBLE
    unitSize = 8 ;
#else
    unitSize = 4 ;
#endif

    //X1
    MISA_DirStreamDesc_t  X1_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&tmp[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           X1_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    X1_DirStreamDesc.type   = DIR_STREAM;
    X1_DirStreamDesc.valid  = VALID;
    X1_DirStreamDesc.active =  ACTIVE ;
    X1_DirStreamDesc.descInfo.streamDesc=X1_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * X1Desc = (unsigned long long * ) &X1_DirStreamDesc;

    //X2
    MISA_DirStreamDesc_t  X2_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&y[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           X2_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    X2_DirStreamDesc.type   = DIR_STREAM;
    X2_DirStreamDesc.valid  = VALID;
    X2_DirStreamDesc.active =  ACTIVE ;
    X2_DirStreamDesc.descInfo.streamDesc=X2_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * X2Desc = (unsigned long long * ) &X2_DirStreamDesc;
    //Y1
    MISA_DirStreamDesc_t  Y1_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&x[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           Y1_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    Y1_DirStreamDesc.type   = DIR_STREAM;
    Y1_DirStreamDesc.valid  = VALID;
    Y1_DirStreamDesc.active =  ACTIVE ;
    Y1_DirStreamDesc.descInfo.streamDesc=Y1_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * Y1Desc = (unsigned long long * ) &Y1_DirStreamDesc;

    printf("A   of size %d*%d \n",_PB_N,_PB_N);
    printf("&A[0][0]=%p , A Descriptor = %#llx %llx  \n",&A[0][0],*(matADesc),*(matADesc+1));
    printf("&A[_PB_N-1][_PB_N-1]=%p    \n",&A[_PB_N-1][_PB_N-1] );
    printf("&B[0][0]=%p , B Descriptor = %#llx %llx  \n",&A[0][0],*(matBDesc),*(matBDesc+1));
    printf("&B[_PB_N-1][_PB_N-1]=%p    \n",&B[_PB_N-1][_PB_N-1] );
    printf("&tmp[0]=%p , tmp Descriptor = %#llx %llx  \n",&tmp[0],*(X1Desc),*(X1Desc+1));
    printf("&X[0]=%p , X Descriptor = %#llx %llx  \n",&x[0],*(X2Desc),*(X2Desc+1));
    printf("&Y[0]=%p , Y Descriptor = %#llx %llx  \n",&y[0],*(Y1Desc),*(Y1Desc+1));
    printf("&tmp[_PB_N-1]=%p    \n",&tmp[_PB_N-1] );
    printf("&X[_PB_N-1]=%p    \n",&x[_PB_N-1] );
    printf("&Y[_PB_N-1]=%p    \n",&y[_PB_N-1] );


    write_csr(0x800, *loop1Desc    ); // LOOP 1
    write_csr(0x801, *(loop1Desc+1)); //

    write_csr(0x802, *loop2Desc    ); // LOOP 2
    write_csr(0x803, *(loop2Desc+1)); //

    write_csr(0x804, *(matADesc)  ); // Matrix A
    write_csr(0x805, *(matADesc+1)); //

    write_csr(0x806, *(matBDesc)  ); // Matrix B
    write_csr(0x807, *(matBDesc+1)); //

    write_csr(0x808, *(X1Desc)  ); // X1
    write_csr(0x809, *(X1Desc+1)); //

    write_csr(0x80A, *(X2Desc)  ); // X2
    write_csr(0x80B, *(X2Desc+1)); //


    write_csr(0x80C, *(Y1Desc)  ); // Y1
    write_csr(0x80D, *(Y1Desc+1)); //





    //m5_reset_stats(0,0);

#pragma scop
  for (i = 0; i < _PB_N; i++)
    {
      tmp[i] = SCALAR_VAL(0.0);
      y[i] = SCALAR_VAL(0.0);
      for (j = 0; j < _PB_N; j++)
	{
	  tmp[i] = A[i][j] * x[j] + tmp[i];
	  y[i] = B[i][j] * x[j] + y[i];
	}
      y[i] = alpha * tmp[i] + beta * y[i];
    }
#pragma endscop

}


int main(int argc, char** argv)
{
  /* Retrieve problem size. */
  int n = N;

  /* Variable declaration/allocation. */
  DATA_TYPE alpha;
  DATA_TYPE beta;
  POLYBENCH_2D_ARRAY_DECL(A, DATA_TYPE, N, N, n, n);
  POLYBENCH_2D_ARRAY_DECL(B, DATA_TYPE, N, N, n, n);
  POLYBENCH_1D_ARRAY_DECL(tmp, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(x, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(y, DATA_TYPE, N, n);


  /* Initialize array(s). */
  init_array (n, &alpha, &beta,
	      POLYBENCH_ARRAY(A),
	      POLYBENCH_ARRAY(B),
	      POLYBENCH_ARRAY(x));

  /* Start timer. */
  polybench_start_instruments;

  /* Run kernel. */
  kernel_gesummv (n, alpha, beta,
		  POLYBENCH_ARRAY(A),
		  POLYBENCH_ARRAY(B),
		  POLYBENCH_ARRAY(tmp),
		  POLYBENCH_ARRAY(x),
		  POLYBENCH_ARRAY(y));

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(n, POLYBENCH_ARRAY(y)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(A);
  POLYBENCH_FREE_ARRAY(B);
  POLYBENCH_FREE_ARRAY(tmp);
  POLYBENCH_FREE_ARRAY(x);
  POLYBENCH_FREE_ARRAY(y);

  return 0;
}
