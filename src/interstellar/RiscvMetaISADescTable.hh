#ifndef RISCV_METAISA_BUFFERS_HH
#define RISCV_METAISA_BUFFERS_HH

#include "interstellar/base_metaisa.hpp"
#include "base/trace.hh"
#include <string>
using namespace std;


#define PROCESSORS_BITS 4
#define TABLE2_ADD_BITS 12

/* The following are offsets to walk over elements of table 2 */
typedef uint64_t Addr;
const   uint64_t           PageShift = 26   ;//RiscvISA::PageShift
#define PAGE_OFFSET_MASK    (( 1uL << PageShift)-1 )

/***** Linked List related defines **********/
#define LINKED_LIST_NODE_MASK  0x1F // <- If mask solution is used with linked list detection
#define VAs_PER_PTR            4
#define NODE_SIZE  16
#define CACHE_GRANULVL_MASK 0x3F // Cache Granularity Level Mask

const int MAX_PROCESSORS  = 64;

#define PAGE_MASK         ~(PAGE_OFFSET_MASK)
enum loopDescOffsets  {iterationNumber  };
//enum dirStreamOffsets {crntPfnOffset,crntVAOffset};
//In case of fast translations ["4" PFNs associated to the same direct stream will be stored]
#define PFNs_Per_STREAM 64  //#define VFNs_Per_DIR_STREAM 4
enum StreamOffsets //APplies to direct and Indirect Streams
{
    crntPfn1Offset = 0 /* PFNs_Per_DIR_STREAM location for offsets  */,
    crntVAOffset     = PFNs_Per_STREAM /* VA Offset of current Translation */ ,
    FIRST_PFN        = PFNs_Per_STREAM + 1 ,
    FIRST_PFN_OFFSET = PFNs_Per_STREAM + 2 ,
    LAST_PFN         = PFNs_Per_STREAM + 3 ,
    LAST_PFN_OFFSET  = PFNs_Per_STREAM + 4 ,
    StreamRunTimeInfoSize1
};

enum dirIndirStrRunTime
{
    BASEVA_LOC,
    ENDVA_LOC ,
    StreamRunTimeInfoSize
};

enum PtrDescOffsets //Applies to Pointer Stream
{

    PtrVAValue  = 0,//In place of PFN[0] for direct stream;
    /** THere are two approaches here to resolve the
     * linked list problem #3 (caching some nodes in between):
     * 1- store some of recent VAs
     * 2- store upper and lower bounds of VAs -> This is implemented here !
     * ******/
    PtrPAValue  = VAs_PER_PTR   ,
    PtrVaUBound = VAs_PER_PTR+1 , //Upper bound
    PtrVaLBound = PtrVaUBound+1 , //Lower bound ,
    PtrRuntTimeInfoSize
};


/* The following array will be indexed by the descType
to get table 2 'number of entires' ; The elegent way to map
 sequential numbers to numbers  */
//Long table doesn't use TLB-1
const int entriesNumTbl2Long[]= { /*LOOP*/         1 ,
                              /*DIR_STREAM   */ StreamRunTimeInfoSize ,
                              /*INDIR_STREAM */ StreamRunTimeInfoSize ,
                              /*PTR_Chase    */ PtrRuntTimeInfoSize ,
                              };

const int entriesNumTbl2[]= { /*LOOP*/         1 ,
                              /*DIR_STREAM   */ StreamRunTimeInfoSize ,
                              /*INDIR_STREAM */ StreamRunTimeInfoSize ,
                              /*PTR_Chase    */ PtrRuntTimeInfoSize ,
                              };
const  static   string descNames[6]   = {"Loop","Direct Stream", "Indirect Stream","Pointer Chasing" , " Branch","Code Slcie"};

struct Table1_Entry
{
    MISA_Desc_t   entryCSRData                     ;  /* 128 bit */
    uint8_t       metaISAStreamID                  ;  /* 8  bits */
    uint32_t      metaISARequestorID               ;  /* 32 bits */
   // unsigned int  cpu_id:PROCESSORS_BITS           ;  /* P-bits  */
    unsigned int  extraFieldsLoc :TABLE2_ADD_BITS  ;  /* T2-bits */
};

class DescTable
{
    /*  For fast random access , make all entries of same size
        the other hand is to make every entry of variable size
        This will be compact in size and more efficient but will be slow
        in access and search   */

private:
    /***        Can't be dynamically allocated -> Hardware    ***/
    const static int  MAX_ENTRIES          = 100     ;
    const static int  MAX_EXTRA_FIELDS     = 7000    ; //70*100
    Table1_Entry      descripTable1[MAX_ENTRIES]     ;
    uint64_t          descripTable2[MAX_EXTRA_FIELDS];

    //map< uint64_t , Table1_Entry > descripTable1;
    //map< uint64_t , uint64_t     > descripTable2;
    Table1_Entry      descTable1[MAX_PROCESSORS][MAX_ENTRIES]     ;
    uint64_t          descTable2[MAX_PROCESSORS][MAX_EXTRA_FIELDS];


    int               tbl1EntriesNum               ;
    int               tbl1EntNum[MAX_PROCESSORS]   ;
    int               tbl2EntriesNum               ;
    int               tbl2EntNum[MAX_PROCESSORS]   ;
    int               lastIDPerProcesser[MAX_PROCESSORS]      ;
    uint32_t getTbl1Len()               { return tbl1EntriesNum; }
    uint32_t getTbl1Len(uint32_t p)          { return tbl1EntNum[p]; }
    uint32_t getTbl2Len()               { return tbl2EntriesNum; }
    uint32_t getTbl2Len(uint32_t p)          { return tbl2EntNum[p]; }

