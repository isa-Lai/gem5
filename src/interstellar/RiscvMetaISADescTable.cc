#include "RiscvMetaISADescTable.hh"
#include "debug/MetaISA_IPP_LOOP.hh"
#include "debug/MetaISA_IPP_DIRST.hh"
#include "debug/MetaISA_TLB_INTERNAL.hh"
#include "debug/MetaISA_LLC_Miss_Dir.hh"
#include "debug/MetaISA_LLC_Miss_Ptr.hh"
#include "debug/MetaISA_LLC_Miss_Others.hh"
#include "debug/MetaISA_LLC_Miss_All.hh"
#include "debug/MetaISA_DescTable.hh"
#include "debug/MetaISA_Ptr_Logic.hh"
#include "debug/MetaISA_TLB_INTERNAL_Detailed.hh"

/**********************************************************
 *              DescTable::DescTable
 *  DescTable CLass Constructor
 *
 *
 *
 * ********************************************************/
DescTable::DescTable()
{
    /* Upon recive the reset signal in hw , both registers should be zero */
    this->tbl1EntriesNum = 0;
    this->tbl2EntriesNum = 0;
    for(int i = 0 ; i<MAX_PROCESSORS ;i++)
    {
        this->tbl1EntNum[i] = 0;
        this->tbl2EntNum[i] = 0;
        this->lastIDPerProcesser[i] = -1 ;
    }
}

void DescTable::initExtraFields(descType entryType, int loc1, int loc2)
{
    //printf("Init descriptor of type %d , loc1 = %d , loc2 =%d\n",entryType,loc1,loc2);
    if(entryType==LOOP)
    {
        /* Inital value for the iteration number is zero */
        this->descripTable2[loc2 + iterationNumber] = 0;
    }
    else
    {
        if(entryType==DIR_STREAM || entryType==INDIR_STREAM)
        {
        /* Current Page Start Physical address = -1 (Unknown Yet ) */
            for(int pfnC=0;pfnC<PFNs_Per_STREAM;pfnC++)
                this->descripTable2[loc2 +crntPfn1Offset+ pfnC] =-1; // @TOTO Initlaization not with insertion Initial PFN (-1) Means Putting 1 in the invalid bit location (The MSB)
            this->descripTable2[loc2+FIRST_PFN] = -1;
            this->descripTable2[loc2+LAST_PFN]  = -1;


            /* Inital VA= (Base VA-stride)    */
            uint64_t baseAddr = this->descripTable1[loc1].entryCSRData.descInfo.streamDesc.baseAddr;
            uint64_t stride = this->descripTable1[loc1].entryCSRData.descInfo.streamDesc.stride;

            // Following line if increment is done with first loop line by stride
            //Commendted:  this->descripTable2[loc2 + crntVAOffset] = baseAddr - stride;
            //Following line if increment is done each llc miss with stride multiple
            this->descripTable2[loc2 + crntVAOffset] = baseAddr ;
        }

    }

}



void DescTable::initRunTimeFields(descType entryType, int loc1, int loc2,int metaISARequestorID)
{
    DPRINTF(MetaISA_DescTable, "Init descriptor for core %d of type %d , loc1 = %d , loc2 =%d\n",metaISARequestorID,entryType,loc1,loc2);
    if(entryType==LOOP)
    {
        /* Inital value for the iteration number is zero */
        this->descripTable2[loc2 + iterationNumber] = 0;
        DPRINTF(MetaISA_DescTable, "Loop [ID=%d] Counter start = %d , Counter end = %d\n",
        loc1,
        this->descripTable1[loc1].entryCSRData.descInfo.loopDesc.initVal,
        this->descripTable1[loc1].entryCSRData.descInfo.loopDesc.endVal);
    }
    else
    {
        if(entryType==DIR_STREAM || entryType==INDIR_STREAM)
        {
        /* Current Page Start Physical address = -1 (Unknown Yet ) */
            MISA_Desc_t::DescInfo descObj = this->descripTable1[loc1].entryCSRData.descInfo;
            int parentLoopID;
            if(entryType==DIR_STREAM)
                parentLoopID = descObj.streamDesc.loopDescId;
            else //Indirect Stream
                parentLoopID = descObj.stream.parentStreamId;
            //Get the target idx that is associated with parent ID and metaISARequesotrID
            int targetIDX = 0;
            for(int i = 0 ; i<this->tbl1EntriesNum;i++)
                if(this->descripTable1[i].metaISARequestorID ==metaISARequestorID )
                    if(this->descripTable1[i].metaISAStreamID == i)
                        targetIDX = i;
            //Compute the number of iterations of the loop
            uint64_t loopStart = this->descripTable1[targetIDX].entryCSRData.descInfo.loopDesc.initVal;
            uint64_t loopStop = this->descripTable1[targetIDX].entryCSRData.descInfo.loopDesc.endVal;
            uint64_t loopStep = this->descripTable1[targetIDX].entryCSRData.descInfo.loopDesc.inc;
            uint64_t loopIterations= (loopStop-loopStart)/loopStep;//Should  be integer (But what if the step /start/inc is float?)

            if(entryType==DIR_STREAM)
                this->descripTable2[loc2+BASEVA_LOC] = descObj.streamDesc.baseAddr;
            else //Indirect Stream
                this->descripTable2[loc2+BASEVA_LOC] = descObj.stream.baseAddr;
            uint64_t step;
            if(entryType==DIR_STREAM)
                step = descObj.streamDesc.stride;
            else //Indirect Stream
                step = descObj.stream.stride;


            this->descripTable2[loc2+ENDVA_LOC] =
                   this->descripTable2[loc2+BASEVA_LOC]+((uint64_t)loopIterations*(uint64_t)step);

            DPRINTF(MetaISA_DescTable, "\t\tParent Loop ID = %d\n",parentLoopID);
            DPRINTF(MetaISA_DescTable, "\t\tFound in DescriptTable 1 at Idx  = %d\n",targetIDX);
            DPRINTF(MetaISA_DescTable, "\t\t Loop Start =%lx , Loop stop = %lx , Loop Step = %lx , Loop Iterations = %lx \n",loopStart , loopStop , loopStep ,    loopIterations);
            DPRINTF(MetaISA_DescTable, "\t\t Entry type = %d : BaseVA=%lx , endVA = %lx \n",entryType,this->descripTable2[loc2+BASEVA_LOC],this->descripTable2[loc2+ENDVA_LOC]);

        }

    }

}



void DescTable::initRunTimeFieldsPerCore(descType entryType, int loc1, int loc2,int p)
{

    DPRINTF(MetaISA_DescTable, "Init descriptor of type %d , loc1 = %d , loc2 =%d\n",entryType,loc1,loc2);
    if(entryType==LOOP)
    {
        /* Inital value for the iteration number is zero */
        this->descTable2[p][loc2 + iterationNumber] = 0;
        DPRINTF(MetaISA_DescTable, "Loop [ID=%d] Counter start = %d , Counter end = %d\n",
        loc1,
        this->descTable1[p][loc1].entryCSRData.descInfo.loopDesc.initVal,
        this->descTable1[p][loc1].entryCSRData.descInfo.loopDesc.endVal);
    }
    else
    {
        if(entryType==DIR_STREAM || entryType==INDIR_STREAM)
        {
        /* Current Page Start Physical address = -1 (Unknown Yet ) */
            MISA_Desc_t::DescInfo descObj = this->descTable1[p][loc1].entryCSRData.descInfo;
            int parentLoopID;
            if(entryType==DIR_STREAM)
                parentLoopID = descObj.streamDesc.loopDescId;
            else //Indirect Stream
                parentLoopID = descObj.stream.parentStreamId;
            //Get the target idx that is associated with parent ID and metaISARequesotrID
            int targetIDX = 0;
            for(int i = 0 ; i<this->tbl1EntNum[p];i++)
                if(this->descTable1[p][i].metaISARequestorID == p )//Redunbdent Checl
                    if(this->descTable1[p][i].metaISAStreamID == parentLoopID)
                    {
                        targetIDX = i;
                        break;
                    }
            //Compute the number of iterations of the loop
            uint64_t loopStart = this->descTable1[p][targetIDX].entryCSRData.descInfo.loopDesc.initVal;
            uint64_t loopStop  = this->descTable1[p][targetIDX].entryCSRData.descInfo.loopDesc.endVal;
            uint64_t loopStep  = this->descTable1[p][targetIDX].entryCSRData.descInfo.loopDesc.inc;
            uint64_t loopIterations= (loopStop-loopStart)/loopStep;//Should  be integer (But what if the step /start/inc is float?)

            if(entryType==DIR_STREAM)
                this->descTable2[p][loc2+BASEVA_LOC] = descObj.streamDesc.baseAddr;
            else //Indirect Stream
                this->descTable2[p][loc2+BASEVA_LOC] = descObj.stream.baseAddr;
            uint64_t step;
            if(entryType==DIR_STREAM)
                step = descObj.streamDesc.stride;
            else //Indirect Stream
                step = descObj.stream.stride;


            this->descTable2[p][loc2+ENDVA_LOC] =
                   this->descTable2[p][loc2+BASEVA_LOC]+((uint64_t)loopIterations*(uint64_t)step);

            DPRINTF(MetaISA_DescTable, "\t\tParent Loop ID = %d\n",parentLoopID);
            DPRINTF(MetaISA_DescTable, "\t\tFound in DescriptTable 1 at Idx  = %d\n",targetIDX);
            DPRINTF(MetaISA_DescTable, "\t\t Loop Start =%lx , Loop stop = %lx , Loop Step = %lx , Loop Iterations = %lx \n",loopStart , loopStop , loopStep ,    loopIterations);
            DPRINTF(MetaISA_DescTable, "\t\t Entry type = %d : BaseVA=%lx , endVA = %lx \n",entryType,this->descTable2[p][loc2+BASEVA_LOC],this->descTable2[p][loc2+ENDVA_LOC]);

        }

    }

}

