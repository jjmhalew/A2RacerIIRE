/* audio.h - the sound mixer: the game's WAV samples (PCM 8/16-bit, mono/stereo, 8000-44100 Hz) played on voices with
 * volume, pan and pitch (the engine sound follows the revs, docs/SOUND.md), looping or once, plus one streamed track
 * for the films' sound. The original plays them through DirectSound buffers (winlib sound.cpp, Raser.ini
 * [SOUND] BUFFERS=60, 3dsound=1 in Game.ini for positional sound).
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_AUDIO_H
#define A2_AUDIO_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t *data;          /* interleaved */
    int frames, channels, rate;
} Sample;

int  sample_decode_wav(const uint8_t *d, size_t n, Sample *out);
int  sample_load(const char *name, Sample *out);      /* from RCS (or a loose file) */
void sample_free(Sample *s);

void audio_init(void);                                /* starts the device (plat_audio_start) */
void audio_set_master(float sfx, float music);        /* 0..1 */

/* plays s (which must stay loaded while it sounds); returns a voice handle > 0, or 0 when all voices are busy */
int  audio_play(const Sample *s, float volume, float pan, float pitch, int loop);
void audio_set(int voice, float volume, float pan, float pitch);
void audio_stop(int voice);
int  audio_playing(int voice);
void audio_stop_all(void);

/* the stream (films): queue signed 16-bit frames at the given rate / channels; returns the frames still queued */
void audio_stream_open(int rate, int channels);
void audio_stream_push(const int16_t *pcm, int frames);
int  audio_stream_queued(void);
void audio_stream_close(void);
#endif
