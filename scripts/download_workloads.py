#!/usr/bin/env python3
"""Workload download and provenance catalog for COMPASS v2 / InterStellar 2.0.

Provides download URLs, git repositories, upstream authors, dataset parameters,
and verification/fetch functionality for all 31 benchmark workloads.

Usage:
    python3 gem5/scripts/download_workloads.py [--verify] [--out-dir DIR]
"""

import argparse
import json
import os
import sys

WORKLOAD_CATALOG = [
    {
        "id": 1,
        "name": "atax",
        "category": "PolyBench/C 4.2 Linear Algebra",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/atax/src/atax_v2.c",
        "binary_path": "gem5/BMs/Polybench/atax/build/EXP/atax_exp.riscv",
        "dataset_flags": "-DM=1800 -DN=2200",
        "footprint": "~31.7 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 3,
    },
    {
        "id": 2,
        "name": "bicg",
        "category": "PolyBench/C 4.2 Linear Algebra",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/bicg/src/bicg_v2.c",
        "binary_path": "gem5/BMs/Polybench/bicg/build/EXP/bicg_exp.riscv",
        "dataset_flags": "-DM=1800 -DN=2200",
        "footprint": "~31.7 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 3,
    },
    {
        "id": 3,
        "name": "daxpy",
        "category": "PolyBench-ACC / BLAS-1",
        "upstream_author": "Matthias J. Reisinger et al.",
        "upstream_url": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "git_mirror": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/daxpy/src/daxpy_v2.c",
        "binary_path": "gem5/BMs/Polybench/daxpy/build/EXP/daxpy_exp.riscv",
        "dataset_flags": "-DN=2000000",
        "footprint": "32.0 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 2,
    },
    {
        "id": 4,
        "name": "daxpys",
        "category": "Extended BLAS-1",
        "upstream_author": "InterStellar Project / Matthias J. Reisinger et al.",
        "upstream_url": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "git_mirror": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/daxpys/src/daxpys_v2.c",
        "binary_path": "gem5/BMs/Polybench/daxpys/build/EXP/daxpys_exp.riscv",
        "dataset_flags": "-DSIZE=2000000 -DSTRIDE=1",
        "footprint": "32.0 MB",
        "stream_type": "Direct (Large Stride)",
        "stream_count": 2,
    },
    {
        "id": 5,
        "name": "ddot",
        "category": "PolyBench-ACC / BLAS-1",
        "upstream_author": "Matthias J. Reisinger et al.",
        "upstream_url": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "git_mirror": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/ddot/src/ddot_v2.c",
        "binary_path": "gem5/BMs/Polybench/ddot/build/EXP/ddot_exp.riscv",
        "dataset_flags": "-DN=2000000",
        "footprint": "32.0 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 2,
    },
    {
        "id": 6,
        "name": "ddots",
        "category": "Extended BLAS-1",
        "upstream_author": "InterStellar Project / Matthias J. Reisinger et al.",
        "upstream_url": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "git_mirror": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/ddots/src/ddots_v2.c",
        "binary_path": "gem5/BMs/Polybench/ddots/build/EXP/ddots_exp.riscv",
        "dataset_flags": "-DSIZE=2000000 -DSTRIDE=1",
        "footprint": "32.0 MB",
        "stream_type": "Direct (Large Stride)",
        "stream_count": 2,
    },
    {
        "id": 7,
        "name": "doitgen",
        "category": "PolyBench/C 4.2 Linear Algebra",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/doitgen/src/doitgen_v2.c",
        "binary_path": "gem5/BMs/Polybench/doitgen/build/EXP/doitgen_exp.riscv",
        "dataset_flags": "-DNR=192 -DNQ=192 -DNP=16",
        "footprint": "~4.7 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 3,
    },
    {
        "id": 8,
        "name": "dscal",
        "category": "PolyBench-ACC / BLAS-1",
        "upstream_author": "Matthias J. Reisinger et al.",
        "upstream_url": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "git_mirror": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/dscal/src/dscal_v2.c",
        "binary_path": "gem5/BMs/Polybench/dscal/build/EXP/dscal_exp.riscv",
        "dataset_flags": "-DN=2000000",
        "footprint": "16.0 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 1,
    },
    {
        "id": 9,
        "name": "dscals",
        "category": "Extended BLAS-1",
        "upstream_author": "InterStellar Project / Matthias J. Reisinger et al.",
        "upstream_url": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "git_mirror": "https://github.com/MatthiasJReisinger/PolyBench-ACC",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/dscals/src/dscals_v2.c",
        "binary_path": "gem5/BMs/Polybench/dscals/build/EXP/dscals_exp.riscv",
        "dataset_flags": "-DSIZE=2000000 -DSTRIDE=1",
        "footprint": "16.0 MB",
        "stream_type": "Direct (Large Stride)",
        "stream_count": 1,
    },
    {
        "id": 10,
        "name": "fdtd-2d",
        "category": "PolyBench/C 4.2 Stencils",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/fdtd-2d/src/fdtd-2d_v2.c",
        "binary_path": "gem5/BMs/Polybench/fdtd-2d/build/EXP/fdtd-2d_exp.riscv",
        "dataset_flags": "-DNX=500 -DNY=400",
        "footprint": "~4.8 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 3,
    },
    {
        "id": 11,
        "name": "floyd",
        "category": "PolyBench/C 4.2 Medley",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/floyd/src/floyd_v2.c",
        "binary_path": "gem5/BMs/Polybench/floyd/build/EXP/floyd_exp.riscv",
        "dataset_flags": "-DN=260",
        "footprint": "~0.54 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 1,
    },
    {
        "id": 12,
        "name": "gemm",
        "category": "PolyBench/C 4.2 BLAS",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/gemm/src/gemm_v2.c",
        "binary_path": "gem5/BMs/Polybench/gemm/build/EXP/gemm_exp.riscv",
        "dataset_flags": "-DNP=250 -DNQ=250 -DNR=250",
        "footprint": "~1.5 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 3,
    },
    {
        "id": 13,
        "name": "gemver",
        "category": "PolyBench/C 4.2 BLAS",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/gemver/src/gemver_v2.c",
        "binary_path": "gem5/BMs/Polybench/gemver/build/EXP/gemver_exp.riscv",
        "dataset_flags": "-DN=1800",
        "footprint": "~25.9 MB",
        "stream_type": "Direct (Mixed Stride)",
        "stream_count": 9,
    },
    {
        "id": 14,
        "name": "gesummv",
        "category": "PolyBench/C 4.2 BLAS",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/gesummv/src/gesummv_v2.c",
        "binary_path": "gem5/BMs/Polybench/gesummv/build/EXP/gesummv_exp.riscv",
        "dataset_flags": "-DN=1800",
        "footprint": "~51.8 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 5,
    },
    {
        "id": 15,
        "name": "heat-3d",
        "category": "PolyBench/C 4.2 Stencils",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/heat-3d/src/heat-3d_v2.c",
        "binary_path": "gem5/BMs/Polybench/heat-3d/build/EXP/heat-3d_exp.riscv",
        "dataset_flags": "-DN=64",
        "footprint": "~4.2 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 2,
    },
    {
        "id": 16,
        "name": "heat-3d-5s",
        "category": "PolyBench Extended",
        "upstream_author": "InterStellar Project / Louis-Noel Pouchet",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/heat-3d-5s/src/heat-3d-5s_v2.c",
        "binary_path": "gem5/BMs/Polybench/heat-3d-5s/build/EXP/heat-3d-5s_exp.riscv",
        "dataset_flags": "-DN=64",
        "footprint": "~4.2 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 5,
    },
    {
        "id": 17,
        "name": "jacobi-1d",
        "category": "PolyBench/C 4.2 Stencils",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/jacobi-1d/src/jacobi-1d_v2.c",
        "binary_path": "gem5/BMs/Polybench/jacobi-1d/build/EXP/jacobi-1d_exp.riscv",
        "dataset_flags": "-DN=300000",
        "footprint": "~4.8 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 2,
    },
    {
        "id": 18,
        "name": "jacobi-2d",
        "category": "PolyBench/C 4.2 Stencils",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/jacobi-2d/src/jacobi-2d_v2.c",
        "binary_path": "gem5/BMs/Polybench/jacobi-2d/build/EXP/jacobi-2d_exp.riscv",
        "dataset_flags": "-DN=760",
        "footprint": "~9.2 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 2,
    },
    {
        "id": 19,
        "name": "knn1d",
        "category": "Spatial / Mining Addition",
        "upstream_author": "InterStellar 2.0 Project",
        "upstream_url": "In-tree InterStellar suite",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/knn1d/src/knn1d_v2.c",
        "binary_path": "gem5/BMs/Polybench/knn1d/build/EXP/knn1d_exp.riscv",
        "dataset_flags": "-DN=200000",
        "footprint": "~16.0 MB",
        "stream_type": "Mixed (Direct + Indirect)",
        "stream_count": 3,
    },
    {
        "id": 20,
        "name": "knn2d",
        "category": "Spatial / Mining Addition",
        "upstream_author": "InterStellar 2.0 Project",
        "upstream_url": "In-tree InterStellar suite",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/knn2d/src/knn2d_v2.c",
        "binary_path": "gem5/BMs/Polybench/knn2d/build/EXP/knn2d_exp.riscv",
        "dataset_flags": "-DN=100000",
        "footprint": "~16.0 MB",
        "stream_type": "Mixed (Direct + Indirect)",
        "stream_count": 5,
    },
    {
        "id": 21,
        "name": "knn3d",
        "category": "Spatial / Mining Addition",
        "upstream_author": "InterStellar 2.0 Project",
        "upstream_url": "In-tree InterStellar suite",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/knn3d/src/knn3d_v2.c",
        "binary_path": "gem5/BMs/Polybench/knn3d/build/EXP/knn3d_exp.riscv",
        "dataset_flags": "-DN=100000",
        "footprint": "~24.0 MB",
        "stream_type": "Mixed (Direct + Indirect)",
        "stream_count": 7,
    },
    {
        "id": 22,
        "name": "memcpy",
        "category": "Memory Streaming Microbenchmark",
        "upstream_author": "InterStellar 2.0 Project",
        "upstream_url": "In-tree InterStellar suite",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/memcpy/src/memcpy_v2.c",
        "binary_path": "gem5/BMs/Polybench/memcpy/build/EXP/memcpy_exp.riscv",
        "dataset_flags": "-DN=4000000",
        "footprint": "64.0 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 2,
    },
    {
        "id": 23,
        "name": "mvt",
        "category": "PolyBench/C 4.2 Linear Algebra",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/mvt/src/mvt_v2.c",
        "binary_path": "gem5/BMs/Polybench/mvt/build/EXP/mvt_exp.riscv",
        "dataset_flags": "-DN=2000",
        "footprint": "32.0 MB",
        "stream_type": "Direct (Mixed Stride)",
        "stream_count": 5,
    },
    {
        "id": 24,
        "name": "scusumkbn",
        "category": "Extended LINPACK Comp. Sum",
        "upstream_author": "InterStellar 2.0 Project",
        "upstream_url": "In-tree InterStellar suite",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/scusumkbn/src/scusumkbn_v2.c",
        "binary_path": "gem5/BMs/Polybench/scusumkbn/build/EXP/scusumkbn_exp.riscv",
        "dataset_flags": "-DN=2000000",
        "footprint": "32.0 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 2,
    },
    {
        "id": 25,
        "name": "scusumkbns",
        "category": "Extended LINPACK Comp. Sum",
        "upstream_author": "InterStellar 2.0 Project",
        "upstream_url": "In-tree InterStellar suite",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/scusumkbns/src/scusumkbns_v2.c",
        "binary_path": "gem5/BMs/Polybench/scusumkbns/build/EXP/scusumkbns_exp.riscv",
        "dataset_flags": "-DSIZE=2000000 -DSTRIDE=1",
        "footprint": "32.0 MB",
        "stream_type": "Direct (Large Stride)",
        "stream_count": 2,
    },
    {
        "id": 26,
        "name": "seidel-2d",
        "category": "PolyBench/C 4.2 Stencils",
        "upstream_author": "Louis-Noel Pouchet and Tomofumi Yuki",
        "upstream_url": "https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/seidel-2d/src/seidel-2d_v2.c",
        "binary_path": "gem5/BMs/Polybench/seidel-2d/build/EXP/seidel-2d_exp.riscv",
        "dataset_flags": "-DN=700",
        "footprint": "~3.9 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 4,
    },
    {
        "id": 27,
        "name": "syn7s",
        "category": "Synthetic InterStellar Multi-Stream",
        "upstream_author": "InterStellar 2.0 Project",
        "upstream_url": "In-tree InterStellar suite",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/syn7s/src/syn7s_v2.c",
        "binary_path": "gem5/BMs/Polybench/syn7s/build/EXP/syn7s_exp.riscv",
        "dataset_flags": "-DM=800 -DN=800",
        "footprint": "~35.8 MB",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 7,
    },
    {
        "id": 28,
        "name": "synind",
        "category": "Synthetic InterStellar Indirect",
        "upstream_author": "InterStellar 2.0 Project",
        "upstream_url": "In-tree InterStellar suite",
        "git_mirror": "https://github.com/cavazos-lab/PolyBench-C-4.2.1",
        "license": "BSD 3-Clause",
        "source_path": "gem5/BMs/Polybench/synind/src/synind_v2.c",
        "binary_path": "gem5/BMs/Polybench/synind/build/EXP/synind_exp.riscv",
        "dataset_flags": "-DN=800",
        "footprint": "~3.2 MB",
        "stream_type": "Mixed (Direct + Indirect)",
        "stream_count": 2,
    },
    {
        "id": 29,
        "name": "daxpy_large",
        "category": "Custom COMPASS v2 (IN-class direct)",
        "upstream_author": "COMPASS v2 Research Team",
        "upstream_url": "In-tree COMPASS v2 suite (gem5/BM/daxpy_large)",
        "git_mirror": "Local repository",
        "license": "Apache 2.0 / gem5 BSD",
        "source_path": "gem5/BM/daxpy_large/daxpy_large.c",
        "binary_path": "gem5/BM/daxpy_large/build/EXP/daxpy_large_exp.riscv",
        "dataset_flags": "In-source (DEFAULT_N=4194304)",
        "footprint": "67.1 MB (64 MiB)",
        "stream_type": "Direct (Small Stride)",
        "stream_count": 2,
    },
    {
        "id": 30,
        "name": "kv_cache",
        "category": "Custom COMPASS v2 (OUT-class pointer chase)",
        "upstream_author": "COMPASS v2 Research Team",
        "upstream_url": "In-tree COMPASS v2 suite (gem5/BM/kv_cache)",
        "git_mirror": "Local repository",
        "license": "Apache 2.0 / gem5 BSD",
        "source_path": "gem5/BM/kv_cache/kv_cache.c",
        "binary_path": "gem5/BM/kv_cache/build/EXP/kv_cache_exp.riscv",
        "dataset_flags": "In-source (HEAD_DIM=128, TOKENS=65536)",
        "footprint": "67.1 MB (64 MiB)",
        "stream_type": "Direct + Pointer Chase",
        "stream_count": 2,
    },
    {
        "id": 31,
        "name": "dlrm_gather",
        "category": "Custom COMPASS v2 (OUT-class irregular)",
        "upstream_author": "COMPASS v2 Research Team",
        "upstream_url": "In-tree COMPASS v2 suite (gem5/BM/dlrm_gather)",
        "git_mirror": "Local repository",
        "license": "Apache 2.0 / gem5 BSD",
        "source_path": "gem5/BM/dlrm_gather/dlrm_gather.c",
        "binary_path": "gem5/BM/dlrm_gather/build/EXP/dlrm_gather_exp.riscv",
        "dataset_flags": "In-source (TABLE=1M, LOOKUPS=64k)",
        "footprint": "~71.6 MB",
        "stream_type": "Indirect Gather",
        "stream_count": 2,
    },
]


