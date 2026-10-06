/*
 * smk.h - self-contained Smacker (SMK2 / SMK4) video + audio decoder.
 *
 * Part of A2RacerRE, a reimplementation of the "A2 Racer II" engine.
 * Copyright (C) 2026 A2RacerRE contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the repository root.
 *
 * Written from the public format description (MultimediaWiki "Smacker");
 * it contains no code from libsmacker or FFmpeg.
 *
 * Usage:
 *   smk_t *s = smk_open_file("intro.smk");
 *   smk_info(s, &w, &h, &frames, &usec, &ymode);
 *   while (smk_next_frame(s) != SMK_DONE) {
 *       const uint8_t *pix = smk_pixels(s);   // w*h indices, pitch w
 *       const uint8_t *pal = smk_palette(s);  // 256 * RGB
 *       const int16_t *pcm = smk_audio(s, 0, &n); // n samples per channel
 *       ...present for usec microseconds...
 *   }
 *   smk_close(s);
 *
 * The decoder never reads outside the input buffer; damaged or truncated
 * files produce SMK_FRAME_DAMAGED frames (with valid, possibly glitched
 * output) instead of crashes. No globals: separate smk_t objects may be
 * used from separate threads.
 */
#ifndef A2_SMK_H
#define A2_SMK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMK_MAX_TRACKS 7

/* Vertical display scaling requested by the file header (smk_info). */
enum {
    SMK_Y_NORMAL     = 0, /* display as is                                */
    SMK_Y_INTERLACED = 1, /* display at 2x height, every other line black  */
    SMK_Y_DOUBLED    = 2  /* display at 2x height, every line duplicated   */
};

/* Audio track coding (smk_audio_info return value). */
enum {
    SMK_AUDIO_NONE        = 0, /* track not present                        */
    SMK_AUDIO_PCM         = 1, /* uncompressed PCM                          */
    SMK_AUDIO_DPCM        = 2, /* Huffman-coded DPCM ("compressed")         */
    SMK_AUDIO_UNSUPPORTED = 3  /* perceptual (Bink audio) coding; skipped,
                                  smk_audio() returns no samples           */
};

/* smk_next_frame return values. */
enum {
    SMK_DONE          = 0,  /* no more frames                                    */
    SMK_FRAME         = 1,  /* a frame was decoded                               */
    SMK_FRAME_DAMAGED = -1  /* a frame was decoded but its data was corrupt or
                               truncated; output is valid memory, maybe glitched */
};

typedef struct smk_s smk_t;

/* Open a file (read completely into memory). NULL on failure. */
smk_t *smk_open_file(const char *path);
/* Open from memory; the data is copied. */
smk_t *smk_open_mem(const void *data, size_t size);
/* Open from memory without copying; data must outlive the smk_t. */
smk_t *smk_open_mem_ref(const void *data, size_t size);
void   smk_close(smk_t *s);

/* Video properties. Any output pointer may be NULL.
 * usec_per_frame: frame duration in microseconds (fps = 1e6 / usec).
 * y_mode: SMK_Y_NORMAL / SMK_Y_INTERLACED / SMK_Y_DOUBLED.
 * version: 2 or 4 (from the "SMK2"/"SMK4" signature). */
void smk_info(const smk_t *s, unsigned *width, unsigned *height,
              unsigned *frames, unsigned *usec_per_frame, int *y_mode);
int  smk_version(const smk_t *s);

/* Audio track properties (track 0..6). Returns SMK_AUDIO_* coding; for
 * SMK_AUDIO_NONE the outputs are set to 0. bits is the stored sample size
 * (8 or 16); smk_audio() always delivers signed 16-bit. */
int smk_audio_info(const smk_t *s, int track, unsigned *rate,
                   unsigned *channels, unsigned *bits);

/* Decode the next frame (video, palette and all audio tracks). */
int smk_next_frame(smk_t *s);
/* Index (0-based) of the frame decoded last, -1 before the first. */
int smk_frame_index(const smk_t *s);
/* Restart at frame 0 (picture and palette are cleared). */
void smk_rewind(smk_t *s);

/* width*height palette indices of the current frame, pitch = width. */
const uint8_t *smk_pixels(const smk_t *s);
/* 256 RGB triplets (768 bytes) of the current frame. */
const uint8_t *smk_palette(const smk_t *s);
/* Nonzero if the last decoded frame carried a palette chunk. */
int smk_palette_changed(const smk_t *s);
/* Audio of the last decoded frame for a track: interleaved signed 16-bit
 * samples, *nsamples = samples per channel (0 if none). Never NULL. */
const int16_t *smk_audio(const smk_t *s, int track, size_t *nsamples);

/* Convenience: convert the current frame to packed RGB24, applying the
 * Y-scaling mode. dst must hold width * display_height * 3 bytes (pitch
 * width*3); display_height = height, or 2*height for interlaced/doubled. */
unsigned smk_display_height(const smk_t *s);
void smk_render_rgb24(const smk_t *s, uint8_t *dst);

#ifdef __cplusplus
}
#endif

#endif /* A2_SMK_H */
