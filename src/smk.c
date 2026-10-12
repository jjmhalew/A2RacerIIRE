/*
 * smk.c - self-contained Smacker (SMK2 / SMK4) video + audio decoder.
 *
 * Part of A2RacerIIRE, a reimplementation of the "A2 Racer II" engine.
 * Copyright (C) 2026 A2RacerIIRE contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the repository root.
 *
 * Written from the public format description (MultimediaWiki "Smacker") and
 * verified against the game's own films; no libsmacker/FFmpeg code is used.
 *
 * ---------------------------------------------------------------------------
 * Format overview (all integers little-endian):
 *
 *   Header (104 bytes)
 *     0  "SMK2" or "SMK4"
 *     4  width, 8 height, 12 frames (logical; a ring frame may follow)
 *     16 frame rate (int32): >0 ms per frame, <0 -(10 us units), 0 = 10 fps
 *     20 flags: bit0 ring frame, bit1 Y-interlaced, bit2 Y-doubled
 *     24 audio_size[7]  largest unpacked audio chunk per track
 *     52 trees_size     bytes of the Huffman tree section
 *     56 mmap/mclr/full/type allocation sizes (hints only, unused here)
 *     72 audio_rate[7]  bit31 compressed, bit30 present, bit29 16-bit,
 *                       bit28 stereo, bits27-26 !=0: Bink audio,
 *                       bits23-0 sample rate
 *     100 unused
 *   uint32 frame_size[n]   (low 2 bits are flags: bit0 = keyframe; mask them)
 *   uint8  frame_type[n]   bit0 palette chunk, bit(1+t) audio chunk track t
 *   trees  (trees_size bytes, bitstream): MMap, MClr, Full, Type big trees
 *   frame data, frame after frame: [palette] [audio t0..t6] video
 *   with n = frames + (ring frame ? 1 : 0).
 *
 * Bitstreams are read LSB first within each byte.
 * ---------------------------------------------------------------------------
 */
#include "smk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sanity limits that keep corrupt headers from causing huge allocations. */
#define SMK_MAX_DIM        4096u
#define SMK_MAX_FRAMES     (1u << 22)
#define SMK_MAX_TREE_NODES (1u << 18)  /* a 16-bit tree needs < 2^17 nodes */
#define SMK_MAX_TREE_DEPTH 64          /* far above any real code length    */
#define SMK_MAX_AUDIO      (1u << 22)  /* max unpacked bytes per audio chunk */

/* ======================================================================== */
/* Bit reader (LSB first, bounds checked: reading past the end yields 0 bits
 * and sets the overrun flag).                                              */
/* ======================================================================== */

typedef struct {
    const uint8_t *p, *end;
    unsigned acc, n;   /* current byte and number of unread bits in it */
    int overrun;
} bitrd;

static void br_init(bitrd *b, const uint8_t *p, size_t len)
{
    b->p = p;
    b->end = p + len;
    b->acc = 0;
    b->n = 0;
    b->overrun = 0;
}

static unsigned br_bit(bitrd *b)
{
    unsigned v;
    if (b->n == 0) {
        if (b->p >= b->end) {
            b->overrun = 1;
            return 0;
        }
        b->acc = *b->p++;
        b->n = 8;
    }
    v = b->acc & 1u;
    b->acc >>= 1;
    b->n--;
    return v;
}

static unsigned br_bits(bitrd *b, unsigned count)
{
    unsigned v = 0, i;
    for (i = 0; i < count; i++)
        v |= br_bit(b) << i;
    return v;
}

/* ======================================================================== */
/* Huffman trees                                                            */
/*                                                                          */
/* A packed tree is: Tag bit (0 = no tree), then the tree in pre-order:     */
/* a 1 bit is an internal node followed by its '0' subtree and then its '1' */
/* subtree; a 0 bit is a leaf followed by its value. After the tree a       */
/* single 0 bit terminates it. Byte trees store 8-bit leaf values; "big"    */
/* trees store 16-bit values, each coded with two byte trees (low byte     */
/* first, then high byte).                                                  */
/*                                                                          */
/* Unpacked trees are kept in pre-order in a flat array, so the '0' child   */
/* of node i is always node i+1 and only the '1' child index is stored:     */
/*   internal node: index of its '1' child                                  */
/*   leaf:          TREE_LEAF | (cache slot+1) << 16 | value                */
/* An empty tree (count == 0) decodes to 0 without consuming bits; a tree   */
/* that is a single leaf decodes to that leaf without consuming bits.      */
/* ======================================================================== */

