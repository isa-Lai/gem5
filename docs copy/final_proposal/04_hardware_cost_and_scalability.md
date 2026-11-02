# 04 — Hardware Storage Accounting, Cost Comparison & Scalability

**Document:** `docs/final_proposal/04_hardware_cost_and_scalability.md`  
**Project:** COMPASS v2 — Bounded Stream Preparation and Delivery with Amortised Request Tracking  
**Target Venues:** ISCA / MICRO / ASPLOS / HPCA  
**Status:** Refined Hardware Cost Specification & Iso-Resource Accounting

## 1. Principles of Honest Hardware Cost Accounting

Compact request tracking in COMPASS v2 is a **scientific hypothesis about representation cost**, not a license to hide state in unbounded simulator maps. Even when per-line speculative MSHRs are replaced by interval authorisations, per-line validity, out-of-order completion tracking, and demand merging still require concrete hardware state.

To meet top-tier computer architecture standards (ISCA/MICRO/ASPLOS/HPCA), our hardware cost accounting follows four strict rules from Sections 6, 9, 10, and 12:
1. **Disclose All Controller and Cache Structures:** Account explicitly for `readq`, `writeq`, `actq`, `pendingActq`, ingress/response queues, `iMC` per-bank `iBuffer`, `iPF` data/reservation slots, `iCoordinator` descriptor tables, `srTLB` mappings, interval descriptors, per-line state bitmaps, and credit/revocation records. A smaller `readq` does not establish smaller total controller storage if `actq` or `iBuffer` holds the displaced state.
2. **Treat Bitmaps and Tables as Fixed Hardware Parameters:** Every bitmap (`Authorised_BM`, `Completed_BM`, `Resident_BM`, `DemandWait_BM`) and descriptor table has a fixed hardware dimension (e.g., max interval width $W_{\text{max}} = 64$ bits per descriptor, fixed descriptor count per core), never an unbounded `std::map` or `std::unordered_set`.
3. **Compare Against MSHRs Across Multiple Dimensions:** Compare equal conventional LLC MSHR counts first (with all additional structures disclosed), then compare against giving current COMPASS and tuned `iPF` **more MSHRs** (e.g., +64 prefetch MSHRs in `plan_B_delivery.md`, or 32/64/128 shared MSHRs).
4. **Distinguish Storage Bits from Lookup Logic & Area:** Equal storage bits provide only a preliminary comparison. True iso-area and power comparisons must account for **associative CAM comparators vs. range/offset lookup logic, read/write port counts, and critical-path timing**. Likewise, energy comparisons must include DRAM background/refresh energy, ACT/PRE, RD/WR bursts, and SRAM buffer accesses (fewer ACT/PRE commands do not automatically establish an equal percentage reduction in total memory energy).

## 2. Complete Hardware State Inventory (8-Core, 2-Channel Baseline)

First, we separate the **Inherited Baseline Structures** (already present in InterStellar and current COMPASS) from the **New COMPASS v2 Interval & Preparation Structures** so reviewers can see the exact incremental hardware delta.

### 2.1 Inherited COMPASS Baseline Structures (8 Cores, 2 Channels, 16 Banks/Channel)
From `interstellar.pdf` (Section 6.2 & Table 1) and `COMPASS_TC.pdf` (Section V):

