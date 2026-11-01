#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/metaisa.h"

#define EMBED_DIM 16
#define TABLE_ENTRIES 1048576 // 1M vectors * 16 floats * 4B = 64 MB table
#define NUM_LOOKUPS 65536     // 64k gather operations

int
main(int argc, char **argv)
{
    size_t num_lookups = NUM_LOOKUPS;
    if (argc > 1) {
        size_t parsed = atol(argv[1]);
        if (parsed > 0) {
            num_lookups = parsed;
        }
    }

    size_t table_bytes = TABLE_ENTRIES * EMBED_DIM * sizeof(float);
    printf("COMPASS v2 Meta DLRM-v2 Sparse Embedding Table Gather: %zu "
           "lookups, dim=%d (%zu MB table)\n",
           num_lookups, EMBED_DIM, table_bytes / (1024 * 1024));

    float *table = (float *)malloc(table_bytes);
    uint32_t *indices = (uint32_t *)malloc(num_lookups * sizeof(uint32_t));
    float *out = (float *)malloc(num_lookups * EMBED_DIM * sizeof(float));

    if (!table || !indices || !out) {
        fprintf(stderr, "Allocation failed!\n");
        return 1;
    }

    // Touch table to allocate pages
    for (size_t i = 0; i < TABLE_ENTRIES * EMBED_DIM; i += 1024) {
        table[i] = 0.001f * (float)(i & 0xFF);
    }
    table[0] = 1.0f;
    table[TABLE_ENTRIES * EMBED_DIM - 1] = 1.0f;

    // Generate indices (strided / pseudo-random gather pattern)
    for (size_t i = 0; i < num_lookups; ++i) {
        indices[i] = (uint32_t)((i * 997 + 13) % TABLE_ENTRIES);
    }

    // Setup MetaISA Descriptors
    // Loop 1: Over all lookups
    MISA_LoopDesc_t loop_meta = {.parentLoopId = LOOP1_ID,
                                 .initValLinked = INIT_NOT_LINKED,
                                 .endValLinked = END_NOT_LINKED,
                                 .initVal = 0,
                                 .endVal = (uint32_t)num_lookups,
                                 .inc = 1,
                                 .headerPCOffset = 0x1000};
    MISA_Desc_t loop_desc = {.type = LOOP,
                             .valid = VALID,
                             .active = ACTIVE,
                             .descInfo.loopDesc = loop_meta};
    uint64_t *loop_words = (uint64_t *)&loop_desc;

    // Stream 1: Direct stream on indices array
    MISA_DirStreamDesc_t idx_meta = {.loopDescId = LOOP1_ID,
                                     .linked = 0,
                                     .reserved = RESERVED,
                                     .baseAddr = (uint64_t)indices,
                                     .stride = sizeof(uint32_t),
                                     .streamAttr = 0,
                                     .reserved_ = RESERVED};
    MISA_Desc_t idx_desc = {.type = DIR_STREAM,
                            .valid = VALID,
                            .active = ACTIVE,
                            .descInfo.streamDesc = idx_meta};
    uint64_t *idx_words = (uint64_t *)&idx_desc;

    // Stream 2: Indirect stream on embedding table (Type = 0x02 INDIR_STREAM)
    MISA_IndirStreamDesc_t tbl_meta = {.parentStreamId =
                                           1, // index stream is stream 1
                                       .linked = 0,
                                       .reserved = RESERVED,
                                       .baseAddr = (uint64_t)table,
                                       .stride = EMBED_DIM * sizeof(float),
                                       .streamAttr = 0,
                                       .reserved_ = RESERVED};
    MISA_Desc_t tbl_desc = {.type = INDIR_STREAM,
                            .valid = VALID,
                            .active = ACTIVE,
                            .descInfo.indirDesc = tbl_meta};
    uint64_t *tbl_words = (uint64_t *)&tbl_desc;

    printf("Configuring MetaISA descriptors: Table=%p, Indices=%p\n",
           (void *)table, (void *)indices);

    write_csr(0x800, loop_words[0]);
    write_csr(0x801, loop_words[1]);

    write_csr(0x802, idx_words[0]);
    write_csr(0x803, idx_words[1]);

    write_csr(0x804, tbl_words[0]);
    write_csr(0x805, tbl_words[1]);

    // Gather kernel: for each lookup, copy embedding vector
    for (size_t i = 0; i < num_lookups; ++i) {
        uint32_t idx = indices[i];
        const float *vec = &table[idx * EMBED_DIM];
        float *dst = &out[i * EMBED_DIM];
        for (int d = 0; d < EMBED_DIM; ++d) {
            dst[d] = vec[d];
        }
    }

    float checksum = out[0] + out[num_lookups * EMBED_DIM / 2] +
                     out[num_lookups * EMBED_DIM - 1];
    printf("DLRM embedding table gather complete. Checksum: %f\n", checksum);

    free(table);
    free(indices);
    free(out);
    return 0;
}
