/**
 * This version is stamped on May 10, 2016
 *
 * Contact:
 *   Louis-Noel Pouchet <pouchet.ohio-state.edu>
 *   Tomofumi Yuki <tomofumi.yuki.fr>
 *
 * Web address: http://polybench.sourceforge.net
 */
/* fdtd-2d.c: this file is part of PolyBench/C */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
#include "fdtd-2d.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"

/* Array initialization. */
static
void init_array (int tmax,
		 int nx,
		 int ny,
		 DATA_TYPE POLYBENCH_2D(ex,NX,NY,nx,ny),
		 DATA_TYPE POLYBENCH_2D(ey,NX,NY,nx,ny),
		 DATA_TYPE POLYBENCH_2D(hz,NX,NY,nx,ny),
		 DATA_TYPE POLYBENCH_1D(_fict_,TMAX,tmax))
{
  int i, j;

  for (i = 0; i < tmax; i++)
    _fict_[i] = (DATA_TYPE) i;
  for (i = 0; i < nx; i++)
    for (j = 0; j < ny; j++)
      {
	ex[i][j] = ((DATA_TYPE) i*(j+1)) / nx;
	ey[i][j] = ((DATA_TYPE) i*(j+2)) / ny;
	hz[i][j] = ((DATA_TYPE) i*(j+3)) / nx;
      }
}


/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static
void print_array(int nx,
		 int ny,
		 DATA_TYPE POLYBENCH_2D(ex,NX,NY,nx,ny),
		 DATA_TYPE POLYBENCH_2D(ey,NX,NY,nx,ny),
		 DATA_TYPE POLYBENCH_2D(hz,NX,NY,nx,ny))
{
  int i, j;

  POLYBENCH_DUMP_START;
  POLYBENCH_DUMP_BEGIN("ex");
  for (i = 0; i < nx; i++)
    for (j = 0; j < ny; j++) {
      if ((i * nx + j) % 20 == 0) fprintf(POLYBENCH_DUMP_TARGET, "\n");
      fprintf(POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, ex[i][j]);
    }
  POLYBENCH_DUMP_END("ex");
  POLYBENCH_DUMP_FINISH;

  POLYBENCH_DUMP_BEGIN("ey");
  for (i = 0; i < nx; i++)
    for (j = 0; j < ny; j++) {
      if ((i * nx + j) % 20 == 0) fprintf(POLYBENCH_DUMP_TARGET, "\n");
      fprintf(POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, ey[i][j]);
    }
  POLYBENCH_DUMP_END("ey");

  POLYBENCH_DUMP_BEGIN("hz");
  for (i = 0; i < nx; i++)
    for (j = 0; j < ny; j++) {
      if ((i * nx + j) % 20 == 0) fprintf(POLYBENCH_DUMP_TARGET, "\n");
      fprintf(POLYBENCH_DUMP_TARGET, DATA_PRINTF_MODIFIER, hz[i][j]);
    }
  POLYBENCH_DUMP_END("hz");
}


/* Main computational kernel. The whole function will be timed,
   including the call and return. */
static
void kernel_fdtd_2d(int tmax,
		    int nx,
		    int ny,
		    DATA_TYPE POLYBENCH_2D(ex,NX,NY,nx,ny),
		    DATA_TYPE POLYBENCH_2D(ey,NX,NY,nx,ny),
		    DATA_TYPE POLYBENCH_2D(hz,NX,NY,nx,ny),
		    DATA_TYPE POLYBENCH_1D(_fict_,TMAX,tmax))
{
  int t, i, j;

  /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop 1
    int LOOP1_START=1, LOOP1_END = _PB_NY , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;

    // 2- The loop 2
    int LOOP2_START=1, LOOP2_END = _PB_NY*_PB_NX , LOOP2_INC = 1 , PC2_OFFSET=01000;
    MISA_LoopDesc_t   loop2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC2_OFFSET };
    MISA_Desc_t       loop2_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop2_LoopDesc.type   = LOOP    ;
    loop2_LoopDesc.valid  = VALID   ;
    loop2_LoopDesc.active =  ACTIVE ;
    loop2_LoopDesc.descInfo.loopDesc=loop2_metaData;
    unsigned long long * loop2Desc = (unsigned long long * ) &loop2_LoopDesc;






    // 1- _fict_[0] 1D
    int arrAStride;
