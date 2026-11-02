**COMPASS v2: bounded stream preparation and delivery with amortised request tracking**

Proposed architectural direction, updated 26 October 2026. This document supersedes the priorities in [suggested_high_level_plan.md](suggested_high_level_plan.md). It proposes experiments and mechanisms; it does not claim that the extensions are implemented or that their performance or novelty has been established. The broader experiment inventory remains in [COMPASS_performance_experiment_plan.md](compass_runs/COMPASS_performance_experiment_plan.md); the staged decisions below take priority over launching that entire inventory.

**Research priority agreed on 26 October.** Architectural exploration remains the primary work for the intended ISCA submission. Existing-mechanism ablations and baseline revalidation support that work; they do not replace a test of the proposed architecture. The immediate target is one minimal, actual interval-delivery prototype, starting with current iMC preparation and a bounded private read-only scope. Complete the preparation/delivery comparison on selected operating points. The contribution remains a hypothesis until the protocol, cost, and performance are evaluated; novelty cannot be guaranteed by the submission target.

The separate prefetch-MSHR pool in [plan_B_delivery.md](plan_B_delivery.md) is a capacity diagnostic. It allocates conventional per-line MSHRs, does not implement interval authorisation or amortised tracking, and does not test removal of per-line request messages. Both policies recovering with 64 extra prefetch entries is therefore not a stopping rule for the interval protocol. Test the actual protocol before accepting or rejecting its representation and control-path hypotheses.

**1. Central hypothesis and scope**

COMPASS can exploit stream structure to prepare useful data at the memory controller without allocating an LLC MSHR for every prepared line. A separately controlled delivery stage can then move data toward the LLC using bounded receiver resources. The research question is whether this separation, together with compact stream-based transaction representation, increases useful throughput or reduces exposed demand waiting at a defensible hardware cost.

There are two performance comparisons:

- Against tuned iPF: does coordination use finite cache-side request resources more effectively?
- Against current COMPASS: does the proposed preparation or delivery mechanism remove an additional demonstrated bottleneck?

The second comparison measures the incremental value of the new mechanism. A larger COMPASS-over-iPF gain on long paths does not by itself prove that current COMPASS needs a new delivery protocol. The low-MSHR, long-path admission evidence provides a specific initial operating point for architectural exploration; 64-MSHR points remain essential controls rather than a universal prerequisite for prototyping.

Start with fixed windows and explicit credits. First validate private, read-only direct streams with mapped pages. Extend to read-modify-write streams after the receive, invalidation, and demand-merging paths are validated. A read-only descriptor for one core does not establish that another agent cannot write the same physical line. Preserve ordinary architectural behavior for unsupported accesses.

**2. What the current evidence supports**

The following is a provisional snapshot from the discussion, not a newly reproduced results table. Use [RESULTS_LOG.md](compass_runs/RESULTS_LOG.md), completed run directories, and executable/configuration hashes as the experiment record.

| Observation | Supported interpretation | Unresolved question |
|---|---|---|
| Current COMPASS approximately preserves its speed at 16 LLC MSHRs where iPF needs substantially more | Controller preparation can reduce dependence on LLC request capacity | How much of the new design's gain would require additional tracking elsewhere? |
| At 8 cores / 2 channels and 120 cycles each way, reported margins over best single are +19.0% for ddot and +16.8% for daxpy | Long paths expose an operating point where coordination provides appreciable speed gains | Are remaining stalls in preparation, individual pulls, or another shared resource? |
| mvt, bicg, and gemver gain little at that long-path point | These windows may already have little exposed supply latency | Do demand tails or other phases behave differently? |
| The corrected cores-per-channel sweep has modest gains without the old sharp peak | The pre-leak knee is not a basis for predicting large gains | Which resource actually determines each operating point? |
| Private L2s show little change in the tested 8-core comparisons | Shared L2 topology is a reduced concern for those points | Does this transfer to 32 cores and other phases? |
| Yield and age-cap tests provide little relief, and some column walkers worsen | The tested protection mechanisms do not repair the interference problem | Where do affected requests wait, including after moving to actq? |

