#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "dataset.h"
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"


//#define MAX_ITERATION {{ MAX_ITERATION }}


// Problem Constants
#define nAtoms        N
#define maxNeighbors  16
// LJ coefficients


#define lj1           1.5
#define lj2           2.0

void core_kernel(double* force_x,
               double* force_y,
               double* force_z,
               double* position_x,
               double* position_y,
               double* position_z,
               int32_t* NL       ,
               uint64_t nfAtoms   ,
               uint64_t maxfNeighbors)
{
    double delx, dely, delz, r2inv;
    double r6inv, potential, force, j_x, j_y, j_z;
    double i_x, i_y, i_z, fx, fy, fz;

    int32_t i, j, jidx;

/************************************************************************************/
    /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop 1
    int LOOP1_START=1, LOOP1_END = nfAtoms , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loop1_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop1_LoopDesc.type   = LOOP    ;
    loop1_LoopDesc.valid  = VALID   ;
    loop1_LoopDesc.active =  ACTIVE ;
    loop1_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loop1Desc = (unsigned long long * ) &loop1_LoopDesc;

    // 2- The loop 2
    int LOOP2_START=1, LOOP2_END = nfAtoms*maxfNeighbors , LOOP2_INC = 1 , PC2_OFFSET=01000;
    MISA_LoopDesc_t   loop2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC2_OFFSET };
    MISA_Desc_t       loop2_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loop2_LoopDesc.type   = LOOP    ;
    loop2_LoopDesc.valid  = VALID   ;
    loop2_LoopDesc.active =  ACTIVE ;
    loop2_LoopDesc.descInfo.loopDesc=loop2_metaData;
    unsigned long long * loop2Desc = (unsigned long long * ) &loop2_LoopDesc;


   int  unitSize = 8 ; /* Double */
     // NL
    MISA_DirStreamDesc_t  NL_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&NL[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           NL_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    NL_DirStreamDesc.type   = DIR_STREAM;
    NL_DirStreamDesc.valid  = VALID;
    NL_DirStreamDesc.active =  ACTIVE ;
    NL_DirStreamDesc.descInfo.streamDesc=NL_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * NLDesc = (unsigned long long * ) &NL_DirStreamDesc;

    // position_x
     /*****************************************************************************************/
   /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
    int position_xStride = unitSize ;
    MISA_IndirStreamDesc_t   position_x_metaData   = {LOOP1_ID,0 , RESERVED,  (uint64_t)&position_x[0],
                                                position_xStride , 0 , RESERVED   };
    MISA_Desc_t           position_x_InDirStreamDesc ;
    position_x_InDirStreamDesc.type   = INDIR_DTREAM;
    position_x_InDirStreamDesc.valid  = VALID;
    position_x_InDirStreamDesc.active =  ACTIVE ;
    position_x_InDirStreamDesc.descInfo.stream=position_x_metaData;
    unsigned long long * position_xDesc = (unsigned long long * ) &position_x_InDirStreamDesc;



    // force_x
    MISA_DirStreamDesc_t  force_x_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&force_x[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           force_x_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    force_x_DirStreamDesc.type   = DIR_STREAM;
    force_x_DirStreamDesc.valid  = VALID;
    force_x_DirStreamDesc.active =  ACTIVE ;
    force_x_DirStreamDesc.descInfo.streamDesc=force_x_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * force_xDesc = (unsigned long long * ) &force_x_DirStreamDesc;


    // position_y
     /*****************************************************************************************/
   /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
    int position_yStride = unitSize ;
    MISA_IndirStreamDesc_t   position_y_metaData   = {LOOP1_ID,0 , RESERVED,  (uint64_t)&position_y[0],
                                                position_yStride , 0 , RESERVED   };
    MISA_Desc_t           position_y_InDirStreamDesc ;
    position_y_InDirStreamDesc.type   = INDIR_DTREAM;
    position_y_InDirStreamDesc.valid  = VALID;
    position_y_InDirStreamDesc.active =  ACTIVE ;
    position_y_InDirStreamDesc.descInfo.stream=position_y_metaData;
    unsigned long long * position_yDesc = (unsigned long long * ) &position_y_InDirStreamDesc;



    // force_y
    MISA_DirStreamDesc_t  force_y_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&force_y[0],
											     unitSize , 0 , RESERVED };
    MISA_Desc_t           force_y_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    force_y_DirStreamDesc.type   = DIR_STREAM;
    force_y_DirStreamDesc.valid  = VALID;
    force_y_DirStreamDesc.active =  ACTIVE ;
    force_y_DirStreamDesc.descInfo.streamDesc=force_y_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * force_yDesc = (unsigned long long * ) &force_y_DirStreamDesc;















/************************************************************************************/
printf("&position_x[0] = %p , &position_X[atmos-1] = %p \n",&position_x[0],&position_x[nfAtoms-1]);
printf("&force_x[0]    = %p , &force_X   [atmos-1] = %p \n",&force_x[0],&force_x[nfAtoms-1]);

printf("&position_y[0] = %p , &position_y[atmos-1] = %p \n",&position_y[0],&position_y[nfAtoms-1]);
printf("&force_y[0]    = %p , &force_y   [atmos-1] = %p \n",&force_y[0],&force_y[nfAtoms-1]);


printf("&NL[0]    = %p , &NL[ngeihbours*atmos-1] = %p \n",&NL[0],&NL[maxfNeighbors*nfAtoms-1]);

    write_csr(0x800, *loop1Desc    ); // LOOP 1
    write_csr(0x801, *(loop1Desc+1)); //

    write_csr(0x802, *loop2Desc    ); // X
    write_csr(0x803, *(loop2Desc+1)); //

    write_csr(0x804, *(NLDesc)  ); // Y
    write_csr(0x805, *(NLDesc+1)); //

    write_csr(0x806, *(position_xDesc)  ); // Y
    write_csr(0x807, *(position_xDesc+1)); //

    write_csr(0x808, *(force_xDesc)  ); // Y
    write_csr(0x809, *(force_xDesc+1)); //

    write_csr(0x80a, *(position_yDesc)  ); // Y
    write_csr(0x80b, *(position_yDesc+1)); //

    write_csr(0x80c, *(force_yDesc)  ); // Y
    write_csr(0x80d, *(force_yDesc+1)); //

    //m5_reset_stats(0,0);


loop_i : for (i = 0; i < nfAtoms; i++){
             i_x = position_x[i];
             i_y = position_y[i];
            //  i_z = position_z[i];
             fx = 0;
             fy = 0;
            //  fz = 0;
loop_j : for( j = 0; j < maxfNeighbors; j++){
             // Get neighbor
             jidx = NL[i*maxfNeighbors + j];
             // Look up x,y,z positions
             j_x = position_x[jidx];
             j_y = position_y[jidx];
            //  j_z = position_z[jidx];
             // Calc distance
             delx = i_x - j_x;
             dely = i_y - j_y;
            //  delz = i_z - j_z;
             r2inv = 1.0/( delx*delx + dely*dely );//+ delz*delz );
             // Assume no cutoff and aways account for all nodes in area
             r6inv = r2inv * r2inv * r2inv;
             potential = r6inv*(lj1*r6inv - lj2);
             // Sum changes in force
             force = r2inv*potential;
             fx += delx * force;
             fy += dely * force;
            //  fz += delz * force;
         }
         //Update forces after all neighbors accounted for.
         force_x[i] = fx;
         force_y[i] = fy;
        //  force_z[i] = fz;clear
         //printf("dF=%lf,%lf,%lf\n", fx, fy, fz);
         }
}



