#define LIMIT -999
//#define TRACE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
//#include <omp.h>
//#define OPENMP
//#define NUM_THREAD 4

// InterStellar v1
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"

#define BLOCK_SIZE 16

////////////////////////////////////////////////////////////////////////////////
// declaration, forward
void runTest( int argc, char** argv);

// Returns the current system time in microseconds
long long get_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000000) + tv.tv_usec;

}

#ifdef OMP_OFFLOAD
#pragma omp declare target
#endif
int maximum( int a,
		 int b,
		 int c){

	int k;
	if( a <= b )
		k = b;
	else
	k = a;

	if( k <=c )
	return(c);
	else
	return(k);
}
#ifdef OMP_OFFLOAD
#pragma omp end declare target
#endif


int blosum62[24][24] = {
{ 4, -1, -2, -2,  0, -1, -1,  0, -2, -1, -1, -1, -1, -2, -1,  1,  0, -3, -2,  0, -2, -1,  0, -4},
{-1,  5,  0, -2, -3,  1,  0, -2,  0, -3, -2,  2, -1, -3, -2, -1, -1, -3, -2, -3, -1,  0, -1, -4},
{-2,  0,  6,  1, -3,  0,  0,  0,  1, -3, -3,  0, -2, -3, -2,  1,  0, -4, -2, -3,  3,  0, -1, -4},
{-2, -2,  1,  6, -3,  0,  2, -1, -1, -3, -4, -1, -3, -3, -1,  0, -1, -4, -3, -3,  4,  1, -1, -4},
{ 0, -3, -3, -3,  9, -3, -4, -3, -3, -1, -1, -3, -1, -2, -3, -1, -1, -2, -2, -1, -3, -3, -2, -4},
{-1,  1,  0,  0, -3,  5,  2, -2,  0, -3, -2,  1,  0, -3, -1,  0, -1, -2, -1, -2,  0,  3, -1, -4},
{-1,  0,  0,  2, -4,  2,  5, -2,  0, -3, -3,  1, -2, -3, -1,  0, -1, -3, -2, -2,  1,  4, -1, -4},
{ 0, -2,  0, -1, -3, -2, -2,  6, -2, -4, -4, -2, -3, -3, -2,  0, -2, -2, -3, -3, -1, -2, -1, -4},
{-2,  0,  1, -1, -3,  0,  0, -2,  8, -3, -3, -1, -2, -1, -2, -1, -2, -2,  2, -3,  0,  0, -1, -4},
{-1, -3, -3, -3, -1, -3, -3, -4, -3,  4,  2, -3,  1,  0, -3, -2, -1, -3, -1,  3, -3, -3, -1, -4},
{-1, -2, -3, -4, -1, -2, -3, -4, -3,  2,  4, -2,  2,  0, -3, -2, -1, -2, -1,  1, -4, -3, -1, -4},
{-1,  2,  0, -1, -3,  1,  1, -2, -1, -3, -2,  5, -1, -3, -1,  0, -1, -3, -2, -2,  0,  1, -1, -4},
{-1, -1, -2, -3, -1,  0, -2, -3, -2,  1,  2, -1,  5,  0, -2, -1, -1, -1, -1,  1, -3, -1, -1, -4},
{-2, -3, -3, -3, -2, -3, -3, -3, -1,  0,  0, -3,  0,  6, -4, -2, -2,  1,  3, -1, -3, -3, -1, -4},
{-1, -2, -2, -1, -3, -1, -1, -2, -2, -3, -3, -1, -2, -4,  7, -1, -1, -4, -3, -2, -2, -1, -2, -4},
{ 1, -1,  1,  0, -1,  0,  0,  0, -1, -2, -2,  0, -1, -2, -1,  4,  1, -3, -2, -2,  0,  0,  0, -4},
{ 0, -1,  0, -1, -1, -1, -1, -2, -2, -1, -1, -1, -1, -2, -1,  1,  5, -2, -2,  0, -1, -1,  0, -4},
{-3, -3, -4, -4, -2, -2, -3, -2, -2, -3, -2, -3, -1,  1, -4, -3, -2, 11,  2, -3, -4, -3, -2, -4},
{-2, -2, -2, -3, -2, -1, -2, -3,  2, -1, -1, -2, -1,  3, -3, -2, -2,  2,  7, -1, -3, -2, -1, -4},
{ 0, -3, -3, -3, -1, -2, -2, -3, -3,  3,  1, -2,  1, -1, -2, -2,  0, -3, -1,  4, -3, -2, -1, -4},
{-2, -1,  3,  4, -3,  0,  1, -1,  0, -3, -4,  0, -3, -3, -2,  0, -1, -4, -3, -3,  4,  1, -1, -4},
{-1,  0,  0,  1, -3,  3,  4, -2,  0, -3, -3,  1, -1, -3, -1,  0, -1, -3, -2, -2,  1,  4, -1, -4},
{ 0, -1, -1, -1, -2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2,  0,  0, -2, -1, -1, -1, -1, -1, -4},
{-4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,  1}
};

