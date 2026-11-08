/**
 * @file
 * InterStellar Prefetcher template instantiations.
 */

 #include "interstellar/prefetcher/interstellar.hh"

#include <algorithm>
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

InterStellar::InterStellarEntry::InterStellarEntry(
    const SatCounter8 &init_confidence, TagExtractor ext)
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
       threshConf(p.confidence_threshold / 100.0),
       useRequestorId(p.use_requestor_id),
       degree(p.degree),
       distance(p.distance),
       pcTableInfo(p.table_assoc, p.table_entries, p.table_indexing_policy,
                   p.table_replacement_policy),
       stats_ihwp(this),
       useCachelineAddr(p.use_cache_line_address),
       intervalW(p.intervalW),
       initCredits(p.credits),
       cellModeStr(p.cellMode),
       cellMode(p.cellMode.empty() ? 'A' : p.cellMode[0])
 {
    iDirStreamBuffer       = new IDirStreamBuffer();
    iPrefetcherCacheBuffer = new gem5::prefetch::circBuffer<gem5::prefetch::IpreftecherQueueKey, gem5::prefetch::IpreftecherQueueEntry>();
    iPrefetcherCacheBuffer->setCircBufferSize( queueSize   );

    sTotal = queueSize > 0 ? queueSize : 64;
    sFree = sTotal;
    sReserved = 0;
    sResident = 0;
    creditsTotal = initCredits > 0 ? initCredits : 16;
    creditsAvailable = creditsTotal;
    assert(sFree + sReserved + sResident == sTotal);

    cout << "iHWP Configs: ihwp_data_latency =" << iHWP_data_latency
         << ", is Parallel to Cache = " << iHWP_work_parallel_to_Cache
         << ", cellMode=" << cellMode << ", intervalW=" << intervalW
         << ", credits=" << creditsTotal << endl;
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
               "number of useful prefetches where demand requests hits in "
               "iHWP Buffer before creating MSHR"),
      ADD_STAT(pfdemandMSHRHitinIbuffer, statistics::units::Count::get(),
               "number of useful prefetches where demand requests that "
               "coleased in MSHR and later hits in iHWP Buffer"),
      ADD_STAT(tot_req_iPrefteched, statistics::units::Count::get(),
               "Number of generated interstellar hardware prefetches"),
      ADD_STAT(tot_iHWP_full, statistics::units::Count::get(),
               "Number of times iHWP got full"),
      ADD_STAT(tot_iHWP_dropped, statistics::units::Count::get(),
               "Number of iHWP buffer entries released because Queued dropped "
               "the prefetch candidate"),
      ADD_STAT(
          tot_compass_seeds, statistics::units::Count::get(),
          "Number of cell B/D interval-window preparation seeds authorised "
          "(first position of a new window, issued demand-class at the "
          "memory controller)")
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
      if (iPrefetchStep <= 0) {
          return false;
      }

      // The initial interval prototype is intentionally limited to private
      // read-only streams.  In particular, do not treat a store as a
      // coherence-safe interval authorisation.
      if ((cellMode == 'B' || cellMode == 'D') && !pkt->isRead()) {
          return false;
      }

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
        int pref_cnt = 0;
        int lookahead = (distance > 0) ? distance : 4;

        if (cellMode == 'B' || cellMode == 'D') {
            // Stage 2: Bounded Interval Delivery & Credits
            IntervalDescriptor &interval = intervalTable[iDirStreamTableKey];
            if (interval.startAddr == 0 ||
                baseAddr < interval.startAddr ||
                baseAddr >=
                    interval.startAddr + interval.windowSize * iPrefetchStep) {
                interval.startAddr = baseAddr;
                const Addr pageBase = baseAddr & ~PAGE_OFFSET_MASK;
                const Addr pageEnd = pageBase + (1ULL << PageShift);
                const uint64_t pageLines =
                    (pageEnd - baseAddr + iPrefetchStep - 1) / iPrefetchStep;
                interval.windowSize = std::min<uint32_t>(
                    {intervalW, 64, static_cast<uint32_t>(pageLines)});
                interval.authorisedBitmap = 0;
                interval.completedBitmap = 0;
                interval.residentBitmap = 0;
                interval.demandWaitBitmap = 0;
            }

            const uint32_t winLimit = interval.windowSize;
            for (int i = 0; i < winLimit; i++) {
                if (!(interval.authorisedBitmap & (1ULL << i))) {
                    if (creditsAvailable == 0 || sFree == 0) {
                        break;
                    }
                    if (iPrefetcherCacheBuffer->IsFull()) {
                        stats_ihwp.tot_iHWP_full++;
                        break;
                    }

                    preftchedAddress =
                        interval.startAddr + (lookahead + i) * iPrefetchStep;
                    IpreftecherQueueKey iPrefetcherQkey(preftchedAddress,
                                                        streamID, requestorID);

                    if (!iPrefetcherCacheBuffer->find(iPrefetcherQkey)) {
                        interval.authorisedBitmap |= (1ULL << i);
                        creditsAvailable--;
                        sFree--;
                        sReserved++;
                        assert(sFree + sReserved + sResident == sTotal);

                        addresses.push_back(AddrPriority(preftchedAddress, 0));
                        IPreftecherQueueEntry iPreftchQEnt;
                        iPreftchQEnt.W = 1;
                        iPreftchQEnt.R = 0;
                        ++stats_ihwp.tot_req_iPrefteched;
                        iPrefetcherCacheBuffer->Push(
                            std::make_pair(iPrefetcherQkey, iPreftchQEnt));
                        pref_cnt++;

                        // COMPASS v2 cell B/D preparation-seed charging:
                        // offset 0 is the first authorised position of the
                        // new window, i.e. the position whose demand miss
                        // would have seeded iMC preparation in cell A. The
                        // packet issued for it must reach the memory
                        // controller as demand-class, so record its (block
                        // aligned) address for the Queued issue path.
                        // Page bound: never seed beyond the window's page.
                        if (i == 0) {
                            const Addr seedPageBase =
                                interval.startAddr & ~PAGE_OFFSET_MASK;
                            const Addr seedPageEnd =
                                seedPageBase + (1ULL << PageShift);
                            if (preftchedAddress < seedPageEnd) {
                                pendingSeedAddrs.insert(preftchedAddress &
                                                        ~(Addr)(blkSize - 1));
                            }
                        }
                    } else {
                        // Already in buffer: mark authorised in interval bitmap so we don't
                        // retry it, but do NOT consume a credit or slot reservation.
                        interval.authorisedBitmap |= (1ULL << i);
                    }
                }
            }
            DPRINTF(HWPrefetch,
                    "[IntervalDeliv Cell %c] %d entries authorised "
                    "(credits=%d, sRes=%d, sFree=%d)\n",
                    cellMode, pref_cnt, creditsAvailable, sReserved, sFree);
        } else {
            // Stage 1 / Cell A or C: Line pulls
            for (int pref_ind = 1; pref_ind < N + 1; pref_ind++) {
                // Don't generate iHWP if it is full
                if (iPrefetcherCacheBuffer->IsFull()) {
                    stats_ihwp.tot_iHWP_full++;
                    break;
                }

                preftchedAddress =
                    baseAddr + (lookahead + pref_ind - 1) * iPrefetchStep;
                IpreftecherQueueKey iPrefetcherQkey(preftchedAddress, streamID,
                                                    requestorID);

                if (!iPrefetcherCacheBuffer->find(iPrefetcherQkey)) {
                    addresses.push_back(AddrPriority(preftchedAddress, 0));
                    IPreftecherQueueEntry iPreftchQEnt;
                    iPreftchQEnt.W = 1;               // Waiting for data
                    iPreftchQEnt.R = 0;               // Not Requested Yet
                    ++stats_ihwp.tot_req_iPrefteched; // Total interstellar HWP
                                                      // in cache prefetches
                    iPrefetcherCacheBuffer->Push(
                        std::make_pair(iPrefetcherQkey, iPreftchQEnt));
                    pref_cnt++;
                }
            }
            DPRINTF(HWPrefetch,
                    "%d iHWP request entries are added to iHWP Buffer for "
                    "addr 0x%lx [Step=%d]\n",
                    pref_cnt, baseAddr, iPrefetchStep);
        }
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

            if (cellMode == 'B' || cellMode == 'D') {
                uint64_t iDirStreamTableKey =
                    (((uint64_t)streamID << METAISA_REQID_BITS) + requestorID);
                auto it = intervalTable.find(iDirStreamTableKey);
                if (it != intervalTable.end()) {
                    IntervalDescriptor &interval = it->second;
                    int streamStride = pkt->getMetaISAStride();
                    int iPrefetchStep = streamStride;
                    if (iPrefetchStep > 0) {
                        while (iPrefetchStep < blkSize) {
                            iPrefetchStep += streamStride;
                        }
                    }
                    if (iPrefetchStep > 0 && pktAddr >= interval.startAddr) {
                        int offset =
                            (pktAddr - interval.startAddr) / iPrefetchStep;
                        if (offset >= 0 && offset < 64) {
                            interval.completedBitmap |= (1ULL << offset);
                            interval.residentBitmap |= (1ULL << offset);
                        }
                    }
                }
                if (sReserved > 0) {
                    sReserved--;
                    sResident++;
                    assert(sFree + sReserved + sResident == sTotal);
                }
                if (creditsAvailable < creditsTotal) {
                    creditsAvailable++;
                }
            }

            // Check if the entry is requested
            if((*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].R==1)
            {
                (*this->iPrefetcherCacheBuffer).delete_data(iPrefetcherQkey);
                if (cellMode == 'B' || cellMode == 'D') {
                    if (sResident > 0) {
                        sResident--;
                        sFree++;
                        assert(sFree + sReserved + sResident == sTotal);
                    }
                }
                DPRINTF(HWPrefetch,
                        "Demand Request (coleased in MSHR before) Hit in "
                        "iHWP Buffer: Delete entry from iHWP (A:%x), "
                        "Len =(%d/%d)\n",
                        pktAddr, (*this->iPrefetcherCacheBuffer).getLength(),
                        (*this->iPrefetcherCacheBuffer).getMaxSize());
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
            if (cellMode == 'B' || cellMode == 'D') {
                if (sResident > 0) {
                    sResident--;
                    sFree++;
                    assert(sFree + sReserved + sResident == sTotal);
                }
            }
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
    // A demand reaching this line squashes the queued candidate; drop a
    // pending preparation-seed mark it may carry (it will never be issued).
    pendingSeedAddrs.erase(reqAddress & ~(Addr)(blkSize - 1));
    IpreftecherQueueKey   iPrefetcherQkey(reqAddress,streamID, requestorID);
    if (iPrefetcherCacheBuffer->find(iPrefetcherQkey))
    {
        const bool waiting =
            (*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].W != 0;
        (*this->iPrefetcherCacheBuffer).delete_data(iPrefetcherQkey);
        if (cellMode == 'B' || cellMode == 'D') {
            if (waiting && sReserved > 0) {
                sReserved--;
                sFree++;
                if (creditsAvailable < creditsTotal) {
                    creditsAvailable++;
                }
                assert(sFree + sReserved + sResident == sTotal);
            } else if (!waiting && sResident > 0) {
                sResident--;
                sFree++;
                assert(sFree + sReserved + sResident == sTotal);
            }
        }
      DPRINTF(HWPrefetch,
              "HWP (A:%x) is squashed as demand request comes before "
              "generating it ! iHWP[%d/%d]\n",
              reqAddress, (*this->iPrefetcherCacheBuffer).getLength(),
              (*this->iPrefetcherCacheBuffer).getMaxSize());
      return true;
    }
    else
      return  false;

 }

 bool
 InterStellar::takeSeedAddr(Addr addr)
 {
     const Addr blkAddr = addr & ~(Addr)(blkSize - 1);
     auto it = pendingSeedAddrs.find(blkAddr);
     if (it == pendingSeedAddrs.end()) {
         return false;
     }
     pendingSeedAddrs.erase(it);
     DPRINTF(HWPrefetch,
             "[CompassSeed] packet issued for seed addr %#x -> demand-class\n",
             blkAddr);
     return true;
 }

 void
 InterStellar::notifySeedIssued(const PacketPtr &pkt)
 {
     ++stats_ihwp.tot_compass_seeds;
 }

 void
 InterStellar::releaseDroppedPrefetch(const PacketPtr &pkt, Addr addr)
 {
     // If the dropped candidate carried a pending preparation-seed mark,
     // drop the mark too: no packet carrying it will be issued.
     pendingSeedAddrs.erase(addr & ~(Addr)(blkSize - 1));

     // A candidate pushed into the iHWP buffer by calculatePrefetch() was
     // dropped by Queued before reaching the prefetch queue (page crossing
     // without MMU, already in cache/MSHR, throttled): free its entry,
     // otherwise the buffer leaks until IsFull() disables prefetching.
     IpreftecherQueueKey key(addr, pkt->getMetaISAStreamID(),
                             pkt->getMetaISARequestorID());
     if (!iPrefetcherCacheBuffer->find(key)) {
         return;
     }
     const bool waiting = (*this->iPrefetcherCacheBuffer)[key].W != 0;
     iPrefetcherCacheBuffer->delete_data(key);
     if (cellMode == 'B' || cellMode == 'D') {
         if (waiting && sReserved > 0) {
             sReserved--;
             sFree++;
             if (creditsAvailable < creditsTotal) {
                 creditsAvailable++;
             }
             assert(sFree + sReserved + sResident == sTotal);
         }
     }
    ++stats_ihwp.tot_iHWP_dropped;
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
          const bool waiting =
              (*this->iPrefetcherCacheBuffer)[iPrefetcherQkey].W != 0;
          (*this->iPrefetcherCacheBuffer).delete_data(iPrefetcherQkey);
          if (cellMode == 'B' || cellMode == 'D') {
              if (waiting && sReserved > 0) {
                  sReserved--;
                  sFree++;
                  if (creditsAvailable < creditsTotal) {
                      creditsAvailable++;
                  }
                  assert(sFree + sReserved + sResident == sTotal);
              } else if (!waiting && sResident > 0) {
                  sResident--;
                  sFree++;
                  assert(sFree + sReserved + sResident == sTotal);
              }
          }
        DPRINTF(HWPrefetch,
                "HWP (A:%x) is deleted (InValidation Comes!) ! "
                "iHWP[%d/%d]\n",
                reqAddress, (*this->iPrefetcherCacheBuffer).getLength(),
                (*this->iPrefetcherCacheBuffer).getMaxSize());
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