#ifdef DATA_TYPE_IS_DOUBLE
    arrAStride = 8 ;
#else
    arrAStride = 4 ;
#endif
    MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&_fict_[0],
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrA_DirStreamDesc.type   = DIR_STREAM;
    arrA_DirStreamDesc.valid  = VALID;
    arrA_DirStreamDesc.active =  ACTIVE ;
    arrA_DirStreamDesc.descInfo.streamDesc=arrA_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrADesc = (unsigned long long * ) &arrA_DirStreamDesc;


    // 2- ey[i-1][j-1] ->
#ifdef DATA_TYPE_IS_DOUBLE
    int arrBStride = 8 ;
#else
    int arrBStride = 4 ;
#endif

    MISA_DirStreamDesc_t  arrB_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&ey[0][0],
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB_DirStreamDesc.type   = DIR_STREAM;
    arrB_DirStreamDesc.valid  = VALID;
    arrB_DirStreamDesc.active =  ACTIVE ;
    arrB_DirStreamDesc.descInfo.streamDesc=arrB_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_DirStreamDesc;


       // 3- hz[i-1][j-1] ->

#ifdef DATA_TYPE_IS_DOUBLE
    int arrCStride = 8 ;
#else
    int arrCStride = 4 ;
#endif

    MISA_DirStreamDesc_t  arrC_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&hz[0][0],
											     arrCStride , 0 , RESERVED };
    MISA_Desc_t           arrC_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrC_DirStreamDesc.type   = DIR_STREAM;
    arrC_DirStreamDesc.valid  = VALID;
    arrC_DirStreamDesc.active =  ACTIVE ;
    arrC_DirStreamDesc.descInfo.streamDesc=arrC_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrCDesc = (unsigned long long * ) &arrC_DirStreamDesc;

       // 4- ex[i-1][j-1] ->
#ifdef DATA_TYPE_IS_DOUBLE
    int arrDStride = 8 ;
#else
    int arrDStride = 4 ;
#endif

    MISA_DirStreamDesc_t  arrD_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&ex[0][0],
											     arrDStride , 0 , RESERVED };
    MISA_Desc_t           arrD_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrD_DirStreamDesc.type   = DIR_STREAM;
    arrD_DirStreamDesc.valid  = VALID;
    arrD_DirStreamDesc.active =  ACTIVE ;
    arrD_DirStreamDesc.descInfo.streamDesc=arrD_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrDDesc = (unsigned long long * ) &arrD_DirStreamDesc;






    printf("Number of iterations = %d\n",_PB_TMAX);
    printf("ey , hz and ex of size %d*%d \n",_PB_NX,_PB_NY);

    printf("&_fict_[0]=%p , _fict_ Descriptor = %#llx %llx  \n",&_fict_[0],*(arrADesc),*(arrADesc+1));
    printf("&ey[0][0]=%p  , ey Descriptor = %#llx %llx  \n",&ey[0][0],*(arrBDesc),*(arrBDesc+1));
    printf("&hz[0][0]=%p  , hz Descriptor = %#llx %llx  \n",&hz[0][0],*(arrCDesc),*(arrCDesc+1));
    printf("&ex[0][0]=%p  , ex Descriptor = %#llx %llx  \n",&ex[0][0],*(arrDDesc),*(arrDDesc+1));

    printf("&_fict_[_PB_NX-1] =%p  , _fict_ Descriptor = %#llx %llx  \n",&_fict_[_PB_NX-1] ,*(arrADesc),*(arrADesc+1));
    printf("&ey[_PB_NX-1][_PB_NY-1]=%p , ey Descriptor = %#llx %llx  \n",&ey[_PB_NX-1][_PB_NY-1],*(arrBDesc),*(arrBDesc+1));
    printf("&hz[_PB_NX-1][_PB_NY-1]=%p , hz Descriptor = %#llx %llx  \n",&hz[_PB_NX-1][_PB_NY-1],*(arrCDesc),*(arrCDesc+1));
    printf("&ex[_PB_NX-1][_PB_NY-1]=%p , ex Descriptor = %#llx %llx  \n",&ex[_PB_NX-1][_PB_NY-1],*(arrDDesc),*(arrDDesc+1));

