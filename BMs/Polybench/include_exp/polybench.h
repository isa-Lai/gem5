#ifndef POLYBENCH_H
#define POLYBENCH_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DATA_TYPE double
#define DATA_TYPE_IS_DOUBLE 1
#define DATA_PRINTF_MODIFIER "%0.2lf "

/* ------------------------------------------------------------------ *
 * Simplified array allocation for single-file compilation            *
 * (malloc-based pointer arrays; kernels receive them through the     *
 *  POLYBENCH_kD() prototype macros below)                            *
 * ------------------------------------------------------------------ */
#define POLYBENCH_2D_ARRAY_DECL(A, TYPE, M, N, m, n)                          \
    TYPE **A = (TYPE **)malloc((m) * sizeof(TYPE *));                         \
    for (int i = 0; i < (m); i++)                                             \
    A[i] = (TYPE *)malloc((n) * sizeof(TYPE))

#define POLYBENCH_1D_ARRAY_DECL(A, TYPE, N, n)                                \
    TYPE *A = (TYPE *)malloc((n) * sizeof(TYPE))

#define POLYBENCH_3D_ARRAY_DECL(A, TYPE, X, Y, Z, x, y, z)                    \
    TYPE ***A = (TYPE ***)malloc((x) * sizeof(TYPE **));                      \
    for (int i = 0; i < (x); i++) {                                           \
        A[i] = (TYPE **)malloc((y) * sizeof(TYPE *));                         \
        for (int j = 0; j < (y); j++)                                         \
            A[i][j] = (TYPE *)malloc((z) * sizeof(TYPE));                     \
    }

/* Prototype-side macros: expand to the pointer indirection levels that
   match the *_ARRAY_DECL allocation above. */
#define POLYBENCH_1D(A, N, n) *A
#define POLYBENCH_2D(A, M, N, m, n) **A
#define POLYBENCH_3D(A, X, Y, Z, x, y, z) ***A
#define POLYBENCH_4D(A, W, X, Y, Z, w, x, y, z) ****A

#define POLYBENCH_ARRAY(A) (A)

#define POLYBENCH_FREE_ARRAY(A)                                               \
    do {                                                                      \
        free((void *)(A));                                                    \
    } while (0)

#define POLYBENCH_LOOP_BOUND(N, n) (n)

/* Timing instruments: no-op stubs (the golden check measures sim_insts). */
#define polybench_start_instruments                                           \
    do {                                                                      \
    } while (0)
#define polybench_stop_instruments                                            \
    do {                                                                      \
    } while (0)
#define polybench_print_instruments                                           \
    do {                                                                      \
    } while (0)

/* Keep live-out data reachable so the kernel/print_array is not DCE'd. */
#define polybench_prevent_dce(x)                                              \
    do {                                                                      \
        __asm__ __volatile__("" ::: "memory");                                \
        x;                                                                    \
        __asm__ __volatile__("" ::: "memory");                                \
    } while (0)

#define POLYBENCH_DUMP_START
#define POLYBENCH_DUMP_END
#define POLYBENCH_DUMP_BEGIN(x)
#define POLYBENCH_DUMP_FINISH
#define POLYBENCH_DUMP_TARGET stdout

#define SCALAR_VAL(x) x
#define SQRT_FUN(x) sqrt(x)
#define EXP_FUN(x) exp(x)
#define POW_FUN(x, y) pow(x, y)

#endif
