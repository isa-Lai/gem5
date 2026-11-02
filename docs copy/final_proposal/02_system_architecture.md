# 02 — System Architecture: Two-Stage Bounded Preparation & Interval Delivery

**Document:** `docs/final_proposal/02_system_architecture.md`  
**Project:** COMPASS v2 — Bounded Stream Preparation and Delivery with Amortised Request Tracking  
**Target Venues:** ISCA / MICRO / ASPLOS / HPCA  
**Status:** Refined Architectural Proposal & Microarchitectural Specification

## 1. Bottleneck-Driven Architectural Intervention

COMPASS v2 does not apply a blind, monolithic hardware change across all workloads. Instead, the architectural intervention is selected directly from the observed bottleneck at each operating point, and structured so that **Stage 1 (Preparation)** and **Stage 2 (Delivery)** can be evaluated independently and jointly in a controlled $2\times 2$ comparison:

| Measured Diagnostic Condition | Primary Architectural Intervention | Target Experimental Cell |
| :--- | :--- | :--- |
| **Useful data ready at `iMC`; LLC MSHR allocation stalls prevent timely pulls** | Prototype **Bounded Interval Delivery** with existing `iMC` preparation | **Cell B** (Delivery change) |
| **Many pulls arrive at `iMC` before data are ready, exposing DRAM waiting** | Extend **Bounded Independent Preparation ($K$ segments)** with ordinary line delivery | **Cell C** (Preparation change) |
| **Both restrictions occur (or solving one exposes the other)** | Combine **Extended Independent Preparation ($K$)** with **Bounded Interval Delivery** and quantify interaction | **Cell D** (Combined design) |
| **Data ready at `iMC` and LLC admission available, but delivery is slow** | Investigate NoC link, response queue, arbitration, and `iPF` fill-path capacity | Infrastructure / Link Diagnostic |
| **Demands already find data locally and resource stalls are low** (e.g., `mvt`, `bicg`, `gemver`) | Preserve as a **low-headroom control**; investigate core, cache, or write-path limits | Control Baseline |

> **Diagnostic Guardrail:** Long remote residency in the `iMC` buffer alone is not evidence of harm—retaining far-future data at the controller until receiver slots open can be intentional. Likewise, high buffer or queue occupancy alone does not establish a bottleneck. Every architectural claim must link measured admission blocking or unready pulls to late demand accesses and end-to-end execution time.

## 2. Two-Stage Architectural Pipeline

COMPASS v2 builds directly on the verified COMPASS baseline (`iCoordinator` + `iCache`/`iPF` + `iMC`) and organizes stream data supply into two distinct stages with complementary immediate objectives and shared flow-control constraints:

```
[Mapped Future Stream Segments (via Descriptor Table + srTLB)]
       |
       |  (Stage 1: Preparation Seed / Eligibility Horizon K = {1, 2, 4} segments)
       v
[Bounded, DRAM-Efficient Preparation at iMC]
  - Admits up to K mapped row/page segments per stream
  - Respects global iBuffer capacity and per-bank queued-row limits (actq / pendingActq)
  - Uses iBatch for row-local batching (width B) and iPP for per-stream row closure (PHits / WHits)
       |
       v
[Ready Data Staged in Per-Bank iMC Buffer (iBuffer)]
       |
       |  (Stage 2: Interval Authorisation W_del = {8, 16, 32, 64} lines & Credits C = {8, 16, 32, 64})
       v
[Authorised, Credit-Controlled Interval Delivery across NoC]
  - Persistent Interval Descriptor replaces per-line LLC request messages & speculative MSHRs
  - Conserves iPF receiver slot reservations and reusable transport permits
       |
       v
[Valid, Coherence-Verified Data in LLC iPF Buffer]
       |
       v
[Ordinary CPU / L2 Demand Consumption (or Demand-Merge on In-Flight Interval)]
```

