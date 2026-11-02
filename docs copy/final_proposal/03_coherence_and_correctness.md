# 03 — Progress, Validity, Credit Semantics & Correctness Gates

**Document:** `docs/final_proposal/03_coherence_and_correctness.md`  
**Project:** COMPASS v2 — Bounded Stream Preparation and Delivery with Amortised Request Tracking  
**Target Venues:** ISCA / MICRO / ASPLOS / HPCA  
**Status:** Refined Protocol Specification & Correctness/Modelling Gates

## 1. Per-Position State Representation: Counters vs. Bounded Bitmaps

A central correctness requirement of COMPASS v2 is maintaining precise state for each distinct cache-line position within an active stream window without assuming that streams progress in lockstep order.

### 1.1 Eight Distinct Lifecycle States
Across the `iMC` and `iPF`, a logical cache-line position $p$ within a stream epoch can occupy the following states (several of which legitimately coexist):

| State Flag | Location | Precise Architectural Meaning | Coexistence & Ordering Notes |
| :--- | :--- | :--- | :--- |
| **1. Planned** | `iCoordinator` / `iMC` | Position $p$ lies within the valid loop bounds (`start_VA` to `end_VA`) of an active descriptor | May not yet be translated or admitted |
| **2. Admitted** | `iMC` | Position $p$ belongs to one of the $K$ active translated preparation segments and has an allocated `iBuffer` slot | Coexists with *Authorised for delivery* if Stage 2 pre-authorises future lines |
| **3. Issued** | `iMC` | The DRAM read command (`CAS`) for position $p$ has been scheduled into `readq`/`actq`/command queue (`W = 1` in `iBuffer`) | Awaits DRAM data burst return |
| **4. Ready (at `iMC`)** | `iMC` (`iBuffer`) | DRAM data have returned to the per-bank `iBuffer` (`W = 0`, valid data present at controller) | Ready for transmission if delivery is authorised and credits permit |
| **5. Authorised for Delivery** | `iPF` & `iMC` | Position $p$ is covered by an active Stage 2 Interval Descriptor with a reserved destination slot in `iPF Buffer` | **May coexist with Admitted/Issued** (authorisation covers data not yet ready at DRAM) |
| **6. In Transport** | NoC / Link | Data packet for position $p$ has been dispatched by `iMC Response Unit` and is traversing the interconnect | Consumes one in-flight transport permit and one reserved `iPF` slot |
| **7. Locally Resident** | LLC (`iPF Buffer`) | Data have arrived at LLC, passed coherence checks, and reside in the reserved `iPF Buffer` slot (`W = 0` at `iPF`) | Occupies a **resident slot** until consumed, evicted, or invalidated |
| **8. Observed Demand Use** | LLC (`iCache`) | An LLC demand access (or L2 miss) matched and consumed position $p$, or bypassed speculation via cache hit | Progress estimate of core execution (upper-level L1/L2 hits are invisible to LLC) |

### 1.2 Why Global Frontier Ordering Is Not a Universal Invariant
In a realistic out-of-order memory hierarchy, **a strict inequality $\text{Consumed Frontier} \le \text{Delivered Frontier} \le \text{Prepared Frontier}$ is not a universal correctness invariant**:
1. **Historical Completion vs. Current Residency:** A cache line at position $p$ may be delivered to the `iPF Buffer` (advancing a historical completion counter) and subsequently evicted due to capacity pressure or invalidated by an external write *before* the core's demand access reaches $p$.
2. **Cache Hits and Demand Bypass:** Some positions within an interval may already reside in L1/L2/LLC from prior reuse, or a core demand miss may outrun speculative preparation and fetch position $p$ directly via the ordinary demand MSHR path, bypassing one or both speculative stages.
3. **Out-of-Order Bank Service:** Because consecutive lines in a strided or multi-page stream may map to different DRAM banks or channels, position $p+2$ (hitting an open row in Bank 1) may become *Ready* and be *Delivered* before position $p$ (waiting on a row conflict in Bank 0).

