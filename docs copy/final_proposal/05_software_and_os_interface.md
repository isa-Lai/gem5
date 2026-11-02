# 05 — HW/SW Interface, Stream Descriptors & Translation Semantics

**Document:** `docs/final_proposal/05_software_and_os_interface.md`  
**Project:** COMPASS v2 — Bounded Stream Preparation and Delivery with Amortised Request Tracking  
**Target Venues:** ISCA / MICRO / ASPLOS / HPCA  
**Status:** Refined HW/SW Interface & Translation Specification

## 1. Preserving the Non-Intrusive InterStellar / COMPASS HW/SW Interface

COMPASS v2 retains the exact, verified hardware/software interface introduced in InterStellar (`interstellar.pdf`, Section 4.1 & Fig. 11) and COMPASS (`COMPASS_TC.pdf`, Section V-A & Fig. 7) **without requiring new CPU instructions or operating-system modifications**.

1. **Standard RISC-V CSR Communication:** The LLVM compiler pass identifies hot loops and their direct and indirect streams, encoding them into 128-bit descriptors written via standard RISC-V `csrrw` instructions to custom Control and Status Registers (CSRs) in the loop preheader (32 64-bit CSRs per core, supporting 16 128-bit descriptors per core: up to 9 streams and 4 loops per context).
2. **Memory-Mapped Fallback:** On platforms without accessible user/custom CSRs, descriptors can be communicated via stores to a reserved memory region.
3. **Strict Baseline Compatibility:** Retaining the exact descriptor encoding ensures that COMPASS v2 compares apples-to-apples against unmodified current COMPASS, `iPF`, and `iMC` using identical compiled binaries and Regions of Interest (ROIs).

## 2. Exact 128-Bit Descriptor Formats (From COMPASS Fig. 7 / InterStellar Fig. 11)

All four 128-bit descriptor types share an 8-bit `Header` field in bits `[127:120]` that encodes the descriptor type (`DType`: `0` = Loop, `1` = Direct Stream, `2` = Indirect Stream, `3` = Link Variable / Other) and activation status:

```
1. LOOP DESCRIPTOR (128 bits):
+------------------+------------------+-----+-----+---------------+---------------+-----------+------------------+
| 127          120 | 119          114 | 113 | 112 | 111        80 | 79         48 | 47     32 | 31             0 |
|      Header      |  Parent Loop ID  | SL  | EL  |  Start Value  |   End Value   |   Step    | Offset PC Header |
+------------------+------------------+-----+-----+---------------+---------------+-----------+------------------+

2. DIRECT STREAM DESCRIPTOR (128 bits):
+------------------+------------------+-----+-----+-------------------------------+-----------+------------------+
| 127          120 | 119          114 | 113 | 112 | 111                        48 | 47     32 | 31             0 |
|      Header      |   Loop Desc ID   | BL  |  S  |       Base Address (BA)       |  Stride   |       R/V        |
+------------------+------------------+-----+-----+-------------------------------+-----------+------------------+

3. INDIRECT STREAM DESCRIPTOR (128 bits):
+------------------+------------------+-----+-----+-------------------------------+--------------+---------------+
| 127          120 | 119          114 | 113 | 112 | 111                        48 | 47        32 | 31          0 |
|      Header      |  Stream Desc ID  | BL  |  S  |       Base Address (BA)       | Element Size |  Stream Size  |
+------------------+------------------+-----+-----+-------------------------------+--------------+---------------+

4. LINK VARIABLE DESCRIPTOR (128 bits):
+------------------+------------------+-------------------------------------------+--------------+---------------+
| 127          120 | 119          112 | 111                                    48 | 47        32 | 31          0 |
|      Header      |       R/V        |                  Address                  |     Size     |      R/V      |
+------------------+------------------+-------------------------------------------+--------------+---------------+
```

### 2.1 Field Semantics and Range Derivation
- **Loop Descriptor:** Defines iteration bounds (`Start Value`, `End Value`, `Step`) and nesting (`Parent Loop ID`). `SL` (Start Linked) and `EL` (End Linked) indicate when runtime bounds are supplied by a `Link Variable Descriptor`.
- **Direct Stream Descriptor:** Associates the stream with its enclosing `Loop Desc ID`, specifies `Base Address (BA)` (`BL = 1` if linked to a runtime register/variable), `Stride` (in bytes), and `S` (Shared flag across threads/cores).
  - The `iCoordinator` derives the active virtual-address range for direct stream $i$ as:
    $$\text{start\_VA}_i = \text{base\_VA}_i + S_{\text{Loop\_ID}[i]} \times \text{stride}_i$$
    $$\text{end\_VA}_i = \text{base\_VA}_i + E_{\text{Loop\_ID}[i]} \times \text{stride}_i$$
- **Indirect Stream Descriptor:** Specifies the index stream ID (`Stream Desc ID`), `Base Address (BA)`, `Element Size`, and `Stream Size`, with virtual range $[\text{base\_VA}_i, \text{base\_VA}_i + \text{size}_i)$.

## 3. Virtual-to-Physical Translation (`srTLB`) and Page-Bounded Semantics