/**********************************************************
 *              DescTable::insertDesc
 *  Insert a new descriptor to table and update its pointer
 *     to extra fields in table 2
 *
 * @param entryVal the entry to be inserted
 * @return true in case of success , false otherwise
 * @todo   Update the offset calculation to accomodate
 *         for new descriptor types other than loop desc
 *          and direct stream descriptor.
 * @todo  it may be better to cluster same descriptos after
 *        each other
 * ********************************************************/
bool DescTable::insertDesc(MISA_Desc_t entryVal, int  streamID , uint32_t      metaISARequestorID)
{
    uint32_t p = metaISARequestorID;

    // DEBUG: Print insertion details with proper type field extraction
    DPRINTF(MetaISA_DescTable, "🔧 INSERT: p=%d streamID=%d type=%d valid=%d active=%d lastID=%d\n",
           p, streamID, (int)entryVal.type, entryVal.valid, entryVal.active,
           this->lastIDPerProcesser[p]);
    DPRINTF(MetaISA_DescTable, "   Descriptor fields: initVal=0x%016llx baseAddr=0x%016llx\n",
           entryVal.descInfo.loopDesc.initVal, entryVal.descInfo.streamDesc.baseAddr);

    /*************** Add the entiry to table 1   ***************/
    if (streamID >= this->MAX_ENTRIES)
        return false;                                 /* Table 1 is full */
    this->descTable1[p][streamID].entryCSRData = entryVal; /* Shallow copy -> no problem as there is no dynamically allocated area */


    if(streamID>this->lastIDPerProcesser[p])
    {
        //printf("Update table len1 : this->tbl1EntNum[%d]) = %d\n",p,this->tbl1EntNum[p]);
        DPRINTF(MetaISA_DescTable, "🔧 UPDATE TABLE LEN: p=%d streamID=%d old len=%d new len=%d\n",
               p, streamID, this->tbl1EntNum[p], streamID+1);
        this->setTbl1Len(p,streamID+1);
        this->lastIDPerProcesser[p] = streamID;
        DPRINTF(MetaISA_DescTable, "   VERIFIED: tbl1EntNum[%d]=%d lastIDPerProcesser[%d]=%d\n",
               p, this->tbl1EntNum[p], p, this->lastIDPerProcesser[p]);
    } else {
        DPRINTF(MetaISA_DescTable, "🔧 SKIP TABLE LEN UPDATE: streamID=%d <= lastID=%d\n",
               streamID, this->lastIDPerProcesser[p]);
    }
    //this->descTable1[p][idx].metaISAStreamID =  this->lastIDPerProcesser[metaISARequestorID];  // The stream ID is simply the index (order) of insertion

    this->descTable1[p][streamID].metaISAStreamID    = streamID            ;
    this->descTable1[p][streamID].metaISARequestorID =  metaISARequestorID ;   // The stream ID is simply the index (order) of insertion

    /*************** Add extra fields to table 2 ***************/
    int tbl2Loc = 0;
    if (streamID > 0)
    {
        descType prevEntryType = getDescType( p , streamID - 1);
        tbl2Loc = entriesNumTbl2[prevEntryType];
        tbl2Loc += this->descTable1[p][streamID - 1].extraFieldsLoc;
    }
    this->descTable1[p][streamID].extraFieldsLoc = tbl2Loc;
    descType crntEntryType = getDescType(p , streamID);
    if ((tbl2Loc + entriesNumTbl2[crntEntryType]) >= MAX_EXTRA_FIELDS)
        return false; /* Table 2 is full */
    initRunTimeFieldsPerCore(crntEntryType, streamID , tbl2Loc , metaISARequestorID);

    /*************** Increase actual Entries Num ***************/
    //printf(" DescTable entry(%d) inserted : table2 start offset = %d  \n",streamID,tbl2Loc);
    //this->setTbl1Len(p,this->getTbl1Len()+1);
    return true;
}

void  DescTable::removeAllDesc( uint32_t      metaISARequestorID)
{
    //reset lengths of static and runtime info for this requestor
    this->tbl1EntNum[metaISARequestorID] = 0;
    this->tbl2EntNum[metaISARequestorID] = 0;
}

void   DescTable::removeDesc       (uint32_t streamID , uint32_t      metaISARequestorID)
{
     this->descTable1[metaISARequestorID][streamID].entryCSRData.valid= INAVLID;
}


/**********************************************************
 *              DescTable::insertPtrDesc
 *  Insert a new POinter Chasing descriptor to table and
 *     add VAdrr , PAddr to extra fields in table 2
 *
 * @param entryVal the entry to be inserted
 * @return true in case of success , false otherwise
 * @todo   Update the offset calculation to accomodate
 *         for new descriptor types other than loop desc
 *          and direct stream descriptor.
 * @todo  it may be better to cluster same descriptos after
 *        each other
 * ********************************************************/
bool DescTable::insertPtrDesc(MISA_Desc_t entryVal,Addr VAddr, Addr PAddr)
{
    uint32_t idx = this->getTbl1Len();

    /*************** Add the entiry to table 1   ***************/
    if (idx == this->MAX_ENTRIES)
        return false;                                 /* Table 1 is full */
    this->descripTable1[idx].entryCSRData = entryVal; /* Shallow copy -> no problem as there is no dynamically allocated area */
    this->descripTable1[idx].metaISAStreamID =  idx;  // The stream ID is simply the index (order) of insertion
    /*************** Add extra fields to table 2 ***************/
    int tbl2Loc = 0;
    if (idx > 0)
    {
        descType prevEntryType = getDescType(idx - 1);
        tbl2Loc = entriesNumTbl2[prevEntryType];
        tbl2Loc += this->descripTable1[idx - 1].extraFieldsLoc;
    }
    this->descripTable1[idx].extraFieldsLoc = tbl2Loc;
    descType crntEntryType = getDescType(idx);
    if ((tbl2Loc + entriesNumTbl2[crntEntryType]) >= MAX_EXTRA_FIELDS)
        return false; /* Table 2 is full */
    this->descripTable2[tbl2Loc+PtrVAValue] = VAddr;
    //Store it inside the upper and lower VA as well !
    this->descripTable2[tbl2Loc+PtrVaUBound] = VAddr;
    this->descripTable2[tbl2Loc+PtrVaLBound] = VAddr;

    this->descripTable2[tbl2Loc+PtrPAValue] = PAddr;
	DPRINTF(MetaISA_Ptr_Logic,"Descriptor is inserted : VA:%#x , PA :%#x \n",VAddr,PAddr);

    this->setTbl1Len(++idx);
    return true;

}

bool     DescTable::setPtrChaseVAs         (int idx2,Addr ptrVA[])
{

    Addr vaLower = this->descripTable2[idx2+PtrVaLBound]  ;
    Addr vaUpper = this->descripTable2[idx2+PtrVaUBound]  ;


    for(int i =0;i<VAs_PER_PTR;i++)
    {
        this->descripTable2[idx2+PtrVAValue+i] = ptrVA[i];
        if(ptrVA[i]==0x0)
            continue;
        //DPRINTF(MetaISA_Ptr_Logic," Insert VA=%#lx in Descriptor bounded by[%#lx,%#lx]\n",ptrVA[i],vaLower,vaUpper);
        if(ptrVA[i]<vaLower)
            this->descripTable2[idx2+PtrVaLBound] = ptrVA[i];
        else
            if(ptrVA[i]>vaUpper)
                this->descripTable2[idx2+PtrVaUBound] = ptrVA[i];
        //DPRINTF(MetaISA_Ptr_Logic," Now VA=%#lx in Descriptor bounded by[%#lx,%#lx]\n",ptrVA[i],vaLower,vaUpper);

    }

    return true;
}


bool     DescTable::setPtrChaseVA         (int idx2,Addr ptrVA)
{

    Addr vaLower = this->descripTable2[idx2+PtrVaLBound]  ;
    Addr vaUpper = this->descripTable2[idx2+PtrVaUBound]  ;


    if(ptrVA==0x0)
            return false;
    //DPRINTF(MetaISA_Ptr_Logic," Insert VA=%#lx in Descriptor bounded by[%#lx,%#lx]\n",ptrVA[i],vaLower,vaUpper);
    if(ptrVA<vaLower)
            this->descripTable2[idx2+PtrVaLBound] = ptrVA;
    else
            if(ptrVA>vaUpper)
                this->descripTable2[idx2+PtrVaUBound] = ptrVA;
    //DPRINTF(MetaISA_Ptr_Logic," Now VA=%#lx in Descriptor bounded by[%#lx,%#lx]\n",ptrVA[i],vaLower,vaUpper);

    return true;
}



bool   DescTable::setPtrChasePABySrchAll         (Addr ptrPA, Addr ptrVA)
{
    for (int i = 0; i < this->tbl1EntriesNum; i++)
    {

        if(getDescType(i)!=PTR_CHASE)
            continue;
        int loc2 = this->descripTable1[i].extraFieldsLoc;

        //Search against all expected VAs
        Addr expectedVA;
        for(int i =0;i<VAs_PER_PTR;i++)
        {

            expectedVA = this->descripTable2[loc2+PtrVAValue+i];
            if(expectedVA==0)
                continue;//Ignore if NULL is stored
            //Comparing the addresses can result in the problem where accessed address is within node not start of the node
            // do comparison of address in cache granularity level
            if( (ptrVA&~(CACHE_GRANULVL_MASK)) == ( expectedVA&~(CACHE_GRANULVL_MASK)))
            {
                DPRINTF(MetaISA_Ptr_Logic," Pointer chase (%i) PA is changed to:%#lx\n",i,ptrPA);
                this->descripTable2[loc2+PtrPAValue] = ptrPA;
                return true;
            }
        }

    }
    return false;

}


bool   DescTable::setPtrChasePA        (Table1_Entry *pTbl1Ent, uint64_t pa)
{
    DPRINTF(MetaISA_Ptr_Logic," Pointer chase (ID=%i) PA is changed to:%#lx\n",pTbl1Ent->metaISAStreamID,pa);
    int loc2 = pTbl1Ent->extraFieldsLoc;
    this->descripTable2[loc2+PtrPAValue] = pa;
    return true;
}

Addr     DescTable::getPtrChasePA(int loc2)
{

    return this->descripTable2[loc2+PtrPAValue];
}


