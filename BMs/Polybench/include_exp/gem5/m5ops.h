/*
 * Stub gem5 m5ops header for the EXP workload builds.
 *
 * Some annotated v2 sources contain ACTIVE m5_* calls (e.g. m5_reset_stats).
 * Under gem5 m5ops are normally pseudo-instructions; for these static
 * RISC-V builds we expand them to no-ops, which is the correct behavior for
 * the golden AtomicSimpleCPU check (stats are read from stats.txt anyway).
 *
 * Kept macro-only (no function bodies) so it parses standalone.
 */
#ifndef GEM5_M5OPS_H
#define GEM5_M5OPS_H

#define m5_reset_stats(x, y)                                                  \
    do {                                                                      \
    } while (0)
#define m5_dump_stats(x, y)                                                   \
    do {                                                                      \
    } while (0)
#define m5_checkpoint(x, y)                                                   \
    do {                                                                      \
    } while (0)
#define m5_work_begin(x, y)                                                   \
    do {                                                                      \
    } while (0)
#define m5_work_end(x, y)                                                     \
    do {                                                                      \
    } while (0)
#define m5_work_begin_count(x, y)                                             \
    do {                                                                      \
    } while (0)
#define m5_work_end_count(x, y)                                               \
    do {                                                                      \
    } while (0)

#endif /* GEM5_M5OPS_H */