double gettime() {
  struct timeval t;
  gettimeofday(&t,NULL);
  return t.tv_sec+t.tv_usec*1e-6;
}

////////////////////////////////////////////////////////////////////////////////
// Program main
////////////////////////////////////////////////////////////////////////////////
int
main( int argc, char** argv)
{
    runTest( argc, argv);

    return EXIT_SUCCESS;
}

void usage(int argc, char **argv)
{
	fprintf(stderr, "Usage: %s <max_rows/max_cols> <penalty> <num_threads>\n", argv[0]);
	fprintf(stderr, "\t<dimension>      - x and y dimensions\n");
	fprintf(stderr, "\t<penalty>        - penalty(positive integer)\n");
	fprintf(stderr, "\t<num_threads>    - no. of threads\n");
	exit(1);
}

void nw_optimized(int *input_itemsets, int *output_itemsets, int *referrence,
        int max_rows, int max_cols, int penalty)
{

    int*input_itemsets_l = (int *)malloc( (BLOCK_SIZE + 1) *(BLOCK_SIZE+1) * sizeof(int) );
    int*reference_l      = (int *)malloc( BLOCK_SIZE * BLOCK_SIZE * sizeof(int) );

   /***************************************************************************/
   /***************************************************************************/
   /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop A
    int LOOP1_START=1, LOOP1_END = max_rows*max_cols , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loopA_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopA_LoopDesc.type   = LOOP    ;
    loopA_LoopDesc.valid  = VALID   ;
    loopA_LoopDesc.active =  ACTIVE ;
    loopA_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loopADesc = (unsigned long long * ) &loopA_LoopDesc;

    // 2- The loop B
    int LOOP2_START=1, LOOP2_END = BLOCK_SIZE*BLOCK_SIZE , LOOP2_INC = 1 ;
    MISA_LoopDesc_t   loop2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC_OFFSET };
    MISA_Desc_t       loopB_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopB_LoopDesc.type   = LOOP    ;
    loopB_LoopDesc.valid  = VALID   ;
    loopB_LoopDesc.active =  ACTIVE ;
    loopB_LoopDesc.descInfo.loopDesc=loop2_metaData;
    unsigned long long * loopBDesc = (unsigned long long * ) &loopB_LoopDesc;


 /***************************************************************************/
    /***************************************************************************/
    /***************************************************************************/


    // 3- input_itemsets[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrAStride;
    arrAStride = 4 ;//  int
    MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&input_itemsets[0],
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrA_DirStreamDesc.type   = DIR_STREAM;
    arrA_DirStreamDesc.valid  = VALID;
    arrA_DirStreamDesc.active =  ACTIVE ;
    arrA_DirStreamDesc.descInfo.streamDesc=arrA_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrADesc = (unsigned long long * ) &arrA_DirStreamDesc;


    // 4- output_itemsets[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrBStride = 4 ; // int


    MISA_DirStreamDesc_t  arrB_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&output_itemsets[0],
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB_DirStreamDesc.type   = DIR_STREAM;
    arrB_DirStreamDesc.valid  = VALID;
    arrB_DirStreamDesc.active =  ACTIVE ;

    arrB_DirStreamDesc.descInfo.streamDesc=arrB_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_DirStreamDesc;


    // 5- referrence[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrCStride = 4 ; // int


    MISA_DirStreamDesc_t  arrC_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&referrence[0],
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



    // 6- A[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrInLStride;
    arrInLStride = 4 ;//  float
    MISA_DirStreamDesc_t  arrInL_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&input_itemsets_l[0],
											     arrInLStride , 0 , RESERVED };
    MISA_Desc_t           arrInL_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrInL_metaData};
    arrInL_DirStreamDesc.type   = DIR_STREAM;
    arrInL_DirStreamDesc.valid  = VALID;
    arrInL_DirStreamDesc.active =  ACTIVE ;
    arrInL_DirStreamDesc.descInfo.streamDesc=arrInL_metaData;
    // Copy from arrInLy descriptor struct to CSRs
    unsigned long long * arrInLDesc = (unsigned long long * ) &arrInL_DirStreamDesc;


    // 7- B[i-1][j-1] -> Stride = 2*MaxSize+2
    int arrRefLStride = 4 ; // float


    MISA_DirStreamDesc_t  arrRefL_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&reference_l[0],
											     arrRefLStride , 0 , RESERVED };
    MISA_Desc_t           arrRefL_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrRefL_metaData};
    arrRefL_DirStreamDesc.type   = DIR_STREAM;
    arrRefL_DirStreamDesc.valid  = VALID;
    arrRefL_DirStreamDesc.active =  ACTIVE ;

    arrRefL_DirStreamDesc.descInfo.streamDesc=arrRefL_metaData;
    // Copy from arrRefLy descriptor struct to CSRs
    unsigned long long * arrRefLDesc = (unsigned long long * ) &arrRefL_DirStreamDesc;

    /***************************************************************************/
    /***************************************************************************/
    printf("max_rows = %d  max_cols =%d rows*cols = %d \n",max_rows ,max_cols ,max_rows*max_cols );
    printf("&input_itemsets[0][0]=%p  , input_itemsets Descriptor = %#llx %llx  \n"  ,&input_itemsets[0],*(arrADesc),*(arrADesc+1));
    printf("&output_itemsets[0][0]=%p , output_itemsets Descriptor = %#llx %llx  \n" ,&output_itemsets[0],*(arrBDesc),*(arrBDesc+1));
    printf("&referrence[0][0]=%p      , referrence Descriptor = %#llx %llx  \n"      ,&referrence[0],*(arrCDesc),*(arrCDesc+1));



    printf("&input_itemsets[rows-1][cols-1]=%p    \n",&input_itemsets [max_rows*max_cols-1] );
    printf("&output_itemsets[rows-1][cols-1]=%p    \n",&output_itemsets[max_rows*max_cols-1] );
    printf("&referrence[rows-1][cols-1]=%p    \n",&referrence     [max_rows*max_cols-1] );

    printf("&input_itemsets_l[0][0]=%p , input_itemsets_l Descriptor = %#llx %llx  \n",&input_itemsets_l[0],*(arrInLDesc),*(arrInLDesc+1));
    printf("&reference_l[0][0]=%p      , reference_l      Descriptor = %#llx %llx  \n",&reference_l[0],*(arrRefLDesc),*(arrRefLDesc+1));
    printf("&input_itemsets_l[rows-1][cols-1]=%p    \n",&input_itemsets_l[BLOCK_SIZE*BLOCK_SIZE-1]);
    printf("&reference_lrows-1][cols-1]=%p    \n",&reference_l[BLOCK_SIZE*BLOCK_SIZE-1]);


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


    write_csr(0x80A, *arrRefLDesc    ); // S
    write_csr(0x80B, *(arrRefLDesc+1)); //

    write_csr(0x80C, *arrInLDesc    ); // N
    write_csr(0x80D, *(arrInLDesc+1)); //
    m5_reset_stats(0,0);                    //Actual code starts here

    for( int blk = 1; blk <= (max_cols-1)/BLOCK_SIZE; blk++ )
    {

        for( int b_index_x = 0; b_index_x < blk; ++b_index_x)
        {
            int b_index_y = blk - 1 - b_index_x;
            // int input_itemsets_l[(BLOCK_SIZE + 1) *(BLOCK_SIZE+1)] __attribute__ ((aligned (64)));
            // int reference_l[BLOCK_SIZE * BLOCK_SIZE] __attribute__ ((aligned (64)));

            // Copy referrence to local memory
            for ( int i = 0; i < BLOCK_SIZE; ++i )
            {
                for ( int j = 0; j < BLOCK_SIZE; ++j)
                {
                    reference_l[i*BLOCK_SIZE + j] = referrence[max_cols*(b_index_y*BLOCK_SIZE + i + 1) + b_index_x*BLOCK_SIZE +  j + 1];
                }
            }

            // Copy input_itemsets to local memory
            for ( int i = 0; i < BLOCK_SIZE + 1; ++i )
            {
#pragma omp simd
                for ( int j = 0; j < BLOCK_SIZE + 1; ++j)
                {
                    input_itemsets_l[i*(BLOCK_SIZE + 1) + j] = input_itemsets[max_cols*(b_index_y*BLOCK_SIZE + i) + b_index_x*BLOCK_SIZE +  j];
                }
            }

            // Compute
            for ( int i = 1; i < BLOCK_SIZE + 1; ++i )
            {
                for ( int j = 1; j < BLOCK_SIZE + 1; ++j)
                {
                    input_itemsets_l[i*(BLOCK_SIZE + 1) + j] = maximum( input_itemsets_l[(i - 1)*(BLOCK_SIZE + 1) + j - 1] + reference_l[(i - 1)*BLOCK_SIZE + j - 1],
                            input_itemsets_l[i*(BLOCK_SIZE + 1) + j - 1] - penalty,
                            input_itemsets_l[(i - 1)*(BLOCK_SIZE + 1) + j] - penalty);
                }
            }

            // Copy results to global memory
            for ( int i = 0; i < BLOCK_SIZE; ++i )
            {
#pragma omp simd
                for ( int j = 0; j < BLOCK_SIZE; ++j)
                {
                    input_itemsets[max_cols*(b_index_y*BLOCK_SIZE + i + 1) + b_index_x*BLOCK_SIZE +  j + 1] = input_itemsets_l[(i + 1)*(BLOCK_SIZE+1) + j + 1];
                }
            }

        }
    }

    printf("Processing bottom-right matrix\n");



    for ( int blk = 2; blk <= (max_cols-1)/BLOCK_SIZE; blk++ )
    {

        for( int b_index_x = blk - 1; b_index_x < (max_cols-1)/BLOCK_SIZE; ++b_index_x)
        {
            int b_index_y = (max_cols-1)/BLOCK_SIZE + blk - 2 - b_index_x;


            // Copy referrence to local memory
            for ( int i = 0; i < BLOCK_SIZE; ++i )
            {
                for ( int j = 0; j < BLOCK_SIZE; ++j)
                {
                    reference_l[i*BLOCK_SIZE + j] = referrence[max_cols*(b_index_y*BLOCK_SIZE + i + 1) + b_index_x*BLOCK_SIZE +  j + 1];
                }
            }

            // Copy input_itemsets to local memory
            for ( int i = 0; i < BLOCK_SIZE + 1; ++i )
            {
#pragma omp simd
                for ( int j = 0; j < BLOCK_SIZE + 1; ++j)
                {
                    input_itemsets_l[i*(BLOCK_SIZE + 1) + j] = input_itemsets[max_cols*(b_index_y*BLOCK_SIZE + i) + b_index_x*BLOCK_SIZE +  j];
                }
            }

            // Compute
            for ( int i = 1; i < BLOCK_SIZE + 1; ++i )
            {
                for ( int j = 1; j < BLOCK_SIZE + 1; ++j)
                {
                    input_itemsets_l[i*(BLOCK_SIZE + 1) + j] = maximum( input_itemsets_l[(i - 1)*(BLOCK_SIZE + 1) + j - 1] + reference_l[(i - 1)*BLOCK_SIZE + j - 1],
                            input_itemsets_l[i*(BLOCK_SIZE + 1) + j - 1] - penalty,
                            input_itemsets_l[(i - 1)*(BLOCK_SIZE + 1) + j] - penalty);
                }
            }

            // Copy results to global memory
            for ( int i = 0; i < BLOCK_SIZE; ++i )
            {

                for ( int j = 0; j < BLOCK_SIZE; ++j)
                {
                    input_itemsets[max_cols*(b_index_y*BLOCK_SIZE + i + 1) + b_index_x*BLOCK_SIZE +  j + 1] = input_itemsets_l[(i + 1)*(BLOCK_SIZE+1) + j +1];
                }
            }
        }
    }



}

