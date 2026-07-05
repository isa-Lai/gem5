/**
 * @author
 *  Abdelrhman Abotaleb - April 2025
 * @file
 * Describes a stream aware prefetcher.
 */

 #ifndef __MEM_CACHE_PREFETCH_INTERSTELLAR_HH__
 #define __MEM_CACHE_PREFETCH_INTERSTELLAR_HH__

 #include <string>
 #include <unordered_map>
 #include <vector>

 #include "interstellar/baseInterstellarEngine.hh"
 #include "interstellar/RiscvMetaISAEngine.hh"
 #include "base/cache/associative_cache.hh"
 #include "base/sat_counter.hh"
 #include "base/types.hh"
 #include "mem/cache/prefetch/queued.hh"
 #include "mem/cache/replacement_policies/replaceable_entry.hh"
 #include "mem/cache/tags/indexing_policies/set_associative.hh"
 #include "mem/cache/tags/tagged_entry.hh"
 #include "mem/packet.hh"
 #include "params/InterStellarPrefetcherHashedSetAssociative.hh"
 #include "interstellar/base_metaisa.hpp"
 #include "interstellar/prefetcher/IDirStreamBuffer.h"
 #include "interstellar/prefetcher/iPrefetcherCircBuffer.h"

 namespace gem5
 {

 namespace replacement_policy
 {
     class Base;
 }
 struct InterStellarPrefetcherParams;

 namespace prefetch
 {

 /**
  * Override the default set associative to apply a specific hash function
  * when extracting a set.
  */
 class InterStellarPrefetcherHashedSetAssociative : public TaggedSetAssociative
 {
   protected:
     uint32_t extractSet(const KeyType &key) const override;
     Addr extractTag(const Addr addr) const override;

   public:
     InterStellarPrefetcherHashedSetAssociative(
         const InterStellarPrefetcherHashedSetAssociativeParams &p)
       : TaggedSetAssociative(p)
     {
     }
     ~InterStellarPrefetcherHashedSetAssociative() = default;
 };

 class InterStellar : public Queued
 {
   protected:
    struct iStats : public statistics::Group
    {
        iStats(statistics::Group *parent);
        // STATS
        statistics::Scalar pfdemandHitinIbufferEarly;
        statistics::Scalar pfdemandMSHRHitinIbuffer ;
        statistics::Scalar tot_req_iPrefteched;
        statistics::Scalar tot_iHWP_full;
    } stats_ihwp;
      /**
      * The latency of data access of an iHWP. It occurs when there is
      * an access to the iHWP.
      */
     const Cycles iHWP_data_latency         ;
     bool  iHWP_work_parallel_to_Cache  ;
     BaseInterstellarEngine *     interstellar_nucleus ;
     /** Initial confidence counter value for the pc tables. */
     const SatCounter8 initConfidence;

     /** Confidence threshold for prefetch generation. */
     const double threshConf;

     const bool useRequestorId;

     const int degree;

     /** How far ahead of the demand stream to start prefetching.
      *
      * Skip this number of strides ahead of the first identified
      * prefetch, then generate `degree` prefetches at `stride`
      * intervals. A value of zero indicates no skip.
      */
     const int distance;

     /**
      * Information used to create a new PC table. All of them behave equally.
      */
     const struct PCTableInfo
     {
         const int assoc;
         const int numEntries;

         TaggedIndexingPolicy* const indexingPolicy;
         replacement_policy::Base* const replacementPolicy;

         PCTableInfo(int assoc, int num_entries,
                     TaggedIndexingPolicy* indexing_policy,
                     replacement_policy::Base* repl_policy)
           : assoc(assoc), numEntries(num_entries),
             indexingPolicy(indexing_policy), replacementPolicy(repl_policy)
         {
         }
     } pcTableInfo;

     /** Tagged by hashed PCs. */
     struct InterStellarEntry : public TaggedEntry
     {
         InterStellarEntry(const SatCounter8& init_confidence, TagExtractor ext);

         void invalidate() override;

         Addr lastAddr;
         int stride;
         SatCounter8 confidence;
     };
     using PCTable = AssociativeCache<InterStellarEntry>;
     std::unordered_map<int, std::unique_ptr<PCTable>> pcTables;

     /**
      * If this parameter is set to true, then the prefetcher will operate at
      * the granularity of cache line. Otherwise it would operate on the
      * granularity of word addresses
      */
     const bool useCachelineAddr;

     /**
      * Try to find a table of entries for the given context. If none is
      * found, a new table is created.
      *
      * @param context The context to be searched for.
      * @return The table corresponding to the given context.
      */
     PCTable& findTable(int context);

     /**
      * Create a PC table for the given context.
      *
      * @param context The context of the new PC table.
      * @return The new PC table
      */
     PCTable& allocateNewContext(int context);
     /***  Abotaleb : Direct Stream InterStellar Table  *******/
     IDirStreamBuffer * iDirStreamBuffer;
     gem5::prefetch::circBuffer  < gem5::prefetch::IpreftecherQueueKey, gem5::prefetch::IpreftecherQueueEntry> * iPrefetcherCacheBuffer  ;

   public:
     InterStellar(const InterStellarPrefetcherParams &p);

     void calculatePrefetch(const PrefetchInfo &pfi,
                            std::vector<AddrPriority> &addresses,
                            const CacheAccessor &cache) override;

     bool calculatePrefetch(const PacketPtr &pkt,
                            std::vector<AddrPriority> &addresses,
                            const CacheAccessor &cache) override;
     bool squashInternalBuffer(const PacketPtr &pkt) override;

     bool handleFill(const PacketPtr &pkt ,  CacheBlk *blk) override;

     bool  isIntelligentHWP()  override
     {
        return true;
     }
     bool updateiHWPEntry(PacketPtr pkt , CacheBlk* &blk) override;
     bool deleteiHWPEntry(const PacketPtr &pkt)      override;
     bool hitIniHWP(PacketPtr pkt) override;
     Cycles get_iHWP_data_latency()   override;
     bool  is_iHWP_work_parallel_to_Cache()   override;

 };

 } // namespace prefetch
 } // namespace gem5

 #endif // __MEM_CACHE_PREFETCH_INTERSTELLAR_HH__
