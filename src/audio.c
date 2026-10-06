/* audio.c - see audio.h. Voices are mixed on the audio thread with linear interpolation (pitch = playback rate
 * relative to the sample's own rate) and an equal-power-ish linear pan; changes from the game thread go under the
 * device lock.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "audio.h"
#include "plat.h"
#include "res.h"
#include <stdlib.h>
#include <string.h>

#define MAX_VOICES 60          /* Raser.ini [SOUND] BUFFERS=60 */
#define STREAM_CAP (44100 * 4) /* frames: ~4 s of film sound */

typedef struct {
    const Sample *s;
    double pos, step;          /* in sample frames */
    float vol, pan, pitch;
    int loop, id;
} Voice;

static Voice g_v[MAX_VOICES];
static int g_next_id = 1;
static float g_sfx = 1, g_music = 1;
static int g_rate = 44100;
static int16_t g_stream[STREAM_CAP * 2];
static int g_s_rd, g_s_wr, g_s_count, g_s_rate, g_s_ch, g_s_open;
static double g_s_frac;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

int sample_decode_wav(const uint8_t *d, size_t n, Sample *out)
{
    memset(out, 0, sizeof *out);
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return 0;
    int fmt = 0, ch = 0, rate = 0, bits = 0;
    const uint8_t *data = NULL;
    size_t dlen = 0, p = 12;
    while (p + 8 <= n) {
        size_t len = rd32(d + p + 4);
        if (!memcmp(d + p, "fmt ", 4) && len >= 16 && p + 8 + 16 <= n) {
            fmt = rd16(d + p + 8); ch = rd16(d + p + 10); rate = (int)rd32(d + p + 12); bits = rd16(d + p + 22);
        } else if (!memcmp(d + p, "data", 4)) {
            data = d + p + 8;
            dlen = len <= n - p - 8 ? len : n - p - 8;
        }
        p += 8 + len + (len & 1);
    }
    if (fmt != 1 || !data || ch < 1 || ch > 2 || (bits != 8 && bits != 16) || rate <= 0) return 0;
    int bps = bits / 8 * ch;
    out->frames = (int)(dlen / (size_t)bps);
    out->channels = ch;
    out->rate = rate;
    out->data = malloc(sizeof(int16_t) * (size_t)out->frames * (size_t)ch + 2);
    if (!out->data) return 0;
    size_t ns = (size_t)out->frames * (size_t)ch;
    for (size_t i = 0; i < ns; i++)
        out->data[i] = bits == 8 ? (int16_t)((data[i] - 128) << 8) : (int16_t)rd16(data + i * 2);
    return 1;
}

int sample_load(const char *name, Sample *out)
{
    size_t n = 0;
    uint8_t *d = res_load(name, &n);
    memset(out, 0, sizeof *out);
    if (!d) return 0;
    int ok = sample_decode_wav(d, n, out);
    free(d);
    return ok;
}

void sample_free(Sample *s)
{
    /* a voice may still play it */
    plat_audio_lock();
    for (int i = 0; i < MAX_VOICES; i++)
        if (g_v[i].s == s) g_v[i].s = NULL;
    plat_audio_unlock();
    free(s->data);
    memset(s, 0, sizeof *s);
}