**Design Rule:** Use contiguous progress counters (`Head`/`Tail` pointers) only to bracket the active sliding window $[P_{\text{base}}, P_{\text{base}} + W_{\text{del}})$, and maintain **bounded per-line bitmaps** (`Authorised_BM`, `Completed_BM`, `Resident_BM`, `DemandWait_BM`) within that window to record exact per-position availability and gaps.

## 2. Receiver Data-Slot Conservation and Transport Credit Semantics

COMPASS v2 enforces strict conservation of receiver storage and flow-control permits. A frequent design error in push/interval protocols is returning a receiver slot credit as soon as a packet arrives at the cache—which allows the controller to overwrite unconsumed data in the `iPF Buffer`.

### 2.1 Receiver Data-Slot Conservation Law
At all times, the total number of entries $S_{\text{total}}$ in the LLC `iPF Buffer` (or per-stream partition) satisfies the exact conservation invariant:

$$S_{\text{free}} + S_{\text{reserved\_unfilled}} + S_{\text{resident}} = S_{\text{total}}$$

Every state transition preserves $S_{\text{total}}$:
1. **Interval Authorisation (`Free -> Reserved Unfilled`):** Before sending an interval authorisation for $m$ uncached positions to the `iMC`, the LLC `iPF Management Unit` must reserve $m$ slots:
   $$S_{\text{free}} \leftarrow S_{\text{free}} - m, \quad S_{\text{reserved\_unfilled}} \leftarrow S_{\text{reserved\_unfilled}} + m$$
   If fewer than $m$ free slots are available, the interval count is clamped to $S_{\text{free}}$ (a **partial interval**) or delayed until slots are reclaimed.
2. **Data Arrival at LLC (`Reserved Unfilled -> Resident`):** When an authorised data packet arrives from the `iMC` and is written into its reserved `iPF Buffer` slot:
   $$S_{\text{reserved\_unfilled}} \leftarrow S_{\text{reserved\_unfilled}} - 1, \quad S_{\text{resident}} \leftarrow S_{\text{resident}} + 1$$
   **Crucially, arrival does not increment $S_{\text{free}}$.** The data slot remains occupied until the resident line leaves the buffer.
3. **Slot Reclamation (`Resident -> Free`):** A resident slot transitions back to $S_{\text{free}}$ (and generates a replenishment credit for future authorisations) **only** upon one of three events:
   - **Demand Consumption / Promotion:** A demand read hits the `iPF Buffer` entry, consumes the data (or promotes the line into the LLC/L2), and releases the staging slot:
     $$S_{\text{resident}} \leftarrow S_{\text{resident}} - 1, \quad S_{\text{free}} \leftarrow S_{\text{free}} + 1$$
   - **Explicit Replacement / Eviction:** The `iPF` replacement policy evicts an unconsumed resident entry (recorded in the *discarded-before-use* diagnostic counter).
   - **Coherence Invalidation / Descriptor Flush:** An external write invalidates the physical line, or the stream descriptor is replaced.
4. **Cancellation of Unfilled Reservation (`Reserved Unfilled -> Free`):** If an authorised position is satisfied via an ordinary demand MSHR fallback, cancelled by `iMC` rejection, or revoked on epoch change before data arrival, its unfilled reservation is released **exactly once**:
   $$S_{\text{reserved\_unfilled}} \leftarrow S_{\text{reserved\_unfilled}} - 1, \quad S_{\text{free}} \leftarrow S_{\text{free}} + 1$$

### 2.2 Separate Transport Permits and Replenishment Messages
In addition to receiver data-slot reservations, COMPASS v2 models **Transport Permits ($C_{\text{trans}}$)** if the ingress/response pipeline between `iMC` and `iPF` has a smaller ingress FIFO than the full `iPF Buffer`:
- Transport permits bound the number of packets simultaneously in flight on the interconnect and in the LLC fill ingress queue; they are returned upon receiver acceptance into the `iPF Buffer`.
- Data-slot credits bound the total uncommitted + resident footprint in the `iPF Buffer`; they are returned only upon slot reclamation (`Resident -> Free`).
- **Explicit Credit-Return Modelling:** Replenishment messages from LLC to `iMC` are not free side-band wires. They are explicitly modelled with forward control-path latency ($L_{\text{ctrl}}$) and configurable batching (e.g., returning a credit update every $k \in \{1, 4, 8\}$ reclaimed slots or piggybacking on the next interval seed/authorisation message).