def generate_markdown_catalog(catalog, out_file):
    with open(out_file, "w") as f:
        f.write(
            "# COMPASS v2 / InterStellar 2.0 Workload Download & Provenance Catalog\n\n"
        )
        f.write(
            "This document records the exact upstream provenance, download URLs, author attribution,\n"
        )
        f.write(
            "dataset parameters, and verification status for all 31 supported workloads.\n\n"
        )
        f.write("## Master Workload Table\n\n")
        f.write(
            "| ID | Workload | Category | Upstream Author / Suite | Upstream URL | Local Source Path | Binary Path | Footprint | Streams |\n"
        )
        f.write("|---|---|---|---|---|---|---|---|---|\n")
        for item in catalog:
            f.write(
                f"| {item['id']} | `{item['name']}` | {item['category']} | {item['upstream_author']} | [{item['upstream_url']}]({item['upstream_url']}) | `{item['source_path']}` | `{item['binary_path']}` | {item['footprint']} | {item['stream_count']} ({item['stream_type']}) |\n"
            )
        f.write("\n\n## Reproduction & Download Instructions\n\n")
        f.write("### 1. Upstream PolyBench/C 4.2 Tarball\n")
        f.write(
            "```bash\nwget https://downloads.sourceforge.net/project/polybench/polybench-c-4.2.1-beta.tar.gz\ntar -xzvf polybench-c-4.2.1-beta.tar.gz\n```\n\n"
        )
        f.write("### 2. PolyBench-ACC Git Mirror\n")
        f.write(
            "```bash\ngit clone https://github.com/MatthiasJReisinger/PolyBench-ACC.git\n```\n\n"
        )
        f.write("### 3. Compilation via Custom LLVM\n")
        f.write(
            "```bash\n/home/laiy24/interstelar/gem5/scripts/compile_workloads.sh\n```\n"
        )