/**********************************************************
 *              DescTable::removeDesc

     Shift the remaining descriptors and shift associated table 2 extra fields
      modify the other remaining descriptors pointers to table 2
 *
 * @param entryVal the entry to be removed
 * @return true in case of success , false otherwise
 * @todo   It is better to pass teh ID of descriptor to
 *         be removed not the full descriptor !
 *
 * ********************************************************/
void DescTable::removeDesc(MISA_Desc_t entryVal)
{

    //
}

/**********************************************************
 *              DescTable::getDescType
 *  Make sure that the descriptor is valid and active
 *      then returns its type.
 * @param idx location of the descriptor in table 1
 * @return type of the secriptor if valid , None if not
 *
 * ********************************************************/
descType DescTable::getDescType(int idx)
{
    // CRITICAL FIX: Use per-core table for processor 0 instead of global table
    // This ensures consistency with insertDesc which uses per-core tables
    if (this->descTable1[0][idx].entryCSRData.active == 0)
        return NONE;
    return (descType)(this->descTable1[0][idx].entryCSRData.type);
}

descType DescTable::getDescType(int p /* requestor id */, int idx)
{
    if (this->descTable1[p][idx].entryCSRData.active == 0)
        return NONE;
    return (descType)(this->descTable1[p][idx].entryCSRData.type);
}



/**************************************************************
 *              DescTable::getDirStreamBaseAdress
 *  Get the value of the virtual address of the base of
 *      a direct stream either it is linked or not
 * @param  idx location of the descriptor in table 1
 * @return the 64-bit VA of the start addrress
 * @todo   complete the function for the linked case
 * ***********************************************************/
uint64_t DescTable::getDirStreamBaseAdress(int idx)
{
    /****** (1) Make sure it is active and of direct stream type  */
    if (getDescType(idx) != DIR_STREAM)
        return 0; /* NULL means error */

    /****** (2) Now Compute the base VA                     ******/
    MISA_Desc_t::DescInfo descObj = this->descTable1[0][idx].entryCSRData.descInfo;
    /* TODO : Complete for the linked case */
    if (descObj.streamDesc.linked == 0)
        return (descObj.streamDesc.baseAddr);
    return 0;
}

/**************************************************************
 *              DescTable::getDirStreamExpectedVA
 *  Get the value of the virtual address of the current
 *      location to be accessed by a direct stream
 * @param  idx location of the descriptor in table 1
 * @return the 64-bit VA of the current location
 *
 * ***********************************************************/
bool DescTable::getDirStreamExpectedVA(int idx, uint64_t& expectedVA)
{
    /** (1) Make sure it is active and of direct stream type           **/
    if (getDescType(idx) != DIR_STREAM)
        return false;
    /** (2) Get the pointer to associated extra fields table entry     **/
    int extraFieldsLoc = this->descripTable1[idx].extraFieldsLoc;
    /** (3) Get the value of the expected VA                                 **/
    expectedVA = descripTable2[extraFieldsLoc + crntVAOffset];
    DPRINTF(MetaISA_TLB_INTERNAL," DescTable entry(%d) : table2 start offset = %d , Inquiry => table2[%d]=expected VA =%llx\n",idx,extraFieldsLoc,extraFieldsLoc + crntVAOffset,expectedVA);
    return true;
}

/**************************************************************
 *              DescTable::getDirStreamExpectedPA
 *  Get the value of the virtual address of the current
 *      location to be accessed by a direct stream
 * @param  idx location of the descriptor in table 1
 * @return the 64-bit VA of the current location
 *
 * ***********************************************************/
uint64_t DescTable::getDirStreamExpectedPA(int idx)
{
    /** (1) Make sure it is active and of direct stream type           **/
    if (getDescType(idx) != DIR_STREAM)
        return 0; /* NULL means error */
    /** (2) Get the pointer to associated extra fields table entry     **/
    int extraFieldsLoc = this->descripTable1[idx].extraFieldsLoc;
    /** (3) Get the value of the expected VA                                 **/
    uint64_t expectedVA = descripTable2[extraFieldsLoc + crntVAOffset];
    uint64_t expectedPageOffset = expectedVA & PAGE_OFFSET_MASK /* Extract Page Offset*/;
    #define crntPfnOffset 0
    uint64_t expectedPFN = descripTable2[extraFieldsLoc + crntPfnOffset];
    uint64_t expectedPA = (expectedPFN << PageShift) + expectedPageOffset;
    DPRINTF(MetaISA_LLC_Miss_All, "Desctable entry(%d) expectedVA = %llx , expectedPageOffset = %llx , expectedPFN = %llx , expectedPA = %llx   \n", idx, expectedVA, expectedPageOffset, expectedPFN, expectedPA);
    return expectedPA;
}


/**************************************************************
 *              DescTable::getDirStreamExpectedPA
 *  check if a given descriptor table entry of index (idx) has PA (expectedPA)
 *        in its queue of expected PFNs to arrive at LLC at index PFN_Index
 * @param  idx location of the descriptor in table 1
 * @param  PFN_Index  location of the PFN within associated PFN Queue
 * @param  expectedPA expected PA to arrive at LLC
 * @return true if PA exists , false otherwise
 *
 * ***********************************************************/
bool DescTable::getDirStreamExpectedPA(int idx ,int PFN_Index,uint64_t &expectedPA)
{
    /** (1) Make sure it is active and of direct stream type           **/
    if (getDescType(idx) != DIR_STREAM)
        return false; /* NULL means error */
    /** (2) Get the pointer to associated extra fields table entry     **/
    int extraFieldsLoc = this->descripTable1[idx].extraFieldsLoc;
    /** (3) Get the value of the expected VA                                 **/
    uint64_t expectedVA = descripTable2[extraFieldsLoc + crntVAOffset];
    uint64_t expectedPageOffset = expectedVA & PAGE_OFFSET_MASK /* Extract Page Offset*/;
    uint64_t expectedPFN = descripTable2[extraFieldsLoc + crntPfn1Offset+PFN_Index];
    if(expectedPFN==-1)//Invalid (0xFFFFFFFFFFFFFFFF)
        return false;
    expectedPA = (expectedPFN << PageShift) + expectedPageOffset;
    DPRINTF(MetaISA_LLC_Miss_All, "Desctable entry(%d) expectedVA = %llx , expectedPageOffset = %llx , expectedPFN = %llx , expectedPA = %llx   \n", idx, expectedVA, expectedPageOffset, expectedPFN, expectedPA);
    return true;
}


/**************************************************************
 *              DescTable::getStrExpectedPaAtLLC
 *  check if a given descriptor table entry of index (idx) is Valid Stream
 *    (Direct/IndirecT) and return its expectedPA to arrive at LLC
 *        in its queue of expected PFNs to arrive at LLC at index PFN_Index
 * @param [in]   idx location of the descriptor in table 1
 * @param [in]   PFN_Index  location of the PFN within associated PFN Queue
 * @param [out]  expectedPA expected PA to arrive at LLC
 * @param [out]  _descType  descriptor type  to be returned
 * @return true if PA is valid stream which access memory , false otherwise
 *
 * ***********************************************************/
bool DescTable::getStrExpectedPaAtLLC(int idx ,int PFN_Index,uint64_t &expectedPA, descType&_descType)
{

    // return the descriptor type
    _descType = getDescType(idx) ;

    /** (1) Make sure it is active and of any stream type other than LOOP    **/

    if (_descType == LOOP)
        return false; //LOOP is not a DRAM descriptor
    /** (2) Get the pointer to associated extra fields table entry     **/
    int extraFieldsLoc = this->descripTable1[idx].extraFieldsLoc;
    /** (3) Get the value of the expected VA                                 **/
    uint64_t expectedMMUVA      = descripTable2[extraFieldsLoc + crntVAOffset];
    uint64_t expectedPageOffset = expectedMMUVA & PAGE_OFFSET_MASK /* Extract Page Offset*/;
    uint64_t expectedLLCPFN     = descripTable2[extraFieldsLoc + crntPfn1Offset+PFN_Index];
    if(expectedLLCPFN==-1)// expected to come at LLC
        return false; //Invalid (0xFFFFFFFFFFFFFFFF)
    // return the expected PA at LLC
    expectedPA = (expectedLLCPFN << PageShift) + expectedPageOffset;

    DPRINTF(MetaISA_LLC_Miss_All, "Desctable entry(%d) expectedVA@MMU = %llx , expectedPageOffset@LLC = %llx , expectedPFN@LLC= %llx , expectedPA@LLC = %llx   \n", idx, expectedMMUVA, expectedPageOffset, expectedLLCPFN, expectedPA);
    return true;
}


/**************************************************************
 *              DescTable::getStrExpectedPaAtLLC
 *  check if a given descriptor table entry of index (idx) is Valid Stream
 *    (Direct/IndirecT) and return its expectedPA to arrive at LLC
 *        in its queue of expected PFNs to arrive at LLC at index PFN_Index
 * @param [in]   loc2 location of the descriptor in table 2
 * @param [in]   PFN_Index  location of the PFN within associated PFN Queue
 * @param [out]  expectedPFN expected PFN to arrive at LLC
 * @param [out]  _descType  descriptor type  to be returned
 * @return true if PA is valid stream which access memory , false otherwise
 *
 * ***********************************************************/
bool DescTable::getStrExpectedPfnAtLLC(int loc2 ,int PFN_Index,uint64_t &expectedPFN, descType&_descType)
{

    expectedPFN     = descripTable2[loc2 + crntPfn1Offset+PFN_Index];
    if(expectedPFN==-1)// expected to come at LLC
        return false; //Invalid (0xFFFFFFFFFFFFFFFF)

    return true;
}


/**************************************************************
 *              DescTable::getAssocLoopIterNum
 *  Get the value of the loop iteration number of the associated
 *     loop with a certain  direct stream descriptor
 * @param  pTbl1Ent pointer to the direct stream descripotor
 * @return an 64-bit specify the current loop iteration number
 * TODO WRONG
 *        : Uncompleted  No Loop IDs -> Assume Associated loop
 *        descriptor is at index 0 for now !!!!!
 *
 * ***********************************************************/
