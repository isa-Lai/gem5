/***************************************************************************
 *
 *            (C) Copyright 2010 The Board of Trustees of the
 *                        University of Illinois
 *                         All Rights Reserved
 *
 ***************************************************************************/
#include <parboil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
// InterStellar v1
#include "metaisa.hpp"
#include <encoding.h>
//This include to do annotations
#include "gem5/m5ops.h"

#if N == 10000
#include "img_large.h"
#endif

#define UINT8_MAX 255

/******************************************************************************
* Implementation: Reference
* Details:
* This implementations is a scalar, minimally optimized version. The only
* optimization, which reduces the number of pointer chasing operations is the
* use of a temporary pointer for each row.
******************************************************************************/

int main(int argc, char* argv[]) {
  struct pb_TimerSet timers;
  struct pb_Parameters *parameters;

  printf("Base implementation of histogramming.\n");
  printf("Maintained by Nady Obeid <obeid1@ece.uiuc.edu>\n");

  /*
  parameters = pb_ReadParameters(&argc, argv);
  if (!parameters)
    return -1;
  */


  // parameters->inpFiles = (char**)malloc((2) * sizeof(char *)); // Allocatre list of files [One Input FIle]
  // if(N==10000)
  // {
  //     //1MB Image case
  //     strcpy(parameters->inpFiles[0],"img_large.bin");
  // }


  // if(!parameters->inpFiles[0]){
  //   fputs("Input file expected\n", stderr);
  //   return -1;
  // }

  int numIterations = 2;

  /*
  if (argc >= 2){
    numIterations = atoi(argv[1]);
  } else {
    fputs("Expected at least one command line argument\n", stderr);
    return -1;
  }
  */



  printf("init timers ");
  pb_InitializeTimerSet(&timers);

  char *inputStr = "Input";
  char *outputStr = "Output";
  printf("Before  pb_AddSubTimer 1 ");

  pb_AddSubTimer(&timers, inputStr, pb_TimerID_IO);
  printf("Input File = %s\n",parameters->inpFiles[0]);

  printf("Before  pb_AddSubTimer 2");

  pb_AddSubTimer(&timers, outputStr, pb_TimerID_IO);
  printf("Input File = %s\n",parameters->inpFiles[0]);

  printf("Before  pb_AddSubTimer 3 ");

  pb_SwitchToSubTimer(&timers, inputStr, pb_TimerID_IO);
  printf("Input File = %s\n",parameters->inpFiles[0]);

  unsigned int img_width, img_height;
  unsigned int histo_width, histo_height;
  if(N==10000)
  {
      //Put the dataset explicitly inside the file - remove need for file read
      img_width     = 996  ;
      img_height    = 1040 ;
      histo_width   = 256  ;
      histo_height  = 4096 ;
  }
//  FILE* f = fopen(parameters->inpFiles[0],"rb");
  int result = 0;

  /*printf("Input File = %s\n",parameters->inpFiles[0]);
  if(f==NULL)
    printf("Fail to open the file!\n");*/

//  result += fread(&img_width,    sizeof(unsigned int), 1, f);
//  result += fread(&img_height,   sizeof(unsigned int), 1, f);
//  result += fread(&histo_width,  sizeof(unsigned int), 1, f);
//  result += fread(&histo_height, sizeof(unsigned int), 1, f);


  printf("img_width     = %d , img_height   = %d \n"  , img_width   , img_height   );
  printf("histo_width   = %d , histo_height = %d \n"  , histo_width , histo_height );

  // if (result != 4){
  //   fputs("Error reading input and output dimensions from file\n", stderr);
  //   return -1;
  // }


  unsigned int* img = (unsigned int*) malloc (img_width*img_height*sizeof(unsigned int));
  unsigned char* histo = (unsigned char*) calloc (histo_width*histo_height, sizeof(unsigned char));




   /*****************************************************************************************/
   /******* Setup the MetaISA Descriptors *******/
   /********************** Fill descriptor data structure for loop 1 ***********************/
    // 1- The loop A
    int LOOP1_START=1, LOOP1_END = img_width*img_height , LOOP1_INC = 1 , PC_OFFSET=01000;
    MISA_LoopDesc_t   loop1_metaData       = { LOOP1_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP1_START, LOOP1_END , LOOP1_INC  , PC_OFFSET };
    MISA_Desc_t       loopA_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopA_LoopDesc.type   = LOOP    ;
    loopA_LoopDesc.valid  = VALID   ;
    loopA_LoopDesc.active =  ACTIVE ;
    loopA_LoopDesc.descInfo.loopDesc=loop1_metaData;
    unsigned long long * loopADesc = (unsigned long long * ) &loopA_LoopDesc;
    // 2- The loop B
    int LOOP2_START=1, LOOP2_END = histo_width*histo_height , LOOP2_INC = 1 ;
    MISA_LoopDesc_t   loop2_metaData       = { LOOP2_ID , INIT_NOT_LINKED ,END_NOT_LINKED ,
                                                LOOP2_START, LOOP2_END , LOOP2_INC  , PC_OFFSET };
    MISA_Desc_t       loopB_LoopDesc; //      ={LOOP,VALID,ACTIVE ,loop1_metaData};
    loopB_LoopDesc.type   = LOOP    ;
    loopB_LoopDesc.valid  = VALID   ;
    loopB_LoopDesc.active =  ACTIVE ;
    loopB_LoopDesc.descInfo.loopDesc=loop2_metaData;
    unsigned long long * loopBDesc = (unsigned long long * ) &loopB_LoopDesc;


   // 3- A[i-1][j-1] -> img
    int arrAStride;
    arrAStride = 4 ;//  int
    MISA_DirStreamDesc_t  arrA_metaData      = { LOOP1_ID , 0 , RESERVED , (uint64_t)&img[0],
											     arrAStride , 0 , RESERVED };
    MISA_Desc_t           arrA_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrA_DirStreamDesc.type   = DIR_STREAM;
    arrA_DirStreamDesc.valid  = VALID;
    arrA_DirStreamDesc.active =  ACTIVE ;
    arrA_DirStreamDesc.descInfo.streamDesc=arrA_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrADesc = (unsigned long long * ) &arrA_DirStreamDesc;


    // 4- B[i-1][j-1] -> histo
    int arrBStride = 1 ; // char
    MISA_DirStreamDesc_t  arrB_metaData      = { LOOP2_ID , 0 , RESERVED , (uint64_t)&histo[0],
											     arrBStride , 0 , RESERVED };
    MISA_Desc_t           arrB_DirStreamDesc; // = { DIR_STREAM , VALID , ACTIVE  ,arrA_metaData};
    arrB_DirStreamDesc.type   = INDIR_DTREAM;
    arrB_DirStreamDesc.valid  = VALID;
    arrB_DirStreamDesc.active =  ACTIVE ;

    arrB_DirStreamDesc.descInfo.streamDesc=arrB_metaData;
    // Copy from array descriptor struct to CSRs
    unsigned long long * arrBDesc = (unsigned long long * ) &arrB_DirStreamDesc;


    printf("&Img[0]=%p   , Img    Descriptor = %#llx %llx  \n"   , &img[0],*(arrADesc),*(arrADesc+1));
    printf("&Histo[0]=%p , Histo  Descriptor = %#llx %llx  \n" , &histo[0],*(arrBDesc),*(arrBDesc+1));

    printf("&Img[img_width*img_height-1]  =%p    \n",&img[img_width*img_height-1] );
    printf("&Histo[histo_width*histo_height-1]=%p    \n",&histo[histo_width*histo_height-1] );

    write_csr(0x800, *loopADesc    ); // LOOP A
    write_csr(0x801, *(loopADesc+1)); //

    write_csr(0x802, *loopBDesc    ); // LOOP B
    write_csr(0x803, *(loopBDesc+1)); //

    write_csr(0x804, *arrADesc    ); // img
    write_csr(0x805, *(arrADesc+1)); //

    write_csr(0x806, *arrBDesc    ); // histo
    write_csr(0x807, *(arrBDesc+1)); //

  pb_SwitchToSubTimer(&timers, "Input", pb_TimerID_IO);

  //result = fread(img, sizeof(unsigned int), img_width*img_height, f);

  //fclose(f);

  // if (result != img_width*img_height){
  //   fputs("Error reading input array from file\n", stderr);
  //   return -1;
  // }
  printf("FImg is read from the file!\n");

  pb_SwitchToTimer(&timers, pb_TimerID_COMPUTE);

	printf("Start the HISTO main loop\n");
    m5_reset_stats(0,0);                    //Actual code starts here

  int iter;
  for (iter = 0; iter < numIterations; iter++){
    printf("iter=%d\n",iter);
    memset(histo,0,histo_height*histo_width*sizeof(unsigned char));
    unsigned int i;
    for (i = 0; i < img_width*img_height; ++i) {
      const unsigned int value = img[i];
      if (histo[value] < UINT8_MAX) {
        ++histo[value];
      }
    }
  }

//  pb_SwitchToTimer(&timers, pb_TimerID_IO);
  pb_SwitchToSubTimer(&timers, outputStr, pb_TimerID_IO);

  if (parameters->outFile) {
    dump_histo_img(histo, histo_height, histo_width, parameters->outFile);
  }

  pb_SwitchToTimer(&timers, pb_TimerID_COMPUTE);

  free(img);
  free(histo);

  pb_SwitchToTimer(&timers, pb_TimerID_NONE);

  printf("\n");
  pb_PrintTimerSet(&timers);
  pb_FreeParameters(parameters);

  return 0;
}