////////////////////////////////////////////////////////////////////////////////
//! Run a simple test for CUDA
////////////////////////////////////////////////////////////////////////////////
void
runTest( int argc, char** argv)
{
    int max_rows, max_cols, penalty;
    int *input_itemsets, *output_itemsets, *referrence;
    //int *matrix_cuda, *matrix_cuda_out, *referrence_cuda;
    //int size;
    int omp_num_threads;


    // the lengths of the two sequences should be able to divided by 16.
    // And at current stage  max_rows needs to equal max_cols
    max_rows =  N;
    max_cols =  N;
    penalty          =    10;
    omp_num_threads  =     1;


    // if (argc == 4)
    // {
    //     max_rows = atoi(argv[1]);
    //     max_cols = atoi(argv[1]);
    //     penalty = atoi(argv[2]);
    //     omp_num_threads = atoi(argv[3]);
    // }
    // else{
    //     usage(argc, argv);
    // }

    max_rows = max_rows + 1;
    max_cols = max_cols + 1;
    referrence      = (int *)malloc( max_rows * max_cols * sizeof(int) );
    input_itemsets  = (int *)malloc( max_rows * max_cols * sizeof(int) );
    output_itemsets = (int *)malloc( max_rows * max_cols * sizeof(int) );


    if (!input_itemsets)
        fprintf(stderr, "error: can not allocate memory");

    srand ( 7 );

    for (int i = 0 ; i < max_cols; i++){
        for (int j = 0 ; j < max_rows; j++){
            input_itemsets[i*max_cols+j] = 0;
        }
    }

    printf("Start Needleman-Wunsch\n");

    for( int i=1; i< max_rows ; i++){    //please define your own sequence.
        input_itemsets[i*max_cols] = rand() % 10 + 1;
    }
    for( int j=1; j< max_cols ; j++){    //please define your own sequence.
        input_itemsets[j] = rand() % 10 + 1;
    }


    for (int i = 1 ; i < max_cols; i++){
        for (int j = 1 ; j < max_rows; j++){
            referrence[i*max_cols+j] = blosum62[input_itemsets[i*max_cols]][input_itemsets[j]];
        }
    }

    for( int i = 1; i< max_rows ; i++)
        input_itemsets[i*max_cols] = -i * penalty;
    for( int j = 1; j< max_cols ; j++)
        input_itemsets[j] = -j * penalty;



    //Compute top-left matrix
    printf("Num of threads: %d\n", omp_num_threads);
    printf("Processing top-left matrix\n");

    long long start_time = get_time();

    nw_optimized( input_itemsets, output_itemsets, referrence,
        max_rows, max_cols, penalty );

    long long end_time = get_time();

    printf("Total time: %.3f seconds\n", ((float) (end_time - start_time)) / (1000*1000));

//#define TRACEBACK
#ifdef TRACEBACK

    //FILE *fpo = fopen("result.txt","w");
    //fprintf(fpo, "print traceback value GPU:\n");

    for (int i = max_rows - 2,  j = max_rows - 2; i>=0, j>=0;){
        int nw, n, w, traceback;

        // if ( i == max_rows - 2 && j == max_rows - 2 )
        //     fprintf(fpo, "%d ", input_itemsets[ i * max_cols + j]); //print the first element

        // if ( i == 0 && j == 0 )
            break;
        if ( i > 0 && j > 0 ){
            nw = input_itemsets[(i - 1) * max_cols + j - 1];
            w  = input_itemsets[ i * max_cols + j - 1 ];
            n  = input_itemsets[(i - 1) * max_cols + j];
        }
        else if ( i == 0 ){
            nw = n = LIMIT;
            w  = input_itemsets[ i * max_cols + j - 1 ];
        }
        else if ( j == 0 ){
            nw = w = LIMIT;
            n  = input_itemsets[(i - 1) * max_cols + j];
        }
        else{
        }

        //traceback = maximum(nw, w, n);
        int new_nw, new_w, new_n;
        new_nw = nw + referrence[i * max_cols + j];
        new_w = w - penalty;
        new_n = n - penalty;

        traceback = maximum(new_nw, new_w, new_n);
        if(traceback == new_nw)
            traceback = nw;
        if(traceback == new_w)
            traceback = w;
        if(traceback == new_n)
            traceback = n;

        //fprintf(fpo, "%d ", traceback);

        if(traceback == nw )
        {i--; j--; continue;}

        else if(traceback == w )
        {j--; continue;}

        else if(traceback == n )
        {i--; continue;}

        else
            ;
    }

    //fclose(fpo);

#endif

    // free(referrence);
    // free(input_itemsets);
    // free(output_itemsets);

}
