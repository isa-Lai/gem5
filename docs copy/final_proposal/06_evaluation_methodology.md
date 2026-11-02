# 06 — Experimental Methodology, 2x2 Design & Staged Benchmark Plan

**Document:** `docs/final_proposal/06_evaluation_methodology.md`  
**Project:** COMPASS v2 — Bounded Stream Preparation and Delivery with Amortised Request Tracking  
**Target Venues:** ISCA / MICRO / ASPLOS / HPCA  
**Status:** Refined Experimental Design & Benchmark Campaign Specification

## 1. Isolating Preparation and Delivery: The Controlled 2 x 2 Comparison

To rigorously test the central hypothesis and determine whether performance gains come from **Stage 2 (Interval Delivery)**, **Stage 1 (Extended Independent Preparation)**, or their **synergistic interaction**, COMPASS v2 centers its experimental evaluation on a controlled $2\times 2$ factorial design (Section 7):

| Preparation Policy \ Delivery Mechanism | Column 1: Individual Line Pulls (Per-Line LLC MSHRs) | Column 2: Bounded Interval Delivery (Amortised Tracking & Credits) |
| :--- | :--- | :--- |
| **Row 1: Current Preparation Policy** (Reactive `iBatch` within current row) | **Cell A: Current COMPASS Reference** (Baseline coordinated `iPF` + `iMC`) | **Cell B: Delivery Change Only** (Interval delivery with current preparation) |
| **Row 2: Extended Independent Preparation** ($K \in \{1, 2, 4\}$ future segments) | **Cell C: Preparation Change Only** ($K$-segment preparation with individual pulls) | **Cell D: Combined COMPASS v2 Design** ($K$-segment preparation + Interval delivery) |

### 1.1 Experimental Controls Within Rows and Columns
1. **Within-Row Consistency (Preparation Control):** Keep the preparation eligibility algorithm, batch width $B$, `iBuffer` capacity, and per-bank queue limits identical across Cells A and B, and across Cells C and D.
   - **No Silent Lookahead in Cell B:** An interval authorisation in Cell B **must not** silently trigger the extra future-segment preparation intended for Cells C and D. Because current preparation in Cell A is triggered by arriving LLC requests, Cell B defines and charges an explicit **preparation-seed interface** with identical segment-trigger eligibility to Cell A.
   - **Refactoring Validation Gate:** If implementing the clean $2\times 2$ modular interface requires refactoring Cell A, the refactored Cell A must be validated against **unmodified current COMPASS** (`COMPASS_TC`). If any behavioral difference exceeds noise, retain unmodified current COMPASS as an explicit additional baseline.
2. **Within-Column Consistency (Delivery Control):** Keep the delivery implementation, `iPF Buffer` data capacity, transport link assumptions, and receiver slot limits identical across Cells A and C, and across Cells B and D.
3. **Closed-Loop Execution:** Closed-loop execution naturally changes request timing and DRAM contention across A, B, C, and D. Do not artificially force identical timestamps or replay traces in the main performance experiment (a common-input trace replay is permitted only as an optional diagnostic).

### 1.2 Pre-Defined Factorial Interaction Metric
Rather than informally adding percentage speedups, we define the $2\times 2$ main effects and interaction on **log execution time** ($\ln T$) in advance:
- **Delivery Main Effect (at current prep):** $\Delta_{\text{B/A}} = \ln(T_A / T_B)$ (and speedup $S_{B/A} = T_A / T_B$)
- **Preparation Main Effect (at line pulls):** $\Delta_{\text{C/A}} = \ln(T_A / T_C)$ (and speedup $S_{C/A} = T_A / T_C$)
- **Combined Effect:** $\Delta_{\text{D/A}} = \ln(T_A / T_D)$ (and speedup $S_{D/A} = T_A / T_D$)
- **Factorial Interaction Term:**
  $$I_{\text{prep}\times\text{del}} = \ln(T_A / T_D) - \Big[ \ln(T_A / T_B) + \ln(T_A / T_C) \Big] = \ln\!\left(\frac{T_B \cdot T_C}{T_A \cdot T_D}\right)$$
  - $I_{\text{prep}\times\text{del}} > 0$ indicates **positive complementarity (synergy)**: extended preparation and interval delivery mutually unlock each other's bottleneck (e.g., preparing $K$ segments only helps once interval delivery removes LLC MSHR choking, and interval delivery only shines when $K$-segment preparation prevents DRAM readiness stalls).
  - To support a coordination-specific claim when $D$ beats both $B$ and $C$, we also ablate or measure what exchanged progress, readiness, and credit information enables beyond a static combination (noting that correctness-preserving slot reservations cannot be disabled as an ablation).

## 2. Required External Baselines and Resource Comparisons

In addition to Cells A, B, C, and D, every evaluation stage compares against a complete, fair set of external baselines with matching inputs, data storage capacity (`iPF Buffer` and `iMC iBuffer`), link assumptions, and generic controller demand protection:

1. **Current COTS:** Conventional out-of-order cores + shared LLC + adaptive commercial-style FR-FCFS memory controller (without stream descriptors).
2. **Tuned Standalone `iPF`:** Cache-side stream prefetcher using the `iPF Buffer` and `iCoordinator` descriptors with COTS memory controller.
3. **Strengthened `iPF` Baseline:** Standalone `iPF` receiving the **exact same applicable tracking and storage resources** added by COMPASS v2 (and if the interval receiver mechanism can operate with standalone `iPF` without `iMC` batching, evaluate that variant too).
4. **Standalone `iMC` (InterStellar):** Controller-side `iBatch` + `iPP` without cache-side `iPF`.
5. **Unmodified Current COMPASS:** Coordinated `iPF` + `iMC` with ordinary shared LLC MSHRs (16, 32, 64, 128).
6. **Current COMPASS + Separate Prefetch-MSHR Pool (`plan_B_delivery.md`):** Current COMPASS augmented with the 64-entry conventional per-line prefetch-MSHR pool (the capacity diagnostic).
7. **Current COMPASS + More Ordinary Shared MSHRs:** Scaling conventional LLC MSHRs across **16, 32, 64, and 128** entries (including the recovery point where MSHR admission stalls disappear).
8. **Supporting Baseline Ablations:** Remeasure **frontier feedback** and **hierarchy-wide presence filtering** on the frozen controller default (including their joint removal) to document how much of baseline COMPASS's efficiency comes from those existing mechanisms.

> **Baseline Integrity Rule:** Compare geometric means **only over identical completed workload sets**. Never compare a provisional 6-kernel geomean against a 12-kernel geomean, and never select a "best single mechanism" before all required baselines finish. Incomplete baselines are marked *pending*, never reported as poor performers. Reuse prior run results from `RESULTS_LOG.md` only when the executable hash, configuration hash, ROI, and inputs match exactly.

## 3. Immediate Experiment Sequence and Benchmark Allocations (10-Stage Plan)

Each workload copy in a multicore run executes as an independent process with private arrays. Row/column labels (`mvt-row`, `mvt-column`, `gemver-row`, `gemver-column`) refer to verified program phases. We use the **private-L2 topology** consistently as the default for the intended architecture while retaining the inherited shared-L2 topology as a documented sensitivity check.

| Stage | Workloads & System Configurations | Required Quantitative Outputs & Validation Criteria |
| :--- | :--- | :--- |
| **Stage 1: Validate Event Timing & State** | 1-core `ddot`, descriptor-replacement microbenchmarks, then short multicore runs | Bit-exact outputs; verified request visibility timestamps at `iMC`; exact completion, state, and slot/credit conservation ($S_{\text{free}} + S_{\text{reserved}} + S_{\text{resident}} = S_{\text{total}}$) |
| **Stage 2: Diagnose Current COMPASS** | `8x ddot`, `8x daxpy`, `8x dscal`, `8x mvt-row`; 8 cores / 2 channels, **40 and 120 cycles each way**, **16 and 64 MSHRs** | Quantify whether preparation latency, ready-data delivery (MSHR admission blocking while data are ready at `iMC`), or another resource blocks progress |
| **Stage 3: Read-Only Interval / Preparation Prototype** | `ddot` first, then a second validated private read-only direct-stream kernel (or validated read-only region) at 8 cores / 2 channels, 120 cycles, 16/32/64 MSHRs | Working Interval Descriptor & credit protocol with ordinary demand fallback and charged control/data transport; **zero free unsolicited fills** |
| **Stage 4: Controlled $2\times 2$ Screen (A, B, C, D)** | Same anchor points (`8c/2ch`, `40` & `120` cyc, `16/32/64` MSHRs) on eligible read-only workloads first; then `daxpy` and `dscal` **after write-path validation** | Incremental gain of B, C, D over current COMPASS (A), comparison vs. prefetch-MSHR pool, and log-time factorial interaction $I_{\text{prep}\times\text{del}}$ |
| **Stage 5: Latency & Concurrency Attribution** | Path delays: **40, 80, 120 cycles each way**; LLC MSHRs: **16, 32, 64, 128** on informative points (`ddot`, `daxpy`, `dscal`, plus `mvt-row` low-headroom control) | Determine at what MSHR count conventional COMPASS/`iPF` recovers and whether interval delivery reduces sensitivity to MSHR count and path latency |
| **Stage 6: Mixed Development Screen** | `4x daxpy + 4x ddot`; `4x mvt-row + 4x dscal`; `4x dscal + 4x ddot` (8 cores / 2 channels) | Useful delivered lines/sec, weighted speedup, and per-stream progress under competing preparation and delivery rates |
| **Stage 7: Scaling Confirmation** | **16 cores / 4 channels** (`8+8` homogeneous & development mixes); selected **32-core / 8-channel** points later | Paired policies with equal per-core scaling of LLC MSHRs (32/64/128 at 16c), `iPF Buffer`, and `iMC` channels |
| **Stage 8: Held-Out Transfer Validation** | `4x mvt-row + 4x jacobi-2d`; `4x jacobi-2d + 4x atax`; `4x gemver-row + 4x seidel-2d` (evaluated with **frozen parameters** from development screen) | Transferability of frozen $K$, $W_{\text{del}}$, and credit settings to stencil and matrix access patterns outside the development mixes (reported separately from development results) |
| **Stage 9: Interference Checks During Development** | `7x daxpy + 1x chase`; `4x mvt-column + 4x heat-3d` (and `15+1` at 16 cores) | Ensure no hidden severe regression to pointer-chasing bystanders or non-batchable column-walk traffic during mechanism tuning |
| **Stage 10: Final Interference Validation** | `7x mvt-row + 1x chase`; `7x bicg + 1x chase`; `4x gemver-column + 4x heat-3d` (co-runners kept active throughout mix interval) | Report weighted speedup, maximum slowdown, and bystander latency tails (95th/99th percentile) relative to isolated single-core reference under the corresponding policy |

