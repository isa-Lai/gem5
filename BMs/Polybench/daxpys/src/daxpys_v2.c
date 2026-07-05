/**
* @license Apache-2.0
*
* Copyright (c) 2018 The Stdlib Authors.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "daxpy.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"
#define StrideX STRIDE
#define StrideY STRIDE

/**
* Multiplies a vector `X` by a constant and adds the result to `Y`.
*
* @param N        number of indexed elements
* @param alpha    scalar
* @param X        input array
* @param strideX  X stride length
* @param Y        output array
* @param strideY  Y stride length
*/

void c_daxpy( const int N, const double alpha, const double *X, const int strideX, double *Y, const int strideY ) {
	int ix;
	int iy;
	int i;
	int m;

	if ( N <= 0 ) {
		return;
	}
	// If `alpha` is `0`, then `y` is unchanged...
	if ( alpha == 0.0 ) {
		return;
	}
	// If both strides are equal to `1`, use unrolled loops...
	if ( strideX == 1 && strideY == 1 ) {
		m = N % 4;

		// If we have a remainder, do a clean-up loop...
		if ( m > 0 ) {
			for ( i = 0; i < m; i++ ) {
				Y[ i ] += alpha * X[ i ];
			}
			if ( N < 4 ) {
				return;
			}
		}
		for ( i = m; i < N; i += 4 ) {
			Y[ i ] += alpha * X[ i ];
			Y[ i+1 ] += alpha * X[ i+1 ];
			Y[ i+2 ] += alpha * X[ i+2 ];
			Y[ i+3 ] += alpha * X[ i+3 ];
		}
		return;
	}
	if ( strideX < 0 ) {
		ix = (1-N) * strideX;
	} else {
		ix = 0;
	}
	if ( strideY < 0 ) {
		iy = (1-N) * strideY;
	} else {
		iy = 0;
	}

	   //daxpy kernel
    /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop 1
    int LOOP1_START=1, LOOP1_END = N , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;


   int  unitSize = 8 ; /* Double */
     //X2
    MISA_DirStreamDesc_t  X_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&X[0],
											     strideX*8 , 0 , RESERVED };
    MISA_Desc_t           X_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    X_DirStreamDesc.type   = DIR_STREAM;
    X_DirStreamDesc.valid  = VALID;
    X_DirStreamDesc.active =  ACTIVE ;
    X_DirStreamDesc.descInfo.streamDesc=X_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * XDesc = (unsigned long long * ) &X_DirStreamDesc;
    //Y
    MISA_DirStreamDesc_t  Y_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&Y[0],
											     strideY*8 , 0 , RESERVED };
    MISA_Desc_t           Y_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    Y_DirStreamDesc.type   = DIR_STREAM;
    Y_DirStreamDesc.valid  = VALID;
    Y_DirStreamDesc.active =  ACTIVE ;
    Y_DirStreamDesc.descInfo.streamDesc=Y_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * YDesc = (unsigned long long * ) &Y_DirStreamDesc;

    printf("x and y of size %d  \n",N);
    printf("&X[0]=%p , X Descriptor = %#llx %llx  \n",&X[0],*(XDesc),*(XDesc+1));
    printf("&Y[0]=%p , Y Descriptor = %#llx %llx  \n",&Y[0],*(YDesc),*(YDesc+1));
    printf("&X[N-1]=%p    \n",&X[N*strideX-1] );
    printf("&Y[N-1]=%p    \n",&Y[N*strideY-1] );

    write_csr(0x800, *loop1Desc    ); // LOOP 1
    write_csr(0x801, *(loop1Desc+1)); //

    write_csr(0x802, *XDesc    ); // X
    write_csr(0x803, *(XDesc+1)); //

    write_csr(0x804, *(YDesc)  ); // Y
    write_csr(0x805, *(YDesc+1)); //

    m5_reset_stats(0,0);


	for ( i = 0; i < N; i++ ) {
		Y[ iy ] += alpha * X[ ix ];
		ix += strideX;
		iy += strideY;
	}
	return;
}
int main(void)
{
   double  *x=(double*)malloc(SIZE*StrideX*sizeof(double));
   double  *y=(double*)malloc(SIZE*StrideY*sizeof(double));
   const double XVAL = rand() % 1000000;
   const double YVAL = rand() % 1000000;
   const double AVAL = rand() % 1000000;

   /*for (int i = 0; i < SIZE*STRIDE; i = i + STRIDE) {
      x[i] = XVAL;
      y[i] = YVAL;
   }*/


    c_daxpy( SIZE,  AVAL, x,  StrideX, y , StrideY );
   /*double elapsed = t.elapsed_msec();
   std::cout << "Elapsed: " << elapsed << " ms" << std::endl;
   saxpy_verify(y);
   */

   return 0;
}