uint64_t DescTable::getAssocLoopIterNum(Table1_Entry *pTbl1Ent)
{

    /** (2) Get the index of the associated loop stream                        **/
    MISA_Desc_t::DescInfo descObj = pTbl1Ent->entryCSRData.descInfo;
    int loop_id = descObj.streamDesc.loopDescId;
    /** (3) Goto the associated loop and pick the pointer to its extra data    **/
    /** TODO No Descp ID field inside the descriptor !! **/
    /** (4) From associated loop extra data Get the current iteration number **/

    int tbl2Idx = descripTable1[0].extraFieldsLoc;
    uint64_t itNum = descripTable2[tbl2Idx + iterationNumber];

    return itNum;
}

/**************************************************************
 *              DescTable::getDirStreamEntry
 *  Get a pointer to a direct stream descriptor in table 1
 *     where a specific VA belongs to its address space
 * @param  crntTLBVA current VA sniffed on TLB trasnaction
 * @return Pointer to direct stream descriptor where
 *                  this VA belongs to its address space
 *
 * ***********************************************************/
Table1_Entry *DescTable::getDirStreamEntryByVA(uint64_t crntTLBVA)
{
    for (int i = 0; i < this->tbl1EntriesNum; i++)
    {
        /* getDirStreamCrntAdress  will make sure that descriptor at i is direct stream and active */
        uint64_t  nextVA   ;
        bool     expectedVAStatus = getDirStreamExpectedVA(i,nextVA);
        uint64_t baseAddr         = getDirStreamBaseAdress(i);
        if(expectedVAStatus)
        {
            bool vaInStream1 = ((nextVA&PAGE_MASK) == (crntTLBVA&PAGE_MASK));//Same VPN
            bool vaInStream2 = ((nextVA&PAGE_MASK) == (crntTLBVA&PAGE_MASK)+1);//or Adjacent Higher VPN

            DPRINTF(MetaISA_TLB_INTERNAL, "crntTLBVA(vpn) = %llx(%#lx) , expected VA(vpn) = %llx(%#lx) \n", crntTLBVA,(crntTLBVA&PAGE_MASK),nextVA,(nextVA&PAGE_MASK));
            if ((crntTLBVA>=baseAddr)&&(vaInStream1||vaInStream2))
            {
                return &(this->descripTable1[i]);
            }
        }
    }
    return nullptr; // Not found
}


/**************************************************************
 *              DescTable::getStreamEntryByVARange
 *  Get a pointer to a stream (either direct or indirect) \
 *    descriptor in table 1 where a specific VA belongs
 *    to its address space by searching against range not specific expected VA
 * @param  crntTLBVA current VA sniffed on TLB trasnaction
 * @return Pointer to direct/Indirect stream descriptor where
 *                  this VA belongs to its address space
 *
 * ***********************************************************/
Table1_Entry *DescTable::getStrEntByVARange(uint64_t crntTLBVA,uint64_t &baseVA,uint64_t&endVA)
{
    int _descType ;
    for (int i = 0; i < this->tbl1EntriesNum; i++)
    {
        /* getDirStreamCrntAdress  will make sure that descriptor at i is direct stream and active */
        _descType = getDescType(i);
        if(_descType==LOOP)
            continue;
        if(_descType> 5 || _descType<0)
        {
            DPRINTF(MetaISA_TLB_INTERNAL_Detailed, "None Stream Type!\n");
            continue;
        }


        if(_descType==DIR_STREAM || _descType ==INDIR_STREAM)
        {
            MISA_Desc_t::DescInfo descObj = this->descripTable1[i].entryCSRData.descInfo;
            int parentLoopID;
            if(getDescType(i)==DIR_STREAM)
                parentLoopID = descObj.streamDesc.loopDescId;
            else //Indirect Stream
                parentLoopID = descObj.stream.parentStreamId;
            //Compute the number of iterations of the loop
            int loopStart = this->descripTable1[parentLoopID].entryCSRData.descInfo.loopDesc.initVal;
            int loopStop = this->descripTable1[parentLoopID].entryCSRData.descInfo.loopDesc.endVal;
            int loopStep = this->descripTable1[parentLoopID].entryCSRData.descInfo.loopDesc.inc;
            int long long loopIterations= (loopStop-loopStart)/loopStep;//Should  be integer (But what if the step /start/inc is float?)

            if(getDescType(i)==DIR_STREAM)
                baseVA = descObj.streamDesc.baseAddr;
            else //Indirect Stream
                baseVA = descObj.stream.baseAddr;
            int step;
            if(getDescType(i)==DIR_STREAM)
                step = descObj.streamDesc.stride;
            else //Indirect Stream
                step = descObj.stream.stride;


            endVA=baseVA+(loopIterations*step);

            //Get the parent Loop ID
            DPRINTF(MetaISA_TLB_INTERNAL_Detailed, "Stream(%d) -> Its Loop iterations = %lld , Start VA = %#llx - End VA= %#llx \n",i,loopIterations,baseVA,endVA);

            if(crntTLBVA>=baseVA && crntTLBVA<endVA)
            {
                DPRINTF(MetaISA_TLB_INTERNAL, "crntTLBVA(vpn) = %llx is found to be %s\n", crntTLBVA,descNames[getDescType(i)]);
                return &(this->descripTable1[i]);
            }
        }

        if(_descType ==PTR_CHASE)
        {
            //Search against the bound of linked lists VAs
            Addr expectedVAL,expectedVAU;//Lower and upper bounds
            Addr maskedVA =  (crntTLBVA&~(CACHE_GRANULVL_MASK));
            int loc2      = this->descripTable1[i].extraFieldsLoc;
            expectedVAL   = this->descripTable2[loc2+PtrVaLBound];
            expectedVAU   = this->descripTable2[loc2+PtrVaUBound];
            bool vaBigThanL,vaLessThanU;
            vaBigThanL  =( maskedVA >= ( expectedVAL&~(CACHE_GRANULVL_MASK)));
            vaLessThanU =( maskedVA <= ( expectedVAU&~(CACHE_GRANULVL_MASK)));
            DPRINTF(MetaISA_TLB_INTERNAL, "crntTLBVA(vpn) = %llx is under checking whether to be %s as VA in range [%#lx,%#lx]\n", crntTLBVA,descNames[getDescType(i)],expectedVAL,expectedVAU);

            if(vaBigThanL && vaLessThanU  )
            {
                DPRINTF(MetaISA_TLB_INTERNAL, "crntTLBVA(vpn) = %llx is found to be %s as VA in range [%#lx,%#lx]\n", crntTLBVA,descNames[getDescType(i)],expectedVAL,expectedVAU);
                return &(this->descripTable1[i]);
            }


        }



    }
    return nullptr; // Not found
}



/**************************************************************
 *              DescTable::getStreamEntryByVARangeOptimized
 *  Get a pointer to a stream (either direct or indirect) \
 *    descriptor in table 1 where a specific VA belongs
 *    to its address space by searching against range not specific expected VA
 * @param  crntTLBVA current VA sniffed on TLB trasnaction
 * @return Pointer to direct/Indirect stream descriptor where
 *                  this VA belongs to its address space
 *
 * ***********************************************************/
Table1_Entry *DescTable::getStrEntByVARangeOpt(uint64_t crntTLBVA )
{
    // @TODO  This function needed to be re-written as the tables as now per processor
    int _descType   ;
    Addr crntTLBVPN = crntTLBVA >>PageShift ;
    for(int p =0 ; p< MAX_PROCESSORS ; p++)
    {
        for (int i = 0; i < this->getTbl1Len(p); i++)
        {
            /* getDirStreamCrntAdress  will make sure that descriptor at i is direct stream and active */
            _descType = getDescType(p,i);

            if(_descType==DIR_STREAM || _descType ==INDIR_STREAM)
            {

                int j   = this->descTable1[p][i].extraFieldsLoc;
                Addr baseVPN = this->descTable2[p][j+BASEVA_LOC] >> PageShift;
                Addr endVPN  = this->descTable2[p][j+ENDVA_LOC ] >> PageShift;
                DPRINTF(MetaISA_TLB_INTERNAL, "BASEVA  = %lx , ENDVA = %lx \n",this->descripTable2[j+BASEVA_LOC],this->descripTable2[j+ENDVA_LOC ] );
                DPRINTF(MetaISA_TLB_INTERNAL, "Address = %llx of VPN = %llx is checked against to be [%llx : %llx] range for direct stream\n", crntTLBVA,crntTLBVPN,baseVPN,endVPN);

                if(crntTLBVPN>=baseVPN && crntTLBVPN<=endVPN)
                {
                    DPRINTF(MetaISA_TLB_INTERNAL, "crntTLBVA(vpn) = %llx is found to be %s\n", crntTLBVA,descNames[getDescType(p,i)]);
                    return &(this->descTable1[p][i]);
                }
            }

            if(_descType ==PTR_CHASE)
            {
                //Search against the bound of linked lists VAs
                Addr expectedVAL,expectedVAU;//Lower and upper bounds
                Addr maskedVA =  (crntTLBVA&~(CACHE_GRANULVL_MASK));
                int loc2      = this->descripTable1[i].extraFieldsLoc;
                expectedVAL   = this->descripTable2[loc2+PtrVaLBound];
                expectedVAU   = this->descripTable2[loc2+PtrVaUBound];
                bool vaBigThanL,vaLessThanU;
                vaBigThanL  =( maskedVA >= ( expectedVAL&~(CACHE_GRANULVL_MASK)));
                vaLessThanU =( maskedVA <= ( expectedVAU&~(CACHE_GRANULVL_MASK)));
                DPRINTF(MetaISA_TLB_INTERNAL, "crntTLBVA(vpn) = %llx is under checking whether to be %s as VA in range [%#lx,%#lx]\n", crntTLBVA,descNames[getDescType(i)],expectedVAL,expectedVAU);

                if(vaBigThanL && vaLessThanU  )
                {
                    DPRINTF(MetaISA_TLB_INTERNAL, "crntTLBVA(vpn) = %llx is found to be %s as VA in range [%#lx,%#lx]\n", crntTLBVA,descNames[getDescType(i)],expectedVAL,expectedVAU);
                    return &(this->descripTable1[i]);
                }


            }
        }



    }
    return nullptr; // Not found
}