#define TREE_LEAF 0x80000000u

typedef struct {
    uint32_t *node;
    uint32_t count, cap;
} htree;

/* A big (16-bit) tree with the 3-entry "recent values" cache. The header
 * names three escape values; leaves carrying them are not literal values
 * but refer to cache slots 0..2. After every decode, a value differing from
 * cache[0] is pushed to the front (cache[2] <- cache[1] <- cache[0] <- v).
 * The cache is cleared to zero at the start of each video frame. */
typedef struct {
    htree t;
    uint16_t cache[3];
} bigtree;

static int ht_alloc(htree *t)
{
    if (t->count >= t->cap) {
        uint32_t ncap = t->cap ? t->cap * 2 : 512;
        uint32_t *nn;
        if (t->count >= SMK_MAX_TREE_NODES)
            return -1;
        if (ncap > SMK_MAX_TREE_NODES)
            ncap = SMK_MAX_TREE_NODES;
        nn = (uint32_t *)realloc(t->node, ncap * sizeof *nn);
        if (!nn)
            return -1;
        t->node = nn;
        t->cap = ncap;
    }
    return (int)t->count++;
}

static unsigned ht_decode(const htree *t, bitrd *b)
{
    uint32_t i = 0, n;
    if (t->count == 0)
        return 0;
    /* Children always have larger indices, so this loop terminates even on
     * an exhausted bitstream (which supplies zero bits). */
    while (!((n = t->node[i]) & TREE_LEAF))
        i = br_bit(b) ? n : i + 1;
    return n;
}

/* Recursively unpack one subtree of a byte tree. */
static int bytetree_node(htree *t, bitrd *b, int depth)
{
    int idx;
    if (depth > SMK_MAX_TREE_DEPTH || b->overrun)
        return -1;
    if ((idx = ht_alloc(t)) < 0)
        return -1;
    if (br_bit(b)) {
        if (bytetree_node(t, b, depth + 1) < 0)
            return -1;
        t->node[idx] = t->count;           /* '1' child follows the '0' subtree */
        return bytetree_node(t, b, depth + 1);
    }
    t->node[idx] = TREE_LEAF | br_bits(b, 8);
    return b->overrun ? -1 : 0;
}

/* Read a complete packed byte tree (tag, tree, terminating bit). */
static int bytetree_read(htree *t, bitrd *b)
{
    t->count = 0;
    if (!br_bit(b))
        return b->overrun ? -1 : 0;        /* no tree: decodes to 0 */
    if (bytetree_node(t, b, 0) < 0) {
        t->count = 0;
        return -1;
    }
    br_bit(b);                             /* end-of-tree bit (0) */
    return b->overrun ? -1 : 0;
}

typedef struct {
    const htree *lo, *hi;
    unsigned esc[3];
    int found[3];
} bigctx;

static int bigtree_node(htree *t, bitrd *b, bigctx *c, int depth)
{
    int idx, k;
    unsigned v;
    if (depth > SMK_MAX_TREE_DEPTH || b->overrun)
        return -1;
    if ((idx = ht_alloc(t)) < 0)
        return -1;
    if (br_bit(b)) {
        if (bigtree_node(t, b, c, depth + 1) < 0)
            return -1;
        t->node[idx] = t->count;
        return bigtree_node(t, b, c, depth + 1);
    }
    v = ht_decode(c->lo, b) & 0xFFu;
    v |= (ht_decode(c->hi, b) & 0xFFu) << 8;
    t->node[idx] = TREE_LEAF | v;
    /* A leaf equal to an escape value becomes a reference to a cache slot
     * (first matching escape wins; each escape is used for one leaf). */
    for (k = 0; k < 3; k++) {
        if (!c->found[k] && v == c->esc[k]) {
            c->found[k] = 1;
            t->node[idx] = TREE_LEAF | ((uint32_t)(k + 1) << 16);
            break;
        }
    }
    return b->overrun ? -1 : 0;
}

/* Read one of the four header trees:
 *   Tag; low-byte tree; high-byte tree; 3 x 16-bit escapes; 16-bit tree;
 *   end bit. tmp_lo/tmp_hi are scratch byte trees. */
