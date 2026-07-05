# InterStellar 2.0 — gem5 v25.1 port

**InterStellar 2.0** is a gem5 + Ramulator implementation of a stream-aware
hardware/software co-design for scalable, high-bandwidth multi-channel DRAM.

The software side exposes a program's memory-access *streams* to the memory
controller through custom RISC-V **CSRs** (there are **no new CPU instructions** —
the entire SW/HW interface is CSR writes to `0x800–0x8C7`). The hardware side runs
a centralized **InterStellar Engine** between the LLC and main memory that steers
an **IPP** (Intelligent Page Policy) row policy, **iBatch** request coalescing,
and an **iHWP** (intelligent Hardware Prefetcher) inside Ramulator.

This tree is the **v25.1.0.1 port** of the engine (originally implemented on a
gem5 v20-era fork, `../fanosgem5/`). The full port design lives in
`../migration-plan/`.

> **Papers**
> - A. M. Abotaleb, M. Goudarzi, T. Czajkowski, R. Azimi, M. Hassan,
>   *"Stream-Aware Intelligent Memory Controller Through HW/SW Co-Design,"*
>   ACM/IEEE 11th Int. Symp. on Memory Systems (MEMSYS '25).
> - A. M. Abotaleb et al.,
>   *"InterStellar 2.0: Fine-Grained Stream–Guided HW/SW Co-Design for
>   Multi-Channel DRAM Performance Steering,"* Journal of Systems Architecture (under review).

---

## Port status

The engine is ported phase by phase; each phase has a hard compile/run gate.

| Phase | What | Status |
|---|---|---|
| 0 | Clean v25.1.0.1 baseline build | ✅ green |
| 1 | Stock-file "rails" (CSR plumbing, packet metadata, cache/prefetch/CPU hooks), `PageShift=26` | 🚧 in progress |
| 2 | Engine SimObjects under `src/interstellar/` | ⏳ pending |
| 3 | Vendored Ramulator + gem5 wrapper | ⏳ pending |
| 4 | Configs & Python wiring (`se.py`, `MetaISAEngineConfig.py`, CLI flags) | ⏳ pending |
| 5 | Benchmarks & end-to-end run | ⏳ pending |
| 6 | Golden-stats verification | ⏳ pending |

> ⚠️ **The run command below needs Phases 2–5.** Until then, `se.py` will reject
> `--mem-type=Ramulator` and `--meta-isa-type=IPP` (those flags land in Phase 4).
> The scripts and config here are provided up front so the workflow is
> reproducible the moment the port completes.

---

## Repository layout
```
gem5/
  src/interstellar/          # Engine: BaseInterstellarEngine, RiscvMetaISAEngine,
                             #   RiscvMetaISADescTable, RiscvMemoryStressor, the
                             #   iHWP prefetcher, and the canonical base_metaisa.hpp
  ext/ramulator/             # Vendored Ramulator (IPP / iBatch / FRFCFS_PriorHit)
  configs/ramulator/         # DDR4-config-Gen.cfg (InterStellar policy keys)
  configs/example/se.py      # Entry point (--meta-isa-type, --ramulator-config)
  scripts/                   # build_gem5.sh, run_benchmark.sh
  INTERSTELLAR_README.md     # this file
```
Benchmark binaries live outside this tree, in `../fanosgem5/BMs/` (Polybench,
Parboil, Phoenix, Rodinia, HPCG). Copy them in, or point `BMS_ROOT` at that path
(see *Run benchmarks*).

---

## Requirements

Tested on Ubuntu 22.04. gem5's SCons expects **clang**.
```bash
sudo apt install -y build-essential git m4 scons python3 python3-dev \
  zlib1g-dev libprotobuf-dev protobuf-compiler libgoogle-perftools-dev \
  libboost-all-dev pkg-config libpng-dev clang lld
```

---

## Build
```bash
scripts/build_gem5.sh          # optimized  -> build/RISCV/gem5.opt
scripts/build_gem5.sh debug    # debug      -> build/RISCV/gem5.debug
```

InterStellar custom debug flags (require the debug build):
`Interstellar_Filter_Pkt`, `MetaISA_IPP_CSR`, `MetaISA_DescTable`,
`MetaISA_LLC_Miss_{Dir,Ptr,Others}`, `MetaISA_TLB*`, `Interstellar_IPP*`,
`MemStress*`, `Ramulator`. List with `--debug-help`; enable with
`--debug-flags=<name>`.

---

## Configure Ramulator (InterStellar policy)

Edit `configs/ramulator/DDR4-config-Gen.cfg`. InterStellar-relevant keys:

| Key | Meaning | Options |
|---|---|---|
| `channels` | # DRAM channels | 1, 2, 4, 8, 16, 32 |
| `row_policy` | row policy | **IPP** (InterStellar), Adaptive (COTS), Opened (PARBS/BLISS) |
| `scheduling_policy` | scheduler | **FRFCFS_PriorHit** (InterStellar), PARBS, BLISS |
| `address_mapping` | address map | RoBaRaCoCh, ChRaBaRoCo, RoCoBaRaCh |
| `InDir_Str_Win` | indirect-stream tracking window (int) | active only under IPP |
| `metaisa_none_window_threshold[_Low]` | "none" stream window (0–1) | active only under IPP |
| `metaisa_indir_window_threshold[_Low]` | indirect stream window (0–1) | active only under IPP |
| `print_ipp_logic_trace` | IPP debug trace | on / off |

> The fork did **not** ship this file; it is reconstructed from the fork README
> and the Ramulator `Config.cpp` parser. Re-validate on the first run (Phase 5).

---

## Run benchmarks
```bash
scripts/run_benchmark.sh atax       # 8-core atax (N=724 working set ≈ 4 MiB of doubles)
scripts/run_benchmark.sh atax 1     # single-core
scripts/run_benchmark.sh bicg
scripts/run_benchmark.sh daxpy
scripts/run_benchmark.sh ddot
```
Each kernel's prebuilt binary lives at
`<BMS_ROOT>/Polybench/<bench>/build/<variant>/<bench>_v2_s1_i0_O3_M724kB_SdoubleB.riscv`.
`--cmd` lists the binary once per CPU, semicolon-separated, matching `-n`.

The equivalent raw command (paper config: 8× RISC-V OoO @ 2.4 GHz):
```bash
./build/RISCV/gem5.opt \
  --outdir=./BM/atax/out/map2/N8 \
  configs/example/se.py \
  -n 8 \
  --cpu-type=DerivO3CPU --cpu-clock=2400MHz --sys-clock=2400MHz \
  --caches --l1d_size=64kB --l1d_assoc=2 --l1d_mshrs=16 \
           --l1i_size=64kB --l1i_assoc=2 --l1i_mshrs=16 \
  --l2cache --l2_size=512kB --l2_assoc=4 --l2_mshrs=32 \
  --l3cache --l3_size=2MB   --l3_assoc=8 --l3_mshrs=64 \
  --mem-type=Ramulator \
  --ramulator-config=configs/ramulator/DDR4-config-Gen.cfg \
  --meta-isa-type=IPP \
  --cmd './BMs/Polybench/atax/build/.../atax_*.riscv;...(one binary per -n CPU)'
```

### Paper configuration
- **CPU:** 1 or 8× RISC-V out-of-order PEs @ 2.4 GHz.
- **L1-I/L1-D:** 64 KiB, 2-way, 16 MSHRs. **L2:** 512 KiB, 4-way, 32 MSHRs. **L3:** 2 MiB, 8-way, 64 MSHRs.
- **Memory:** Ramulator DDR4_2440R (4 bank groups × 4 banks).

### Output
- `m5out/stats.txt` — gem5 stats + Ramulator custom stats
  (`Total_Requests_Completed_iBatch`, `iBatches_Completed_count_per_stream_`,
  `IPrefetches_Wasted_count_per_stream_`).
- `*.memLoopVA2PA.out` — engine VA→PA stream trace (`VA: 0x.. ; PA: 0x..`).
- Energy is computed **offline** with **DRAMPower v3.1** on Ramulator command traces.

---

## Generate RISC-V binaries (LLVM/Clang)

Prebuilt binaries assume **64 MiB pages** (`PageShift=26`). To rebuild a kernel
(the source must `#include "metaisa.hpp"` so the InterStellar pass can emit
descriptor `csrrw` writes):
```bash
clang -O3 -Wall --target=riscv64 -march=rv64gc -mno-relax \
  --sysroot=/path/to/riscv64-unknown-elf \
  --gcc-toolchain=/path/to/toolchain \
  -o atax_N724.riscv atax.c
```
The custom LLVM pass in `../llvm-project/` (`InterStellarAnalysisPass`) analyzes
loops with SCEV, classifies each memory access (direct / indirect / pointer-chase),
and lowers the 128-bit descriptors to paired `csrrw` writes on `0x800–0x8C7`.

---

## Troubleshooting
- **`--mem-type=Ramulator` rejected** — Phase 4 (configs) not done yet.
- **Page-fault storm on boot** — `PageShift` is not 26, or the binary wasn't built for 64 MiB pages.
- **SCons picks Python 2** — run via `/usr/bin/env python3 "$(which scons)" ...`.
- **Debug flags missing** — they require the debug build (`build/RISCV/gem5.debug`).

---

## Cite this work
```bibtex
@inproceedings{InterStellar,
  author    = {Abdelrhman Abotaleb and Maziar Goudarzi and Tomasz Czajkowski and Reza Azimi and Mohamed Hassan},
  title     = {Stream-Aware Intelligent Memory Controller through {HW/SW} Co-Design},
  booktitle = {ACM/IEEE 11th International Symposium on Memory Systems (MEMSYS '25)},
  year      = {2025}, location = {Washington, DC, USA}
}
```

## License
Research code. See the fork (`../fanosgem5/LICENSE`) for terms.

## References
- MEMSYS '25 program: <https://www.memsys.io/program-2/>
- PolyBench/C: <https://www.cs.colostate.edu/~pouchet/software/polybench/>
- DRAMPower v3.1: <https://github.com/tukl-msd/DRAMPower/tree/3.1>
- Baselines: PARBS (ISCA '08), BLISS (ICCD '14); prefetchers: AMPM (ICS '09),
  BOP (HPCA '16), IMP (MICRO '15), SPP (MICRO '16).