Subsequent diagnosis through 26 October adds three findings. Admission accounts for much of the measured lateness at 16–32 MSHRs on the long path, establishing a concrete concurrency-limited exploration point. A separate conventional prefetch-MSHR pool helps both iPF and COMPASS, establishing that extra request capacity is a strong baseline. Finally, controller scheduling fixes substantially improve the mvt/chase mix (reported chase progress 0.61 to 0.97 of COTS) while column-walk interference remains unresolved. Preserve both the early measurements and these later distinctions; do not describe the new controller defaults as a general fairness guarantee.

Compare geomeans over identical completed workload sets. Do not compare a provisional six-kernel mean directly with a twelve-kernel mean, or select the best single mechanism before all required baselines finish. The 120-cycle path is a synthetic latency sensitivity; it is not a complete chiplet or CXL model.

The current implementation already includes controller-generated batching, frontier feedback, hierarchy-wide presence filtering, promotion, and stream-lifetime handling. Those provide the baseline. Independent preparation, feedback, or descriptor flushing cannot be claimed as new merely by renaming existing functionality.

**3. Distinguish the quantities being controlled**

| Quantity | Definition | Main constraint |
|---|---|---|
| Preparation horizon | Future stream positions eligible for DRAM preparation | Valid translations, controller capacity, useful demand, interference |
| Delivery horizon | Future positions authorised for movement toward the LLC | Consumption lead, receiver capacity, transport |
| Batch width | Requests grouped for row-efficient service | Row/page boundaries, bank parallelism, fairness |
| Outstanding transaction capacity | Transfers admitted but incomplete | MSHRs or alternative transaction records and credits |
| Resident data capacity | Completed lines retained for later use | iMC/iPF buffers and replacement |

A delivery authorisation may cover data that are not ready yet. Its extent is not evidence of completion. Increasing batch width within the current row is also different from advancing preparation across future row/page segments.

Use descriptor-relative distinct cache-line positions within an epoch. Physical addresses need not increase with stream position because pages may be scattered. Do not equate an element count with a distinct cache-line count when several elements share a line.

For a steady stream consuming r lines per unit time, an initial timing model is:

    delivery lead       >= r * L_pull
    preparation lead    >= r * (L_prepare + L_pull)

Here L_pull is the complete ready-data pull path, including request and return transport; L_prepare includes the time required to make eligible, translated controller work ready. Include control/translation availability and variability where they are not already represented. These are approximate lead requirements, not a policy or a promise that the requested rate is feasible.

With ordinary line pulls, a useful capacity model is:

    useful delivery rate <= min(
        M_pull / L_MSHR,
        useful controller preparation rate,
        available payload bandwidth / cache-line size,
        receiver fill rate
    )

M_pull is the capacity actually available to the relevant pulls, and L_MSHR their average allocation-to-release time. Account separately for pending-DRAM pulls, demand traffic, and other classes rather than applying a ready-pull average to all requests.

An interval protocol replaces some constraints but does not remove flow control:

    delivery rate <= reusable transport credits / average credit lifetime

Credit lifetime includes the events and feedback needed before a credit can be reused. Data in flight still require storage and identification. An eight-credit interval is not automatically equivalent to 32 simultaneous line transactions.

**4. Choose the intervention from the observed bottleneck**

| Measured condition | First intervention |
|---|---|
| Useful data ready at iMC; LLC allocation stalls prevent timely pulls | Prototype interval delivery with existing preparation |
| Many pulls arrive before data are ready, exposing DRAM waiting | Extend bounded preparation with ordinary delivery |
| Both restrictions occur | Implement both factors and quantify their interaction |
| Data ready and admission available, but delivery is slow | Investigate link, response, arbitration, and fill-path capacity |
| Demands already find data locally and resource stalls are low | Preserve this as a low-headroom control; investigate core/cache/write limits |

