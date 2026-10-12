/* crt.h - rand() of the MSVC C runtime the original links (spel.dat sub_470120: holdrand at 0x4a21b8 = 1 at start,
 * holdrand * 214013 + 2531011, return bits 16..30, 0..32767; srand only in prt_alloc_pool 0x431fb0).
 *
 * The engine was written against it: ai.c scales rand() by 1/32767, particles.c subtracts 0x3fff.
 * glibc, bionic (Android) and newlib (Switch) have RAND_MAX 0x7fffffff and another sequence, so the host's rand() would
 * give AI speed factors in the thousands there. crt_rand() is the same on every platform and matches the Windows build.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_CRT_H
#define A2_CRT_H

#define CRT_RAND_MAX 32767

extern unsigned g_crt_seed;            /* main.c; holdrand 0x4a21b8, 1 at start */

static inline int crt_rand(void)
{
    g_crt_seed = g_crt_seed * 214013u + 2531011u;
    return (int)(g_crt_seed >> 16 & 0x7fff);
}

static inline void crt_srand(unsigned s) { g_crt_seed = s; }

#endif