/**************************************************************
 *              DescTable::incAllDirStrVAStride
 *  Add stride to the page offset inside the extra
 *     fields table , this function should be called
 *     after each iteration of the loop loopID.
 *
 * @param  none
 * @return void
 *
 * ***********************************************************/
void DescTable::incAllDirStrVAStride()
{
    for (uint32_t i = 0; i < this->getTbl1Len(); i++)
    {
        if (this->getDescType(i) == DIR_STREAM)
        {
            uint32_t j = this->descripTable1[i].extraFieldsLoc;
            int stride = this->descripTable1[i].entryCSRData.descInfo.streamDesc.stride;
            this->descripTable2[j + crntVAOffset] += stride;
            DPRINTF(MetaISA_IPP_DIRST, " Direct Stream of ID=%d New VA = 0x%llx\n", i, this->descripTable2[j + crntVAOffset]);
        }
    }
}

/**************************************************************
 *              DescTable::incDirStrVAMultStride
 *  Add stride to the page offset inside the extra
 *     fields table
 *  function should be called with each TLB call for direct stream
 *
 *
 * @param  pAssocDirStream
 * @return void
 *
 * ***********************************************************/
void DescTable::incDirStrVAStride(Table1_Entry * pAssocDirStream )
{
        int j = pAssocDirStream->extraFieldsLoc;
        int stride = pAssocDirStream->entryCSRData.descInfo.streamDesc.stride;
        int loopID = pAssocDirStream->entryCSRData.descInfo.streamDesc.loopDescId;
        this->descripTable2[j + crntVAOffset] += stride;
        DPRINTF(MetaISA_IPP_DIRST, " Direct Stream inside loop %d New VA = 0x%llx\n", loopID, this->descripTable2[j + crntVAOffset]);

}

/**************************************************************
 *              DescTable::incDirStrVAMultStride
 *  Add multiples of stride to (that least equals or greater than LLC
 *     cache line size)  to the page offset inside the extra
 *     fields table , for a specific dir stream descriptor
 *     this function should be called  after each LLC miss.
 *
 * @param  pAssocDirStream
 * @param  strideMultiple
 * @return void
 *
 * ***********************************************************/
void DescTable::incDirStrVAMultStride(Table1_Entry * pAssocDirStream,int strideMult )
{
        int j = pAssocDirStream->extraFieldsLoc;
        this->descripTable2[j + crntVAOffset] += strideMult;
        int loopID = pAssocDirStream->entryCSRData.descInfo.streamDesc.loopDescId;
        DPRINTF(MetaISA_IPP_DIRST, " Direct Stream inside loop %d New VA = 0x%llx\n", loopID, this->descripTable2[j + crntVAOffset]);

}

/**************************************************************
 *              DescTable::setDirStrPFN
 *  Add stride to the page offset inside the extra
 *     fields table , this function should be called
 *     after each iteration.
 *
 * @param  none
 * @return void
 *
 * ***********************************************************/
void DescTable::setDirStrPFN(Table1_Entry *pTbl1Ent, uint64_t pa, uint64_t va, Addr baseVA,Addr endVA)
{

    int loc2 = pTbl1Ent->extraFieldsLoc;
    //DPRINTF(MetaISA_TLB_INTERNAL,"Check PFNs starting at index %d\n",loc2);
    uint64_t entryVPN = (this->descripTable2[loc2 + crntVAOffset]) >> PageShift;
    Addr vpn          =  va     >> PageShift;
    Addr baseVPN      =  baseVA >> PageShift;
    Addr endVPN       =  endVA  >> PageShift;
    uint8_t  PFN_Flag = 0x0;
    Addr pfn      = (pa >> PageShift) ;
    //If First Physical Page
    bool isFirstPFN = vpn==baseVPN;
    bool isLastPFN  = vpn==endVPN ;


    if(isFirstPFN)
    {
        //if FIRST_PFN not inserted before then insert
        if(this->descripTable2[loc2 + FIRST_PFN       ]==-1)
        {
            // PA and VA are of the same offset within the page
            this->descripTable2[loc2 + FIRST_PFN_OFFSET]=(baseVA&PAGE_OFFSET_MASK);
            this->descripTable2[loc2 + FIRST_PFN       ]=(pfn);
            DPRINTF(MetaISA_TLB_INTERNAL," First Corresponding PFN (%#lx) is inserted, start offset =%#lx \n",this->descripTable2[loc2+FIRST_PFN],this->descripTable2[loc2 + FIRST_PFN_OFFSET]);
        }
    }
    if(isLastPFN)
    {
        //if LAST_PFN not inserted before then insert
        if(this->descripTable2[loc2 + LAST_PFN       ]==-1)
        {
            // PA and VA are of the same offset within the page
            this->descripTable2[loc2 + LAST_PFN_OFFSET]=(endVA&PAGE_OFFSET_MASK);
            this->descripTable2[loc2 + LAST_PFN       ]=(pfn);
            DPRINTF(MetaISA_TLB_INTERNAL," Last Corresponding PFN (%#lx) is inserted, last offset =%#lx \n",this->descripTable2[loc2+LAST_PFN],this->descripTable2[loc2 + LAST_PFN_OFFSET]);
        }
    }
    if(isFirstPFN||isLastPFN)
        return;
    //Any other PFN will be inserted in the queue of the associated PFNs
    for(int i= 0 ; i< PFNs_Per_STREAM ; i++ )
    {
        uint64_t entryPFN = this->descripTable2[loc2 +crntPfn1Offset+ i];
        //DPRINTF(MetaISA_TLB_INTERNAL,"PFN at index  %d = %ld\n",loc2+crntPfn1Offset+ i,entryPFN);

        //Compare only the page frame number neglecting the PFN flag bits
        if(entryPFN==pfn)//The new request of same PFN of one of stored PFNs
            return;

        if(entryPFN==-1)//Empty Place to store PFN
        {

            this->descripTable2[loc2 +crntPfn1Offset+ i] = (pfn);
            // Store "PFNs_Per_STREAM" PFN associated with each direct stream
            string streamTypeStr = pTbl1Ent->entryCSRData.type==DIR_STREAM?"DirStream":"InDirStream";
            DPRINTF(MetaISA_TLB_INTERNAL, "Updating %s(loc2=%d) Table 2:  PFN(%i) = %llx ; VPN=%llx \n",
                streamTypeStr.c_str(),loc2,crntPfn1Offset+ i,pfn, entryVPN);
            return;
        }
    }
    //If not exist before and there is no place , shift left
    for(int i= 0 ; i< PFNs_Per_STREAM-1 ; i++ )
    {
        this->descripTable2[loc2 +crntPfn1Offset+i] = this->descripTable2[loc2 + crntPfn1Offset+i+1] ;
    }
    this->descripTable2[loc2 + crntPfn1Offset+PFNs_Per_STREAM-1] =pfn;
    DPRINTF(MetaISA_TLB_INTERNAL, "Updating Stream Table 2:  PFN(%i) = %llx VPN=%llx \n",
                crntPfn1Offset+PFNs_Per_STREAM-1,pfn, entryVPN);

}


/**************************************************************
 *              DescTable::getDirStreamEntryByPA
 *  Get the direct stream where the physical
 *      address of LLC miss belongs to its current access
 *
 * @param  llcMissPA current LLC physical address
 * @return pointer to the direct stream or nullptr if not found
 *
 * ***********************************************************/
Table1_Entry *
DescTable::getDirStreamEntryByPA(uint64_t llcMissPA, std::string cmdType,uint64_t CACHE_GRANU_MASK)
{
    // DEBUG: Print table state when searching
    DPRINTF(MetaISA_DescTable, "🔍 STREAM MATCH: Searching for addr=%#lx, tbl1EntriesNum=%d\n", llcMissPA, this->tbl1EntriesNum);

    for (int i = 0; i < this->tbl1EntriesNum; i++)
    {
        /* getDirStreamCrntAdress  will make sure that descriptor at i is direct stream and active */
        /* TODO: Is it better to compare PFN ONly?
            I'm afraid that "no more than one stream can be in same page"
            assumption is valid always*/
        // In Ramulator (or Memory COntroller) PA Address will be sent in LLC Granularity
        // i.e. the least 6-bits will be zero (if cache line size is 64 bit)
        // so it may be better if we compare the PFN rather than PA
        for(int pfnInd = 0 ; pfnInd<PFNs_Per_STREAM ; pfnInd++ )
        {
            uint64_t expectedPA;
            bool expectedPAStatus =  getDirStreamExpectedPA(i,pfnInd,expectedPA);

            if ( (getDescType(i) != DIR_STREAM) || !(expectedPAStatus))
                continue;   //Current Descriptor is not direct stream @todo : make it as pramter than return address
            // Check if the next miss in the same page (or @todo : check if same PA masking the cache bits)
            DPRINTF(MetaISA_LLC_Miss_Dir,"llcmissPA =%#lx , expectedPA=%#lx\n",llcMissPA,expectedPA);
            uint64_t stride = this->descripTable1[i].entryCSRData.descInfo.streamDesc.stride;
            bool cond1 = (llcMissPA&~CACHE_GRANU_MASK) == ((expectedPA&~CACHE_GRANU_MASK));
            bool cond2 = ((llcMissPA+stride)&~CACHE_GRANU_MASK) == ((expectedPA&~CACHE_GRANU_MASK));
            DPRINTF(MetaISA_LLC_Miss_Dir,"(llcMissPA&~CACHE_GRANU_MASK)  =%#lx , (expectedPA&~CACHE_GRANU_MASK)=%#lx\n",(llcMissPA&~CACHE_GRANU_MASK) ,(expectedPA&~CACHE_GRANU_MASK));
            DPRINTF(MetaISA_LLC_Miss_Dir,"((llcMissPA+stride)&~CACHE_GRANU_MASK)  =%#lx , (expectedPA&~CACHE_GRANU_MASK)=%#lx\n",((llcMissPA+stride)&~CACHE_GRANU_MASK) ,(expectedPA&~CACHE_GRANU_MASK));

            if(!(cond1||cond2))
                continue;

            bool paInStream = ((expectedPA& PAGE_MASK)  == (llcMissPA& PAGE_MASK));
            DPRINTF(MetaISA_LLC_Miss_Dir," llc pag=%lx pfn(%d)=%lx\n",(llcMissPA& PAGE_MASK),pfnInd,(expectedPA& PAGE_MASK));
            if (paInStream)
            {
                DPRINTF(MetaISA_LLC_Miss_Dir, "desc(%d) : llcMissPA = 0x%llx , next PA = 0x%llx , type = %s\n", i,llcMissPA, expectedPA,cmdType.c_str());
                return &(this->descripTable1[i]);
            }
        }
     }
    // Current PA doesn't belong to any of the direct stream descriptors
    DPRINTF(MetaISA_LLC_Miss_Others, "Not desc : llcMissPA = 0x%llx , type = %s \n", llcMissPA ,cmdType.c_str() );
    return nullptr; // Not found
}



