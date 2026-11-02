# 01 — Motivation, Empirical Evidence & Central Hypothesis

**Document:** `docs/final_proposal/01_motivation_and_hypothesis.md`  
**Project:** COMPASS v2 — Bounded Stream Preparation and Delivery with Amortised Request Tracking  
**Target Venues:** ISCA / MICRO / ASPLOS / HPCA  
**Status:** Refined Architectural Proposal & Experimental Specification (Aligned with 26 Oct Research Priority)

## 1. Problem Formulation: Concurrency Limits in Coordinated Memory Systems

Modern multicore systems face a compound memory-hierarchy bottleneck: processor performance is simultaneously restricted by processor-visible cache-miss/interconnect latency and by DRAM row-buffer conflicts and bank contention. Prior work progressed in two steps:

1. **InterStellar (MemSys'25):** Introduced a non-intrusive hardware/software co-design in which the compiler communicates loop and stream descriptors (direct and indirect streams) via standard RISC-V CSR writes (`csrrw`), and a central hardware engine (`Nucleus`) associates LLC misses with their originating streams via an inverse translation table (`Stream TLB^-1` / `srTLB`). At the memory controller, **Intelligent Request Batching (`iBatch`)** proactively generates row-local stream accesses into per-bank buffers (`iBuffer`), while the **Intelligent Page Policy (`iPP`)** manages row residency per stream using expected page-hit counts (`PHits` vs. `MAX/Page`) and sliding conflict windows (`WHits`).
2. **COMPASS (IEEE TC):** Extended InterStellar's stream abstraction upward to coordinate an **Intelligent Cache Prefetcher (`iPF`)** at the LLC with the **Intelligent Memory Controller (`iMC`)** through a shared `iCoordinator`. The `iPF` prefetches predictable direct-stream lines into a dedicated LLC-side `iPF Buffer` to hide LLC-miss and NoC traversal latency, while the `iMC` forms DRAM-local batches and merges overlapping requests so that duplicate DRAM transactions are avoided.

### 1.1 The Exposed Bottleneck: Per-Line Request Tracking Under Constrained Concurrency and Long Paths

While current COMPASS already includes controller-generated batching (`iBatch`), frontier feedback, hierarchy-wide presence filtering, promotion, and stream-lifetime handling, moving prepared data from the `iMC` to the `iPF Buffer` still relies on **conventional per-line cache-side pulls**. Specifically, each speculative stream transfer initiated from the LLC side requires:
- Issuing an individual cache-line request message across the interconnect, and
- Allocating an individual LLC Miss Status Holding Register (MSHR) from start of request until data fill completion.

When cache-side concurrency is constrained (e.g., 16–32 LLC MSHRs across 8 cores) or when the round-trip path between the LLC and the memory controller increases (e.g., 80–120 cycles each way, representative of multi-die/chiplet interconnects or extended fabric paths), per-line MSHR occupancy becomes a primary throughput limiter. By Little's Law, the sustainable rate of ordinary line pulls is upper-bounded by $M_{\text{pull}} / L_{\text{MSHR}}$, where $M_{\text{pull}}$ is the MSHR capacity available to pulls and $L_{\text{MSHR}}$ is the average allocation-to-release lifetime. Even when useful stream data have already been prepared and sit ready in the `iMC` per-bank `iBuffer`, **MSHR allocation stalls at the LLC prevent timely pulls**, leaving cores exposed to demand waiting.

## 2. Central Hypothesis and Scope

**Central Hypothesis:** COMPASS can exploit compiler-exposed stream structure to prepare useful data at the memory controller without allocating an LLC MSHR for every prepared line. A separately controlled delivery stage can then move prepared data toward the LLC (`iPF Buffer`) using bounded receiver resources and compact, credit-controlled interval authorisations.

The core research question for COMPASS v2 is whether **decoupling bounded preparation from credit-controlled interval delivery**, together with **compact stream-based transaction representation**, increases useful throughput or reduces exposed demand waiting at a defensible hardware cost.

### 2.1 Two Required Performance Comparisons

To establish a rigorous architectural contribution suitable for ISCA/MICRO/ASPLOS/HPCA, COMPASS v2 must be evaluated across two distinct comparisons:

1. **Against Tuned Standalone `iPF` (and Strengthened Prefetch Baselines):** Does cross-layer stream coordination use finite cache-side request and tracking resources more effectively than cache-only prefetching?
2. **Against Current COMPASS:** Does the proposed preparation or interval-delivery mechanism remove an additional demonstrated bottleneck beyond what existing `iBatch`, frontier feedback, and presence filtering already achieve?

The second comparison measures the true incremental value of COMPASS v2. A larger COMPASS-over-`iPF` gain on long paths does not by itself prove that current COMPASS needs a new delivery protocol; the evaluation must demonstrate that interval delivery (and/or extended independent preparation) improves upon **unmodified current COMPASS** or achieves equivalent performance at demonstrably lower tracking cost.

### 2.2 Prototype Scope and Staged Validation

To isolate architectural mechanisms cleanly without conflating protocol bugs with performance effects, COMPASS v2 follows a strict staged scope:
- **Initial Prototype Scope:** Start with fixed preparation windows ($K$ segments), fixed delivery intervals, and explicit transport/slot credits on **private, read-only direct streams with mapped pages** (e.g., `ddot`).
- **Extension to Writes:** Extend to read-modify-write streams (`daxpy`, `dscal`) and kernels with output updates (`mvt`) **only after** the receive, invalidation, and demand-merging paths are validated. A read-only descriptor for one core does not guarantee that another agent cannot write the same physical line.
- **Fallback Guarantee:** Preserve ordinary architectural cache and coherence behavior for all unsupported or non-stream accesses.

## 3. What Current Empirical Evidence Supports

The table below summarizes the verified diagnostic evidence from completed `compass_runs` (`RESULTS_LOG.md`) and subsequent diagnosis through 26 October. Consistent with rigorous scientific reporting, these observations are treated as the empirical baseline motivation—not as claims that COMPASS v2 extensions are already complete.

| Empirical Observation | Supported Architectural Interpretation | Open Research Question for COMPASS v2 |
| :--- | :--- | :--- |
| Current COMPASS approximately preserves its speed at 16 LLC MSHRs where standalone `iPF` needs substantially more | Controller-side preparation (`iBatch`) reduces dependence on LLC request capacity | How much of the new design's gain requires additional tracking state elsewhere (bitmaps, descriptors, ports)? |
| At 8 cores / 2 channels and 120 cycles each way, reported margins over best single mechanism are **+19.0% for `ddot`** and **+16.8% for `daxpy`** | Long paths expose an operating point where cross-layer coordination provides appreciable speed gains | Are the remaining stalls caused by preparation limits, individual line-pull MSHR stalls, or another shared resource? |
| `mvt`, `bicg`, and `gemver` gain little at that long-path operating point | These program windows may already have little exposed supply latency | Do demand latency tails, column-walk phases, or other windows behave differently? |
| Diagnosis at 16–32 MSHRs on the 120-cycle path shows **MSHR admission accounts for much of the measured lateness** | Establishes a concrete concurrency-limited operating point where data are ready at `iMC` but blocked at LLC admission | Can compact interval delivery relieve admission stalls without allocating 64+ conventional MSHRs? |
| The separate 64-entry prefetch-MSHR pool (`plan_B_delivery.md`) helps both `iPF` and COMPASS | Extra request-tracking capacity is a strong baseline and confirms MSHR admission is the bottleneck | Does the actual interval protocol match or beat the prefetch-MSHR pool via amortised representation and fewer request messages? |
| Controller scheduling fixes improve the `mvt`/`chase` mix (reported `chase` progress **0.61 to 0.97 of COTS**), while column-walk interference remains unresolved | Controller defaults improve pointer-chase fairness in row-friendly mixes, but do not provide a general fairness guarantee | Where do affected requests wait during column walks, including after moving from `readq` to `actq` and across write-mode periods? |
| Private L2s show little change in tested 8-core comparisons, and the corrected cores-per-channel sweep shows modest gains without the old sharp peak | Shared L2 topology is a reduced concern at 8 cores, and pre-fix artefacts are excluded | Which exact shared resource governs scaling at 16 cores (4 channels) and 32 cores? |

### 3.1 Role of the Separate Prefetch-MSHR Pool (`plan_B_delivery.md`)

The separate 64-entry prefetch-MSHR pool evaluated in `plan_B_delivery.md` is a **capacity diagnostic**, not the COMPASS v2 architecture. Because it allocates conventional per-line MSHRs, does not implement interval authorisation or amortised tracking, and does not test the removal of per-line request messages, the fact that both `iPF` and COMPASS recover performance with 64 extra prefetch MSHRs is **not a stopping rule** for the interval protocol. Rather, it proves that MSHR concurrency is the limiting bottleneck and serves as a mandatory iso-capacity comparison baseline.

## 4. Analytical Model of Controlled Quantities

COMPASS v2 explicitly distinguishes and controls five quantities across the cache-memory boundary. Conflating these quantities leads to flawed experimental attribution.

| Controlled Quantity | Architectural Definition | Primary Governing Constraints |
| :--- | :--- | :--- |
| **1. Preparation Horizon ($K$)** | Future stream segments (row/page-bounded) eligible for DRAM preparation at `iMC` | Valid page translations (`srTLB`), controller buffer/queue capacity, useful demand, inter-stream interference |
| **2. Delivery Horizon ($W_{\text{del}}$)** | Future stream positions (in distinct cache lines) authorised for movement toward the LLC | Core consumption lead, `iPF` receiver slot capacity, interconnect transport bandwidth |
| **3. Batch Width ($B$)** | Requests grouped within an active DRAM row for row-efficient service | DRAM row/page boundaries (`MAX/Page`), bank parallelism, per-bank fairness |
| **4. Outstanding Transaction Capacity** | Transfers admitted into the hierarchy but not yet completed | Conventional LLC MSHRs, or COMPASS v2 interval descriptors, bitmaps, and transport credits |
| **5. Resident Data Capacity** | Completed cache lines retained in staging storage awaiting consumption | `iMC` per-bank `iBuffer` capacity and `iPF` buffer capacity / replacement policy |

### 4.1 Position Accounting Rules
- **Distinct Cache-Line Positions:** All horizons and intervals are measured in **descriptor-relative distinct cache-line positions within an epoch**. Do not equate element count with cache-line count when multiple array elements share a 64-byte line (e.g., eight 8-byte doubles per line).
- **Scattered Physical Pages:** Physical addresses need not increase monotonically with logical stream position across page boundaries. Therefore, preparation segments and initial delivery intervals are bounded by actual mapped page/row boundaries.
- **Decoupling Batch Width from Preparation Horizon:** Increasing batch width $B$ within the currently open row is distinct from advancing the preparation horizon $K$ across future row or page segments. Likewise, a delivery authorisation may cover stream positions whose DRAM reads are still pending; authorisation extent is not evidence of DRAM completion.

### 4.2 Lead-Time and Rate Bounds

For a steady stream consuming $r$ distinct cache lines per cycle, avoiding exposed demand stalls requires maintaining sufficient lead in both stages:

$$\text{Delivery Lead} \ge r \cdot L_{\text{pull}}$$

$$\text{Preparation Lead} \ge r \cdot (L_{\text{prepare}} + L_{\text{pull}})$$

where:
- $L_{\text{pull}}$ is the complete ready-data pull path latency (including request/authorisation transport, controller lookup, and return data transport).
- $L_{\text{prepare}}$ is the controller-side time required to make eligible, translated stream work ready in the `iMC` buffer (including queueing in `readq`/`actq`, DRAM `tRP + tRCD + tCL`, and control/translation availability).

Under **ordinary per-line pulls** (current COMPASS and `iPF`), the useful delivery rate is constrained by the minimum of four ceilings:

$$\text{Useful Delivery Rate}_{\text{line}} \le \min\!\left( \frac{M_{\text{pull}}}{L_{\text{MSHR}}},\; R_{\text{prep}},\; \frac{BW_{\text{payload}}}{\text{LineSize}},\; R_{\text{fill}} \right)$$

where $M_{\text{pull}}$ is the MSHR capacity actually available to stream pulls, $L_{\text{MSHR}}$ is their average allocation-to-release lifetime (accounting separately for ready-at-`iMC` pulls, pending-DRAM pulls, and demand misses rather than applying a ready-pull average to all traffic), $R_{\text{prep}}$ is the useful controller preparation rate, $BW_{\text{payload}}$ is available data link bandwidth, and $R_{\text{fill}}$ is the receiver fill rate.

Under the **COMPASS v2 interval protocol**, the per-line MSHR ceiling $M_{\text{pull}} / L_{\text{MSHR}}$ is replaced by credit-controlled flow control:

$$\text{Useful Delivery Rate}_{\text{interval}} \le \frac{C_{\text{transport}}}{L_{\text{credit}}}$$

where $C_{\text{transport}}$ is the number of reusable transport/slot credits and $L_{\text{credit}}$ is the average credit lifetime (including forward data transport, receiver acceptance/consumption, and explicit credit-replenishment message latency). An 8-credit interval is not automatically equivalent to 32 simultaneous line transactions; data in flight still require destination slot reservations and completion identity.

## 5. Positioning and Novelty Relative to Prior Work

To pass scrutiny at ISCA, MICRO, ASPLOS, or HPCA, COMPASS v2 must state its novelty precisely without overclaiming existing COMPASS mechanisms or ignoring prior memory-system literature:

1. **What Already Exists in InterStellar and Current COMPASS (Baseline):**
   - Compiler-generated CSR loop/stream descriptors (`csrrw`), `iCoordinator`/`Nucleus`, and `srTLB` (`Stream TLB^-1`).
   - Controller-side batch generation (`iBatch`), per-stream row policy (`iPP`), frontier feedback, hierarchy-wide presence filtering, promotion, and stream-lifetime handling.
   - *Rule:* Independent preparation, frontier feedback, or descriptor flushing cannot be claimed as novel merely by renaming existing functionality.
2. **Required External Prior-Art Comparisons:**
   - **Bank Parallelism & Prefetch/MC Integration — [BAPI / BPMRI](https://hps.ece.utexas.edu/pub/lee_micro09.pdf) (Lee et al., MICRO 2009):** Explored bank-level parallelism awareness and prefetch-aware memory controllers, but operated on conventional per-line cache prefetch requests and MSHRs.
   - **Coordinated Prefetch Throttling & Resource Control — [HPAC](https://people.inf.ethz.ch/omutlu/pub/coordinated-prefetching_micro09.pdf) (Ebrahimi et al., MICRO 2009) and [CBP](https://research.chalmers.se/en/publication/529211):** Throttle hardware prefetcher aggressiveness to manage inter-core interference, without replacing per-line cache-to-MC request tracking or staging multi-segment preparation at the controller.
   - **Pushed Prefetching — [AMD Pushed-Prefetch Patent Application](https://patents.google.com/patent/US20240111678A1/en) (US20240111678A1):** Describes controller/memory-pushed prefetches, but in its disclosed example acquires conventional per-line cache-side MSHRs to track incoming prefetches.
   - **Stream & Descriptor Interfaces — [Intel DSA](https://www.intel.com/content/www/us/en/developer/articles/technical/accelerate-memory-bandwidth-bound-kernels-with-dsa.html) and [Snitch SSRs](https://arxiv.org/abs/2002.10143) (Schuiki et al., 2020):** Establish that encoding multiple memory operations in a stream descriptor is well-known.
3. **The Specific COMPASS v2 Contribution Hypothesis:**
   - The potential contribution is **not** stream descriptors alone or generic controller batching, but the **specific bounded two-stage architecture, compact interval/bitmap representation, and credit-controlled delivery protocol** that enables useful controller preparation and LLC delivery beyond the concurrency limit of conventional per-line LLC MSHRs—while preserving demand merging, cache coherence, and bounded receiver state at a fully disclosed hardware cost.