Long remote residency alone is not evidence of harm. Retaining far-future data at iMC can be intentional. Measure whether admission delays useful delivery, whether demands catch up, and whether a change reduces execution time. High occupancy alone does not establish a bottleneck either.

**5. Minimal architectural proposal**

Use two stages with different immediate objectives and shared constraints:

    mapped future stream segments
        -> bounded, DRAM-efficient preparation
        -> ready data at iMC
        -> authorised, credit-controlled delivery
        -> valid data in the iPF buffer
        -> ordinary demand use

Preparation seeks useful row locality and bank parallelism while meeting bounded timeliness and interference requirements. Delivery seeks local availability before demand while respecting storage and transport limits. Neither stage should maximise its local metric independently of progress at the other stage.

The preparation stage admits K future row/page segments per stream, subject to global state/data capacity and per-bank queued-row limits. A segment ends at actual mapping or translation boundaries; it need not span an entire DRAM row. Multiple queued rows for a bank do not imply multiple simultaneously open rows in that bank. Preparation can advance after admission without needing an individual new LLC response to trigger every segment.

The delivery stage authorises an interval of future positions separately from K. Keep ordinary individual pulls as the reference implementation. The candidate protocol uses a persistent interval authorisation and receiver reservations, allowing iMC to send eligible data incrementally. Calling this interval pull or authorised push is secondary to its actual allocation and completion rules. Implement one such protocol first.

A conceptual interval descriptor contains:

    address-space/context, stream ID, descriptor epoch,
    logical start position, position count,
    translated segment references,
    destination window/slot generation,
    receiver reservations and delivery eligibility

The exact encoding and sizes are design work. Do not assume a logical interval authorises reads from unmapped future pages, owns coherence permission, or permits preparation beyond the independent preparation limit.

**6. Progress, validity, and credit semantics**

Maintain separate state for planned, admitted, issued, ready, authorised for delivery, in transport, locally resident, and observed demand use. Some flags coexist; a line may be both authorised and still awaiting DRAM. Use contiguous progress counters only where their meaning is precise, with bounded bitmaps or equivalent records for gaps and current availability.

Distinguish historical completion from current residency. Eviction or invalidation can remove data below a completed-position counter. Cached or demand-supplied lines may bypass one or both speculative stages. Consequently, a global ordering of consumed, delivered, and prepared frontiers is not a universal correctness invariant. Track which positions are satisfied and by which valid path. Observed LLC use is a progress estimate, not exact knowledge of all retired core accesses.

Receiver data-slot accounting should conserve capacity:

    free slots + reserved unfilled slots + resident slots = total slots

Arrival changes a slot from reserved to resident; it does not automatically make that data slot free. Consumption, eviction, or invalidation reclaims it according to the protocol. Use separate transport permits if they can be returned on receiver acceptance. Model replenishment messages, their latency, and batching explicitly.

Completions carry enough identity to select the correct slot and epoch. Reserve destination capacity before transmission. Count rejected or cancelled transfers and release each reservation exactly once. Descriptor replacement must revoke or drain old authorisations; late packets must not populate a new descriptor's slots. State for outstanding revocations and late completions must also be bounded.

A demand arriving before delivery must find pending speculative state or fall back to the ordinary cache/coherence path. If it allocates an ordinary demand MSHR, merge or discard the speculative completion safely and wake the demand once. Handle overlapping streams by physical-line identity and context rather than treating every stream-position record as a separate required DRAM transaction.

The receiver must use a valid coherence path for supplied data, including dirty-owner sourcing or invalidation as required by the model. A snoop-filter presence test alone does not grant permission to use an old DRAM value. Initial private read-only workloads simplify validation; generalising to writes requires additional evidence.