| Subsystem | Structure Name | Baseline Organization | Entry Size (Data + Metadata) | Total Baseline Storage | Lookup / Port Organization |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Core / CSRs** | **Stream CSRs** | 32 CSRs (16 descriptors $\times$ 128b) per core $\times$ 8 cores | 128 bits | **2.00 KB** (mapped to existing RISC-V custom CSR space) | Direct CSR index (`csrrw`) |
| **`iCoordinator`** | **Descriptor Table** | 16 streams $\times$ 8 cores = 128 entries | 128b static + 2b type + 40b `end_VA` + `Cid`/`Did` = 177 bits | **2.76 KB** | Parallel range comparator per active stream |
| **`iCoordinator`** | **Stream Reverse TLB (`srTLB`)** | 256 entries (shared across LLC) | `PFN` (36b) + `VPN` (36b) + `Cid` (3b) + `Valid` (1b) = 76 bits | **2.38 KB** | Set-associative lookup on `PFN` (in parallel with LLC tag check) |
| **LLC (`iCache`)** | **Shared LLC MSHRs** | 16, 32, 64, or 128 entries (swept; default 64 in TC paper, 16/32 in constrained points) | `LineAddr` (42b) + `TargetList` (64b) + `State` (10b) = 116 bits | **0.23 KB** (at 16 MSHRs) to **0.91 KB** (at 64 MSHRs) | Fully-associative CAM on 42-bit physical line address |
| **LLC (`iCache`)** | **`iPF` Data Buffer** | 64 KB (1,024 lines; also swept to 128 KB / 256 KB in TC Fig. 16) | 64B data (512b) + 42b `Addr` + `R` (1b) + `W` (1b) = 556 bits | **69.50 KB** (at 1,024 entries; **8.69 KB** at 128 entries) | Set-associative tag array + SRAM data array |
| **`iMC`** | **Per-Bank `iBuffer`** | 64 entries/bank $\times$ 16 banks = 1,024 entries (or per-channel scaled) | 64B data (512b) + 42b `Addr` + `R` (1b) + `W` (1b) = 554 bits | **69.25 KB** per 1,024 entries | Distributed per-bank SRAM; indexed on bank address |
| **`iMC`** | **Stream Status Window & `iPP`** | 128 stream trackers (`PHits`, `WHits`, sliding conflict shift registers) | ~64 bits per stream | **1.00 KB** | Direct index by `StreamID` (`Did`, `Cid`) |
| **`iMC`** | **Controller Queues (`readq`, `writeq`, `actq`, `pendingActq`, `respq`)** | Standard MC queues (e.g., 64-entry `readq`/`writeq`, per-bank `actq`/`pendingActq`, 64-entry `respq`) | ~72–128 bits per command/request entry | **~2.50 KB** per channel | CAM for write-read forwarding; FIFO/priority scan for FR-FCFS |

### 2.2 New COMPASS v2 Structures (Incremental State for Stage 1 & Stage 2)

To replace per-line speculative MSHR allocations with **Stage 1 ($K$-segment independent preparation)** and **Stage 2 (Bounded interval delivery)**, COMPASS v2 adds the following bounded structures (sized for the maximum exploratory window $W_{\text{del}} \le 64$ lines and $K \le 4$ segments across 8 cores, up to 4 active direct streams per core = 32 active interval descriptors):

| Subsystem | New COMPASS v2 Structure | Organization & Capacity | Bit-Level Breakdown per Entry | Total Incremental Bits / Bytes | Lookup & Port Cost |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **LLC (`iPF`)** | **LLC Interval Descriptor Table (IDT)** | 32 entries (8 cores $\times$ 4 active direct streams) | `Cid` (3b) + `Did` (4b) + `Epoch` (4b) + `LogicalStart` (20b) + `Count` (7b, up to 64) + `Stride` (16b) + `PFN_Ref` (8b `srTLB` idx) + `SlotGen` (8b) = **70 bits** | 2,240 bits (**280 Bytes**) | Direct index by `(Cid, Did)` on return; range/hash check on demand miss |
| **LLC (`iPF`)** | **Per-Interval State Bitmaps** | 32 descriptors $\times$ 4 bitmaps of 64 bits ($W_{\text{max}}=64$) | `Authorised_BM` (64b) + `Completed_BM` (64b) + `Resident_BM` (64b) + `DemandWait_BM` (64b) = **256 bits** | 8,192 bits (**1,024 Bytes = 1.00 KB**) | 1-hot bit set/clear by `Logical_Offset` $[0..63]$ |
| **LLC (`iPF`)** | **Receiver Slot & Credit Counters** | 32 streams $\times$ 2 channels | `FreeSlots` (8b) + `ReservedUnfilled` (8b) + `ResidentSlots` (8b) + `TransportCredits` (7b) + `PendingReplenish` (7b) = **38 bits** | 2,432 bits (**304 Bytes**) | Up/down saturating counters per cycle |
| **LLC (`iPF`)** | **Draining / Revocation Epoch Table** | 32 entries (1 prior draining epoch per descriptor slot) | `Old_Epoch` (4b) + `Outstanding_Count` (7b) + `Active` (1b) = **12 bits** | 384 bits (**48 Bytes**) | Checked on Epoch mismatch of arriving packet |
| **`iMC`** | **Stage 1 $K$-Segment Preparation Table** | 32 active streams $\times$ $K_{\text{max}}=4$ segments = 128 segment entries | `Valid` (1b) + `Segment_BasePA` (42b) + `Lines_Remaining` (6b) + `Row_ID` (16b) + `Bank_ID` (4b) + `State` (3b: Planned/Admitted/Issued) = **72 bits** | 9,216 bits (**1,152 Bytes = 1.13 KB**) | Indexed by `(Cid, Did, SegIdx)` |
| **`iMC`** | **Controller Interval Authorisation Mirror** | 32 entries per channel $\times$ 2 channels = 64 entries | `Cid` (3b) + `Did` (4b) + `Epoch` (4b) + `LogicalStart` (20b) + `Count` (7b) + `Sent_BM` (64b) + `AvailCredits` (7b) = **109 bits** | 6,976 bits (**872 Bytes**) | Indexed by `(Cid, Did)` when `iBuffer` line becomes ready |
| **Total Incremental State** | **All New COMPASS v2 Tracking State** | **Across LLC + 2 `iMC` Channels** | **Replaces 64+ per-line speculative MSHRs** | **29,440 bits ($\approx 3.59\text{ KB}$)** | **See Section 3 Comparison vs. CAM MSHRs** |