//  m5_checkpoint(0,0);
  //m5_reset_stats(0,0);
    write_csr(0x800, *loop1Desc);     // csrrw a0 , meta_0l , meta_isa_obj1.desc_word[0];
    write_csr(0x801, *(loop1Desc+1)); // csrrw a0 , meta_0h , meta_isa_obj1.desc_word[1];

    write_csr(0x802, *loop2Desc);     // csrrw a0 , meta_0l , meta_isa_obj1.desc_word[0];
    write_csr(0x803, *(loop2Desc+1)); // csrrw a0 , meta_0h , meta_isa_obj1.desc_word[1];

   // write_csr(0x804, *arrADesc    );  //meta_isa_obj2.desc_word[0]); // csrrw a0 , meta_1l , meta_isa_obj2.desc_word[0];
   // write_csr(0x805, *(arrADesc+1));  //meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

    write_csr(0x804, *(arrBDesc));    //meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];
    write_csr(0x805, *(arrBDesc+1));  //meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

    write_csr(0x806, *(arrCDesc));   //meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];
    write_csr(0x807, *(arrCDesc+1)); //meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

    write_csr(0x808, *(arrDDesc));    //meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];
    write_csr(0x809, *(arrDDesc+1));  //meta_isa_obj2.desc_word[1]); // csrrw a0 , meta_1h , meta_isa_obj2.desc_word[1];

#pragma scop
  for(t = 0; t < _PB_TMAX; t++)
    {

      for (j = 0; j < _PB_NY; j++)
	ey[0][j] = _fict_[t];


      for (i = 1; i < _PB_NX; i++)
	for (j = 0; j < _PB_NY; j++)
	  ey[i][j] = ey[i][j] - SCALAR_VAL(0.5)*(hz[i][j]-hz[i-1][j]);


      for (i = 0; i < _PB_NX; i++)
	for (j = 1; j < _PB_NY; j++)
	  ex[i][j] = ex[i][j] - SCALAR_VAL(0.5)*(hz[i][j]-hz[i][j-1]);


      for (i = 0; i < _PB_NX - 1; i++)
	for (j = 0; j < _PB_NY - 1; j++)
	  hz[i][j] = hz[i][j] - SCALAR_VAL(0.7)*  (ex[i][j+1] - ex[i][j] +
				       ey[i+1][j] - ey[i][j]);


    }

#pragma endscop
}


int main(int argc, char** argv)
{
  /* Retrieve problem size. */
  int tmax = TMAX;
  int nx = NX;
  int ny = NY;

  /* Variable declaration/allocation. */
  POLYBENCH_2D_ARRAY_DECL(ex,DATA_TYPE,NX,NY,nx,ny);
  POLYBENCH_2D_ARRAY_DECL(ey,DATA_TYPE,NX,NY,nx,ny);
  POLYBENCH_2D_ARRAY_DECL(hz,DATA_TYPE,NX,NY,nx,ny);
  POLYBENCH_1D_ARRAY_DECL(_fict_,DATA_TYPE,TMAX,tmax);

  /* Initialize array(s). */
  init_array (tmax, nx, ny,
	      POLYBENCH_ARRAY(ex),
	      POLYBENCH_ARRAY(ey),
	      POLYBENCH_ARRAY(hz),
	      POLYBENCH_ARRAY(_fict_));

  /* Start timer. */
  polybench_start_instruments;

  /* Run kernel. */
  kernel_fdtd_2d (tmax, nx, ny,
		  POLYBENCH_ARRAY(ex),
		  POLYBENCH_ARRAY(ey),
		  POLYBENCH_ARRAY(hz),
		  POLYBENCH_ARRAY(_fict_));

  return 0;

  /* Stop and print timer. */
  polybench_stop_instruments;
  polybench_print_instruments;

  /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  polybench_prevent_dce(print_array(nx, ny, POLYBENCH_ARRAY(ex),
				    POLYBENCH_ARRAY(ey),
				    POLYBENCH_ARRAY(hz)));

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(ex);
  POLYBENCH_FREE_ARRAY(ey);
  POLYBENCH_FREE_ARRAY(hz);
  POLYBENCH_FREE_ARRAY(_fict_);

  return 0;
}
