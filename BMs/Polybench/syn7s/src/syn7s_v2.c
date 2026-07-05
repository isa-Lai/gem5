// msrad_MB.cpp : Defines the entry point for the console application.
// Memory bound Synthestic benchmark reading from  7 streams

//#define OUTPUT


#define	ITERATION
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>



// InterStellar v1
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"

void random_matrix(float *I, int rows, int cols);

void usage(int argc, char **argv)
{
	fprintf(stderr, "Usage: %s <rows> <cols> <y1> <y2> <x1> <x2> <no. of threads><lamda> <no. of iter>\n", argv[0]);
	fprintf(stderr, "\t<rows>   - number of rows\n");
	fprintf(stderr, "\t<cols>    - number of cols\n");
	fprintf(stderr, "\t<y1> 	 - y1 value of the speckle\n");
	fprintf(stderr, "\t<y2>      - y2 value of the speckle\n");
	fprintf(stderr, "\t<x1>       - x1 value of the speckle\n");
	fprintf(stderr, "\t<x2>       - x2 value of the speckle\n");
	fprintf(stderr, "\t<no. of threads>  - no. of threads\n");
	fprintf(stderr, "\t<lamda>   - lambda (0,1)\n");
	fprintf(stderr, "\t<no. of iter>   - number of iterations\n");

	exit(1);
}

