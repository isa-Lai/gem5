
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>


/*
** Scales a vector by a constant.
** Jack Dongarra, linpack, 3/11/78.
** ROLLED version
*/
static void dscal_r(int n,double da,double *dx,int incx)
{
    int i,nincx;

    if (n <= 0)
        return;
    if (incx != 1)
        {

        /* code for increment not equal to 1 */

        nincx = n*incx;
        for (i = 0; i < nincx; i = i + incx)
            dx[i] = da*dx[i];
        return;
        }

    /* code for increment equal to 1 */
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop 1
    int LOOP1_START=1, LOOP1_END = n , LOOP1_INC = 1 , PC_OFFSET=01000;
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
    MISA_DirStreamDesc_t  X_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&dx[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           X_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    X_DirStreamDesc.type   = DIR_STREAM;
    X_DirStreamDesc.valid  = VALID;
    X_DirStreamDesc.active =  ACTIVE ;
    X_DirStreamDesc.descInfo.streamDesc=X_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * XDesc = (unsigned long long * ) &X_DirStreamDesc;
    printf("x and y of size %d  \n",n);
    printf("&X[0]=%p , X Descriptor = %#llx %llx  \n",&dx[0],*(XDesc),*(XDesc+1));
    printf("&X[N-1]=%p    \n",&dx[n-1] );

    write_csr(0x800, *loop1Desc    ); // LOOP 1
    write_csr(0x801, *(loop1Desc+1)); //

    write_csr(0x802, *XDesc    ); // X
    write_csr(0x803, *(XDesc+1)); //


    for (i = 0; i < n; i++)
        dx[i] = da*dx[i];

}

#define cacheSize 65536*4

int main(void)
{
   char cacher[cacheSize], cacher2[cacheSize];

   double  *x=(double*)malloc(N*sizeof(double));
   //const double XVAL = rand() % 1000000;
   const double AVAL = rand() % 1000000;

   //for (size_t i = 0; i < N; i++) {
   //   x[i] = XVAL;
   //}

    //CLear the cache
    //printf("Clearing cache (%d)...\n", cacheSize);
    //Clear Cache
    //for(int j=0; j<cacheSize; j++){
    //    cacher[j] = 4;
    //    cacher2[j] = cacher[j];
    //}

    dscal_r( N,  AVAL, x, 1 );
   /*double elapsed = t.elapsed_msec();
   std::cout << "Elapsed: " << elapsed << " ms" << std::endl;
   saxpy_verify(y);
   */

   return 0;
}