## 3. Demand Catch-Up, Merging, Overlapping Streams, and Revocation

### 3.1 Demand Arriving Before Delivery (Demand Catch-Up)
When a core demand access misses the conventional LLC tag array, it probes the `iPF Buffer` and the active **Interval Descriptor Table** in parallel:
1. **Case 1 — Resident in `iPF Buffer` (`Resident_BM[p] == 1`):** Immediate `iPF` hit (3-cycle hit latency as in COMPASS). The data are returned to the core, `Observed_Demand_Use` is recorded, and the `iPF` slot is reclaimed (`Resident -> Free`).
2. **Case 2 — Authorised / In-Flight in Interval (`Authorised_BM[p] == 1` and `Resident_BM[p] == 0`):**
   - **Path 2a (Wait on Pending Interval):** If the interval completion is imminent or the implementation attaches demands directly to the reserved `iPF` slot (`R = 1`, `W = 1` in `iPF Buffer`, setting `DemandWait_BM[p] = 1`), the demand waits on the reserved slot without issuing a duplicate DRAM request.
   - **Path 2b (Ordinary Demand MSHR Fallback with Safe Merge):** If the demand allocates an ordinary LLC demand MSHR and issues a high-priority demand request to the `iMC`:
     - At the `iMC`, the demand request checks the per-bank `iBuffer` (as in COMPASS Fig. 11), hits the ready or pending `iBatch` entry, and returns via the demand path.
     - At the LLC, if both the demand MSHR response and an in-flight interval completion arrive for the same physical line, the `iCache Response Unit` **merges or discards the duplicate speculative completion safely, wakes the waiting demand exactly once, and releases the reserved `iPF` slot exactly once**.
3. **Case 3 — Outside Active Interval (`Authorised_BM[p] == 0`):** Falls back immediately to the ordinary LLC demand MSHR and coherence path.

### 3.2 Overlapping Streams and Physical-Line Identity
Multiple descriptors (e.g., two streams reading overlapping regions of the same array, or stencil neighbors in `jacobi-2d` / `seidel-2d`) may reference the **same physical cache line** within the same context.
- **Physical-Line Deduplication:** Both the `iPF Buffer` and the per-bank `iMC Buffer` are indexed/checked by **physical line address and context**, not solely by `(StreamID, LogicalPosition)`.
- If Stream 2 authorises a position whose physical address is already resident or pending in the `iPF Buffer` (or `iMC Buffer`) from Stream 1, the hardware merges the reference rather than issuing a duplicate DRAM transaction or allocating a duplicate physical data slot.

### 3.3 Descriptor Replacement and Epoch Revocation
When a loop terminates or software writes a new descriptor to the same CSR slot (`Did`):
1. The `iCoordinator` increments the 4-bit **Descriptor Epoch (`Epoch <- Epoch + 1`)** and sends a charged `Revoke_Descriptor(Cid, Did, Old_Epoch)` control message to the `iMC` (or piggybacks it on the new descriptor registration).
2. At the LLC, all unfilled reservations for `Old_Epoch` enter a bounded **Draining / Revoked State** (or are immediately reclaimed if the protocol tracks bounded in-flight count per epoch, where late packets with `Packet.Epoch == Old_Epoch` are dropped at ingress and counted in `late_revoked_completions`).
3. **Strict Isolation Invariant:** A late completion packet carrying `Old_Epoch` is **never** permitted to populate a slot belonging to `New_Epoch`. State for outstanding revocations and late completions is strictly bounded (at most 1 prior draining epoch per descriptor slot).

## 4. Coherence Path and Staged Write-Stream Validation

