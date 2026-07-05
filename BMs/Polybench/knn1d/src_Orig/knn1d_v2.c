#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "dataset.h"
//#include "m5ops.h"



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

loop_i : for (i = 0; i < nfAtoms; i++){
             i_x = position_x[i];
            //  i_y = position_y[i];
            //  i_z = position_z[i];
             fx = 0;
            //  fy = 0;
            //  fz = 0;
loop_j : for( j = 0; j < maxfNeighbors; j++){
             // Get neighbor
             jidx = NL[i*maxfNeighbors + j];
             // Look up x,y,z positions
             j_x = position_x[jidx];
            //  j_y = position_y[jidx];
            //  j_z = position_z[jidx];
             // Calc distance
             delx = i_x - j_x;
            //  dely = i_y - j_y;
            //  delz = i_z - j_z;
             r2inv = 1.0/( delx*delx);// + dely*dely + delz*delz );
             // Assume no cutoff and aways account for all nodes in area
             r6inv = r2inv * r2inv * r2inv;
             potential = r6inv*(lj1*r6inv - lj2);
             // Sum changes in force
             force = r2inv*potential;
             fx += delx * force;
            //  fy += dely * force;
            //  fz += delz * force;
         }
         //Update forces after all neighbors accounted for.
         force_x[i] = fx;
        //  force_y[i] = fy;
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
