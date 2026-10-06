/* main.c - A2RacerRE: start-up, command line and the top-level loop.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "gfx.h"
#include "image.h"
#include "plat.h"
#include "res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_data(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-' && (i == 1 || argv[i - 1][0] != '-')) return argv[i];
    static const char *cand[] = { "data", "extract/data", "../extract/data" };
    for (size_t i = 0; i < sizeof cand / sizeof cand[0]; i++) {
        char p[600];
        snprintf(p, sizeof p, "%s/RCS.IMG", cand[i]);
        if (plat_exists(p)) return cand[i];
    }
    return "data";
}

static const char *arg_value(int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], name)) return argv[i + 1];
    return NULL;
}

static const char *g_shot;     /* --shot file.png: save the first frame(s) and quit */
static int g_shot_frames = 3;

/* writes the back buffer to g_shot; 1 = done, quit */
static int shot_frame(void)
{
    if (!g_shot || --g_shot_frames > 0) return 0;
    int w, h;
    plat_size(&w, &h);
    Image img = { w, h, malloc((size_t)w * h * 4), NULL, { 0 }, 32 };
    uint8_t *rgb = malloc((size_t)w * h * 3);
    gfx_read_pixels(w, h, rgb);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *s = rgb + ((size_t)(h - 1 - y) * w + x) * 3;
            uint8_t *d = img.rgba + ((size_t)y * w + x) * 4;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
        }
    image_write_png(g_shot, &img);
    free(rgb);
    image_free(&img);
    printf("screenshot: %s\n", g_shot);
    return 1;
}

/* --image NAME [--archive Mrcs]: show one image of the archives (development) */
static int view_image(const char *name, const char *archive)
{
    Image img;
    int ok;
    if (archive) {
        ResArchive *a = res_open(archive);
        size_t n = 0;
        uint8_t *d = res_read(a, res_find(a, name), &n);
        ok = d && image_decode(name, d, n, &img);
        free(d);
        res_close(a);
    } else ok = image_load(name, &img);
    if (!ok) { fprintf(stderr, "cannot load %s\n", name); return 1; }
    printf("%s: %dx%d, %d bpp\n", name, img.w, img.h, img.bpp);
    unsigned tex = gfx_texture(img.rgba, img.w, img.h, TEX_NEAREST | TEX_CLAMP);
    while (plat_poll() && !plat_key_pressed(VK_ESCAPE)) {
        int w, h;
        plat_size(&w, &h);
        gfx_begin(w, h, 0.1f, 0.1f, 0.15f);
        gfx_2d_begin(img.w, img.h);
        gfx_2d_quad(tex, 0, 0, (float)img.w, (float)img.h, 0, 0, 1, 1, 0xffffffffu, BLEND_NONE);
        if (shot_frame()) break;
        plat_swap();
    }
    image_free(&img);
    return 0;
}

int main(int argc, char **argv)
{
    res_set_dir(find_data(argc, argv));
    PlatConfig cfg = { 960, 720, 0, 1, "A2RacerRE" };
    if (!plat_init(&cfg)) return 1;
    if (!gfx_init()) { plat_message("OpenGL 2.1 / OpenGL ES 2.0 shaders are not available.", 1); return 1; }
    int rc = 0;
    g_shot = arg_value(argc, argv, "--shot");
    const char *img = arg_value(argc, argv, "--image");
    if (img) rc = view_image(img, arg_value(argc, argv, "--archive"));
    plat_shutdown();
    return rc;
}
