#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/metaisa.h"

#define DEFAULT_N                                                             \
    4194304 // 4M doubles = 32 MB per array, 64 MB total working set

int
main(int argc, char **argv)
{
    size_t N = DEFAULT_N;
    if (argc > 1) {
        size_t parsed = atol(argv[1]);
        if (parsed > 0) {
            N = parsed;
        }
    }

    printf("COMPASS v2 daxpy_large: N=%zu (%zu MB total working set)\n", N,
           (N * sizeof(double) * 2) / (1024 * 1024));

    double *x = (double *)malloc(N * sizeof(double));
    double *y = (double *)malloc(N * sizeof(double));
    if (!x || !y) {
        fprintf(stderr, "Allocation failed!\n");
        return 1;
    }

    const double a = 3.141592653589793;

    // Initialization: warm touch so pages are mapped
    for (size_t i = 0; i < N; i += 512) {
        x[i] = (double)(i & 0xFF);
        y[i] = (double)((i + 1) & 0xFF);
    }
    // Also touch first and last
    x[0] = 1.0;
    y[0] = 2.0;
    x[N - 1] = 1.0;
    y[N - 1] = 2.0;

    // MetaISA Descriptors Setup
    // 1. Loop Descriptor
    MISA_LoopDesc_t loop1_meta = {.parentLoopId = LOOP1_ID,
                                  .initValLinked = INIT_NOT_LINKED,
                                  .endValLinked = END_NOT_LINKED,
                                  .initVal = 0,
                                  .endVal = (uint32_t)N,
                                  .inc = 1,
                                  .headerPCOffset = 0x1000};
    MISA_Desc_t loop1_desc = {.type = LOOP,
                              .valid = VALID,
                              .active = ACTIVE,
                              .descInfo.loopDesc = loop1_meta};
    uint64_t *loop1_words = (uint64_t *)&loop1_desc;

    // 2. Stream X (Direct Stream, stride = 8 bytes)
    MISA_DirStreamDesc_t x_meta = {.loopDescId = LOOP1_ID,
                                   .linked = 0,
                                   .reserved = RESERVED,
                                   .baseAddr = (uint64_t)x,
                                   .stride = sizeof(double),
                                   .streamAttr = 0,
                                   .reserved_ = RESERVED};
    MISA_Desc_t x_desc = {.type = DIR_STREAM,
                          .valid = VALID,
                          .active = ACTIVE,
                          .descInfo.streamDesc = x_meta};
    uint64_t *x_words = (uint64_t *)&x_desc;

    // 3. Stream Y (Direct Stream, stride = 8 bytes)
    MISA_DirStreamDesc_t y_meta = {.loopDescId = LOOP1_ID,
                                   .linked = 0,
                                   .reserved = RESERVED,
                                   .baseAddr = (uint64_t)y,
                                   .stride = sizeof(double),
                                   .streamAttr = 0,
                                   .reserved_ = RESERVED};
    MISA_Desc_t y_desc = {.type = DIR_STREAM,
                          .valid = VALID,
                          .active = ACTIVE,
                          .descInfo.streamDesc = y_meta};
    uint64_t *y_words = (uint64_t *)&y_desc;

    printf("Configuring MetaISA descriptors: X=%p, Y=%p\n", (void *)x,
           (void *)y);

    // CSR Writes (128-bit descriptors = two 64-bit CSR writes)
    write_csr(0x800, loop1_words[0]);
    write_csr(0x801, loop1_words[1]);

    write_csr(0x802, x_words[0]);
    write_csr(0x803, x_words[1]);

    write_csr(0x804, y_words[0]);
    write_csr(0x805, y_words[1]);

    // Timed streaming kernel
    for (size_t i = 0; i < N; ++i) {
        y[i] += a * x[i];
    }

    double checksum = y[0] + y[N / 2] + y[N - 1];
    printf("daxpy_large complete. Checksum: %f\n", checksum);

    free(x);
    free(y);
    return 0;
}
