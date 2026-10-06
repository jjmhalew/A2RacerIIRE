# Smacker video decoder (`src/smk.c`)

A2 Racer II plays its films (`intro.smk`, `win.smk`, `lose.smk`, `exit.smk`)
through RAD's `smackw32.dll`. The reimplementation decodes them natively with
`src/smk.c` / `src/smk.h`: portable C99, no dependencies beyond the C library,
no global state, GPL-3.0-or-later. It was written from the public format
description (MultimediaWiki "Smacker") and checked against the game's films;
it contains no libsmacker or FFmpeg code.

## The game's films

| file      | size    | frames | rate                    | audio track 0              |
|-----------|---------|--------|-------------------------|----------------------------|
| intro.smk | 320x240 | 1375   | 66.66 ms/frame (15 fps) | DPCM, 22050 Hz, mono, 16-bit |
| win.smk   | 320x240 | 236    | 15 fps                  | DPCM, 22050 Hz, mono, 16-bit |
| lose.smk  | 320x240 | 300    | 15 fps                  | DPCM, 22050 Hz, mono, 16-bit |
| exit.smk  | 320x240 | 99     | 15 fps                  | DPCM, 22050 Hz, mono, 8-bit  |

All are `SMK2`, no ring frame, no Y scaling. The first frame carries about
16 frames' worth of audio (a pre-buffer), so the audio queue must be fed
ahead of the picture; the total audio length equals frames x frame time.

## Format as implemented

All integers are little-endian; bitstreams are read LSB first.

* **Header** (104 bytes): signature `SMK2`/`SMK4`, width, height, frame
  count, frame rate (`>0`: ms per frame, `<0`: units of 10 us, `0`: 10 fps),
  flags (bit 0 ring frame, bit 1 Y-interlaced, bit 2 Y-doubled), 7 largest
  audio chunk sizes, the size of the Huffman tree section, 4 table allocation
  hints (unused), 7 audio track descriptors (bit 31 compressed, 30 present,
  29 16-bit, 28 stereo, 27-26 non-zero = Bink audio, 23-0 sample rate).
* Then `uint32` size per physical frame (low 2 bits are flags, masked off),
  one type byte per frame (bit 0 palette, bit 1+t audio track t), the tree
  section, and the frame data. A ring frame (flag bit 0) adds one physical
  frame; it is not decoded (it only matters for seamless looping).
* **Huffman trees**: a packed tree is a presence bit, then a pre-order walk
  (`1` = node: '0' subtree then '1' subtree, `0` = leaf + value), then a
  terminating `0` bit. The four video trees (MMap, MClr, Full, Type) have
  16-bit leaves coded with a low-byte and a high-byte tree, preceded by
  three 16-bit escape values. Leaves equal to an escape refer to a 3-entry
  cache of recent values: after each decode, a value different from
  `cache[0]` is pushed to the front. The caches are zeroed at every frame.
* **Palette chunk**: length byte (x4 = chunk size), then blocks:
  `1ccccccc` keeps c+1 entries of the old palette, `01cccccc ssssssss`
  copies c+1 entries from old entry s (a remap of the previous palette),
  `00rrrrrr 00gggggg 00bbbbbb` is a new 6-bit colour (scaled `v<<2 | v>>4`).
* **Audio chunk**: `uint32` length, then PCM (8-bit unsigned / 16-bit
  signed) or DPCM: `uint32` unpacked size, bits present/stereo/16-bit, one
  byte tree per sample byte, base values (right channel first, high byte
  first) forming the first sample, then Huffman-coded deltas per byte (low,
  high; left, right). Output is always signed 16-bit interleaved. Bink audio
  tracks are reported as unsupported and skipped.
* **Video**: 4x4 blocks, left to right, top to bottom. A Type value gives
  the block type (bits 0-1), a run length (bits 2-7 index
  1..59, 128, 256, 512, 1024, 2048) and a solid colour (bits 8-15).
  Mono: MClr (high byte for set bits, low byte for clear bits) + 16-bit MMap.
  Full: per row two Full values, pixels 2-3 then 0-1. Void: unchanged.
  Solid: fill. In `SMK4`, every full run starts with a sub-type: `1` double
  (2x2 squares, one value per row pair), `01` half (each coded row repeated
  on the next line), `00` normal. (No SMK4 sample was available; this path
  follows the format description and is untested on real data.)
* **Y flags**: the decoded picture is `width x height`; for interlaced or
  doubled files it is displayed at twice the height (black lines between /
  duplicated lines). `smk_render_rgb24()` does this.

Corrupt or truncated data never causes out-of-bounds access: the bit reader
returns zeros past the end, trees are size/depth limited, palette copies and
block runs are clamped, and the frame is reported as `SMK_FRAME_DAMAGED`.

## API sketch

```c
smk_t *s = smk_open_file("extract/data/intro.smk");   /* or smk_open_mem() */
unsigned w, h, n, usec, rate, ch, bits; int ymode;
smk_info(s, &w, &h, &n, &usec, &ymode);
smk_audio_info(s, 0, &rate, &ch, &bits);
while (smk_next_frame(s) != SMK_DONE) {
    const uint8_t *pix = smk_pixels(s);   /* w*h palette indices */
    const uint8_t *pal = smk_palette(s);  /* 768 bytes RGB */
    size_t ns; const int16_t *pcm = smk_audio(s, 0, &ns);
}
smk_close(s);
```

## Testing

```sh
.venv/Scripts/python -m ziglang cc -std=c99 -O2 -Wall -Wextra src/smk.c src/smktest.c -o smktest.exe
./smktest.exe extract/data/intro.smk outdir 500 3   # frames 500..502 -> PPM
```

`smktest` prints the film properties, decodes all frames (reporting damaged
frames and decoding speed), writes the selected frames as
`outdir/frame_NNNNN.ppm` and the complete track-0 audio as
`outdir/audio0.wav`, with peak/RMS/clipping statistics and the audio length
compared to the video length. View the PPMs e.g. with Pillow
(`Image.open(p).save(p[:-4] + ".png")`). All four films decode without
damaged frames at over 2000 fps; audio length matches the video length.