/**************************************************************
 *              DescTable::getStrEntMatchLLCPA
 *  Get the Stream Entry where the physical
 *      address of LLC miss belongs to its current access
 *
 * @param  llcMissPA current LLC physical address
 * @return pointer to the direct stream or nullptr if not found
 *
 * ***********************************************************/
/*Table1_Entry *
DescTable::getStrEntMatchLLCPA(uint64_t llcMissPA, std::string cmdType,descType &_descType , uint64_t CACHE_GRANU_MASK)
{
    for (int i = 0; i < this->tbl1EntriesNum; i++)
    {*/
        /* getDirStreamCrntAdress  will make sure that descriptor at i is direct stream and active */
        /* TODO: Is it better to compare PFN ONly?
            I'm afraid that "no more than one stream can be in same page"
            assumption is valid always*/
        // In Ramulator (or Memory COntroller) PA Address will be sent in LLC Granularity
        // i.e. the least 6-bits will be zero (if cache line size is 64 bit)
        // so it may be better if we compare the PFN rather than PA
/*        for(int pfnInd = 0 ; pfnInd<PFNs_Per_STREAM ; pfnInd++ )
        {
            uint64_t expectedPA;
            bool expectedPAStatus =  getStrExpectedPaAtLLC(i,pfnInd,expectedPA,_descType);

            if ( (_descType == LOOP) || !(expectedPAStatus))
                continue;   //Current Descriptor is not direct stream @todo : make it as pramter than return address
            // Check if the next miss in the same page (or @todo : check if same PA masking the cache bits)
            DPRINTF(MetaISA_LLC_Miss_Dir,"llcmissPA =%#lx , expectedPA=%#lx\n",llcMissPA,expectedPA);
            uint64_t stride = this->descripTable1[i].entryCSRData.descInfo.streamDesc.stride;
            //Only check if they lie in the same page


            //if the edxpected page is the last page , then check also the offset.
            //last physical page corresponds to last virtual page

            bool cond1 = (llcMissPA&~CACHE_GRANU_MASK) == ((expectedPA&~CACHE_GRANU_MASK));
            bool cond2 = ((llcMissPA+stride)&~CACHE_GRANU_MASK) == ((expectedPA&~CACHE_GRANU_MASK));
            DPRINTF(MetaISA_LLC_Miss_Dir,"(llcMissPA&~CACHE_GRANU_MASK)  =%#lx , (expectedPA&~CACHE_GRANU_MASK)=%#lx\n",(llcMissPA&~CACHE_GRANU_MASK) ,(expectedPA&~CACHE_GRANU_MASK));
            DPRINTF(MetaISA_LLC_Miss_Dir,"((llcMissPA+stride)&~CACHE_GRANU_MASK)  =%#lx , (expectedPA&~CACHE_GRANU_MASK)=%#lx\n",((llcMissPA+stride)&~CACHE_GRANU_MASK) ,(expectedPA&~CACHE_GRANU_MASK));

            if(!(cond1||cond2))
                continue;

            bool paInStream = ((expectedPA& PAGE_MASK)  == (llcMissPA& PAGE_MASK));
            DPRINTF(MetaISA_LLC_Miss_Dir," llc pag=%lx pfn(%d)=%lx\n",(llcMissPA& PAGE_MASK),pfnInd,(expectedPA& PAGE_MASK));
            if (paInStream)
            {
                string _descName = (_descType==DIR_STREAM)?"DirectStream desc":"IndirectStream desc";
                DPRINTF(MetaISA_LLC_Miss_Dir, "%s(%d) : llcMissPA = 0x%llx , next PA = 0x%llx , type = %s\n", _descName,i,llcMissPA, expectedPA,cmdType.c_str());
                return &(this->descripTable1[i]);
            }
        }
     }
    // Current PA doesn't belong to any of the direct stream descriptors
    DPRINTF(MetaISA_LLC_Miss_Others, "Not desc : llcMissPA = 0x%llx , type = %s \n", llcMissPA ,cmdType.c_str() );
    return nullptr; // Not found
}
*/


/**************************************************************
 *              DescTable::getStrEntMatchLLCPA
 *  Get the Stream Entry where the physical
 *      address of LLC miss belongs to its current access
 *
 * @param  llcMissPA current LLC physical address
 * @return pointer to the direct stream or nullptr if not found
 *
 * ***********************************************************/
Table1_Entry *
DescTable::getStrEntMatchLLCPA(uint64_t llcMissPA, std::string cmdType,descType &_descType , uint64_t CACHE_GRANU_MASK)
{
    for (int i = 0; i < this->tbl1EntriesNum; i++)
    {
        /* getDirStreamCrntAdress  will make sure that descriptor at i is direct stream and active */
        /* TODO: Is it better to compare PFN ONly?
            I'm afraid that "no more than one stream can be in same page"
            assumption is valid always*/
        // In Ramulator (or Memory COntroller) PA Address will be sent in LLC Granularity
        // i.e. the least 6-bits will be zero (if cache line size is 64 bit)
        // so it may be better if we compare the PFN rather than PA

        // return the descriptor type
         _descType = getDescType(i) ;
        DPRINTF(MetaISA_LLC_Miss_Dir,"descriptor %d type is %d \n",i,_descType);
        if (_descType == LOOP)
            continue; //LOOP is not a DRAM descriptor;
        string _descName = (_descType==DIR_STREAM)?"DirectStream desc":(_descType==PTR_CHASE)?"Pointer Chasing":"IndirectStream desc";
        //If it is pointer chasing , then examine LLC miss PA against PA inside the descriptor
        if(_descType==PTR_CHASE)
        {
            int  loc2 = this->descripTable1[i].extraFieldsLoc;
            Addr ptr_Paddr = this->descripTable2[loc2+PtrPAValue];
            Addr       LLCpfn, LLCOffset;
            LLCpfn     =  llcMissPA   >> PageShift       ;
            LLCOffset  =  llcMissPA   & PAGE_OFFSET_MASK ;
            // LLC Needn't to be within the node boundary but may be within the cache lien boundary
            if( ((ptr_Paddr)&~(CACHE_GRANULVL_MASK))==  ( (llcMissPA)&~(CACHE_GRANULVL_MASK)))
            {
                DPRINTF(MetaISA_LLC_Miss_Ptr, "Found %s(at descrip %d) : llcMissPA = 0x%llx , type = %s\n", _descName,i,llcMissPA,cmdType.c_str());
                return &(this->descripTable1[i]);
            }


        }
        //Either direct or Indirect Stream
        else
        {
            /*** Extract the current LLC miss PFN and Offset in PA ****/
            Addr       LLCpfn, LLCOffset;
            LLCpfn     =  llcMissPA   >> PageShift       ;
            LLCOffset  =  llcMissPA   & PAGE_OFFSET_MASK ;


            /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
            /**** Check Boundary Condition .. i.e. if exist at first or last PFN *****/
            int  loc2 = this->descripTable1[i].extraFieldsLoc;
            bool      inFirstPFN,inLastPFN,gtStartOff , ltEndOff;
            Addr firstPFNoffset = this->descripTable2[loc2+FIRST_PFN_OFFSET];
            Addr firstPFN       = this->descripTable2[loc2+FIRST_PFN]       ;
            Addr lastPFNoffset  = this->descripTable2[loc2+LAST_PFN_OFFSET ];
            Addr lastPFN        = this->descripTable2[loc2+LAST_PFN]        ;

            inFirstPFN =  (firstPFN == LLCpfn)      ; // Are they lie in the same PFN as first PFN
            inLastPFN  =  (lastPFN  == LLCpfn)      ; // Are they lie in the same PFN as first PFN
            gtStartOff =  (LLCOffset>firstPFNoffset) ; // Is the  current miss PA offset> start PFN start offset ?
            ltEndOff   =  (LLCOffset<lastPFNoffset)  ; // Is the  current miss PA offset< end   PFN end   offset ?
            /***** Implement Filteration Logic for the boundaries ****/
            DPRINTF(MetaISA_LLC_Miss_Dir,"LLC PFN (%#lx) Offset = %#lx - firstPFN(%#lx) first offset = %#lx - lastPFN (%#lx) last offset  = %#lx\n", LLCpfn , LLCOffset , firstPFN , firstPFNoffset , lastPFN   , lastPFNoffset);

            if(inFirstPFN&&inLastPFN)
            {
                if(gtStartOff&&ltEndOff)
                {
                    DPRINTF(MetaISA_LLC_Miss_Dir, "Found %s(at descrip %d) : llcMissPA = 0x%llx , type = %s\n", _descName,i,llcMissPA,cmdType.c_str());
                    return &(this->descripTable1[i]);
                }
                else //Outside the whle page representing the stream - exit
                    continue; // Not found
            }
            else
                if(inFirstPFN  )
                {
                    if(gtStartOff)
                    {
                        DPRINTF(MetaISA_LLC_Miss_Dir, "Found %s(at descrip %d)  in start PFN : llcMissPA = 0x%llx , type = %s\n", _descName,i,llcMissPA,cmdType.c_str());
                        return &(this->descripTable1[i]);
                    }
                    else
                        continue; // Not found
                }
                else
                    if(inLastPFN)
                    {
                        if(ltEndOff)
                        {
                            DPRINTF(MetaISA_LLC_Miss_Dir, "Found %s(at descrip %d) in Last PFN : llcMissPA = 0x%llx , type = %s\n", _descName,i,llcMissPA,cmdType.c_str());
                            return &(this->descripTable1[i]);
                        }
                        else
                            continue; // Not found
                    }


            /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

            Addr pfn ;
            bool      samePFN;

            for(int pfnInd = 0 ; pfnInd<PFNs_Per_STREAM ; pfnInd++ )
            {
                uint64_t expectedPfn;
                bool expectedPAStatus =  getStrExpectedPfnAtLLC(loc2,pfnInd,expectedPfn,_descType);

                if ( (_descType == LOOP) || !(expectedPAStatus))
                    continue;   //Current Descriptor is not direct stream @todo : make it as pramter than return address
                // Check if the next miss in the same page (or @todo : check if same PA masking the cache bits)
                //DPRINTF(MetaISA_LLC_Miss_Dir,"llcmissPA =%#lx , expectedPfn=%#lx\n",llcMissPA,expectedPfn);

                //Only check if they lie in the same page


                //if the expected page is the last page , then check also the offset.
                pfn        =  expectedPfn     ;

                samePFN    =  (pfn == LLCpfn)            ; // Are they lie in the same PFN


                DPRINTF(MetaISA_LLC_Miss_Dir,"Desctable entry(%d) pfn = %#lx  \n",i,pfn );

                if(!samePFN)
                    continue;

                DPRINTF(MetaISA_LLC_Miss_Dir, "Found %s ( table 1 entry # %d) : llcMissPA = 0x%llx , type = %s\n", _descName,i,llcMissPA,cmdType.c_str());
                return &(this->descripTable1[i]);

            }

        }
    }

    // Current PA doesn't belong to any of the direct stream descriptors
    DPRINTF(MetaISA_LLC_Miss_Others, "Not desc : llcMissPA = 0x%llx , type = %s \n", llcMissPA ,cmdType.c_str() );
    return nullptr; // Not found
}



