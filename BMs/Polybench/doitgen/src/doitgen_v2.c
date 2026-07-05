/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* doitgen.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "doitgen.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"

/* Array initialization. */
static
void init_array(int nr, int nq, int np,
		DATA_TYPE POLYBENCH_3D(A,NR,NQ,NP,nr,nq,np),
		DATA_TYPE POLYBENCH_2D(C4,NP,NP,np,np))
{
  int i, j, k;

  for (i = 0; i < nr; i++)
    for (j = 0; j < nq; j++)
      for (k = 0; k < np; k++)
	A[i][j][k] = (DATA_TYPE) ((i*j + k)%np) / np;
  for (i = 0; i < np; i++)
    for (j = 0; j < np; j++)
      C4[i][j] = (DATA_TYPE) (i*j % np) / np;
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int nr, int nq, int np,
		 DATA_TYPE POLYBENCH_3D(A,NR,NQ,NP,nr,nq,np))
{
  int i, j, k;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("A");
  for (i = 0; i < nr; i++)
    for (j = 0; j < nq; j++)
      for (k = 0; k < np; k++) {
	if ((i*nq*np+j*np+k) % 20 == 0) fprintf (POLYBENCH_DUMP_TARGET, "\n");
	fprintf (POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, A[i][j][k]);
      }
  POLYBENCH_DUMP_END("A");
  POLYBENCH_DUMP_FINISH;
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
void kernel_doitgen(int nr, int nq, int np,
		    DATA_TYPE POLYBENCH_3D(A,NR,NQ,NP,nr,nq,np),
		    DATA_TYPE POLYBENCH_2D(C4,NP,NP,np,np),
		    DATA_TYPE POLYBENCH_1D(sum,NP,np))
{
  int r, q, p, s;
  /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop
    int LOOP1_START=1, LOOP1_END = NP*NQ*NR , LOOP1_INC = 1 , PC_OFFSET=01000;
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

    write_csr(0x800, *loopDesc);     // csrrw a0 , meta_0l , meta_isa_obj1.desc_word[0];
    write_csr(0x801, *(loopDesc+1)); // csrrw a0 , meta_0h , meta_isa_obj1.desc_word[1];

    write_csr(0x802, *arrADesc    );//meta_isa_obj2.desc_word[0]); // csrrw a0 , meta_1l , meta_isa_obj2.desc_word[0];
    write_csr(0x803, *(arrADesc+1));//meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];


    printf("A of size %d*%d*%d \n",NP,NQ,NR);
    printf("&A[0][0][0]=%p , A Descriptor = %#llx %llx  \n",&A[0][0][0],*(arrADesc),*(arrADesc+1));
    printf("&sum[0]=%p , &sum[N-1]=%p\n",&sum[0],&sum[NP-1]);
    printf("&C4[0][0]=%p , &C4[N-1]=%p\n",&C4[0][0],&C4[NP-1][NP-1]);

    //m5_reset_stats(0,0);


#pragma scop
  for (r = 0; r < _PB_NR; r++)
    for (q = 0; q < _PB_NQ; q++)  {
      for (p = 0; p < _PB_NP; p++)  {
	sum[p] = SCALAR_VAL(0.0);
	for (s = 0; s < _PB_NP; s++)
	  sum[p] += A[r][q][s] * C4[s][p];
      }
      for (p = 0; p < _PB_NP; p++)
	A[r][q][p] = sum[p];
    }
#pragma endscop

}

#define cacheSize 65536*4

int main(int argc, char** argv)
{
  char cacher[cacheSize], cacher2[cacheSize];

  /* Retrieve problem size. */
  int nr = NR;
  int nq = NQ;
  int np = NP;

  /* Variable declaration/allocation. */
  POLYBENCH_3D_ARRAY_DECL(A,DATA_TYPE,NR,NQ,NP,nr,nq,np);
  POLYBENCH_1D_ARRAY_DECL(sum,DATA_TYPE,NP,np);
  POLYBENCH_2D_ARRAY_DECL(C4,DATA_TYPE,NP,NP,np,np);

  /* Initialize array(s). */
  init_array (nr, nq, np,
	      POLYBENCH_ARRAY(A),
	      POLYBENCH_ARRAY(C4));

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
  kernel_doitgen (nr, nq, np,
		  POLYBENCH_ARRAY(A),
		  POLYBENCH_ARRAY(C4),
		  POLYBENCH_ARRAY(sum));

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(nr, nq, np,  POLYBENCH_ARRAY(A)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(A);
  POLYBENCH_FREE_ARRAY(sum);
  POLYBENCH_FREE_ARRAY(C4);

  return 0;
}