## 3. Direct Cost Comparison: Interval Tracking vs. Adding 64 Prefetch MSHRs (`plan_B_delivery.md`)

A critical question from Sections 6 and 12 is: *How does the cost of COMPASS v2's interval descriptors, bitmaps, and lookup logic compare against simply giving the LLC 64 additional per-line prefetch MSHRs (as in `plan_B_delivery.md`)?*

| Cost Metric | Conventional +64 Prefetch MSHRs (`plan_B_delivery.md`) | COMPASS v2 Interval Delivery + $K$-Segment Tracking ($W_{\text{max}}=64, K=4$) | Architectural Trade-off & Interpretation |
| :--- | :--- | :--- | :--- |
| **Max Tracked In-Flight / Authorised Lines** | **64 lines** (strictly 1 line per MSHR entry) | **Up to 2,048 lines** (32 streams $\times$ 64 lines/interval; bounded in practice by `iPF` slot capacity, e.g., 128–1,024 slots) | Interval representation decouples logical authorisation span from 1-entry-per-line MSHR table limits. |
| **LLC Tracking State (Bits)** | $64 \times 116\text{ b} = \mathbf{7,424\text{ bits}}$ ($\mathbf{0.91\text{ KB}}$) | IDT (280 B) + Bitmaps (1,024 B) + Credits/Revocation (352 B) = **13,248 bits ($\mathbf{1.62\text{ KB}}$ at LLC)** (or **0.87 KB** if $W_{\text{max}}=32$) | At $W_{\text{max}}=32$, LLC bit count (**0.87 KB**) is **smaller** than 64 MSHRs (**0.91 KB**); at $W_{\text{max}}=64$, it is $1.78\times$ the raw bits while covering up to $32\times$ more logical positions. |
| **`iMC` Tracking State (Bits)** | **0 bits** (uses existing `readq` / `iBuffer`) | $K$-Segment Table (1.13 KB) + Interval Mirror (0.85 KB) = **1.98 KB across 2 channels** | Disclosed controller-side state required for independent $K$-segment lookahead and push/interval scheduling. |
| **Associative Lookup / Comparator Cost on L2 Miss** | **64 parallel 42-bit CAM comparators** probed on every LLC miss + **64 CAM comparators** probed on every coherence invalidation | **Existing `iPF Buffer` tag probe** (already present in COMPASS) + **32-entry Interval Table** (direct index on `(Cid, Did)` for stream-tagged misses; page-tag + 6-bit offset range check for untagged/snoop probes) | Conventional MSHRs require full 42-bit associative CAMs that scale poorly in dynamic power and latency beyond 64 entries. |
| **Forward Request Message Traffic (LLC $\to$ `iMC`)** | **1 request message per cache line** (64 messages for 64 lines) | **1 Interval Authorisation per $W_{\text{del}}$ lines** + periodic batched credit returns (**$8\times$ to $32\times$ reduction** in forward request messages) | Directly reduces interconnect request-channel congestion and arbitration overhead. |
| **Data Buffer Capacity (`iPF` & `iBuffer`)** | **Identical** (`iPF Buffer` and per-bank `iBuffer` sizes held constant across both designs) | **Identical** (`iPF Buffer` and per-bank `iBuffer` sizes held constant across both designs) | Isolates transaction representation from data staging capacity. |