Because software descriptors specify virtual addresses (`VA`) while the LLC and `iMC` operate on physical addresses (`PA`), COMPASS v2 uses the `iCoordinator`'s **Stream Reverse TLB (`srTLB` / `Stream TLB^-1`)** alongside explicit mapping-availability rules for both Stage 1 ($K$-segment preparation) and Stage 2 (interval delivery).

### 3.1 How `srTLB` Supplies Translations Without OS Changes
1. **Populating `srTLB` on Core TLB Misses:** Whenever a core TLB miss resolves a page translation `(VA, PA)` whose virtual address falls within an active stream's `[start_VA, end_VA]` range in the `iCoordinator` Descriptor Table, the `(PFN, VPN, Cid)` mapping is inserted into the 256-entry `srTLB`.
2. **Stream Identification at LLC:** On an LLC access, the `Stream Query` unit looks up the physical frame number (`PFN`) in the `srTLB` (in parallel with the LLC tag access) to reconstruct the `VA` and match the active stream descriptor.

### 3.2 Page-Bounded Interval Authorisations and $K$-Segment Preparation
A critical architectural rule in Sections 3, 5, 9, and 13 is: **Do not assume a logical interval authorises reads from unmapped future pages.**
1. **Page-Bounded Initial Prototype (Order 2):**
   - In the initial interval-delivery prototype (Cell B), an interval authorisation of up to $W_{\text{del}}$ distinct cache lines is strictly bounded by the **current mapped physical page** in `srTLB` (`Page_End_PA = (PFN << 12) + 4096`).
   - If the requested window $W_{\text{del}}$ reaches a 4 KB (or huge-page) boundary before $W_{\text{del}}$ lines are accumulated, the hardware emits a **partial interval** covering only the remaining distinct lines within the mapped page.
2. **Multi-Segment Preparation Across Page Boundaries (Order 4 — Cells C & D):**
   - When Stage 1 preparation is extended to $K \in \{1, 2, 4\}$ future segments, a future segment $k > 1$ may remain within the same mapped page (e.g., when a page spans multiple DRAM rows or bank-interleaved row segments) or may cross into the next virtual page `VPN + 1`.
   - If segment $k$ crosses into `VPN + 1`, the `iCoordinator` checks whether `(VPN + 1, Cid)` already has a valid `PFN` mapping in `srTLB` (or via a charged forward TLB/translation lookup).
   - **Explicit Mapping-Availability Accounting:** If the mapping for `VPN + 1` is **not** present in `srTLB`, Stage 1 preparation **stops at the page boundary** and increments the diagnostic counter `prep_blocked_unmapped_page`. It never fabricates or cheats unmapped physical addresses in the simulator.

## 4. Distinct Cache-Line Position Indexing and Element Sharing

As specified in Section 3, stream strides in byte-addressed loops frequently differ from the 64-byte cache-line size:
- **Sub-Line Strides ($\text{stride} < 64\text{ B}$, e.g., 8-byte `double` in `ddot`, `daxpy`, `dscal`):** Eight consecutive loop iterations access the same 64-byte cache line. The interval descriptor and preparation horizon count **distinct 64-byte cache-line positions**, not loop iterations. An interval of $W_{\text{del}} = 16$ distinct cache lines covers $16 \times 8 = 128$ double-precision elements ($1,024\text{ bytes}$).
- **Large Strides ($\text{stride} \ge 64\text{ B}$, e.g., strided `dscal` or matrix column walks):** Each step advances by $\lceil \text{stride} / 64 \rceil$ cache lines. Each distinct cache line accessed by the stream corresponds to one logical position in the interval descriptor, while `MAX/Page` at the `iMC` adjusts accordingly:
  $$\text{MAX/Page}_i = \frac{\text{DRAM Page Size}}{\max(\text{Stride}_i,\; \text{Cache Line Size})}$$

## 5. Descriptor Lifecycle, Flushing, and Phase Verification

1. **Stream Lifetime & Replacement:** Existing COMPASS already implements stream-lifetime handling and descriptor flushing when a loop finishes or a descriptor is overwritten. In COMPASS v2, overwriting or closing a descriptor increments its `Epoch` and triggers the bounded revocation/draining protocol specified in Document `03_coherence_and_correctness.md`, ensuring late packets from the old descriptor never corrupt the new descriptor's slots.
2. **Verified Program Phases (Row vs. Column Labels):**
   - As mandated in Section 8, workload labels such as `mvt-row`, `mvt-column`, `gemver-row`, and `gemver-column` refer to **verified program phases and actual emitted descriptors**, not assumed properties of an arbitrary instruction window.
   - Note that `mvt` performs output array updates (`x1[i] += ...`), meaning the complete `mvt` kernel is **not** a purely read-only prototype by default unless restricted to its read-only input streams or evaluated after write-path validation.
   - Similarly, the first `fdtd-2d` `ey`-sweep window exhibits access behavior similar to `daxpy`; therefore, `fdtd-2d` must either be evaluated on distinct 2D stencil phases or not claimed as an independent stencil validation point.