- **Stage 1 Objective (Preparation at `iMC`):** Maximize useful DRAM row-buffer locality (`iBatch` + `iPP`) and bank-level parallelism while obeying bounded timeliness, per-bank queue caps, and inter-stream interference limits.
- **Stage 2 Objective (Delivery to `iPF`):** Ensure local data availability in the LLC `iPF Buffer` ahead of core demand while respecting finite `iPF` buffer storage and interconnect transport limits.
- **Cross-Stage Coupling Rule:** Neither stage is permitted to maximize its local metric independently of progress at the other stage. Preparation stalls if `iMC` staging storage (`iBuffer`) saturates from undelivered data; delivery stalls if receiver slots are full or if controller preparation has not yet completed the authorised positions.

## 3. Stage 1: Bounded Independent Preparation ($K$ Future Segments)

### 3.1 Baseline Preparation in Current COMPASS (Row A & B Reference)
In unmodified current COMPASS, `iMC` batching (`iBatch`) is reactive to arriving LLC requests tagged by the `iCoordinator`: when a tagged request for stream $i$ arrives at the `iMC` and opens a DRAM row, the Batch Management Unit generates up to $B$ row-local future requests within that same DRAM page (`MAX/Page`) and places them in the per-bank `iBuffer`.

### 3.2 Extended Independent Preparation (Row C & D Mechanism)
When the round-trip path is long (e.g., 80–120 cycles each way) or when individual cache-side requests are replaced by coarse interval authorisations, waiting for individual LLC miss arrivals to trigger every new DRAM row/page segment exposes `tRP + tRCD + tCL` preparation latency (`L_prepare`).

COMPASS v2 introduces **Extended Independent Preparation**:
1. **Segment Definition:** A preparation segment is a contiguous sequence of distinct cache-line positions belonging to stream $i$ that fall within both a **single mapped virtual-to-physical page (`srTLB` entry)** and a **single DRAM row (`MAX/Page`)**. A segment terminates immediately at an actual page-mapping boundary, loop bound (`end_VA`), or DRAM row boundary; it need not span an entire DRAM row.
2. **Preparation Horizon ($K$):** The preparation stage admits up to $K \in \{1, 2, 4\}$ future segments per active stream ahead of the currently delivering segment, while retaining the current intra-row batch width $B$.
3. **Autonomous Segment Advancement:** Once a stream descriptor and its translated page mappings (`srTLB`) are registered at the `iMC`, preparation can advance across up to $K$ mapped segments as prior segments complete or as `iBuffer` space frees—**without needing an individual new LLC request arrival to trigger every segment**.
4. **Explicit Preparation-Seed Interface (For Clean $2\times 2$ Isolation):**
   - To guarantee a rigorous $2\times 2$ comparison between Row 1 (Current preparation: A, B) and Row 2 (Extended preparation: C, D), the trigger mechanism must be decoupled from the delivery mechanism.
   - In **Cell B** (Current preparation + Interval delivery), an interval authorisation **must not** silently unlock $K$-segment lookahead. Instead, Cell B uses an explicit **preparation-seed rule** that matches Cell A's exact segment-trigger eligibility (advancing preparation to the next row segment only when the consumption/request frontier reaches the same trigger position that would have emitted a seed request in Cell A, with all control messages explicitly charged on the interconnect).