extern double mysecond();

double distance(
        double position_x[nAtoms],
        double position_y[nAtoms],
        double position_z[nAtoms],
        int i,
        int j)
{
    double delx, dely, delz, r2inv;
    delx = position_x[i] - position_x[j];
    dely = position_y[i] - position_y[j];
    delz = position_z[i] - position_z[j];
    r2inv = delx * delx + dely * dely + delz * delz;
    return r2inv;
}

inline void insertInOrder(double currDist[maxNeighbors],
        int currList[maxNeighbors],
        int j,
        double distIJ)
{
    int dist, pos, currList_t;
    double currMax, currDist_t;
    pos = maxNeighbors - 1;
    currMax = currDist[pos];
    if (distIJ > currMax){
        return;
    }
    for (dist = pos; dist > 0; dist--){
        if (distIJ < currDist[dist]){
            currDist[dist] = currDist[dist - 1];
            currList[dist]  = currList[pos  - 1];
        }
        else{
            break;
        }
        pos--;
    }
    currDist[dist] = distIJ;
    currList[dist]  = j;
}


int populateNeighborList(double currDist[maxNeighbors],
        int currList[maxNeighbors],
        const int i,
        int NL[nAtoms][maxNeighbors])
{
    int idx, validPairs, distanceIter, neighborIter;
    idx = 0; validPairs = 0;
    for (neighborIter = 0; neighborIter < maxNeighbors; neighborIter++){
        NL[i][neighborIter] = currList[neighborIter];
        validPairs++;
    }
    return validPairs;
}

