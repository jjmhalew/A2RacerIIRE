/* film.c - see film.h. The picture follows the clock of the sound: a frame is shown when the sound queued before it
 * has been played (the first frame of these films carries about a second of sound ahead of the picture).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "film.h"
#include "audio.h"
#include "gfx.h"
#include "plat.h"
#include "res.h"
#include "smk.h"
#include <stdlib.h>

int film_play(const char *name, int fullscreen)
{
    char path[1024];
    res_path(path, sizeof path, name);
    FILE *f = plat_fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    size_t got = data ? fread(data, 1, (size_t)n, f) : 0;
    fclose(f);
    smk_t *s = got == (size_t)n ? smk_open_mem_ref(data, (size_t)n) : NULL;
    if (!s) { free(data); return -1; }
    unsigned w, h, frames, rate = 0, ch = 0, bits = 0, usec = 66667;
    int ymode;
    smk_info(s, &w, &h, &frames, &usec, &ymode);
    unsigned dh = smk_display_height(s);
    int coding = smk_audio_info(s, 0, &rate, &ch, &bits);
    int has_audio = (coding == SMK_AUDIO_PCM || coding == SMK_AUDIO_DPCM) && rate && ch;
    uint8_t *rgb = malloc((size_t)w * dh * 3), *rgba = malloc((size_t)w * dh * 4);
    unsigned tex = 0;
    if (has_audio) audio_stream_open((int)rate, (int)ch);
    int result = 1;
    double start = plat_time();
    long pushed = 0;                       /* sound frames queued so far */
    for (unsigned fr = 0; fr < frames; fr++) {
        int r = smk_next_frame(s);
        if (r == SMK_DONE) break;
        if (has_audio) {
            size_t ns = 0;
            const int16_t *pcm = smk_audio(s, 0, &ns);
            if (pcm && ns) { audio_stream_push(pcm, (int)(ns / ch)); pushed += (long)(ns / ch); }
        }
        smk_render_rgb24(s, rgb);
        for (size_t i = 0; i < (size_t)w * dh; i++) {
            rgba[i * 4] = rgb[i * 3]; rgba[i * 4 + 1] = rgb[i * 3 + 1]; rgba[i * 4 + 2] = rgb[i * 3 + 2]; rgba[i * 4 + 3] = 255;
        }
        if (!tex) tex = gfx_texture(rgba, (int)w, (int)dh, TEX_CLAMP);
        else gfx_texture_update(tex, rgba, (int)w, (int)dh);
        /* wait until this frame is due */
        double due = start + fr * usec / 1e6;
        for (;;) {
            if (!plat_poll()) { result = -2; goto done; }
            unsigned clicked = 0;
            plat_mouse(NULL, NULL, NULL, &clicked);
            if (plat_any_key_pressed() || plat_pad()->pressed || clicked) { result = 0; goto done; }
            int ww, wh;
            plat_size(&ww, &wh);
            gfx_begin(ww, wh, 0, 0, 0);
            gfx_2d_begin(640, 480);
            if (fullscreen) gfx_2d_quad(tex, 0, 0, 640, 480, 0, 0, 1, 1, 0xffffffffu, BLEND_NONE);
            else gfx_2d_quad(tex, 160, 120, 320, 240, 0, 0, 1, 1, 0xffffffffu, BLEND_NONE);
            plat_swap();
            double now = plat_time();
            /* sound clock: what was pushed minus what is still queued */
            if (has_audio) {
                double played = (double)(pushed - audio_stream_queued()) / rate;
                if (played >= fr * usec / 1e6 || now > due + 0.5) break;
            } else if (now >= due) break;
        }
    }
    /* let the last sound play out */
    while (has_audio && audio_stream_queued() > 0) {
        if (!plat_poll()) { result = -2; break; }
        if (plat_any_key_pressed() || plat_pad()->pressed) break;
        plat_sleep_ms(10);
    }
done:
    if (has_audio) audio_stream_close();
    gfx_texture_free(tex);
    smk_close(s);
    free(data);
    free(rgb);
    free(rgba);
    return result;
}