Compact tracking is a hypothesis about representation cost. Per-line validity, completion, and demand merging may still require substantial state. Count the descriptor tables, bitmaps, range comparisons, tags, lookup ports, and demand records against the MSHRs they are intended to relieve.

**7. Isolate preparation and delivery with a 2 x 2 comparison**

| | Individual line pulls | Bounded interval delivery |
|---|---|---|
| Current preparation policy | A: current COMPASS reference | B: delivery change |
| Extended independent preparation | C: preparation change | D: combined design |

Keep the preparation eligibility algorithm and resource limits consistent within each row. An interval authorisation in B must not silently enable the extra future preparation intended for C/D. If current preparation is triggered by individual arrivals, define and charge an explicit preparation-seed interface with equivalent eligibility for both delivery variants. Validate any refactored A against unmodified current COMPASS; retain the original as an additional baseline if the refactoring changes behavior.

Keep the delivery implementation and capacity limits consistent within each column. Measure preparation trigger count, admitted logical positions, mapping availability, DRAM bytes, and preparation timing. Closed-loop execution will naturally change timing and contention. Do not force identical timestamps or DRAM traces in the main experiment; a common-input replay is an optional diagnostic.

Compare B/A, C/A, D/A, and D against both B and C using execution time. Define any interaction metric in advance, for example the factorial interaction on log execution time, rather than informally adding percentage speedups. A combined win establishes complementarity. To support a coordination-specific claim, also explain or ablate what exchanged progress, readiness, and credit information enables beyond a straightforward fixed combination. Correctness-related reservations cannot simply be disabled as an ablation.

**8. Immediate experiment sequence and benchmark allocations**

Each workload copy is an independent process with private arrays. Row/column labels refer to verified program phases, not assumed properties of an arbitrary instruction window.

| Stage | Workloads and configurations | Required output |
|---|---|---|
| Validate event timing and state | One-core ddot, descriptor-replacement microbenchmarks, then short multicore runs | Correct outputs, request visibility, completion/state/credit conservation |
| Diagnose current COMPASS | 8 x ddot, 8 x daxpy, 8 x dscal, 8 x mvt-row; 8 cores / 2 channels, 40 and 120 cycles each way, 16 and 64 MSHRs | Whether preparation, ready-data delivery, or another resource actually blocks progress |
| Read-only interval/preparation prototype | ddot first, then a validated private read-only direct-stream kernel or an eligible region with validated semantics | Ordinary demand fallback and normal data transport; no free unsolicited fills |
| Controlled 2 x 2 screen | Same anchor points on eligible workloads; then daxpy/dscal after write-path validation | Incremental gain over current COMPASS and factor interaction |
| Latency/concurrency attribution | 40, 80, 120 cycles each way; MSHRs 16,32,64,128 on informative points | Whether more MSHRs recover the baseline and whether interval delivery reduces sensitivity |
| Mixed development screen | 4 x daxpy + 4 x ddot; 4 x mvt-row + 4 x dscal; 4 x dscal + 4 x ddot | Useful throughput and progress under competing preparation/delivery rates |
| Scaling confirmation | 16 cores / 4 channels; selected 32-core points later | Paired policies with documented scaling of every intended per-core resource |
| Held-out transfer | 4 x mvt-row + 4 x jacobi-2d; 4 x jacobi-2d + 4 x atax; 4 x gemver-row + 4 x seidel-2d | Frozen settings on access patterns outside development mixes |
| Interference checks during development | 7 x daxpy + 1 x chase; 4 x mvt-column + 4 x heat-3d | No hidden severe regression to bystander or non-batchable traffic |
| Final interference validation | 7 x mvt-row + 1 x chase; 7 x bicg + 1 x chase; 4 x gemver-column + 4 x heat-3d | Per-core slowdown and latency tails with co-runners kept active |

