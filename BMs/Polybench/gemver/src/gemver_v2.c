/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* gemver.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "gemver.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"


/* Array initialization. */
static
void init_array (int n,
		 DATA_TYPE *alpha,
		 DATA_TYPE *beta,
		 DATA_TYPE POLYBENCH_2D(A,N,N,n,n),
		 DATA_TYPE POLYBENCH_1D(u1,N,n),
		 DATA_TYPE POLYBENCH_1D(v1,N,n),
		 DATA_TYPE POLYBENCH_1D(u2,N,n),
		 DATA_TYPE POLYBENCH_1D(v2,N,n),
		 DATA_TYPE POLYBENCH_1D(w,N,n),
		 DATA_TYPE POLYBENCH_1D(x,N,n),
		 DATA_TYPE POLYBENCH_1D(y,N,n),
		 DATA_TYPE POLYBENCH_1D(z,N,n))
{
  int i, j;

  *alpha = 1.5;
  *beta = 1.2;

  DATA_TYPE fn = (DATA_TYPE)n;

  for (i = 0; i < n; i++)
    {
      u1[i] = i;
      u2[i] = ((i+1)/fn)/2.0;
      v1[i] = ((i+1)/fn)/4.0;
      v2[i] = ((i+1)/fn)/6.0;
      y[i] = ((i+1)/fn)/8.0;
      z[i] = ((i+1)/fn)/9.0;
      x[i] = 0.0;
      w[i] = 0.0;
      for (j = 0; j < n; j++)
        A[i][j] = (DATA_TYPE) (i*j % n) / n;
    }
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int n,
		 DATA_TYPE POLYBENCH_1D(w,N,n))
{
  int i;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("w");
  for (i = 0; i < n; i++) {
    if (i % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
    fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, w[i]);
  }
  POLYBENCH_DUMP_END("w");
  POLYBENCH_DUMP_FINISH;
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
static
void kernel_gemver(int n,
		   DATA_TYPE alpha,
		   DATA_TYPE beta,
		   DATA_TYPE POLYBENCH_2D(A,N,N,n,n),
		   DATA_TYPE POLYBENCH_1D(u1,N,n),
		   DATA_TYPE POLYBENCH_1D(v1,N,n),
		   DATA_TYPE POLYBENCH_1D(u2,N,n),
		   DATA_TYPE POLYBENCH_1D(v2,N,n),
		   DATA_TYPE POLYBENCH_1D(w,N,n),
		   DATA_TYPE POLYBENCH_1D(x,N,n),
		   DATA_TYPE POLYBENCH_1D(y,N,n),
		   DATA_TYPE POLYBENCH_1D(z,N,n))
{
  int i, j;

#pragma scop
	int doubleStride = 8;
	int intStride    = 4;

	// 1- Stream 1
	// Size = (_# Rows*27)
	int LOOP1_START=1, LOOP1_END = _PB_N*_PB_N , LOOP1_INC = 1 , PC_OFFSET=01000;
	MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
			LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
	MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
	loop1_LoopDesc.type   =  LOOP    ;
	loop1_LoopDesc.valid  =  VALID   ;
	loop1_LoopDesc.active =  ACTIVE  ;
	loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
	unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;
	// 2- Stream 2
	// Size = (_# Rows)
	int LOOP2_START=1, LOOP2_END =  _PB_N, LOOP2_INC = 1 ; PC_OFFSET=01000;
	MISA_LoopDesc_t   LOOP2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
			LOOP2_START, LOOP2_END , LOOP2_INC  , PC_OFFSET };
	MISA_Desc_t       LOOP2_LoopDesc; //      ={LOOP,VALID,ACTIVE ,LOOP2_metaData};
	LOOP2_LoopDesc.type   =  LOOP    ;
	LOOP2_LoopDesc.valid  =  VALID   ;
	LOOP2_LoopDesc.active =  ACTIVE  ;
	LOOP2_LoopDesc.descInfo.loopDesc=LOOP2_metaData;
	unsigned long long * loop2Desc = (unsigned long long * ) &LOOP2_LoopDesc;


	//3- Stream A  ;   A
	MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&A[0][0]  ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrA_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
	arrA_DirStreamDesc.type   = DIR_STREAM;
	arrA_DirStreamDesc.valid  = VALID;
	arrA_DirStreamDesc.active =  ACTIVE ;
	arrA_DirStreamDesc.descInfo.streamDesc=arrA_metaData;
	// Copy from array descriptor struct to CSRs
	unsigned long long * arrADesc = (unsigned long long * ) &arrA_DirStreamDesc;


	//4- Stream A2 ; mtxIndL
	MISA_DirStreamDesc_t  arrA2_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&u1[0]    ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrA2_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA2_metaData};
	arrA2_DirStreamDesc.type   = DIR_STREAM;
	arrA2_DirStreamDesc.valid  = VALID;
	arrA2_DirStreamDesc.active =  ACTIVE ;
	arrA2_DirStreamDesc.descInfo.streamDesc=arrA2_metaData;
	// Copy from arrA2y descriptor struct to CSRs
	unsigned long long * arrA2Desc = (unsigned long long * ) &arrA2_DirStreamDesc;

	//6- Stream A3 ; nonzerosInRow
	MISA_DirStreamDesc_t  arrA3_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&u2[0]    ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrA3_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA3_metaData};
	arrA3_DirStreamDesc.type   = DIR_STREAM;
	arrA3_DirStreamDesc.valid  = VALID;
	arrA3_DirStreamDesc.active =  ACTIVE ;
	arrA3_DirStreamDesc.descInfo.streamDesc=arrA3_metaData;
	// Copy from arrA3y descriptor struct to CSRs
	unsigned long long * arrA3Desc = (unsigned long long * ) &arrA3_DirStreamDesc;

	//7- Stream A4 ; v1
	MISA_DirStreamDesc_t  arrA4_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&v1[0]    ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrA4_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA4_metaData};
	arrA4_DirStreamDesc.type   = DIR_STREAM;
	arrA4_DirStreamDesc.valid  = VALID;
	arrA4_DirStreamDesc.active =  ACTIVE ;
	arrA4_DirStreamDesc.descInfo.streamDesc=arrA4_metaData;
	// Copy from arrA4y descriptor struct to CSRs
	unsigned long long * arrA4Desc = (unsigned long long * ) &arrA4_DirStreamDesc;

	//8- Stream A5 ;  v2
	MISA_DirStreamDesc_t  arrA5_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)v2[0]    ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrA5_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA5_metaData};
	arrA5_DirStreamDesc.type   = DIR_STREAM;
	arrA5_DirStreamDesc.valid  = VALID;
	arrA5_DirStreamDesc.active =  ACTIVE ;
	arrA5_DirStreamDesc.descInfo.streamDesc=arrA5_metaData;
	// Copy from arrA5y descriptor struct to CSRs
	unsigned long long * arrA5Desc = (unsigned long long * ) &arrA5_DirStreamDesc;


	//6- Stream A6 ; x
	MISA_DirStreamDesc_t  arrX6_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&x[0]    ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrX6_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrX6_metaData};
	arrX6_DirStreamDesc.type   = DIR_STREAM;
	arrX6_DirStreamDesc.valid  = VALID;
	arrX6_DirStreamDesc.active =  ACTIVE ;
	arrX6_DirStreamDesc.descInfo.streamDesc=arrX6_metaData;
	// Copy from arrX6y descriptor struct to CSRs
	unsigned long long * arrX6Desc = (unsigned long long * ) &arrX6_DirStreamDesc;

	//7- Stream A7 ; y
	MISA_DirStreamDesc_t  arrY7_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&y[0]    ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrY7_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrY7_metaData};
	arrY7_DirStreamDesc.type   = DIR_STREAM;
	arrY7_DirStreamDesc.valid  = VALID;
	arrY7_DirStreamDesc.active =  ACTIVE ;
	arrY7_DirStreamDesc.descInfo.streamDesc=arrY7_metaData;
	// Copy from arrY7y descriptor struct to CSRs
	unsigned long long * arrY7Desc = (unsigned long long * ) &arrY7_DirStreamDesc;

	//8- Stream A8 ;  z
	MISA_DirStreamDesc_t  arrZ8_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)z[0]    ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrZ8_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrZ8_metaData};
	arrZ8_DirStreamDesc.type   = DIR_STREAM;
	arrZ8_DirStreamDesc.valid  = VALID;
	arrZ8_DirStreamDesc.active =  ACTIVE ;
	arrZ8_DirStreamDesc.descInfo.streamDesc=arrZ8_metaData;
	// Copy from arrZ8y descriptor struct to CSRs
	unsigned long long * arrZ8Desc = (unsigned long long * ) &arrZ8_DirStreamDesc;


	//8- Stream A8 ;  w
	MISA_DirStreamDesc_t  arrW9_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)w[0]    ,
			doubleStride , 0 , RESERVED };
	MISA_Desc_t           arrW9_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrW9_metaData};
	arrW9_DirStreamDesc.type   = DIR_STREAM;
	arrW9_DirStreamDesc.valid  = VALID;
	arrW9_DirStreamDesc.active =  ACTIVE ;
	arrW9_DirStreamDesc.descInfo.streamDesc=arrW9_metaData;
	// Copy from arrW9y descriptor struct to CSRs
	unsigned long long * arrW9Desc = (unsigned long long * ) &arrW9_DirStreamDesc;

	write_csr(0x800, *loop1Desc);     // csrrw a0 , meta_0l , meta_isa_obj1.desc_word[0];
	write_csr(0x801, *(loop1Desc+1)); // csrrw a0 , meta_0h , meta_isa_obj1.desc_word[1];

	write_csr(0x802, *loop2Desc);
	write_csr(0x803, *(loop2Desc+1));

	write_csr(0x804, *arrADesc    );//meta_isa_obj2.desc_word[0]); // csrrw a0 , meta_1l , meta_isa_obj2.desc_word[0];
	write_csr(0x805, *(arrADesc+1));//meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

	write_csr(0x806, *arrA2Desc    );
	write_csr(0x807, *(arrA2Desc+1));

	write_csr(0x808, *arrA3Desc    );
	write_csr(0x809, *(arrA3Desc+1));

	write_csr(0x80A, *arrA4Desc    );
	write_csr(0x80B, *(arrA4Desc+1));

	write_csr(0x80C, *arrA5Desc    );
	write_csr(0x80D, *(arrA5Desc+1));


	write_csr(0x80E, *arrX6Desc    );
	write_csr(0x80F, *(arrX6Desc+1));

	write_csr(0x810, *arrY7Desc    );
	write_csr(0x811, *(arrY7Desc+1));

	write_csr(0x812, *arrZ8Desc    );
	write_csr(0x813, *(arrZ8Desc+1));

	write_csr(0x814, *arrW9Desc    );
	write_csr(0x815, *(arrW9Desc+1));



	//m5_reset_stats(0,0);


  for (i = 0; i < _PB_N; i++)
    for (j = 0; j < _PB_N; j++)
      A[i][j] = A[i][j] + u1[i] * v1[j] + u2[i] * v2[j];


  arrA_DirStreamDesc.descInfo.streamDesc.stride     = doubleStride*_PB_N ;
  arrA_DirStreamDesc.descInfo.streamDesc.loopDescId = LOOP2_ID   ;
  write_csr(0x804, *(arrADesc)  ); // Matrix A
  write_csr(0x805, *(arrADesc+1)); //

  for (i = 0; i < _PB_N; i++)
    for (j = 0; j < _PB_N; j++)
      x[i] = x[i] + beta * A[j][i] * y[j];

  for (i = 0; i < _PB_N; i++)
    x[i] = x[i] + z[i];


   arrA_DirStreamDesc.descInfo.streamDesc.stride     = doubleStride   ;
   arrA_DirStreamDesc.descInfo.streamDesc.loopDescId = LOOP1_ID       ;
   write_csr(0x804, *(arrADesc)  ); // Matrix A
   write_csr(0x805, *(arrADesc+1)); //

  for (i = 0; i < _PB_N; i++)
    for (j = 0; j < _PB_N; j++)
      w[i] = w[i] +  alpha * A[i][j] * x[j];

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
  POLYBENCH_1D_ARRAY_DECL(u1, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(v1, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(u2, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(v2, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(w, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(x, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(y, DATA_TYPE, N, n);
  POLYBENCH_1D_ARRAY_DECL(z, DATA_TYPE, N, n);


  /* Initialize array(s). */
  init_array (n, &alpha, &beta,
	      POLYBENCH_ARRAY(A),
	      POLYBENCH_ARRAY(u1),
	      POLYBENCH_ARRAY(v1),
	      POLYBENCH_ARRAY(u2),
	      POLYBENCH_ARRAY(v2),
	      POLYBENCH_ARRAY(w),
	      POLYBENCH_ARRAY(x),
	      POLYBENCH_ARRAY(y),
	      POLYBENCH_ARRAY(z));

  /* Start timer. */
  polybench_start_instruments;

  /* Run kernel. */
  kernel_gemver (n, alpha, beta,
		 POLYBENCH_ARRAY(A),
		 POLYBENCH_ARRAY(u1),
		 POLYBENCH_ARRAY(v1),
		 POLYBENCH_ARRAY(u2),
		 POLYBENCH_ARRAY(v2),
		 POLYBENCH_ARRAY(w),
		 POLYBENCH_ARRAY(x),
		 POLYBENCH_ARRAY(y),
		 POLYBENCH_ARRAY(z));

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(n, POLYBENCH_ARRAY(w)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(A);
  POLYBENCH_FREE_ARRAY(u1);
  POLYBENCH_FREE_ARRAY(v1);
  POLYBENCH_FREE_ARRAY(u2);
  POLYBENCH_FREE_ARRAY(v2);
  POLYBENCH_FREE_ARRAY(w);
  POLYBENCH_FREE_ARRAY(x);
  POLYBENCH_FREE_ARRAY(y);
  POLYBENCH_FREE_ARRAY(z);

  return 0;
}