## 4. Disciplined Parameter Sweeps (Section 9 Rules)

We sweep **only the dimensions justified by the Stage 2 & Stage 3 diagnosis**, varying one coordinate initially rather than crossing every dimension with every workload:

1. **Stage 1 Preparation Horizon ($K$):**
   - Sweep $K \in \{1, 2, 4\}$ future mapped row/page segments per stream while retaining the current intra-row batch width $B$.
2. **Stage 2 Delivery Eligibility & Credit Limits ($W_{\text{del}}, C_{\text{lim}}$):**
   - Explore delivery eligibility distances $W_{\text{del}} \in \{8, 16, 32, 64\}$ distinct cache lines and receiver/transport credit limits $C_{\text{lim}} \in \{8, 16, 32, 64\}$ where total `iPF Buffer` capacity permits.
   - These values are **exploratory architectural probes**, not pre-decided hardware recommendations.
3. **Reporting Achieved vs. Requested Windows:**
   - Because shared `iBuffer` caps, `actq` limits, `srTLB` page boundaries, and `iPF` slot availability restrict runtime lookahead, a requested window of $W_{\text{del}} = 64$ or $K = 4$ may not be attained at runtime.
   - Every sweep table must report: **achieved preparation depth**, **achieved delivery window**, **`iMC` ready-hit rate**, **`iPF` slot occupancy breakdown ($S_{\text{free}}, S_{\text{reserved}}, S_{\text{resident}}$)**, and **exact blocking reason distribution**.
4. **No Premature Adaptive Heuristics:**
   - Do not add dynamic urgency prediction, reinforcement learning, or adaptive window sizing until a **fixed mechanism** demonstrates useful headroom and exhibits a specific, measured failure mode that adaptation is needed to solve.

## 5. Required Telemetry, Causal Attribution & Multi-Core Metrics (Section 10)

Every experimental run must collect and report the following telemetry so that every speedup or slowdown has a verified causal explanation:

1. **Throughput & Latency:** End-to-end execution time (ROI cycles), useful delivered cache lines per second, demand `iPF` hit rate, demand merge/in-flight attachment rate, and exposed demand wait cycles.
2. **MSHR & Admission Attribution:**
   - LLC MSHR allocation stall cycles and average MSHR lifetime ($L_{\text{MSHR}}$) **broken down by request class** (demand misses, ready-at-`iMC` pulls, pending-DRAM pulls).
   - **Causal Link Counter (`blocked_while_ready_at_iMC`):** Explicitly record cycles and occurrences where LLC admission/pull issue was blocked by MSHR exhaustion **while eligible useful data for that stream were already sitting ready (`W = 0`) in the `iMC` per-bank `iBuffer`**. Relate this blocking directly to late demand arrivals—never infer benefit from shorter MSHR lifetime alone.
3. **Remote Residency vs. Waste:** Separate **useful remote residency** (lines prepared in `iBuffer` that are subsequently delivered and consumed by core demands) from **discarded-before-use data** (lines evicted from `iBuffer` or `iPF Buffer` before demand use) and **candidate queue losses** (stream positions dropped due to full `actq`/candidate queues).
4. **DRAM Bus & Controller Queue Attribution:**
   - Attribute DRAM bus-idle cycles to documented scheduling conditions (e.g., waiting on `tRP`/`tRCD`, `tFAW`, write-to-read turnaround, refresh blocking, or empty queues while data wait on delivery credits), recording overlapping causes separately.
   - Report peak and average occupancy across **all** queues: `readq`, `writeq`, `actq`, `pendingActq`, ingress/response queues, `iBuffer`, and `iPF Buffer`.
5. **Interconnect Traffic Breakdown:** Count DRAM payload bytes, forward LLC-to-`iMC` request/authorisation messages, credit-return messages, coherence invalidations, and `srTLB`/translation messages separately. (Note that total payload traffic need not fall when per-line request-message overhead falls.)
6. **Multi-Core & Fairness Protocol:**
   - Use **paired allocator seeds** for finalist comparisons and verify identical phase/work completion across all policies.
   - Keep co-runners active throughout each multi-core mix measurement window (or report changing overlap explicitly).
   - Report **Weighted Speedup**, **Geometric Mean Speedup**, **Maximum Slowdown**, and **Bystander Request Latency Tails (50th, 95th, 99th percentiles)** normalized against each workload's isolated single-core execution under the corresponding policy.