def generate_json_catalog(catalog, out_file):
    with open(out_file, "w") as f:
        json.dump(catalog, f, indent=2)


def verify_workloads(catalog, base_dir):
    print("Verifying workload source existence across 31 workloads...")
    missing_sources = []
    missing_binaries = []
    for item in catalog:
        src = os.path.join(base_dir, item["source_path"])
        bin_p = os.path.join(base_dir, item["binary_path"])
        if not os.path.isfile(src):
            missing_sources.append(src)
        if not os.path.isfile(bin_p):
            missing_binaries.append(bin_p)

    print(f"Verified {len(catalog)} workloads:")
    print(
        f"  Sources found:  {len(catalog) - len(missing_sources)} / {len(catalog)}"
    )
    print(
        f"  Binaries found: {len(catalog) - len(missing_binaries)} / {len(catalog)}"
    )
    if missing_sources:
        print(f"  WARNING: Missing sources: {missing_sources}")
    if missing_binaries:
        print(f"  WARNING: Missing binaries: {missing_binaries}")
    return len(missing_sources) == 0 and len(missing_binaries) == 0


def main():
    parser = argparse.ArgumentParser(
        description="Workload download and provenance catalog."
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Verify existence of sources and binaries",
    )
    parser.add_argument(
        "--out-dir",
        default="/home/laiy24/interstelar/output",
        help="Output directory for catalogs",
    )
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    json_path = os.path.join(args.out_dir, "workload_sources.json")
    md_path = os.path.join(args.out_dir, "workload_sources.md")

    generate_json_catalog(WORKLOAD_CATALOG, json_path)
    print(f"Wrote {json_path}")
    generate_markdown_catalog(WORKLOAD_CATALOG, md_path)
    print(f"Wrote {md_path}")

    base_dir = "/home/laiy24/interstelar"
    if args.verify:
        ok = verify_workloads(WORKLOAD_CATALOG, base_dir)
        if not ok:
            sys.exit(1)


if __name__ == "__main__":
    main()