int main(int argc, char* argv[])
{
	int rows, cols, size_I, size_R, niter = 10, iter, k;
    float *I, *J, q0sqr, sum, sum2, tmp, meanROI,varROI ;
	float Jc, G2, L, num, den, qsqr;
	int *iN,*iS,*jE,*jW;
	float *dN,*dS,*dW,*dE;
	int r1, r2, c1, c2;
	float cN,cS,cW,cE;
	float *c, D;
	float lambda;
	int i, j;
    int nthreads;

	// From BM run example
	rows = N;
	cols = M;
	r1 =   0 ;
	r2 = N-1 ;
	c1 =   0 ;
	c2 = M-1 ;

	nthreads = 2   ;
	lambda   = 0.5 ;
	niter    =  1  ;

	// if (argc == 10)
	// {
	// 	rows = atoi(argv[1]); //number of rows in the domain
	// 	cols = atoi(argv[2]); //number of cols in the domain
	// 	if ((rows%16!=0) || (cols%16!=0)){
	// 		fprintf(stderr, "rows and cols must be multiples of 16\n");
	// 		exit(1);
	// 	}
	// 	r1   = atoi(argv[3]); //y1 position of the speckle
	// 	r2   = atoi(argv[4]); //y2 position of the speckle
	// 	c1   = atoi(argv[5]); //x1 position of the speckle
	// 	c2   = atoi(argv[6]); //x2 position of the speckle
	// 	nthreads = atoi(argv[7]); // number of threads
	// 	lambda = atof(argv[8]); //Lambda value
	// 	niter = atoi(argv[9]); //number of iterations
	// }
    // else{
	// 	usage(argc, argv);
    // }


	size_I = cols * rows;
    size_R = (r2-r1+1)*(c2-c1+1);


    /***************************************************************************/
    /***************************************************************************/
   /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop A
    int LOOP1_START=1, LOOP1_END = size_I , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loopA_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopA_LoopDesc.type   = LOOP    ;
    loopA_LoopDesc.valid  = VALID   ;
    loopA_LoopDesc.active =  ACTIVE ;
    loopA_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loopADesc = (unsigned long long * ) &loopA_LoopDesc;

    // 2- The loop B
    int LOOP2_START=1, LOOP2_END = rows , LOOP2_INC = 1 ;
    MISA_LoopDesc_t   loop2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC_OFFSET };
    MISA_Desc_t       loopB_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopB_LoopDesc.type   = LOOP    ;
    loopB_LoopDesc.valid  = VALID   ;
    loopB_LoopDesc.active =  ACTIVE ;
    loopB_LoopDesc.descInfo.loopDesc=loop2_metaData;
    unsigned long long * loopBDesc = (unsigned long long * ) &loopB_LoopDesc;






	I = (float *)malloc( size_I * sizeof(float) );
    J = (float *)malloc( size_I * sizeof(float) );
	c  = (float *)malloc(sizeof(float)* size_I) ;


    /***************************************************************************/
    /***************************************************************************/
    /***************************************************************************/


    // 4- A[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrAStride;
    arrAStride = 4 ;//  float
    MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&I[0],
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrA_DirStreamDesc.type   = DIR_STREAM;
    arrA_DirStreamDesc.valid  = VALID;
    arrA_DirStreamDesc.active =  ACTIVE ;
    arrA_DirStreamDesc.descInfo.streamDesc=arrA_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrADesc = (unsigned long long * ) &arrA_DirStreamDesc;


    // 5- B[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrBStride = 4 ; // float


    MISA_DirStreamDesc_t  arrB_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&J[0],
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB_DirStreamDesc.type   = DIR_STREAM;
    arrB_DirStreamDesc.valid  = VALID;
    arrB_DirStreamDesc.active =  ACTIVE ;

    arrB_DirStreamDesc.descInfo.streamDesc=arrB_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_DirStreamDesc;


    // 6- ARRc[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrCStride = 4 ; // float


    MISA_DirStreamDesc_t  arrC_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&c[0],
											     arrCStride , 0 , RESERVED };
    MISA_Desc_t           arrC_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrC_DirStreamDesc.type   = DIR_STREAM;
    arrC_DirStreamDesc.valid  = VALID;
    arrC_DirStreamDesc.active =  ACTIVE ;
    arrC_DirStreamDesc.descInfo.streamDesc=arrC_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrCDesc = (unsigned long long * ) &arrC_DirStreamDesc;
    /***************************************************************************/
    /***************************************************************************/




    iN = (int *)malloc(sizeof(unsigned int*) * rows) ;
    iS = (int *)malloc(sizeof(unsigned int*) * rows) ;
    jW = (int *)malloc(sizeof(unsigned int*) * cols) ;
    jE = (int *)malloc(sizeof(unsigned int*) * cols) ;


	dN = (float *)malloc(sizeof(float)* size_I) ;
    dS = (float *)malloc(sizeof(float)* size_I) ;
    dW = (float *)malloc(sizeof(float)* size_I) ;
    dE = (float *)malloc(sizeof(float)* size_I) ;

    // 5- A[i-1][j-1] -> Stride = 2*MaxSize+2
    int  arrNStride;
     arrNStride = 4 ;//  float
    MISA_DirStreamDesc_t   arrN_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&dN[0],
											      arrNStride , 0 , RESERVED };
    MISA_Desc_t            arrN_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  , arrN_metaData};
     arrN_DirStreamDesc.type   = DIR_STREAM;
     arrN_DirStreamDesc.valid  = VALID;
     arrN_DirStreamDesc.active =  ACTIVE ;
     arrN_DirStreamDesc.descInfo.streamDesc= arrN_metaData;
    // Copy from  arrNy descriptor struct to CSRs
    unsigned long long *  arrNDesc = (unsigned long long * ) & arrN_DirStreamDesc;


    // 7- B[i-1][j-1] -> Stride = 2*MaxSize+2
    int  arrSStride = 4 ; // float
    MISA_DirStreamDesc_t   arrS_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&dS[0],
											      arrSStride , 0 , RESERVED };
    MISA_Desc_t            arrS_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  , arrS_metaData};
     arrS_DirStreamDesc.type   = DIR_STREAM;
     arrS_DirStreamDesc.valid  = VALID;
     arrS_DirStreamDesc.active =  ACTIVE ;
     arrS_DirStreamDesc.descInfo.streamDesc= arrS_metaData;
    // Copy from  arrSy descriptor struct to CSRs
    unsigned long long *  arrSDesc = (unsigned long long * ) & arrS_DirStreamDesc;


    // 8-  arrW[i-1][j-1] -> Stride = 2*MaxSize+2
    int  arrWStride = 4 ; // float
    MISA_DirStreamDesc_t   arrW_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&dW[0],
											      arrWStride , 0 , RESERVED };
    MISA_Desc_t            arrW_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
     arrW_DirStreamDesc.type   = DIR_STREAM;
     arrW_DirStreamDesc.valid  = VALID;
     arrW_DirStreamDesc.active =  ACTIVE ;
     arrW_DirStreamDesc.descInfo.streamDesc= arrW_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long *  arrWDesc = (unsigned long long * ) & arrW_DirStreamDesc;

    // 9- ARRc[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrEStride = 4 ; // float
    MISA_DirStreamDesc_t    arrE_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&dE[0],
											       arrEStride , 0 , RESERVED };
    MISA_Desc_t             arrE_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  , arrE_metaData};
      arrE_DirStreamDesc.type   = DIR_STREAM;
      arrE_DirStreamDesc.valid  = VALID;
      arrE_DirStreamDesc.active =  ACTIVE ;
      arrE_DirStreamDesc.descInfo.streamDesc=  arrE_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long *   arrEDesc = (unsigned long long * ) &  arrE_DirStreamDesc;


    /***************************************************************************/
    /***************************************************************************/
    printf("rows = %d  cols =%d rows*cols = %d \n",rows ,cols ,size_I );
    printf("&I[0][0]=%p , I Descriptor = %#llx %llx  \n",&I[0],*(arrADesc),*(arrADesc+1));
    printf("&J[0][0]=%p , J Descriptor = %#llx %llx  \n",&J[0],*(arrBDesc),*(arrBDesc+1));
    printf("&c[0][0]=%p , c Descriptor = %#llx %llx  \n",&c[0],*(arrCDesc),*(arrCDesc+1));
    printf("&I[rows-1][cols-1]=%p    \n",&I[size_I*-1] );
    printf("&J[rows-1][cols-1]=%p    \n",&J[size_I-1] );
    printf("&c[rows-1][cols-1]=%p    \n",&c[size_I-1] );
    printf("&dS[0][0]=%p , I Descriptor = %#llx %llx  \n",&dS[0],*(arrSDesc),*(arrSDesc+1));
    printf("&dN[0][0]=%p , J Descriptor = %#llx %llx  \n",&dN[0],*(arrNDesc),*(arrNDesc+1));
    printf("&dW[0][0]=%p , c Descriptor = %#llx %llx  \n",&dW[0],*(arrWDesc),*(arrWDesc+1));
    printf("&dE[0][0]=%p , c Descriptor = %#llx %llx  \n",&dE[0],*(arrEDesc),*(arrEDesc+1));
    printf("&dS[rows-1][cols-1]=%p    \n",&dS[size_I*-1] );
    printf("&dNrows-1][cols-1]=%p    \n",&dN[size_I-1] );
    printf("&dW[rows-1][cols-1]=%p    \n",&dW[size_I-1] );
    printf("&dE[rows-1][cols-1]=%p    \n",&dE[size_I-1] );

    write_csr(0x800, *loopADesc    ); // LOOP A
    write_csr(0x801, *(loopADesc+1)); //

    write_csr(0x802, *loopBDesc    ); // LOOP B
    write_csr(0x803, *(loopBDesc+1)); //

    write_csr(0x804, *arrADesc    ); // A
    write_csr(0x805, *(arrADesc+1)); //

    write_csr(0x806, *arrBDesc    ); // B
    write_csr(0x807, *(arrBDesc+1)); //

    write_csr(0x808, *(arrCDesc)  ); // C
    write_csr(0x809, *(arrCDesc+1)); //


    write_csr(0x80A, *arrSDesc    ); // S
    write_csr(0x80B, *(arrSDesc+1)); //

    write_csr(0x80C, *arrNDesc    ); // N
    write_csr(0x80D, *(arrNDesc+1)); //

    write_csr(0x80E, *(arrWDesc)  ); // W
    write_csr(0x80F, *(arrWDesc+1)); //

    write_csr(0x810, *(arrEDesc)  ); // E
    write_csr(0x811, *(arrEDesc+1)); //


	printf("Start\n");

	for (int i = 0 ; i < rows ; i++) {
            for (int j = 0; j < cols; j++) {
			   k = i*rows+j;
	           Jc += I[k]+J[k]+dN[k]+dS[k]+dW[k]+dE[k]+c[k];
			}
    }

	printf("Computation Done\n");
    printf("JC=%f\n",Jc);

	free(I);
	free(J);
	free(iN); free(iS); free(jW); free(jE);
    free(dN); free(dS); free(dW); free(dE);

	free(c);
	return 0;
}