static int bigtree_read(bigtree *bt, bitrd *b, htree *tmp_lo, htree *tmp_hi)
{
    bigctx c;
    bt->t.count = 0;
    memset(bt->cache, 0, sizeof bt->cache);
    if (!br_bit(b))
        return b->overrun ? -1 : 0;        /* tree absent: all values 0 */
    if (bytetree_read(tmp_lo, b) < 0 || bytetree_read(tmp_hi, b) < 0)
        return -1;
    c.lo = tmp_lo;
    c.hi = tmp_hi;
    c.esc[0] = br_bits(b, 16);
    c.esc[1] = br_bits(b, 16);
    c.esc[2] = br_bits(b, 16);
    c.found[0] = c.found[1] = c.found[2] = 0;
    if (bigtree_node(&bt->t, b, &c, 0) < 0) {
        bt->t.count = 0;
        return -1;
    }
    br_bit(b);                             /* end-of-tree bit (0) */
    return b->overrun ? -1 : 0;
}

static unsigned bigtree_decode(bigtree *bt, bitrd *b)
{
    uint32_t n;
    unsigned v, slot;
    if (bt->t.count == 0)
        return 0;
    n = ht_decode(&bt->t, b);
    slot = (n >> 16) & 3u;
    v = slot ? bt->cache[slot - 1] : (n & 0xFFFFu);
    if (v != bt->cache[0]) {
        bt->cache[2] = bt->cache[1];
        bt->cache[1] = bt->cache[0];
        bt->cache[0] = (uint16_t)v;
    }
    return v;
}

/* ======================================================================== */
/* Decoder state                                                            */
/* ======================================================================== */

enum { TREE_MMAP, TREE_MCLR, TREE_FULL, TREE_TYPE };

typedef struct {
    int coding;            /* SMK_AUDIO_* */
    unsigned rate, channels, bits;
    int16_t *buf;          /* interleaved output of the current frame */
    size_t cap;            /* capacity in samples (all channels) */
    size_t nsamples;       /* per channel */
} smk_track;

struct smk_s {
    const uint8_t *data;
    size_t size;
    uint8_t *owned;        /* copy of the file data (if we own it) */

    int version;           /* 2 or 4 */
    unsigned width, height, frames, nphys;
    unsigned usec_per_frame;
    int y_mode;
    unsigned bw, bh;       /* size in 4x4 blocks */

    size_t *frame_off;     /* nphys entries */
    uint32_t *frame_len;   /* bytes (multiple of 4); bit 0 set = clipped */
    uint8_t *frame_type;

    bigtree tree[4];
    htree audio_tree[4];
    htree scratch[2];

    uint8_t *work;         /* bw*4 x bh*4 decode surface */
    uint8_t *out;          /* width x height (== work if no padding) */
    uint8_t palette[768];
    int pal_changed;

    smk_track track[SMK_MAX_TRACKS];

    int cur;               /* next frame to decode */
};

