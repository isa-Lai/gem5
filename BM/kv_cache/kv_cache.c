#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/metaisa.h"

// LLaMA-3 Head Dimension = 128
#define HEAD_DIM 128
#define DEFAULT_NUM_TOKENS                                                    \
    65536 // 65536 * 128 * 4 bytes = 32 MB for K, 32 MB for V = 64 MB total

int
main(int argc, char **argv)
{
    size_t num_tokens = DEFAULT_NUM_TOKENS;
    if (argc > 1) {
        size_t parsed = atol(argv[1]);
        if (parsed > 0) {
            num_tokens = parsed;
        }
    }

    size_t total_elements = num_tokens * HEAD_DIM;
    size_t kv_bytes = total_elements * sizeof(float);
    printf("COMPASS v2 LLaMA-3 KV-Cache Attention Streaming: %zu tokens, "
           "head_dim=%d (%zu MB total KV cache)\n",
           num_tokens, HEAD_DIM, (kv_bytes * 2) / (1024 * 1024));

    float *key_cache = (float *)malloc(kv_bytes);
    float *val_cache = (float *)malloc(kv_bytes);
    float *query = (float *)malloc(HEAD_DIM * sizeof(float));
    float *out = (float *)malloc(HEAD_DIM * sizeof(float));

    if (!key_cache || !val_cache || !query || !out) {
        fprintf(stderr, "Allocation failed!\n");
        return 1;
    }

    // Initialize query
    for (int d = 0; d < HEAD_DIM; ++d) {
        query[d] = 0.05f * (float)(d & 0x7);
        out[d] = 0.0f;
    }

    // Touch memory to allocate pages
    for (size_t i = 0; i < total_elements; i += 1024) {
        key_cache[i] = 0.01f * (float)(i & 0x1F);
        val_cache[i] = 0.02f * (float)(i & 0x1F);
    }
    key_cache[0] = 0.1f;
    key_cache[total_elements - 1] = 0.1f;
    val_cache[0] = 0.1f;
    val_cache[total_elements - 1] = 0.1f;

    // Setup MetaISA Descriptors
    // Loop 1: Over all tokens
    MISA_LoopDesc_t loop_meta = {.parentLoopId = LOOP1_ID,
                                 .initValLinked = INIT_NOT_LINKED,
                                 .endValLinked = END_NOT_LINKED,
                                 .initVal = 0,
                                 .endVal = (uint32_t)total_elements,
                                 .inc = 1,
                                 .headerPCOffset = 0x1000};
    MISA_Desc_t loop_desc = {.type = LOOP,
                             .valid = VALID,
                             .active = ACTIVE,
                             .descInfo.loopDesc = loop_meta};
    uint64_t *loop_words = (uint64_t *)&loop_desc;

    // Stream 1: Key cache sequential streaming
    MISA_DirStreamDesc_t k_meta = {.loopDescId = LOOP1_ID,
                                   .linked = 0,
                                   .reserved = RESERVED,
                                   .baseAddr = (uint64_t)key_cache,
                                   .stride = sizeof(float),
                                   .streamAttr = 0,
                                   .reserved_ = RESERVED};
    MISA_Desc_t k_desc = {.type = DIR_STREAM,
                          .valid = VALID,
                          .active = ACTIVE,
                          .descInfo.streamDesc = k_meta};
    uint64_t *k_words = (uint64_t *)&k_desc;

    // Stream 2: Value cache sequential streaming
    MISA_DirStreamDesc_t v_meta = {.loopDescId = LOOP1_ID,
                                   .linked = 0,
                                   .reserved = RESERVED,
                                   .baseAddr = (uint64_t)val_cache,
                                   .stride = sizeof(float),
                                   .streamAttr = 0,
                                   .reserved_ = RESERVED};
    MISA_Desc_t v_desc = {.type = DIR_STREAM,
                          .valid = VALID,
                          .active = ACTIVE,
                          .descInfo.streamDesc = v_meta};
    uint64_t *v_words = (uint64_t *)&v_desc;

    printf("Configuring MetaISA descriptors: K=%p, V=%p\n", (void *)key_cache,
           (void *)val_cache);

    write_csr(0x800, loop_words[0]);
    write_csr(0x801, loop_words[1]);

    write_csr(0x802, k_words[0]);
    write_csr(0x803, k_words[1]);

    write_csr(0x804, v_words[0]);
    write_csr(0x805, v_words[1]);

    // Attention streaming kernel (Decode Phase)
    // For each token, compute dot product Q . K[t] and accumulate V[t]
    float scale = 0.088388f; // 1 / sqrt(128)
    for (size_t t = 0; t < num_tokens; ++t) {
        float score = 0.0f;
        const float *k_token = &key_cache[t * HEAD_DIM];
        for (int d = 0; d < HEAD_DIM; ++d) {
            score += query[d] * k_token[d];
        }
        score *= scale;

        const float *v_token = &val_cache[t * HEAD_DIM];
        for (int d = 0; d < HEAD_DIM; ++d) {
            out[d] += score * v_token[d];
        }
    }

    float checksum = out[0] + out[HEAD_DIM / 2] + out[HEAD_DIM - 1];
    printf("KV-Cache Attention streaming complete. Checksum: %f\n", checksum);

    free(key_cache);
    free(val_cache);
    free(query);
    free(out);
    return 0;
}
