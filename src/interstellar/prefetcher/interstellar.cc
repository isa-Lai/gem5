/**
 * @file
 * InterStellar Prefetcher template instantiations.
 */

 #include "interstellar/prefetcher/interstellar.hh"

 #include <cassert>

 #include "base/intmath.hh"
 #include "base/logging.hh"
 #include "base/random.hh"
 #include "base/trace.hh"
 #include "debug/HWPrefetch.hh"
 #include "mem/cache/replacement_policies/base.hh"
 #include "params/InterStellarPrefetcher.hh"

 namespace gem5
 {

 namespace prefetch
 {

 InterStellar::InterStellarEntry::InterStellarEntry(const SatCounter8& init_confidence,
                                  TagExtractor ext)
   : TaggedEntry(), confidence(init_confidence)
 {
     registerTagExtractor(ext);
     invalidate();
 }

 void
 InterStellar::InterStellarEntry::invalidate()
 {
     TaggedEntry::invalidate();
     lastAddr = 0;
     stride = 0;
     confidence.reset();
 }

 InterStellar::InterStellar(const InterStellarPrefetcherParams &p)
   : Queued(p),
     iHWP_data_latency(p.iHWP_data_latency),
     iHWP_work_parallel_to_Cache(p.iHWP_work_parallel_to_Cache),
     interstellar_nucleus(p.interstellar_nucleus),
     initConfidence(p.confidence_counter_bits, p.initial_confidence),
     threshConf(p.confidence_threshold/100.0),
     useRequestorId(p.use_requestor_id),
     degree(p.degree),
     distance(p.distance),
     pcTableInfo(p.table_assoc, p.table_entries, p.table_indexing_policy,
                 p.table_replacement_policy),
     stats_ihwp(this),
     useCachelineAddr(p.use_cache_line_address)
 {
    iDirStreamBuffer       = new IDirStreamBuffer();
    iPrefetcherCacheBuffer = new gem5::prefetch::circBuffer<gem5::prefetch::IpreftecherQueueKey, gem5::prefetch::IpreftecherQueueEntry>();
    iPrefetcherCacheBuffer->setCircBufferSize( queueSize   );
    //printf("iHWP Configs: ihwp_data_latency = %d , is Parallel to Cache = %d \n",idata_latency, iHWP_work_parallel_to_Cache);
    cout<<"iHWP Configs: ihwp_data_latency ="<<iHWP_data_latency<<", is Parallel to Cache = "<<iHWP_work_parallel_to_Cache<<endl;
 }
 Cycles  InterStellar::get_iHWP_data_latency()
 {
     return iHWP_data_latency;
 }

bool   InterStellar::is_iHWP_work_parallel_to_Cache()
{
   return iHWP_work_parallel_to_Cache;
}

 InterStellar::iStats::iStats(statistics::Group *parent)
    : statistics::Group(parent),
    ADD_STAT(pfdemandHitinIbufferEarly, statistics::units::Count::get(),
             "number of useful prefetches where demand requests hits in iHWP Buffer before creating MSHR"),
    ADD_STAT(pfdemandMSHRHitinIbuffer, statistics::units::Count::get(),
             "number of useful prefetches where demand requests that coleased in MSHR and later hits in iHWP Buffer"),
    ADD_STAT(tot_req_iPrefteched, statistics::units::Count::get(),
             "Number of generated interstellar hardware prefetches"),
    ADD_STAT(tot_iHWP_full, statistics::units::Count::get(),
             "Number of times iHWP got full")
{
}


 InterStellar::PCTable&
 InterStellar::findTable(int context)
 {
     // Check if table for given context exists
     auto it = pcTables.find(context);
     if (it != pcTables.end())
         return *(it->second);

     // If table does not exist yet, create one
     return allocateNewContext(context);
 }

 InterStellar::PCTable&
 InterStellar::allocateNewContext(int context)
 {
     std::string table_name = name() + ".PCTable" + std::to_string(context);
     // Create new table
     pcTables[context].reset(new PCTable(
         table_name.c_str(),
         pcTableInfo.numEntries,
         pcTableInfo.assoc,
         pcTableInfo.replacementPolicy,
         pcTableInfo.indexingPolicy,
         InterStellarEntry(initConfidence,
             genTagExtractor(pcTableInfo.indexingPolicy))));

     DPRINTF(HWPrefetch, "Adding context %i with stride entries\n", context);

     // return a reference to the new table
     return *(pcTables[context]);
 }
 void
 InterStellar::calculatePrefetch(const PrefetchInfo &pfi,
                                     std::vector<AddrPriority> &addresses,
                                     const CacheAccessor &cache)
{

}


/*****
*
*/
 bool
 InterStellar::calculatePrefetch(const PacketPtr &pkt,
                                     std::vector<AddrPriority> &addresses,
                                     const CacheAccessor &cache)
 {
      // Send packet to interstellar nucleus to filter it and capture stream's metadata.
      interstellar_nucleus->interstellarFilterPkt(pkt);
      //Generate HWP only if the packet corresponds to a stream
      if(pkt->getMetaISAStreamType()==descType::NONE)
        return false;

      int iPrefetchStep = 0 , streamStride = pkt->getMetaISAStride();
      iPrefetchStep = streamStride;// if req->stride is zero -> infinite loop
      if(iPrefetchStep>0)
        while (iPrefetchStep < blkSize)
            iPrefetchStep += streamStride;

      uint64_t  iDirStreamTableKey;
      uint8_t    streamID    = pkt->getMetaISAStreamID();
      uint32_t   requestorID = pkt->getMetaISARequestorID();

      iDirStreamTableKey = (( (uint64_t) streamID << METAISA_REQID_BITS)     +
                            requestorID);

      //Generate the next (N) prefetches  ; N depends on the stream ID
      int N = degree ;
    //   if(streamID==1)
    //     N=32;

      // 1- Only generate prefetches for direct stream
      if(pkt->getMetaISAStreamType() == DIR_STREAM || pkt->getMetaISAStreamType() == INDIR_STREAM)
      {
        if(iDirStreamBuffer->IDirStreamTable.find(iDirStreamTableKey)==iDirStreamBuffer->IDirStreamTable.end())
        {
            iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].active             = true;
            iDirStreamBuffer->IDirStreamTable[iDirStreamTableKey].stride             = streamStride;
        }
        Addr preftchedAddress,baseAddr = pkt->getAddr();
        int pref_cnt=0;
        for(int pref_ind = 1 ; pref_ind < N+1 ; pref_ind++)
        {
            //Don't generate iHWP if it is full
            if(iPrefetcherCacheBuffer->IsFull())
            {
              stats_ihwp.tot_iHWP_full++;
              break;
            }

            preftchedAddress  = baseAddr+pref_ind*iPrefetchStep;
            IpreftecherQueueKey   iPrefetcherQkey(preftchedAddress,streamID, requestorID);

            if (!iPrefetcherCacheBuffer->find(iPrefetcherQkey))
            {
                addresses.push_back(AddrPriority(preftchedAddress, 0));
                IPreftecherQueueEntry iPreftchQEnt;
                iPreftchQEnt.W = 1  ; // Waiting for data
                iPreftchQEnt.R = 0  ; // Not Requested Yet
                ++stats_ihwp.tot_req_iPrefteched  ; // Total interstellar HWP in cache prefetches
                iPrefetcherCacheBuffer->Push(std::make_pair(iPrefetcherQkey, iPreftchQEnt));
                pref_cnt++;
            }

        }
        DPRINTF(HWPrefetch, "%d iHWP request entries are added to iHWP Buffer for addr 0x%lx [Step=%d]\n",pref_cnt,baseAddr,iPrefetchStep);

      }
      return true;
 }

  bool
  InterStellar::handleFill(const PacketPtr &pkt ,  CacheBlk *blk)
  {
      uint8_t    streamID    = pkt->getMetaISAStreamID();
      uint32_t   requestorID = pkt->getMetaISARequestorID();


      // 1- Only generate prefetches for direct stream
      if(pkt->getMetaISAStreamType() == DIR_STREAM || pkt->getMetaISAStreamType() == INDIR_STREAM)
      {


        Addr pktAddr = pkt->getAddr();

        IpreftecherQueueKey   iPrefetcherQkey(pktAddr,streamID, requestorID);

        if (iPrefetcherCacheBuffer->find(iPrefetcherQkey))
        {
            (*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].cacheBlk = blk;
            // Debug Data
            // DPRINTF(HWPrefetch, "iHWP_Data(A:%lx):  ", pktAddr);
            // for (int ind = 0; ind < blkSize; ++ind) {
            //     printf("[%d]=%02x  ",
            //           ind, (*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].cacheBlk->data[ind]);
            // }
            // printf( "\n");
            // Mark entry as is not waiting for data
            (*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].W=0;
            // Check if the entry is requested
            if((*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].R==1)
            {
                (*this->iPrefetcherCacheBuffer).delete_data(iPrefetcherQkey);
                DPRINTF(HWPrefetch,"Demand Request (coleased in MSHR before) Hit in iHWP Buffer: Delete entry from iHWP (A:%x), Len =(%d/%d)\n",pktAddr,(*this->iPrefetcherCacheBuffer).getLength(),(*this->iPrefetcherCacheBuffer).getMaxSize() );
            }
            else
              DPRINTF(HWPrefetch, "iHWP response for addr %#x stored in internal buffer\n",pkt->getAddr());
        }
        else // If iHWP Buffer is overwritten !
          return false;


      }
      return true;
  }

 /**********
 *
 */
 bool
 InterStellar::hitIniHWP(PacketPtr pkt)
 {
    Addr       reqAddress  = pkt->getAddr();
    uint8_t    streamID    = pkt->getMetaISAStreamID();
    uint32_t   requestorID = pkt->getMetaISARequestorID();
    IpreftecherQueueKey   iPrefetcherQkey(reqAddress,streamID, requestorID);
    if (!iPrefetcherCacheBuffer->find(iPrefetcherQkey))
    {
      return false;
    }
      return true;

 }


 /**********
 *
 */
 bool
 InterStellar::updateiHWPEntry(PacketPtr pkt , CacheBlk* &iHWP_CacheBlk)
 {
    iHWP_CacheBlk = nullptr;
    // Search if the entry exists before in the iPrefetcherCacheBuffer
    // if data is valid
    // --> get the block
    // --> remove the entry
    // --> return true
    Addr       reqAddress  = pkt->getAddr();
    uint8_t    streamID    = pkt->getMetaISAStreamID();
    uint32_t   requestorID = pkt->getMetaISARequestorID();
    IpreftecherQueueKey   iPrefetcherQkey(reqAddress,streamID, requestorID);
    if (!iPrefetcherCacheBuffer->find(iPrefetcherQkey))
    {
      return false;
    }
    else
    {
        // --> mark the entry as requested
        (*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].R= 1 ;
        //Morever if it is valid , then respond
        if((*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].W ==0)
        {
            //Is not waiting for data = Has a valid data
            // Ready: take ownership of the CacheBlk from the buffer using a unique_ptr.

            // Adopt the raw pointer into a unique_ptr so it's protected even if an early return happens.
            std::unique_ptr<CacheBlk> tmpblk((*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].cacheBlk);
            (*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].cacheBlk = nullptr;
            // iHWP_CacheBlk       = new CacheBlk;
            // iHWP_CacheBlk->data = new uint8_t[blkSize];
            // for(int i =0 ;i<blkSize;i++)
            //   iHWP_CacheBlk->data[i]=(*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].cacheBlk->data[i];
            (*this->iPrefetcherCacheBuffer).delete_data(iPrefetcherQkey);
            iHWP_CacheBlk = tmpblk.release();

            //pkt->setData(iHWP_CacheBlk->data);
            DPRINTF(HWPrefetch,"A =%lx , isRead = %d , data[0..3] = %lx,%lx,%lx,%lx\n" , reqAddress , pkt->isRead(),iHWP_CacheBlk->data[0],iHWP_CacheBlk->data[1],iHWP_CacheBlk->data[2],iHWP_CacheBlk->data[3]);
            if(pkt->cmd==MemCmd::ReadReq      || pkt->cmd==MemCmd::ReadExReq||
               pkt->cmd==MemCmd::ReadCleanReq || pkt->cmd==MemCmd::ReadSharedReq)
            {
                if(pkt->isRead()) //Read
                {
                    // Whole Block (i.e. Cache Line will be read)
                    if(!(pkt->hasStaticData() || pkt->hasDynamicData()))
                    {
                        uint8_t *newData = new uint8_t[blkSize];
                        pkt->dataDynamic(newData);
                    }
                    pkt->setDataFromBlock(iHWP_CacheBlk->data, blkSize);
                    DPRINTF(HWPrefetch,"Demand Read Request (A:%lx) Hit in iHWP Buffer: Delete entry from iHWP , [%d/%d]\n",reqAddress,(*this->iPrefetcherCacheBuffer).getLength(),(*this->iPrefetcherCacheBuffer).getMaxSize() );
                }
                else
                {
                    // Write
                    // Copy the data from the packet to the cache block
                    pkt->writeDataToBlock(iHWP_CacheBlk->data, blkSize);
                    DPRINTF(HWPrefetch,"Demand Write Request (A:%lx) Hit in iHWP Buffer: Update the block , [%d/%d]\n",reqAddress,(*this->iPrefetcherCacheBuffer).getLength(),(*this->iPrefetcherCacheBuffer).getMaxSize() );

                }
            }




            return true;
        }
        else
        {
            DPRINTF(HWPrefetch,"Demand Request (A:%lx)  in iHWP Buffer but is waiting for data , [%d/%d]\n",reqAddress,(*this->iPrefetcherCacheBuffer).getLength(),(*this->iPrefetcherCacheBuffer).getMaxSize() );
        }

    }


     return false;
 }

 bool
 InterStellar::squashInternalBuffer(const PacketPtr &pkt)
 {
    Addr       reqAddress  = pkt->getAddr();
    uint8_t    streamID    = pkt->getMetaISAStreamID();
    uint32_t   requestorID = pkt->getMetaISARequestorID();
    IpreftecherQueueKey   iPrefetcherQkey(reqAddress,streamID, requestorID);
    if (iPrefetcherCacheBuffer->find(iPrefetcherQkey))
    {
      (*this->iPrefetcherCacheBuffer).delete_data(iPrefetcherQkey);
      DPRINTF(HWPrefetch,"HWP (A:%x) is squashed as demand request comes before generating it ! iHWP[%d/%d]\n",reqAddress,(*this->iPrefetcherCacheBuffer).getLength(),(*this->iPrefetcherCacheBuffer).getMaxSize() );
      return true;
    }
    else
      return  false;

 }


 bool
 InterStellar::deleteiHWPEntry(const PacketPtr &pkt)
  {
      Addr       reqAddress  = pkt->getAddr();
      uint8_t    streamID    = pkt->getMetaISAStreamID();
      uint32_t   requestorID = pkt->getMetaISARequestorID();
      IpreftecherQueueKey   iPrefetcherQkey(reqAddress,streamID, requestorID);
      if (iPrefetcherCacheBuffer->find(iPrefetcherQkey))
      {
        (*this->iPrefetcherCacheBuffer).delete_data(iPrefetcherQkey);
        DPRINTF(HWPrefetch,"HWP (A:%x) is deleted (InValidation Comes!) ! iHWP[%d/%d]\n",reqAddress,(*this->iPrefetcherCacheBuffer).getLength(),(*this->iPrefetcherCacheBuffer).getMaxSize() );
        int idx_per_stream_per_core  = MAX_STREAMS * pkt->getMetaISARequestorID() + pkt->getMetaISAStreamID();
        prefetchStats.pfInvalidationDeleteiHWPPerStreamPerCore[idx_per_stream_per_core]++;
        return true;
      }
      return false;
 }


 uint32_t
 InterStellarPrefetcherHashedSetAssociative::extractSet(const KeyType &key) const
 {
     const Addr pc = key.address;
     const Addr hash1 = pc >> 1;
     const Addr hash2 = hash1 >> tagShift;
     return (hash1 ^ hash2) & setMask;
 }

 Addr
 InterStellarPrefetcherHashedSetAssociative::extractTag(const Addr addr) const
 {
     return addr;
 }

 } // namespace prefetch
 } // namespace gem5