5. **Global and Per-Bank Resource Caps:**
   - Independent preparation is strictly bounded by:
     - Total free entries in the target DRAM bank's `iBuffer`,
     - Per-bank queued-row limits in `actq` and `pendingActq` (note: multiple queued row segments for a bank in `actq`/`pendingActq` represent pipelined future work and **do not** imply multiple simultaneously open rows in that bank's sense amplifiers), and
     - Mapping availability: if the next logical position crosses into a virtual page not currently present in `srTLB`, independent preparation halts cleanly at the page boundary until a demand or translation update supplies the mapping.

## 4. Stage 2: Bounded Interval Delivery & Amortised Request Tracking

### 4.1 Replacing Per-Line Speculative MSHRs with Interval Authorisations
In conventional `iPF` and current COMPASS (Column 1: Cells A and C), moving 16 prepared cache lines from `iMC` to the `iPF Buffer` requires 16 separate LLC request packets and 16 concurrent LLC MSHR allocations.

In **Bounded Interval Delivery** (Column 2: Cells B and D):
- The LLC `iPF Management Unit` issues a single **Interval Authorisation** message to the `iMC` covering a window of $W_{\text{del}} \in \{8, 16, 32, 64\}$ distinct cache-line positions within the current mapped page.
- No speculative per-line MSHRs are allocated in the conventional LLC MSHR pool. Instead, the interval is tracked by a compact **Interval Descriptor** at the LLC (`iPF` Interval Table) and a corresponding delivery window record at the `iMC`.
- As lines within the authorised interval become ready in the `iMC` per-bank `iBuffer` and transport/receiver credits permit, the `iMC Response Unit` transmits the data lines incrementally to the LLC `iPF Buffer`.

### 4.2 Conceptual Interval Descriptor Structure
Each active interval descriptor at the LLC (and mirrored in the `iMC` Stream Status Window) contains the following architectural fields (exact bit widths are parameterized in Document `04_hardware_cost_and_scalability.md` according to maximum supported windows):

```
+-----------------------------------------------------------------------------------+
|                        CONCEPTUAL INTERVAL DESCRIPTOR                             |
+-----------------------------------------------------------------------------------+
| 1. Address-Space / Context : Core ID (Cid), Thread/Process Context (Tid / ASID)   |
| 2. Stream Identity         : Stream Descriptor ID (Did)                           |
| 3. Descriptor Epoch        : Monotonic generation counter (Epoch) for revocation  |
| 4. Logical Start Position  : Base distinct cache-line index relative to stream    |
| 5. Position Count (W_del)  : Number of distinct cache lines authorised (e.g. 8-64)|
| 6. Translated Segment Ref  : Validated PFN / srTLB entry index & page offset      |
| 7. Destination Window/Slot : Base slot pointer / generation ID in iPF Buffer      |
| 8. Receiver Reservations   : Reserved slot count & transport credit counter       |
| 9. State Bitmaps (W_del b) : Per-line flags (Authorised, Ready/Resident,          |
|                              Completed, Demand-Waiting)                           |
+-----------------------------------------------------------------------------------+
```

### 4.3 Fundamental Architectural Constraints on Interval Descriptors
1. **No Unmapped Page Reads:** An interval descriptor is strictly page-bounded in the initial prototype (`PBSS`). It never authorises reads across an unmapped future page boundary.
2. **No Assumed Coherence Ownership:** An interval descriptor is a data-staging authorisation, not a coherence lock. It does not grant permission to overwrite dirty lines in private caches or use stale DRAM values if another core modifies the physical line.
3. **No Implicit Preparation Escalation:** Authorising a delivery interval of $W_{\text{del}}$ positions does not permit the `iMC` to bypass its Stage 1 preparation limit ($K$ segments) or per-bank queue caps.
4. **Multi-Channel Address Mapping:** When physical cache lines within a page are interleaved across multiple memory channels (e.g., 2 channels at 8 cores, 4 channels at 16 cores under `RoBaBgColRaCh`), the `iCoordinator` either splits the interval authorisation per target channel or tags each sub-interval with the channel-local distinct line count and channel-specific receiver credits, ensuring exact credit conservation per channel.

## 5. Return Data Identity and Slot Selection

Every data completion packet sent from the `iMC` to the LLC `iPF Response Unit` under an interval authorisation carries a compact completion header:
- `(Cid, Did, Epoch, Logical_Offset, Physical_Line_Addr)`
- Upon arrival at the LLC:
  1. The `iCache Response Unit` verifies that `(Cid, Did, Epoch)` matches an active Interval Descriptor.
  2. If `Epoch` does not match (e.g., the descriptor was replaced or flushed at loop exit), the late packet is discarded and its reservation/credit state is reconciled according to the revocation rules in Document `03_coherence_and_correctness.md`.
  3. If valid, `Logical_Offset` indexes directly into the descriptor's state bitmaps and selects the pre-reserved destination slot in the `iPF Buffer`.