### 4.1 Coherence Verification for Supplied Data
The `iMC` resides below the cache coherence domain and reads raw DRAM values. Therefore:
- **Snoop-Filter Presence Is Not Sufficient:** A snoop-filter or directory presence bit indicating that a line is cached does not by itself tell the `iMC` whether a private upper-level cache (L1/L2) holds a **Modified (dirty)** copy or can silently transition from Exclusive (E) to Modified (M). Using an old DRAM value when a core holds a dirty copy violates coherence.
- **Valid Receiver Coherence Path:** Every line delivered into the `iPF Buffer` must either:
  1. Have valid read permission established via the coherence directory / snoop filter (confirming no core holds the line in Modified/Exclusive writable state, or downgrading/sourcing from the dirty owner before the line can be used), and
  2. Be **invalidated immediately** if any core or I/O agent subsequently issues a write/upgrade (`Invalidate` / `ReadEx`) to that physical line address while the line is staged in the `iPF Buffer` or in flight.

### 4.2 Staged Validation Progression: Read-Only First, Then RMW
In accordance with Sections 1, 6, 8, and 13 of the research plan:
1. **Stage A — Private Read-Only Direct Streams (`ddot`):** First implement and validate the interval protocol on private, read-only direct streams where the arrays are not written during the kernel window. Even here, the `iPF Buffer` snoop-invalidation path must be active to preserve correctness if any external write occurs.
2. **Stage B — Read-Modify-Write Streams (`daxpy`, `dscal`) and Output-Updating Kernels (`mvt`):** Before interpreting any speedup on `daxpy`, `dscal`, or `mvt`, validate the write-invalidation, dirty-owner sourcing, store-merge, and writeback interaction paths using reduced-size functional output checks (verifying exact bit-for-bit output equivalence against COTS).

## 5. Mandatory Correctness & Modelling Gates (Section 11 Checklist)

Before any new performance numbers from COMPASS v2 are interpreted or included in the paper, the simulator implementation must pass six explicit modelling and correctness gates:

| Gate ID | Modelling / Correctness Gate | Required Verification Action & Audit Metric |
| :--- | :--- | :--- |
| **Gate 1** | **Request Delay vs. Controller Visibility** | Verify that configured interconnect delay (`40`, `80`, `120` cycles each way) delays **actual arrival and trigger visibility at the `iMC` scheduler and `iBatch` unit**, rather than merely adding a post-hoc response-time charge. Audit idle round-trip latency and arrival/trigger timestamps in short execution traces. |
| **Gate 2** | **Oracle Shadow-Read Callback Audit (Mode 2)** | In the existing codebase, oracle shadow reads (Mode 2) used an empty completion callback, whereas `iBuffer`-ready bookkeeping resides in the normal callback and `iBatch` copies the triggering request's callback. **Repair and audit Mode 2** so shadow reads perform normal controller completion/state maintenance without sending a second architectural response to the LLC. |
| **Gate 3** | **No Silently Dropped Accepted Shadows** | Verify that no accepted shadow request is silently dropped in controller queues (`readq`/`actq`). Note in analysis that changing execution timing naturally alters DRAM batching and traffic, so 100% shadow acceptance is a diagnostic relaxation rather than an additive latency decomposition. |
| **Gate 4** | **Scheduler Starvation & Column-Walk Audit** | Because yield and age-cap heuristics provided little relief and worsened some column walkers (`mvt-column`, `gemver-column`), trace affected bystander/column-walk requests across **`readq` -> `actq` -> `pendingActq` -> write-mode drain periods -> actual DRAM commands**. Ablate `iPP` and `iBatch` separately before introducing any new controller protection heuristic. |
| **Gate 5** | **DRAM Refresh Postponement Audit** | A maximum refresh queue occupancy of 1 does not prove zero refresh postponement. Explicitly log and check **enqueue-to-refresh-command delay distributions, total issued refresh counts, and refresh-induced bank blocking cycles** across all main experiments. |
| **Gate 6** | **Full State & Credit Conservation Under Edge Cases** | Using 1-core `ddot`, descriptor-replacement microbenchmarks, and short multicore runs, assert exact runtime invariants: zero leaked data slots ($S_{\text{free}} + S_{\text{reserved}} + S_{\text{resident}} = S_{\text{total}}$), zero leaked transport credits, zero double demand wakeups, and bit-exact architectural memory outputs across invalidations, descriptor replacements, partial intervals, backpressure, and out-of-order returns. |
