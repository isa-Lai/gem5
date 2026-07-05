// msrad_MB.cpp : Defines the entry point for the console application.
// Memory bound Synthestic benchmark reading from  2 streams - Excessively access a stream while slowly accesses the other

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
	int rows, cols, size_I,  niter = 10, iter, k;
        float *I, *J  ;
	float Jc;
	int i, j;

	// From BM run example
	rows = N;
	cols = M;


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




	I = (float *)malloc( size_I * sizeof(float) );
    J = (float *)malloc( size_I * sizeof(float) );



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
    int arrBStride = 128 ; // float


    MISA_DirStreamDesc_t  arrB_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&J[0],
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB_DirStreamDesc.type   = DIR_STREAM;
    arrB_DirStreamDesc.valid  = VALID;
    arrB_DirStreamDesc.active =  ACTIVE ;

    arrB_DirStreamDesc.descInfo.streamDesc=arrB_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_DirStreamDesc;





    /***************************************************************************/
    /***************************************************************************/
    printf("rows = %d  cols =%d rows*cols = %d \n",rows ,cols ,size_I );
    printf("&I[0][0]=%p , I Descriptor = %#llx %llx  \n",&I[0],*(arrADesc),*(arrADesc+1));
    printf("&J[0][0]=%p , J Descriptor = %#llx %llx  \n",&J[0],*(arrBDesc),*(arrBDesc+1));
    printf("&I[rows-1][cols-1]=%p    \n",&I[size_I*-1] );
    printf("&J[rows-1][cols-1]=%p    \n",&J[size_I-1] );


    write_csr(0x800, *loopADesc    ); // LOOP A
    write_csr(0x801, *(loopADesc+1)); //


    write_csr(0x802, *arrADesc    ); // A
    write_csr(0x803, *(arrADesc+1)); //

    write_csr(0x804, *arrBDesc    ); // B
    write_csr(0x805, *(arrBDesc+1)); //


	printf("Start\n");
    int x;

	for (int i = 0 ; i < rows ; i++) {
            for (int j = 0; j < cols; j++) {
			   k = i*rows+j;
               if(j%32==0) //I is being accessed in two cache lines stride.
                  x=J[k];
	           Jc += x+I[k];
	    }
       }

	printf("Computation Done\n");
    printf("JC=%f\n",Jc);

	free(I);
	free(J);

	return 0;
}