int buildNeighborList(double position_x[nAtoms],
        double position_y[nAtoms],
        double position_z[nAtoms],
        int NL[nAtoms][maxNeighbors]
        )
{
    int totalPairs, i, j, k;
    totalPairs = 0;
    double distIJ;
    for (i = 0; i < nAtoms; i++){
        int currList[maxNeighbors];
        double currDist[maxNeighbors];
        for(k=0; k<maxNeighbors; k++){
            currList[k] = 0;
            currDist[k] = 999999999;
        }
        for (j = 0; j < maxNeighbors; j++){
            if (i == j){
                continue;
            }
            distIJ = distance(position_x, position_y, position_z, i, j);
            currList[j] = j;
            currDist[j] = distIJ;
        }
        totalPairs += populateNeighborList(currDist, currList, i, NL);
    }
    return totalPairs;
}


int main(){


    static double static_src[]= FpDataset;
    // static double static_src[]= IntDataset;

    int i, iter, j, totalPairs;
    iter = 0;

    srand(8650341L);

    printf("here");

    //Allocate arrays
    double * position_x = (double *) malloc( nAtoms *sizeof(double));
    double * position_y = (double *) malloc( nAtoms *sizeof(double));
    double * position_z = (double *) malloc( nAtoms *sizeof(double));
    double * force_x = (double *) malloc( nAtoms *sizeof(double));
    double * force_y = (double *) malloc( nAtoms *sizeof(double));
    double * force_z = (double *) malloc( nAtoms *sizeof(double));
    int NL[nAtoms][maxNeighbors];
    int * neighborList = (int *) malloc(nAtoms*maxNeighbors*sizeof(int));

    for  (i = 0; i < nAtoms; i++)
    {
        position_x[i] = static_src[i%1000];
        position_y[i] = static_src[i%1000];
        position_z[i] = static_src[i%1000];
        force_x[i] = static_src[i%1000];
        force_y[i] = static_src[i%1000];
        force_z[i] = static_src[i%1000];
    }

    for(i=0; i<nAtoms; i++){
        for(j = 0; j < maxNeighbors; ++j){
            NL[i][j] = 0;
        }
    }

    totalPairs = buildNeighborList(position_x, position_y, position_z, NL);

    for(i=0; i<nAtoms; i++){
        for(j = 0; j < maxNeighbors; ++j)
            neighborList[i*maxNeighbors + j] = NL[i][j];
    }




    //Function Call

    // Application execution:
    printf("Started...\n");

	//M5resetstats();

    // for(iter = 0; iter < MAX_ITERATION; iter++) {
       core_kernel(force_x, force_y, force_z, position_x, position_y, position_z, neighborList, nAtoms, maxNeighbors);
    // }

    // M5resetdumpstats();

       return 0;

  //Results printing
  fprintf(stdout, "Results:");
  if(nAtoms < 20){
    for (int i = 0; i < nAtoms; i++){
      fprintf(stdout, "\n\t%d: %lf",i, force_x[i]);
    }
  }
  else
  for(int i = 0; i < nAtoms; i+=nAtoms/20){
    fprintf(stdout, "\n\t%d: %lf",i, force_x[i]);
  }
  fprintf(stdout, "\n");


    return 0;
}