mvt is a useful low-gain control, but its output updates mean the complete kernel is not a private read-only prototype by default. Check actual descriptors and regions. The first fdtd-2d ey-sweep window resembles daxpy, so do not count that window as independent stencil validation; select distinct phases or omit the independence claim.

Use the private-L2 setting consistently for the intended architecture and retain the inherited topology as a sensitivity. The 8-core private-L2 result does not settle every 32-core bottleneck. Scale 4+4 mixes to 8+8 at 16 cores; scale corresponding LLC MSHR budgets equally for all policies. Preserve a single bystander with 15+1 when extending that test.

Use current COTS, tuned iPF, iMC, and current COMPASS as external baselines in addition to A-D. Keep candidate queue capacity, data storage, promotion, translation assumptions, and generic demand protection explicit. Reuse results only when executable, relevant configuration, ROI, and inputs match. Incomplete baselines are pending, not poor performers.

**9. Sweep only the dimensions justified by the first results**

For preparation, start with K = {1,2,4} future segments and retain the current batch width. For delivery, explore eligibility distances {8,16,32,64} distinct lines and receiver/transport limits {8,16,32,64} where total capacity permits. These are exploratory choices, not hardware recommendations.

Vary one coordinate initially; do not cross every dimension with every workload. Report achieved depth, ready-hit rate, occupancy, and blocking reasons because a requested window may not be attainable under shared caps. Treat fixed bitmaps and descriptor-table size as hardware parameters, not unbounded simulator maps.

Start whichever of B or C the diagnosis supports, then complete the matrix on representative points. If B works with current preparation, there is no requirement to finish all preparation optimisations before evaluating it. Add adaptation, urgency prediction, or dynamic allocation only after a fixed mechanism demonstrates useful headroom and the new policy has a measured failure to address.

**10. Measurements and interpretation**

Required evidence includes execution time, useful delivered lines per second, demand coverage/timeliness, MSHR allocation stalls and lifetimes by request class, preparation and delivery event distributions, candidate queue losses, fill throughput, and physical traffic. Pair aggregate counters with short sampled traces where causal attribution is missing.

Record whether admission was blocked while eligible useful data were ready at iMC. Relate that blocking to late demands or lost throughput; do not infer benefit from shorter MSHR lifetime alone. Separate useful remote residency from discarded-before-use data. Attribute bus-idle periods to documented scheduling conditions, with overlapping causes recorded separately if needed.

Account for readq, writeq, actq, pendingActq, ingress/response queues, iBuffer, iPF data/reservation slots, interval descriptors, bitmaps, mappings, and credit/revocation records. A smaller readq does not establish smaller total controller storage. Count DRAM data, request/control messages, credit returns, invalidations, and translations separately. Payload traffic need not fall when transaction-message overhead falls.

Use paired allocator seeds for finalists and the same phase/work across policies. Keep co-runners active throughout each mix interval or report changing overlap explicitly. Report weighted speedup, maximum slowdown, and bystander latency tails, with isolated references under the corresponding policy. Keep development and held-out results separate. Validate complete reduced-size outputs, then check phase/window sensitivity on performance finalists.

**11. Correctness and modelling gates before interpreting new results**

- Request delay must represent actual controller visibility for batching/scheduling, or the approximation must be assessed explicitly. Verify idle latency and arrival/trigger timestamps; the response-time charging model alone does not establish equivalent queueing behavior.
- Oracle shadow reads need normal controller completion/state maintenance without a second architectural response. The discussed implementation used an empty callback, while iBuffer-ready bookkeeping lives in the normal callback and iBatch copies the triggering request. Audit this before using mode 2 with COMPASS. Preserve appropriate feedback and descriptor state as well.
- No accepted shadow can be silently dropped. Nevertheless, changing execution timing can change DRAM traffic and batching, so accepting every shadow is not proof of traffic equivalence. Oracles are optimistic diagnostic relaxations, not guaranteed achievable bounds or additive latency decompositions.
- A failed age cap does not rule out scheduler starvation: follow requests through readq, actq, write-mode periods, and actual DRAM commands. Ablate iPP and batching separately before inventing another protection heuristic.
- Maximum refresh queue occupancy of one is reassuring but does not prove zero postponement. Measure enqueue-to-refresh-command delays, issued refresh counts, and refresh blocking when checking main-result validity.
- Invalidation, descriptor replacement, demand catch-up, cancellation, backpressure, partial intervals, and out-of-order completion must conserve data and credits. Implement bounded state and normal cache semantics before interpreting speedups.

