#ifndef ENCODING_H
#define ENCODING_H

// Stub for RISC-V encoding.
//
// The annotated v2 kernels emit their MetaISA descriptors through paired
// csrrw writes to CSRs 0x800-0x8C7 (see metaisa.hpp).  This is the same
// macro used by the project's historic stub (build_benchmark.sh).
#define write_csr(csr, value)                                                 \
    asm volatile("csrrw x0, %0, %1" ::"i"(csr), "r"(value))

// InterStellar CSR base address
#define CSR_BASE 0x800

#endif