static void mix(int16_t *out, int frames, void *user)
{
    (void)user;
    static float acc[4096 * 2];
    while (frames > 0) {
        int n = frames > 4096 ? 4096 : frames;
        memset(acc, 0, sizeof(float) * (size_t)n * 2);
        for (int k = 0; k < MAX_VOICES; k++) {
            Voice *v = &g_v[k];
            if (!v->s || !v->s->frames) continue;
            const Sample *s = v->s;
            float l = v->vol * g_sfx * (v->pan <= 0 ? 1 : 1 - v->pan), r = v->vol * g_sfx * (v->pan >= 0 ? 1 : 1 + v->pan);
            for (int i = 0; i < n; i++) {
                if (v->pos >= s->frames) {
                    if (v->loop) v->pos -= s->frames * (int)(v->pos / s->frames);
                    else { v->s = NULL; break; }
                }
                int i0 = (int)v->pos, i1 = i0 + 1 < s->frames ? i0 + 1 : (v->loop ? 0 : i0);
                float f = (float)(v->pos - i0), a, b;
                if (s->channels == 1) {
                    a = s->data[i0] + (s->data[i1] - s->data[i0]) * f;
                    b = a;
                } else {
                    a = s->data[i0 * 2] + (s->data[i1 * 2] - s->data[i0 * 2]) * f;
                    b = s->data[i0 * 2 + 1] + (s->data[i1 * 2 + 1] - s->data[i0 * 2 + 1]) * f;
                }
                acc[i * 2] += a * l;
                acc[i * 2 + 1] += b * r;
                v->pos += v->step;
            }
        }
        if (g_s_open && g_s_rate > 0) {
            double step = (double)g_s_rate / g_rate;
            for (int i = 0; i < n && g_s_count > 0; i++) {
                int16_t *fr = g_stream + g_s_rd * 2;
                acc[i * 2] += fr[0] * g_music;
                acc[i * 2 + 1] += fr[1] * g_music;
                g_s_frac += step;
                while (g_s_frac >= 1 && g_s_count > 0) {
                    g_s_frac -= 1;
                    g_s_rd = (g_s_rd + 1) % STREAM_CAP;
                    g_s_count--;
                }
            }
        }
        for (int i = 0; i < n * 2; i++) {
            float x = acc[i];
            out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
        out += n * 2;
        frames -= n;
    }
}

void audio_init(void)
{
    plat_audio_start(mix, NULL);
    g_rate = plat_audio_rate();
}

void audio_set_master(float sfx, float music)
{
    plat_audio_lock();
    g_sfx = sfx;
    g_music = music;
    plat_audio_unlock();
}

static Voice *find(int id)
{
    if (id <= 0) return NULL;
    for (int i = 0; i < MAX_VOICES; i++)
        if (g_v[i].id == id && g_v[i].s) return &g_v[i];
    return NULL;
}

int audio_play(const Sample *s, float volume, float pan, float pitch, int loop)
{
    if (!s || !s->data || !s->frames) return 0;
    plat_audio_lock();
    int id = 0;
    for (int i = 0; i < MAX_VOICES; i++)
        if (!g_v[i].s) {
            Voice *v = &g_v[i];
            v->s = s;
            v->pos = 0;
            v->vol = volume;
            v->pan = pan;
            v->pitch = pitch;
            v->step = (double)s->rate / g_rate * pitch;
            v->loop = loop;
            v->id = id = g_next_id++;
            if (g_next_id <= 0) g_next_id = 1;
            break;
        }
    plat_audio_unlock();
    return id;
}

void audio_set(int voice, float volume, float pan, float pitch)
{
    plat_audio_lock();
    Voice *v = find(voice);
    if (v) {
        v->vol = volume;
        v->pan = pan;
        v->pitch = pitch;
        v->step = (double)v->s->rate / g_rate * pitch;
    }
    plat_audio_unlock();
}

void audio_stop(int voice)
{
    plat_audio_lock();
    Voice *v = find(voice);
    if (v) v->s = NULL;
    plat_audio_unlock();
}

int audio_playing(int voice)
{
    plat_audio_lock();
    int r = find(voice) != NULL;
    plat_audio_unlock();
    return r;
}

void audio_stop_all(void)
{
    plat_audio_lock();
    for (int i = 0; i < MAX_VOICES; i++) g_v[i].s = NULL;
    plat_audio_unlock();
}

void audio_stream_open(int rate, int channels)
{
    plat_audio_lock();
    g_s_rate = rate;
    g_s_ch = channels;
    g_s_rd = g_s_wr = g_s_count = 0;
    g_s_frac = 0;
    g_s_open = 1;
    plat_audio_unlock();
}

void audio_stream_push(const int16_t *pcm, int frames)
{
    plat_audio_lock();
    for (int i = 0; i < frames && g_s_count < STREAM_CAP; i++) {
        int16_t l = pcm[i * g_s_ch], r = g_s_ch > 1 ? pcm[i * g_s_ch + 1] : l;
        g_stream[g_s_wr * 2] = l;
        g_stream[g_s_wr * 2 + 1] = r;
        g_s_wr = (g_s_wr + 1) % STREAM_CAP;
        g_s_count++;
    }
    plat_audio_unlock();
}

int audio_stream_queued(void)
{
    plat_audio_lock();
    int n = g_s_count;
    plat_audio_unlock();
    return n;
}

void audio_stream_close(void)
{
    plat_audio_lock();
    g_s_open = 0;
    g_s_count = 0;
    plat_audio_unlock();
}