/* Two's complement 16-bit wrap without implementation-defined casts. */
static int wrap16(unsigned v)
{
    v &= 0xFFFFu;
    return v >= 0x8000u ? (int)v - 0x10000 : (int)v;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ======================================================================== */
/* Palette chunk                                                            */
/*                                                                          */
/*   byte length (chunk size including this byte, divided by 4)            */
/*   blocks until 256 entries are produced or the chunk ends:               */
/*     1ccccccc           keep (c+1) entries of the previous palette at    */
/*                        the current position                              */
/*     01cccccc ssssssss  copy (c+1) entries from the previous palette,     */
/*                        starting at entry s                               */
/*     00rrrrrr 00gggggg 00bbbbbb  one new entry, 6-bit components         */
/* The copies always read the palette as it was before this chunk (the     */
/* chunk remaps the old palette into the new one). Entries not reached     */
/* keep their previous value.                                               */
/* ======================================================================== */

static uint8_t pal6to8(unsigned v)
{
    v &= 0x3Fu;
    return (uint8_t)((v << 2) | (v >> 4)); /* 0..63 -> 0..255 */
}

static int decode_palette(smk_t *s, const uint8_t *p, size_t len)
{
    uint8_t old[768];
    size_t i = 1;
    unsigned c = 0;
    int bad = 0;

    memcpy(old, s->palette, sizeof old);
    while (c < 256 && i < len) {
        unsigned b = p[i];
        if (b & 0x80) {
            unsigned n = (b & 0x7Fu) + 1;
            if (n > 256 - c)
                n = 256 - c, bad = 1;
            /* entries c..c+n-1 stay as in the old palette */
            c += n;
            i += 1;
        } else if (b & 0x40) {
            unsigned n = (b & 0x3Fu) + 1, src;
            if (i + 1 >= len) {
                bad = 1;
                break;
            }
            src = p[i + 1];
            if (n > 256 - c)
                n = 256 - c, bad = 1;
            if (n > 256 - src)
                n = 256 - src, bad = 1;
            memcpy(s->palette + c * 3, old + src * 3, n * 3);
            c += n;
            i += 2;
        } else {
            if (i + 2 >= len) {
                bad = 1;
                break;
            }
            s->palette[c * 3 + 0] = pal6to8(p[i]);
            s->palette[c * 3 + 1] = pal6to8(p[i + 1]);
            s->palette[c * 3 + 2] = pal6to8(p[i + 2]);
            c++;
            i += 3;
        }
    }
    return bad ? -1 : 0;
}

/* ======================================================================== */
/* Audio chunk                                                              */
/*                                                                          */
/*   uint32 length (including itself)                                       */
/*   PCM:  raw samples (8-bit unsigned or 16-bit signed, interleaved)       */
/*   DPCM: uint32 unpacked_length, then a bitstream:                        */
/*         bit data_present, bit stereo, bit 16-bit,                        */
/*         one byte tree per sample byte (1, 2 or 4) in decode order:       */
/*           [L lo, L hi, R lo, R hi] / [L, R] / [lo, hi] / [mono]          */
/*         base values, 8 bits each: right channel first, then left; a     */
/*           16-bit base is stored high byte first;                         */
/*         the bases are the first output sample; every further sample is   */
/*         the previous one plus a Huffman-coded delta per byte (low byte,  */
/*         then high byte; left, then right). For 16-bit audio lo|hi<<8     */
/*         forms a 16-bit delta added with wrap-around.                     */
/* All output is converted to signed 16-bit.                                */
/* ======================================================================== */

static int track_reserve(smk_track *t, size_t samples)
{
    if (samples > t->cap) {
        int16_t *nb = (int16_t *)realloc(t->buf, samples * sizeof *nb);
        if (!nb)
            return -1;
        t->buf = nb;
        t->cap = samples;
    }
    return 0;
}

static int decode_audio(smk_t *s, int tr, const uint8_t *p, size_t len)
{
    smk_track *t = &s->track[tr];
    unsigned ch = t->channels, bytes = t->bits / 8;
    size_t frame_bytes = (size_t)ch * bytes, n, k;
    int16_t *o;

    t->nsamples = 0;
    if (t->coding == SMK_AUDIO_NONE || t->coding == SMK_AUDIO_UNSUPPORTED)
        return 0;

    if (t->coding == SMK_AUDIO_PCM) {
        n = len / frame_bytes;
        if (track_reserve(t, n * ch) < 0)
            return -1;
        o = t->buf;
        if (bytes == 1) {
            for (k = 0; k < n * ch; k++)
                o[k] = (int16_t)(((int)p[k] - 128) * 256);
        } else {
            for (k = 0; k < n * ch; k++)
                o[k] = (int16_t)wrap16(p[2 * k] | (p[2 * k + 1] << 8));
        }
        t->nsamples = n;
        return (len % frame_bytes) ? -1 : 0;
    }

    /* Huffman DPCM */
    {
        bitrd b;
        uint32_t unpacked;
        unsigned stereo, is16, ntrees, i, c;
        int pred[2] = { 0, 0 };
        int bad = 0;

        if (len < 4)
            return -1;
        unpacked = rd32(p);
        if (unpacked > SMK_MAX_AUDIO) {
            unpacked = SMK_MAX_AUDIO;
            bad = 1;
        }
        n = unpacked / frame_bytes;
        if (track_reserve(t, n * ch + 1) < 0)
            return -1;
        o = t->buf;
        br_init(&b, p + 4, len - 4);

        if (!br_bit(&b)) {                 /* no data: silence */
            memset(o, 0, n * ch * sizeof *o);
            t->nsamples = n;
            return 0;
        }
        stereo = br_bit(&b);
        is16 = br_bit(&b);
        if (stereo != (ch == 2) || is16 != (bytes == 2)) {
            /* stream disagrees with the header: cannot interpret it */
            memset(o, 0, n * ch * sizeof *o);
            t->nsamples = n;
            return -1;
        }
        ntrees = 1u << (stereo + is16);
        for (i = 0; i < ntrees; i++) {
            if (bytetree_read(&s->audio_tree[i], &b) < 0) {
                memset(o, 0, n * ch * sizeof *o);   /* undecodable: silence */
                t->nsamples = n;
                return -1;
            }
        }

        /* bases: right channel first, high byte first */
        for (c = ch; c-- > 0;) {
            if (is16) {
                unsigned hi = br_bits(&b, 8);
                unsigned lo = br_bits(&b, 8);
                pred[c] = wrap16((hi << 8) | lo);
            } else {
                pred[c] = (int)br_bits(&b, 8);
            }
        }

        for (k = 0; k < n; k++) {
            for (c = 0; c < ch; c++) {
                if (k > 0) {
                    if (is16) {
                        unsigned lo = ht_decode(&s->audio_tree[2 * c], &b) & 0xFFu;
                        unsigned hi = ht_decode(&s->audio_tree[2 * c + 1], &b) & 0xFFu;
                        pred[c] = wrap16((unsigned)pred[c] + ((hi << 8) | lo));
                    } else {
                        pred[c] = (pred[c] + (int)(ht_decode(&s->audio_tree[c], &b) & 0xFFu)) & 0xFF;
                    }
                }
                o[k * ch + c] = is16 ? (int16_t)pred[c]
                                     : (int16_t)((pred[c] - 128) * 256);
            }
        }
        t->nsamples = n;
        return (bad || b.overrun) ? -1 : 0;
    }
}

/* ======================================================================== */
/* Video chunk                                                              */
/*                                                                          */
/* The frame is a grid of 4x4 blocks, coded left to right, top to bottom   */
/* as a single bitstream. A 16-bit value from the Type tree starts a run:  */
/*   bits 1-0  block type (0 mono, 1 full, 2 void, 3 solid)                 */
/*   bits 7-2  index into run_len[] = number of blocks of this type        */
/*   bits 15-8 colour for solid blocks                                      */
/* Block payloads (all values from the big trees):                          */
/*   mono : MClr value (hi = colour for 1 bits, lo = colour for 0 bits),    */
/*          MMap value: 16 bits, LSB = top-left, row by row                 */
/*   full : per row two Full values: first gives pixels 2,3 (lo, hi),      */
/*          second gives pixels 0,1.                                        */
/*          SMK4 prefixes each full run with a sub-type: bit 1 = "double"  */
/*          (each Full value paints two 2x2 squares: lo left, hi right;    */
/*          one value per pair of rows); bits 0,1 = "half" (one row coded  */
/*          as above, then repeated on the following row); bits 0,0 = the  */
/*          normal SMK2 full block.                                         */
/*   void : unchanged from the previous frame                               */
/*   solid: filled with the colour in bits 15-8 of the type value           */
/* ======================================================================== */

static const unsigned short run_len[64] = {
     1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048
};

enum { FULL_NORMAL, FULL_DOUBLE, FULL_HALF };

static int decode_video(smk_t *s, const uint8_t *p, size_t len)
{
    bitrd b;
    const size_t pitch = (size_t)s->bw * 4;
    const size_t nblocks = (size_t)s->bw * s->bh;
    size_t blk = 0;
    int i;

    for (i = 0; i < 4; i++)
        memset(s->tree[i].cache, 0, sizeof s->tree[i].cache);
    br_init(&b, p, len);

    while (blk < nblocks) {
        unsigned tv = bigtree_decode(&s->tree[TREE_TYPE], &b);
        unsigned type = tv & 3u;
        unsigned run = run_len[(tv >> 2) & 63u];
        unsigned extra = tv >> 8;
        int fmode = FULL_NORMAL;

        if (type == 1 && s->version == 4) {
            if (br_bit(&b))
                fmode = FULL_DOUBLE;
            else if (br_bit(&b))
                fmode = FULL_HALF;
        }

        for (; run > 0 && blk < nblocks; run--, blk++) {
            uint8_t *o = s->work + (blk / s->bw) * 4 * pitch + (blk % s->bw) * 4;
            unsigned r, v, w;
            switch (type) {
            case 0: { /* mono */
                unsigned colors = bigtree_decode(&s->tree[TREE_MCLR], &b);
                unsigned map = bigtree_decode(&s->tree[TREE_MMAP], &b);
                uint8_t c1 = (uint8_t)(colors >> 8), c0 = (uint8_t)colors;
                for (r = 0; r < 4; r++, o += pitch, map >>= 4) {
                    o[0] = (map & 1u) ? c1 : c0;
                    o[1] = (map & 2u) ? c1 : c0;
                    o[2] = (map & 4u) ? c1 : c0;
                    o[3] = (map & 8u) ? c1 : c0;
                }
                break;
            }
            case 1: /* full */
                if (fmode == FULL_NORMAL) {
                    for (r = 0; r < 4; r++, o += pitch) {
                        v = bigtree_decode(&s->tree[TREE_FULL], &b);
                        o[2] = (uint8_t)v;
                        o[3] = (uint8_t)(v >> 8);
                        v = bigtree_decode(&s->tree[TREE_FULL], &b);
                        o[0] = (uint8_t)v;
                        o[1] = (uint8_t)(v >> 8);
                    }
                } else if (fmode == FULL_DOUBLE) {
                    for (r = 0; r < 2; r++, o += 2 * pitch) {
                        v = bigtree_decode(&s->tree[TREE_FULL], &b);
                        o[0] = o[1] = o[pitch] = o[pitch + 1] = (uint8_t)v;
                        o[2] = o[3] = o[pitch + 2] = o[pitch + 3] = (uint8_t)(v >> 8);
                    }
                } else { /* FULL_HALF */
                    for (r = 0; r < 2; r++, o += 2 * pitch) {
                        w = bigtree_decode(&s->tree[TREE_FULL], &b);
                        v = bigtree_decode(&s->tree[TREE_FULL], &b);
                        o[0] = o[pitch] = (uint8_t)v;
                        o[1] = o[pitch + 1] = (uint8_t)(v >> 8);
                        o[2] = o[pitch + 2] = (uint8_t)w;
                        o[3] = o[pitch + 3] = (uint8_t)(w >> 8);
                    }
                }
                break;
            case 2: /* void */
                break;
            default: /* solid */
                for (r = 0; r < 4; r++, o += pitch)
                    memset(o, (int)extra, 4);
                break;
            }
        }
        if (b.overrun)
            return -1;   /* remaining blocks keep the previous picture */
    }
    return 0;
}

/* ======================================================================== */
/* Opening                                                                  */
/* ======================================================================== */

static smk_t *smk_open_internal(const uint8_t *data, size_t size, uint8_t *owned)
{
    smk_t *s;
    int32_t rate;
    uint32_t flags, trees_size;
    size_t tables, off;
    unsigned i;
    bitrd b;

    if (!data || size < 104 || data[0] != 'S' || data[1] != 'M' ||
        data[2] != 'K' || (data[3] != '2' && data[3] != '4')) {
        free(owned);
        return NULL;
    }
    s = (smk_t *)calloc(1, sizeof *s);
    if (!s) {
        free(owned);
        return NULL;
    }
    s->data = data;
    s->size = size;
    s->owned = owned;
    s->version = data[3] - '0';
    s->width = rd32(data + 4);
    s->height = rd32(data + 8);
    s->frames = rd32(data + 12);
    rate = (int32_t)rd32(data + 16);
    flags = rd32(data + 20);
    trees_size = rd32(data + 52);

    if (s->width == 0 || s->height == 0 || s->width > SMK_MAX_DIM ||
        s->height > SMK_MAX_DIM || s->frames > SMK_MAX_FRAMES)
        goto fail;

    if (rate > 0)
        s->usec_per_frame = (unsigned)rate * 1000u;
    else if (rate < 0)
        s->usec_per_frame = (unsigned)(-(int64_t)rate) * 10u;
    else
        s->usec_per_frame = 100000u;
    s->y_mode = (flags & 2u) ? SMK_Y_INTERLACED
              : (flags & 4u) ? SMK_Y_DOUBLED : SMK_Y_NORMAL;
    s->nphys = s->frames + ((flags & 1u) ? 1u : 0u);

    for (i = 0; i < SMK_MAX_TRACKS; i++) {
        uint32_t ar = rd32(data + 72 + 4 * i);
        smk_track *t = &s->track[i];
        if (!(ar & 0x40000000u)) {
            t->coding = SMK_AUDIO_NONE;
            continue;
        }
        t->rate = ar & 0xFFFFFFu;
        t->bits = (ar & 0x20000000u) ? 16 : 8;
        t->channels = (ar & 0x10000000u) ? 2 : 1;
        if (ar & 0x0C000000u)
            t->coding = SMK_AUDIO_UNSUPPORTED;
        else
            t->coding = (ar & 0x80000000u) ? SMK_AUDIO_DPCM : SMK_AUDIO_PCM;
    }

    /* frame size / type tables */
    tables = 104 + (size_t)s->nphys * 5;
    if (tables > size || trees_size > size - tables)
        goto fail;
    s->frame_off = (size_t *)malloc((s->nphys + 1) * sizeof *s->frame_off);
    s->frame_len = (uint32_t *)malloc((s->nphys + 1) * sizeof *s->frame_len);
    s->frame_type = (uint8_t *)malloc(s->nphys + 1);
    if (!s->frame_off || !s->frame_len || !s->frame_type)
        goto fail;
    off = tables + trees_size;
    for (i = 0; i < s->nphys; i++) {
        uint32_t len = rd32(data + 104 + 4 * (size_t)i) & ~3u;
        s->frame_type[i] = data[104 + 4 * (size_t)s->nphys + i];
        if (off > size)
            off = size;
        s->frame_off[i] = off;
        if (len > size - off) {            /* clip frames running off the end */
            len = (uint32_t)(size - off);
            s->frame_len[i] = (len & ~3u) | 1u;   /* bit 0: clipped */
        } else {
            s->frame_len[i] = len;
        }
        off += len;
    }

    /* Huffman trees: MMap, MClr, Full, Type */
    br_init(&b, data + tables, trees_size);
    for (i = 0; i < 4; i++) {
        if (bigtree_read(&s->tree[i], &b, &s->scratch[0], &s->scratch[1]) < 0)
            goto fail;
    }

    s->bw = (s->width + 3) / 4;
    s->bh = (s->height + 3) / 4;
    s->work = (uint8_t *)calloc((size_t)s->bw * 4, (size_t)s->bh * 4);
    if (!s->work)
        goto fail;
    if (s->bw * 4 != s->width) {
        s->out = (uint8_t *)calloc(s->width, s->height);
        if (!s->out)
            goto fail;
    } else {
        s->out = s->work;
    }
    for (i = 0; i < SMK_MAX_TRACKS; i++)
        if (track_reserve(&s->track[i], 1) < 0)   /* smk_audio never NULL */
            goto fail;
    s->cur = 0;
    return s;

fail:
    smk_close(s);
    return NULL;
}

smk_t *smk_open_mem_ref(const void *data, size_t size)
{
    return smk_open_internal((const uint8_t *)data, size, NULL);
}

smk_t *smk_open_mem(const void *data, size_t size)
{
    uint8_t *copy;
    if (!data || size == 0)
        return NULL;
    copy = (uint8_t *)malloc(size);
    if (!copy)
        return NULL;
    memcpy(copy, data, size);
    return smk_open_internal(copy, size, copy);
}

smk_t *smk_open_file(const char *path)
{
    FILE *f;
    long len;
    uint8_t *buf;
    if (!path || !(f = fopen(path, "rb")))
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return smk_open_internal(buf, (size_t)len, buf);
}

void smk_close(smk_t *s)
{
    int i;
    if (!s)
        return;
    for (i = 0; i < 4; i++) {
        free(s->tree[i].t.node);
        free(s->audio_tree[i].node);
    }
    free(s->scratch[0].node);
    free(s->scratch[1].node);
    for (i = 0; i < SMK_MAX_TRACKS; i++)
        free(s->track[i].buf);
    if (s->out != s->work)
        free(s->out);
    free(s->work);
    free(s->frame_off);
    free(s->frame_len);
    free(s->frame_type);
    free(s->owned);
    free(s);
}

/* ======================================================================== */
/* Playback                                                                 */
/* ======================================================================== */

void smk_info(const smk_t *s, unsigned *width, unsigned *height,
              unsigned *frames, unsigned *usec_per_frame, int *y_mode)
{
    if (width) *width = s ? s->width : 0;
    if (height) *height = s ? s->height : 0;
    if (frames) *frames = s ? s->frames : 0;
    if (usec_per_frame) *usec_per_frame = s ? s->usec_per_frame : 0;
    if (y_mode) *y_mode = s ? s->y_mode : SMK_Y_NORMAL;
}

int smk_version(const smk_t *s)
{
    return s ? s->version : 0;
}

int smk_audio_info(const smk_t *s, int track, unsigned *rate,
                   unsigned *channels, unsigned *bits)
{
    const smk_track *t;
    int present = s && track >= 0 && track < SMK_MAX_TRACKS &&
                  s->track[track].coding != SMK_AUDIO_NONE;
    t = present ? &s->track[track] : NULL;
    if (rate) *rate = t ? t->rate : 0;
    if (channels) *channels = t ? t->channels : 0;
    if (bits) *bits = t ? t->bits : 0;
    return t ? t->coding : SMK_AUDIO_NONE;
}

int smk_next_frame(smk_t *s)
{
    const uint8_t *p;
    size_t len;
    unsigned type, i;
    int bad = 0;

    if (!s || s->cur < 0 || (unsigned)s->cur >= s->frames)
        return SMK_DONE;
    p = s->data + s->frame_off[s->cur];
    len = s->frame_len[s->cur];
    type = s->frame_type[s->cur];
    if (len & 1u)
        bad = 1;                           /* frame was clipped at end of file */
    len &= ~(size_t)3;
    s->cur++;

    for (i = 0; i < SMK_MAX_TRACKS; i++)
        s->track[i].nsamples = 0;

    /* palette chunk */
    s->pal_changed = 0;
    if (type & 1u) {
        size_t plen = len ? (size_t)p[0] * 4 : 0;
        if (plen == 0 || plen > len) {
            bad = 1;
            plen = plen ? len : (len ? 1 : 0);
        }
        if (plen && decode_palette(s, p, plen) < 0)
            bad = 1;
        s->pal_changed = 1;
        p += plen;
        len -= plen;
    }

    /* audio chunks, track 0..6 */
    for (i = 0; i < SMK_MAX_TRACKS; i++) {
        uint32_t alen;
        if (!(type & (2u << i)))
            continue;
        if (len < 4) {
            bad = 1;
            break;
        }
        alen = rd32(p);
        if (alen < 4 || alen > len) {
            bad = 1;
            alen = (uint32_t)len;
        }
        if (decode_audio(s, (int)i, p + 4, alen - 4) < 0)
            bad = 1;
        p += alen;
        len -= alen;
    }

    /* video */
    if (decode_video(s, p, len) < 0)
        bad = 1;
    if (s->out != s->work) {
        unsigned y;
        for (y = 0; y < s->height; y++)
            memcpy(s->out + (size_t)y * s->width,
                   s->work + (size_t)y * s->bw * 4, s->width);
    }
    return bad ? SMK_FRAME_DAMAGED : SMK_FRAME;
}

int smk_frame_index(const smk_t *s)
{
    return s ? s->cur - 1 : -1;
}

void smk_rewind(smk_t *s)
{
    unsigned i;
    if (!s)
        return;
    s->cur = 0;
    s->pal_changed = 0;
    memset(s->palette, 0, sizeof s->palette);
    memset(s->work, 0, (size_t)s->bw * 4 * s->bh * 4);
    if (s->out != s->work)
        memset(s->out, 0, (size_t)s->width * s->height);
    for (i = 0; i < SMK_MAX_TRACKS; i++)
        s->track[i].nsamples = 0;
}

const uint8_t *smk_pixels(const smk_t *s)
{
    return s ? s->out : NULL;
}

const uint8_t *smk_palette(const smk_t *s)
{
    return s ? s->palette : NULL;
}

int smk_palette_changed(const smk_t *s)
{
    return s ? s->pal_changed : 0;
}

const int16_t *smk_audio(const smk_t *s, int track, size_t *nsamples)
{
    static const int16_t none[1] = { 0 };  /* read-only, not mutable state */
    if (!s || track < 0 || track >= SMK_MAX_TRACKS) {
        if (nsamples) *nsamples = 0;
        return none;
    }
    if (nsamples) *nsamples = s->track[track].nsamples;
    return s->track[track].buf;
}

unsigned smk_display_height(const smk_t *s)
{
    if (!s)
        return 0;
    return s->y_mode == SMK_Y_NORMAL ? s->height : s->height * 2;
}

void smk_render_rgb24(const smk_t *s, uint8_t *dst)
{
    unsigned x, y;
    size_t rowbytes;
    if (!s || !dst)
        return;
    rowbytes = (size_t)s->width * 3;
    for (y = 0; y < s->height; y++) {
        const uint8_t *src = s->out + (size_t)y * s->width;
        uint8_t *d = dst + (s->y_mode == SMK_Y_NORMAL ? y : 2 * y) * rowbytes;
        for (x = 0; x < s->width; x++) {
            const uint8_t *c = s->palette + src[x] * 3;
            d[3 * x + 0] = c[0];
            d[3 * x + 1] = c[1];
            d[3 * x + 2] = c[2];
        }
        if (s->y_mode == SMK_Y_DOUBLED)
            memcpy(d + rowbytes, d, rowbytes);
        else if (s->y_mode == SMK_Y_INTERLACED)
            memset(d + rowbytes, 0, rowbytes);
    }
}