    void     setTbl1Len(uint32_t len)   {tbl1EntriesNum = len ;  }
    void     setTbl2Len(uint32_t len)   {tbl2EntriesNum = len ;  }
    void     setTbl1Len(uint32_t p , uint32_t len)   {tbl1EntNum[p] = len ;  }
    void     setTbl2Len(uint32_t p , uint32_t len)   {tbl2EntNum[p] = len ;  }



    Table1_Entry* getTbl1Entry(unsigned int idx)
    {
        if(idx < 0 || idx > this->getTbl1Len())
            return nullptr;
        return &(this->descTable1[0][idx]);
    }
public:
    DescTable();

    /******************************************************************/
    /************** Common Functions to all descriptors ***************/
    bool     insertDesc       (MISA_Desc_t entryVal, int streamID , uint32_t      metaISARequestorID);
    bool     removeDesc       (MISA_Desc_t entryVal, uint32_t      metaISARequestorID);
    void     removeAllDesc    (uint32_t      metaISARequestorID);
    void     removeDesc       (uint32_t streamID , uint32_t      metaISARequestorID);
    bool     insertPtrDesc    (MISA_Desc_t entryVal,Addr VAddr, Addr PAddr);
    /*initExtraFields:: To be opted out after TLB-1 Optimization */
    void     initExtraFields  (descType entryType , int loc1 , int loc2) ;
    void     initRunTimeFields(descType entryType , int loc1 , int loc2 , int metaISARequestorID) ;
    void     initRunTimeFieldsPerCore(descType entryType , int loc1 , int loc2 , int p) ;

    void     removeDesc     (MISA_Desc_t entryVal)                     ;
    descType getDescType    (int idx)                                  ;
    descType getDescType    (int p /* requestor id */, int idx)        ;
    /*\**************************************************************\*/


    /******************************************************************/
    /***************** Direct Stream Functions   **********************/
    uint64_t       getDirStreamBaseAdress(int idx)                                                         ;
    bool           getDirStreamExpectedVA(int idx, uint64_t& expectedVA)                                   ;
    uint64_t       getDirStreamExpectedPA(int idx)                                                         ;
    bool           getDirStreamExpectedPA(int idx ,int PFN_Index,uint64_t &expectedPA)                     ;
    bool           getStrExpectedPaAtLLC (int idx ,int PFN_Index,uint64_t &expectedPA, descType&_descType) ;
    bool           getStrExpectedPfnAtLLC(int loc2,int PFN_Index,uint64_t &expectedPFN, descType&_descType);

    /* getDirStreamEntryByVA & getDirStreamEntryByPA Can be combined into singl function:
     Table1_Entry*  getDirStreamEntry (uint64_t addr, bool vaORpa)                      */
    Table1_Entry*  getDirStreamEntryByVA      (uint64_t crntTLBVA)                                                                        ;
    Table1_Entry*  getStrEntByVARange   (uint64_t crntTLBVA,uint64_t &baseVA,uint64_t&endVA)    ;
    Table1_Entry*  getStrEntByVARangeOpt(uint64_t crntTLBVA )                                   ;

    Table1_Entry*  getDirStreamEntryByPA      (uint64_t llcMissPA, std::string cmdType,uint64_t CACHE_GRANU_MASK=0xFF)                    ;
    Table1_Entry*  getStrEntMatchLLCPA     (uint64_t llcMissPA, std::string cmdType,descType &_descType , uint64_t CACHE_GRANU_MASK=0xFF) ;
    Table1_Entry*  getStrEntMatchLLCPAOpt  (uint64_t llcMissPA, std::string cmdType,descType &_descType , uint32_t   metaISARequestorID , bool & is_base_addr , map< Addr ,  Addr> *tlb_inverse,uint64_t CACHE_GRANU_MASK=0xFF) ;
    Table1_Entry* getStrEntMatchLLCPAOptPerCore(uint64_t llcMissPA, std::string cmdType,descType &_descType , uint32_t   p, bool & is_base_addr , map< Addr ,  Addr> *tlb_inverse,uint64_t CACHE_GRANU_MASK=0xFF);





    uint64_t       getAssocLoopIterNum   (Table1_Entry* pTbl1Ent)                           ;
    void           incAllDirStrVAStride  (    )                                             ;
    void           incDirStrVAStride     (Table1_Entry * pAssocDirStream)                   ;
    void           incDirStrVAMultStride (Table1_Entry * pAssocDirStream,int strideMult)    ;
    void           setDirStrPFN          (Table1_Entry *pTbl1Ent, uint64_t pa, uint64_t va, Addr baseVA,Addr endVA) ;
    /*\**************************************************************\*/


    /******************************************************************/
    /****************** Direct Stream Functions   *********************/
    /* Search for a loop descriptor entry where the current PC identifies its start */
    bool     getLoopStartPC         (int idx,uint64_t * pc)  ;
    int      getLoopDescEntry       (uint64_t crntPC)        ;
    bool     incLoopDescIterNum     (int loopID)             ;
    /*\**************************************************************\*/

    /******************************************************************/
    /****************** Pointer Chasing Functions   *********************/
    bool     setPtrChaseVAs          (int idx2,Addr ptrVA[])  ;
    bool     setPtrChaseVA           (int idx2,Addr ptrVA)    ;
    bool     setPtrChasePABySrchAll  (Addr ptrPA, Addr ptrVA) ;
    bool     setPtrChasePA           (Table1_Entry *pTbl1Ent, uint64_t pa);
    Addr     getPtrChasePA(int loc2);
    /*\**************************************************************\*/

};


#endif