/**************************************************************
 *              DescTable::getStrEntMatchLLCPAOpt
 *  Get the Stream Entry where the physical
 *      address of LLC miss belongs to its current access
 *      by searching TLB-1
 *
 * @param  llcMissPA current LLC physical address
 * @return pointer to the direct stream or nullptr if not found
 *
 * ***********************************************************/
Table1_Entry *
DescTable::getStrEntMatchLLCPAOpt(uint64_t llcMissPA, std::string cmdType,descType &_descType , uint32_t   metaISARequestorID, bool & is_base_addr , map< Addr ,  Addr> *tlb_inverse,uint64_t CACHE_GRANU_MASK)
{

    /*** Extract the current LLC miss PFN and Offset in PA ****/
    Addr       LLCpfn, LLCOffset;
    LLCpfn     =  llcMissPA   >> PageShift       ;
    LLCOffset  =  llcMissPA   & PAGE_OFFSET_MASK ;

    if((*tlb_inverse).find(LLCpfn)==(*tlb_inverse).end())
        return nullptr; //The PFN not in any possible stream
    Addr LLCvpn    = (*tlb_inverse)[LLCpfn];
    Addr LLCva     = (LLCvpn <<PageShift)+LLCOffset;

    for (int i = 0; i < this->tbl1EntriesNum; i++)
    {
        /* getDirStreamCrntAdress  will make sure that descriptor at i is direct stream and active */
        /* TODO: Is it better to compare PFN ONly?
            I'm afraid that "no more than one stream can be in same page"
            assumption is valid always*/
        // In Ramulator (or Memory COntroller) PA Address will be sent in LLC Granularity
        // i.e. the least 6-bits will be zero (if cache line size is 64 bit)
        // so it may be better if we compare the PFN rather than PA

        // return the descriptor type
         _descType = getDescType(i) ;
        DPRINTF(MetaISA_LLC_Miss_Dir,"descriptor %d type is %d \n",i,_descType);
        /* Only Search in Entries belong to same requestor ID */
        if(this->descripTable1[i].metaISARequestorID != metaISARequestorID)
            continue;
        if (_descType == LOOP)
            continue; //LOOP is not a DRAM descriptor;
        string _descName = (_descType==DIR_STREAM)?"DirectStream desc":(_descType==PTR_CHASE)?"Pointer Chasing":"IndirectStream desc";
        //If it is pointer chasing , then examine LLC miss PA against PA inside the descriptor
        if(_descType==PTR_CHASE)
        {
            int  loc2 = this->descripTable1[i].extraFieldsLoc;
            Addr ptr_Paddr = this->descripTable2[loc2+PtrPAValue];
            Addr       LLCpfn, LLCOffset;
            LLCpfn     =  llcMissPA   >> PageShift       ;
            LLCOffset  =  llcMissPA   & PAGE_OFFSET_MASK ;
            // LLC Needn't to be within the node boundary but may be within the cache lien boundary
            if( ((ptr_Paddr)&~(CACHE_GRANULVL_MASK))==  ( (llcMissPA)&~(CACHE_GRANULVL_MASK)))
            {
                DPRINTF(MetaISA_LLC_Miss_Ptr, "Found %s(at descrip %d) : llcMissPA = 0x%llx , type = %s\n", _descName,i,llcMissPA,cmdType.c_str());
                return &(this->descripTable1[i]);
            }


        }
        //Either direct or Indirect Stream
        else
        {

            /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
            /**** Check Boundary Condition .. i.e. if exist at first or last PFN *****/
            int  loc2 = this->descripTable1[i].extraFieldsLoc;
            Addr baseVA = this->descripTable2[loc2+BASEVA_LOC];
            Addr endVA  = this->descripTable2[loc2+ENDVA_LOC ];
            Addr mask_cache_level_bits = ~(0x3F);
            //printf("llcMissPA = %#lx req addr = %#lx - startAddr = %#lx  endAddr = %#lx  requestor id = %d\n",llcMissPA , LLCva&mask_cache_level_bits ,(baseVA&mask_cache_level_bits) , (endVA&mask_cache_level_bits),metaISARequestorID  );
            if
            ((LLCva&mask_cache_level_bits)>=(baseVA&mask_cache_level_bits)
            &&
            ((LLCva&mask_cache_level_bits)<=(endVA&mask_cache_level_bits)))
            {

                DPRINTF(MetaISA_LLC_Miss_Dir,"LLC PFN (%#lx) Offset = %#lx - found in descriptor %d\n", LLCpfn , LLCOffset ,  i);
                if((LLCva&mask_cache_level_bits)==(baseVA&mask_cache_level_bits))
                    is_base_addr = true;
                else
                    is_base_addr = false;

                return &(this->descripTable1[i]);
            }
            /***** Implement Filteration Logic for the boundaries ****/

            /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/


        }
    }

    // Current PA doesn't belong to any of the direct stream descriptors
    DPRINTF(MetaISA_LLC_Miss_Others, "Not desc : llcMissPA = 0x%llx , type = %s \n", llcMissPA ,cmdType.c_str() );
    return nullptr; // Not found
}