### 3.1 Sensitivity to Bitmap Width ($W_{\text{del}} \in \{8, 16, 32, 64\}$)
Because Section 9 sweeps delivery eligibility distances $W_{\text{del}} \in \{8, 16, 32, 64\}$ and receiver/transport limits $\{8, 16, 32, 64\}$, the LLC bitmap cost scales directly with $W_{\text{max}}$:
- At $W_{\text{max}} = 16$: Per-interval bitmaps require only $4 \times 16 = 64\text{ bits}$ ($256\text{ Bytes}$ total for 32 descriptors), bringing total LLC interval state down to **0.87 KB** (less than 64 conventional MSHRs).
- At $W_{\text{max}} = 32$: Per-interval bitmaps require $128\text{ bits}$ ($512\text{ Bytes}$ total), or **1.12 KB** total LLC state.
- At $W_{\text{max}} = 64$: Per-interval bitmaps require $256\text{ bits}$ ($1,024\text{ Bytes}$ total), or **1.62 KB** total LLC state.

This allows the final paper to present a **Pareto efficiency curve** of performance versus tracking state bits and control message overhead across $\{8, 16, 32, 64\}$.

## 4. Scaling Rules for 16-Core and 32-Core Experiments

Section 8 requires scaling confirmation at **16 cores / 4 channels** (and selected **32-core** points later) with documented scaling of every intended per-core and per-channel resource:

| Resource Parameter | 8 Cores / 2 Channels (Anchor Baseline) | 16 Cores / 4 Channels (2x Scaled System) | 32 Cores / 8 Channels (4x Scaled System) | Scaling Rule (Strictly Applied to All Policies) |
| :--- | :--- | :--- | :--- | :--- |
| **Private L1D / L1I per Core** | 64 KB / 64 KB (16 MSHRs) | 64 KB / 64 KB (16 MSHRs) | 64 KB / 64 KB (16 MSHRs) | Constant per core |
| **Private L2 per Core** | 512 KB (32 MSHRs) | 512 KB (32 MSHRs) | 512 KB (32 MSHRs) | Constant per core (private-L2 default; shared-L2 retained as sensitivity) |
| **Shared LLC Capacity** | 2 MB (or 4 MB) | 4 MB (or 8 MB) | 8 MB (or 16 MB) | Scaled $1\times$ per core ($256\text{ KB} - 512\text{ KB}$/core) |
| **Shared LLC MSHR Budget** | Swept: **16, 32, 64, 128** (2 to 16 MSHRs/core) | Swept: **32, 64, 128, 256** (2 to 16 MSHRs/core) | Swept: **64, 128, 256, 512** (2 to 16 MSHRs/core) | **Scaled equally per core for all baselines (`COTS`, `iPF`, `COMPASS`, `COMPASS v2`)** |
| **`iCoordinator` Descriptors** | 16/core (128 total) | 16/core (256 total) | 16/core (512 total) | Constant per core |
| **`srTLB` Entries** | 256 entries | 512 entries | 1,024 entries | Scaled $32$ entries per core |
| **LLC `iPF Buffer` Capacity** | 64 KB (1,024 lines; 128 lines/core) | 128 KB (2,048 lines; 128 lines/core) | 256 KB (4,096 lines; 128 lines/core) | Constant per core (matched across `iPF`, `COMPASS`, and `COMPASS v2`) |
| **COMPASS v2 Interval Descriptors** | 32 entries (4/core) | 64 entries (4/core) | 128 entries (4/core) | Constant 4 active interval descriptors per core |
| **`iMC` Channels & Per-Bank `iBuffer`** | 2 channels (16 banks/ch, 4 KB `iBuffer`/bank) | 4 channels (16 banks/ch, 4 KB `iBuffer`/bank) | 8 channels (16 banks/ch, 4 KB `iBuffer`/bank) | Constant per DRAM channel / bank |
| **Multi-Core Workload Mix Scaling** | `4 + 4` mixes (development/held-out); `7 + 1` (single `chase` bystander) | `8 + 8` mixes; **`15 + 1`** (preserving a single `chase` bystander) | `16 + 16` mixes; **`31 + 1`** (preserving a single `chase` bystander) | Mixes scale proportionally; bystander interference tests preserve **1 isolated victim core** |