**12. Cost, novelty, and decision rules**

Compare equal LLC MSHR counts first, with all additional structures disclosed. Then compare against giving current COMPASS and iPF more MSHRs or other bottleneck-relieving capacity. Equal storage bits are a preliminary comparison; iso-area claims require ports, associative/range lookup, control logic, and timing costs. Energy claims require the relevant energy components; fewer ACT/PRE operations do not directly establish the same reduction in total energy.

Existing iBatch already generates controller-side work. Existing progress tracking and credit protocols also have broad precedent. The potential contribution is the specific bounded representation and protocol that enables more useful work through the cache-memory interface, while retaining demand merging and coherence at lower or more effective resource cost.

Relevant comparisons include [BAPI/BPMRI](https://hps.ece.utexas.edu/pub/lee_micro09.pdf) for bank parallelism, [HPAC](https://people.inf.ethz.ch/omutlu/pub/coordinated-prefetching_micro09.pdf) and [CBP](https://research.chalmers.se/en/publication/529211) for coordinated prefetch/resource control, and [AMD's published pushed-prefetch application](https://patents.google.com/patent/US20240111678A1/en), whose example acquires cache-side MSHRs. Broader descriptor/stream interfaces, including [Intel DSA](https://www.intel.com/content/www/us/en/developer/articles/technical/accelerate-memory-bandwidth-bound-kernels-with-dsa.html) and [Snitch SSRs](https://arxiv.org/abs/2002.10143), establish that representing multiple operations with stream configuration alone is not a sufficient novelty claim. These references define comparison areas; this document is not an exhaustive novelty review.

| Result | Decision |
|---|---|
| B improves current COMPASS with ready data and admission blocking reduced | Prioritise delivery representation and protocol cost |
| C improves readiness and exposed demand latency while B adds little | Prioritise preparation and future-page support |
| D improves beyond either factor and the exchanged information explains the result | Develop the coordinated two-stage claim |
| Gains disappear after a small, affordable baseline resource increase | Quantify efficiency; do not claim an inherent throughput advantage |
| Oracles show headroom but bounded variants do not | Investigate missing mappings, credits, feedback timing, scheduling, or capacity |
| No meaningful headroom remains relative to current COMPASS | Retain the measured current-system result and stop adding mechanisms for that workload |
| Throughput gains depend on starving another program or uncounted resources | Repair or narrow the claim before treating the result as success |

Use the existing diagnosis to begin the bounded intervention below, adding only measurements needed to resolve a specific missing causal link. No universal speedup threshold substitutes for causal evidence, a healthy baseline, credible cost, and transfer beyond a single favourable point.

**13. Architecture-first execution order**

The next deliverable is a working protocol experiment, alongside the minimum supporting ablations. Do not defer it until every original campaign cell, oracle, or controller investigation is complete. Use the validated controller default, and isolate unsupported functionality through the explicit prototype scope.

| Order | Deliverable | Architectural question answered |
|---|---|---|
| 1 | Specify a fixed interval descriptor, slot mapping, credit-return event, completion identity, and ordinary-demand fallback | What per-line request/MSHR work does this protocol actually replace, and what state replaces it? |
| 2 | Implement interval delivery using current preparation; initially private read-only mapped streams and page-bounded intervals | Can existing ready data be delivered through a compact bounded representation? |
| 3 | Compare actual interval delivery against current COMPASS, the existing prefetch-MSHR pool, and additional ordinary MSHRs | Is the gain attributable to protocol/representation or simply added capacity? |
| 4 | Add a small fixed future-segment preparation window, with explicit mapping availability and charged control traffic | Does independent preparation supply useful work the original trigger path could not expose? |
| 5 | Complete A/B/C/D on selected points, including a 64-MSHR control | Do preparation and delivery improve individually or interact, and where does the benefit stop? |
| 6 | Freeze the candidate, validate writes before using daxpy/dscal results, then evaluate held-out work, mixes, and cost | Does the mechanism transfer beyond the initial constrained read-only case? |

For order 2, retain the same iPF data-buffer capacity and model the actual request/control and response paths. A charged transport model is acceptable for an initial architectural experiment if it preserves contention and receiver backpressure; uncharged side channels and zero-time insertion are not. Page-bounded authorisation avoids introducing arbitrary future-page translation in the first delivery experiment. Handle partial intervals, out-of-order returns, descriptor replacement, demand catch-up, and invalidation within the supported scope before interpreting speedups. Explicitly identify any simulator simplifications.

The prototype must replace individual initiating requests with interval authorisation and replace speculative per-line ordinary MSHR allocation with a defined interval/receiver representation. Relabelling the separate MSHR pool as credits is not this experiment. Per-line completion/validity bits and ordinary demand MSHRs may still be required; include them in the accounting.

Start with ddot at 8 cores / 2 channels, 120 cycles each way, and 16/32 MSHRs, plus the matched 64-MSHR control. Use a private read-only direct-stream kernel if a second clean prototype workload is needed. mvt can be a later low-headroom control once its output-write behavior is supported. Repeat informative points at 40 cycles to establish path sensitivity. After correctness support, daxpy and dscal connect the protocol to the strongest existing constrained-resource results. Do not select only favourable workloads in the final report.

Compare at least these mechanisms with matching inputs, data capacity, link assumptions, and generic controller protection:

- Current COMPASS with its ordinary shared MSHRs.
- Current COMPASS with the separate conventional prefetch-MSHR pool.
- Current COMPASS with more ordinary MSHRs, including a recovery point.
- COMPASS with the actual interval protocol and its fully disclosed receiver/tracking budget.
- A strengthened iPF baseline receiving the same applicable tracking and storage resources; if the interval receiver can be shared with standalone iPF, evaluate that variant too.

The initial result may establish performance, lower-cost performance equivalence, or a negative outcome. Architectural exploration is justified by the concrete hypothesis and measured constrained-resource opportunity, not by a guaranteed speedup at 64 MSHRs. A lower-cost result can be valuable if the cost is demonstrated, while a speedup from extra uncounted tracking cannot establish the intended claim.

The supporting ablations should remeasure frontier and presence filtering on the frozen default, including their joint removal. They explain the original design and the reference point for the extension. Their completion alone does not evaluate the interval protocol. Likewise, a repaired ready-at-controller oracle retains the ordinary delivery mechanism and cannot reject every alternative representation.

Stop an implementation branch after its actual mechanism is tested and its limiting resource identified. A small gain with current preparation does not automatically reject extended preparation; a generic benefit from a conventional prefetch pool does not automatically reject amortised request tracking. Conversely, completing the prototype does not oblige the paper to claim it as successful. Separate demonstrated conclusions from the research objective.

**Provisional contribution statement, conditional on the experiments**

COMPASS exploits software-directed stream structure to coordinate DRAM-efficient preparation and timely cache-side delivery. Independently bounded windows and compact, credit-controlled transaction state allow useful preparation and transfers beyond the effective capacity of conventional per-line LLC prefetch requests. The evaluation must establish which restriction is removed, the incremental benefit over existing iBatch/COMPASS, and the storage, control, and correctness costs of the new protocol.