Table1_Entry *
DescTable::getStrEntMatchLLCPAOptPerCore(uint64_t llcMissPA, std::string cmdType,descType &_descType , uint32_t   p, bool & is_base_addr , map< Addr ,  Addr> *tlb_inverse,uint64_t CACHE_GRANU_MASK)
{

    /*** Extract the current LLC miss PFN and Offset in PA ****/
    Addr       LLCpfn, LLCOffset;
    LLCpfn     =  llcMissPA   >> PageShift       ;
    LLCOffset  =  llcMissPA   & PAGE_OFFSET_MASK ;

    // ENHANCED DEBUG: Stream Matching Diagnosis
    DPRINTF(MetaISA_DescTable, "\n=== STREAM MATCHING DEBUG ===\n");
    DPRINTF(MetaISA_DescTable, "Input: llcMissPA=%#lx cmdType=%s reqID=%u\n",
            llcMissPA, cmdType.c_str(), p);

    // Show TLB inverse state
    DPRINTF(MetaISA_DescTable, "TLB inverse state (%zu entries):\n",
            (*tlb_inverse).size());
    if ((*tlb_inverse).empty()) {
        DPRINTF(MetaISA_DescTable, "  ❌ TLB INVERSE EMPTY - All matches will fail!\n");
    } else {
        for (auto const& [pfn, vpn] : *tlb_inverse) {
            DPRINTF(MetaISA_DescTable, "  TLB-1[%#lx]=%#lx\n", pfn, vpn);
        }
    }

    if((*tlb_inverse).find(LLCpfn)==(*tlb_inverse).end()) {
        DPRINTF(MetaISA_DescTable, "❌ TLB LOOKUP FAILED: PFN %#lx not in TLB inverse\n", LLCpfn);
        DPRINTF(MetaISA_DescTable, "=== END STREAM MATCHING (TLB MISS) ===\n\n");
        return nullptr; //The PFN not in any possible stream
    }
    Addr LLCvpn    = (*tlb_inverse)[LLCpfn];
    Addr LLCva     = (LLCvpn <<PageShift)+LLCOffset;

    // ENHANCED DEBUG: Show VA reconstruction
    DPRINTF(MetaISA_DescTable, "VA reconstruction: LLCpfn=%#lx → LLCvpn=%#lx → LLCva=%#lx\n",
            LLCpfn, LLCvpn, LLCva);

    // Show descriptor table summary
    int active_streams = 0;
    for (int i = 0; i < this->tbl1EntNum[p]; i++) {
        descType type = getDescType(p, i);
        if (type == DIR_STREAM || type == INDIR_STREAM || type == PTR_CHASE) {
            active_streams++;
        }
    }
    DPRINTF(MetaISA_DescTable, "Descriptor table: %d active streams (total: %d)\n",
            active_streams, this->tbl1EntNum[p]);

    // CRITICAL DEBUG: Always print table state for processor 0
    DPRINTF(MetaISA_DescTable, "🔍 TABLE STATE: p=%d tbl1EntNum=%d active_streams=%d\n", p, this->tbl1EntNum[p], active_streams);
    DPRINTF(MetaISA_DescTable, "   lastIDPerProcesser[p]=%d\n", this->lastIDPerProcesser[p]);
    for (int i = 0; i < this->tbl1EntNum[p]; i++) {
        DPRINTF(MetaISA_DescTable, "  Desc[%d]: active=%d valid=%d type=%d\n", i,
               this->descTable1[p][i].entryCSRData.active,
               this->descTable1[p][i].entryCSRData.valid,
               this->descTable1[p][i].entryCSRData.type);
    }

    // Show stream boundaries for comparison
    DPRINTF(MetaISA_DescTable, "Stream boundaries:\n");
    for (int i = 0; i < this->tbl1EntNum[p]; i++) {
        descType type = getDescType(p, i);
        if (type == DIR_STREAM || type == INDIR_STREAM) {
            int  loc2   = this->descTable1[p][i].extraFieldsLoc;
            Addr baseVA = this->descTable2[p][loc2+BASEVA_LOC];
            Addr endVA  = this->descTable2[p][loc2+ENDVA_LOC ];
            DPRINTF(MetaISA_DescTable, "  Stream[%d]: baseVA=%#lx endVA=%#lx\n",
                    i, baseVA, endVA);
        }
    }

    for (int i = 0; i < this->tbl1EntNum[p]; i++)
    {
        /* getDirStreamCrntAdress  will make sure that descriptor at i is direct stream and active */
        /* TODO: Is it better to compare PFN ONly?
            I'm afraid that "no more than one stream can be in same page"
            assumption is valid always*/
        // In Ramulator (or Memory COntroller) PA Address will be sent in LLC Granularity
        // i.e. the least 6-bits will be zero (if cache line size is 64 bit)
        // so it may be better if we compare the PFN rather than PA

        // return the descriptor type
         _descType = getDescType(p,i) ;
        DPRINTF(MetaISA_LLC_Miss_Dir,"Checking descriptor %d: type=%d\n",i,_descType);

        if (_descType == LOOP)
            continue; //LOOP is not a DRAM descriptor;
        string _descName = (_descType==DIR_STREAM)?"DirectStream desc":(_descType==PTR_CHASE)?"Pointer Chasing":"IndirectStream desc";
        //If it is pointer chasing , then examine LLC miss PA against PA inside the descriptor
        if(_descType==PTR_CHASE)
        {
            int  loc2 = this->descTable1[p][i].extraFieldsLoc;
            Addr ptr_Paddr = this->descTable2[p][loc2+PtrPAValue];
            Addr       LLCpfn, LLCOffset;
            LLCpfn     =  llcMissPA   >> PageShift       ;
            LLCOffset  =  llcMissPA   & PAGE_OFFSET_MASK ;
            // LLC Needn't to be within the node boundary but may be within the cache lien boundary
            if( ((ptr_Paddr)&~(CACHE_GRANULVL_MASK))==  ( (llcMissPA)&~(CACHE_GRANULVL_MASK)))
            {
                DPRINTF(MetaISA_LLC_Miss_Ptr, "Found %s(at descrip %d) : llcMissPA = 0x%llx , type = %s\n", _descName,i,llcMissPA,cmdType.c_str());
                DPRINTF(MetaISA_DescTable, "✅ STREAM FOUND: descriptor %d (%s) for addr=%#lx\n",
                        i, _descName.c_str(), llcMissPA);
                DPRINTF(MetaISA_DescTable, "=== END STREAM MATCHING (SUCCESS) ===\n\n");

                // STREAM VALIDATION: Count successful match for validation
                static int ptr_match_count = 0;
                ptr_match_count++;
                DPRINTF(MetaISA_LLC_Miss_Ptr, "✅ Stream match SUCCESS: addr=%#lx → descriptor %d (PTR_CHASE, count: %d)\n",
                       llcMissPA, i, ptr_match_count);

                return &(this->descTable1[p][i]);
            }


        }
        //Either direct or Indirect Stream
        else
        {

            /*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
            /**** Check Boundary Condition .. i.e. if exist at first or last PFN *****/
            int  loc2   = this->descTable1[p][i].extraFieldsLoc;
            Addr baseVA = this->descTable2[p][loc2+BASEVA_LOC];
            Addr endVA  = this->descTable2[p][loc2+ENDVA_LOC ];
            Addr mask_cache_level_bits = ~(0x3F);
            //printf("llcMissPA = %#lx req addr = %#lx - startAddr = %#lx  endAddr = %#lx  requestor id = %d\n",llcMissPA , LLCva&mask_cache_level_bits ,(baseVA&mask_cache_level_bits) , (endVA&mask_cache_level_bits),p  );
            if
            ((LLCva&mask_cache_level_bits)>=(baseVA&mask_cache_level_bits)
            &&
            ((LLCva&mask_cache_level_bits)<=(endVA&mask_cache_level_bits)))
            {

                DPRINTF(MetaISA_LLC_Miss_Dir,"LLC PFN (%#lx) Offset = %#lx - found in descriptor %d\n", LLCpfn , LLCOffset ,  i);

                if((LLCva&mask_cache_level_bits)==(baseVA&mask_cache_level_bits))
                    is_base_addr = true;
                else
                    is_base_addr = false;

                // STREAM VALIDATION: Count successful match for validation
                static int dir_stream_match_count = 0;
                dir_stream_match_count++;
                DPRINTF(MetaISA_LLC_Miss_Dir, "✅ Stream match SUCCESS: addr=%#lx → descriptor %d (DIR_STREAM, count: %d)\n",
                       llcMissPA, i, dir_stream_match_count);

                return &(this->descTable1[p][i]);
            }
            /***** Implement Filteration Logic for the boundaries ****/

            /*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/


        }
    }

    // Current PA doesn't belong to any of the direct stream descriptors
    DPRINTF(MetaISA_LLC_Miss_Others, "Not desc : llcMissPA = 0x%llx , type = %s \n", llcMissPA ,cmdType.c_str() );
    DPRINTF(MetaISA_DescTable, "❌ NO STREAM MATCH: addr=%#lx not in any stream boundaries\n",
            llcMissPA);
    DPRINTF(MetaISA_DescTable, "=== END STREAM MATCHING (NOT FOUND) ===\n\n");

    // STREAM VALIDATION: Count no-match for validation
    static int no_match_count = 0;
    no_match_count++;
    DPRINTF(MetaISA_LLC_Miss_Dir, "❌ Stream match FAILED: addr=%#lx (count: %d)\n", llcMissPA, no_match_count);

    return nullptr; // Not found
}

/**************************************************************
 *              DescTable::getLoopStart
 *  Get the value of the start PC of the base of
 *      a loop descriptor
 * @param  idx location of the descriptor in table 1
 * @return true if successful , false otehrwise
 *
 * ***********************************************************/
bool DescTable::getLoopStartPC(int idx, uint64_t *pc)
{
    /****** (1) Make sure it is active and of loop type  */
    if (getDescType(idx) != LOOP)
        return false; /* PC can't be 0 */

    /****** (2) Now Compute the start PC           ******/
    MISA_Desc_t::DescInfo descObj = this->descripTable1[idx].entryCSRData.descInfo;
    *pc = descObj.loopDesc.headerPCOffset;
    return true;
}

/* Search for a loop descriptor entry where the current PC identifies its start */
int DescTable::getLoopDescEntry(uint64_t crntPC)
{
    for (uint32_t i = 0; i < this->getTbl1Len(); i++)
    {
        /* Each function will make sure that descriptor at i is direct stream and active */
        uint64_t loopDescPC;
        bool isLoop = this->getLoopStartPC(i, &loopDescPC);
        if (isLoop && loopDescPC == crntPC)
            return i; // &(this->descripTable1[i]);
    }
    return -1; // Not found
}

/**************************************************************
 *              DescTable::incLoopDescIterNum
 *  Increment the iteration number of a specific
 *    loop descriptor
 * @param  pTbl1Ent pointer to the loop descriptor
 * @return boolean indicate if the operation succeded or not
 *
 * ***********************************************************/
bool DescTable::incLoopDescIterNum(int loopID)
{
    /* return false if the pointer is invalid or not loop descriptor */
    if (loopID == -1)
        return false;
    if (this->descripTable1[loopID].entryCSRData.type != LOOP)
        return false;
    int tbl2Idx = this->descripTable1[loopID].extraFieldsLoc;
    this->descripTable2[tbl2Idx]++;
    DPRINTF(MetaISA_IPP_LOOP, " LOOP iteration (%d) started\n", this->descripTable2[tbl2Idx]);
    return true;
}

/**************************************************************
 *              STREAM VALIDATION SUMMARY
 *  Print comprehensive stream matching statistics
 *  for InterStellar validation
 *
 * ***********************************************************/
void printStreamValidationSummary()
{
    // These are defined as static in the function scope, so we need to access them differently
    // For now, we'll rely on the printf statements in the matching functions
    DPRINTF(MetaISA_DescTable, "\n");
    DPRINTF(MetaISA_DescTable, "==========================================\n");
    DPRINTF(MetaISA_DescTable, "🎯 INTERSTELLAR 2.0 - STREAM VALIDATION SUMMARY\n");
    DPRINTF(MetaISA_DescTable, "==========================================\n");
    DPRINTF(MetaISA_DescTable, "Individual match/fail events printed above\n");
    DPRINTF(MetaISA_DescTable, "Stream matching is working if you see ✅ SUCCESS messages\n");
    DPRINTF(MetaISA_DescTable, "Stream matching needs work if you see many ❌ FAILED messages\n");
    DPRINTF(MetaISA_DescTable, "==========================================\n");
    DPRINTF(MetaISA_DescTable, "\n");
}

// Global counters for validation (will be referenced by the print statements above)
// These are extern references to the static counters in the function
int dir_stream_match_count = 0;
int ptr_match_count = 0;
int no_match_count = 0;
