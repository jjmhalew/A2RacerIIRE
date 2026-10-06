/* main.c - A2RacerRE: start-up, command line and the top-level loop.
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "audio.h"
#include "datasetup.h"
#include "film.h"
#include "gfx.h"
#include "image.h"
#include "game.h"
#include "menu.h"
#include "plat.h"
#include "res.h"
#include "model.h"
#include "objects.h"
#include "paths.h"
#include "race.h"
#include "track.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the first argument that is not an option or an option's value: a data folder, the CD or an ISO image */
static const char *path_arg(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "--menu-shot")) i++;       /* takes two values */
            if (strcmp(argv[i], "--verify") && strcmp(argv[i], "--windowed") && strcmp(argv[i], "--fullscreen")
                && strcmp(argv[i], "--menu") && strcmp(argv[i], "--menu-first")) i++;
            continue;
        }
        return argv[i];
    }
    return NULL;
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

/* the original's coordinates are Direct3D left-handed (y up, z forward); GL wants right-handed: mirror z */
static Mat4 view_lh(Vec3 eye, Vec3 target)
{
    Mat4 flip = m4_scale(v3(1, 1, -1));
    Mat4 v = m4_look_at(v3(eye.x, eye.y, -eye.z), v3(target.x, target.y, -target.z), v3(0, 1, 0));
    return m4_mul(v, flip);
}

/* --track N [--chunk C]: drive the camera along the centre line (Up/Down: speed, Left/Right: look, PgUp/PgDn: height) */
static int view_track(int etappe, int chunk)
{
    Track t;
    if (!track_load(&t, etappe)) { fprintf(stderr, "cannot load etappe%d\n", etappe); return 1; }
    ObjectSet objs;
    objects_load(&objs, &t);
    printf("%d objects, %d paths\n", objs.n, paths_load(etappe));
    printf("etappe%d: %s, %d chunks, %d waypoints, %d rails, %s\n", etappe, t.name, t.nchunks, t.nwaypoints, t.nrails,
           t.city ? "city" : "highway");
    float s = (float)(chunk % t.nchunks), speed = g_shot ? 0 : 0.5f, height = 12, look = 0;
    double last = plat_time();
    while (plat_poll() && !plat_key_pressed(VK_ESCAPE)) {
        double now = plat_time();
        float dt = (float)(now - last);
        last = now;
        if (plat_key(VK_UP)) speed += dt * 2;
        if (plat_key(VK_DOWN)) speed -= dt * 2;
        if (plat_key(VK_LEFT)) look -= dt * 1.5f;
        if (plat_key(VK_RIGHT)) look += dt * 1.5f;
        if (plat_key(VK_PRIOR)) height += dt * 20;
        if (plat_key(VK_NEXT)) height -= dt * 20;
        s += speed * dt * 10;
        while (s >= t.nchunks) s -= t.nchunks;
        while (s < 0) s += t.nchunks;
        int c = (int)s;
        float f = s - c;
        const Chunk *a = &t.chunks[c], *b = &t.chunks[(c + 1) % t.nchunks];
        Vec3 p = v3_lerp(a->pos, b->pos, c + 1 < t.nchunks ? f : 0);
        Vec3 fwd = v3_norm(v3_lerp(a->fwd, b->fwd, f));
        Vec3 right = v3_norm(v3(fwd.z, 0, -fwd.x));
        Vec3 dir = v3_add(v3_scale(fwd, cosf(look)), v3_scale(right, sinf(look)));
        Vec3 eye = v3(p.x, p.y + height, p.z), target = v3_add(eye, v3_add(dir, v3(0, -0.12f, 0)));
        int w, h;
        plat_size(&w, &h);
        gfx_begin(w, h, 0.55f, 0.7f, 0.9f);
        /* the original: near 10, far 12500, 81 degrees across a 4:3 screen (RENDER.md) */
        float fovy = 2 * atanf(tanf(40.5f * PI_F / 180) * 0.75f);
        Mat4 proj = m4_perspective(fovy, (float)w / h, 10, 12500), view = view_lh(eye, target);
        gfx_set_matrices(&proj, &view);
        int start = track_visible_start(&t, c, dir, 2, 30);
        track_draw(&t, start, 30, 0);
        objects_draw(&objs, &t, start, 30);
        paths_update(c, 0, dt > 0 ? dt : 1.0f / 30);
        paths_draw();
        if (shot_frame()) break;
        plat_swap();
    }
    objects_free(&objs);
    track_free(&t);
    return 0;
}

int main(int argc, char **argv)
{
    PlatConfig cfg = { 960, 720, 0, 1, "A2RacerRE" };
    if (getenv("A2_HIDDEN")) setvbuf(stdout, NULL, _IONBF, 0);   /* tests are often stopped by a timeout */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fullscreen")) cfg.fullscreen = 1;
        if (!strcmp(argv[i], "--windowed")) cfg.fullscreen = 0;
    }
    if (!plat_init(&cfg)) return 1;
    if (!gfx_init()) { plat_message("OpenGL 2.1 / OpenGL ES 2.0 shaders are not available.", 1); return 1; }
    int rc = 0;
    const char *iso = arg_value(argc, argv, "--copy-iso");   /* test: --copy-iso file.iso dest */
    if (iso) return data_copy_iso(iso, "out/isocopy") ? 0 : 1;
    const char *data = data_find(path_arg(argc, argv));
    if (!data) { plat_shutdown(); return 1; }
    res_set_dir(data);
    printf("data: %s\n", data);
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--verify")) { rc = data_verify(data) ? 1 : 0; plat_shutdown(); return rc; }
    g_shot = arg_value(argc, argv, "--shot");
    audio_init();
    const char *film = arg_value(argc, argv, "--film");
    if (film) printf("film: %d\n", film_play(film, 1));
    /* --menu / --menu-first (the launcher's \FIRST): the menu program; --menu-shot SCREEN file.png: one screen */
    const char *mdir = arg_value(argc, argv, "--menu-userdir"), *mscript = arg_value(argc, argv, "--menu-script");
    if (mdir) plat_set_user_dir(mdir);       /* a separate folder for the settings files (tests) */
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--menu") || !strcmp(argv[i], "--menu-first") || !strcmp(argv[i], "--menu-script")) {
            int first = !strcmp(argv[i], "--menu-first") || (mscript && !strncmp(mscript, "first", 5));
            rc = mscript ? menu_script(mscript, first) : menu_run(first);
            printf("menu: 0x%x\n", rc);
            plat_shutdown();
            return 0;
        } else if (!strcmp(argv[i], "--menu-shot") && i + 2 < argc) {
            rc = menu_shot(argv[i + 1], argv[i + 2]);
            plat_shutdown();
            return rc;
        }
    if (!models_init()) { plat_message("spel.dat (the race program of the CD) is missing or not version 1.01.", 1); return 1; }
    const char *race = arg_value(argc, argv, "--race");
    if (race) {
        static Race r;
        const char *car = arg_value(argc, argv, "--car"), *drive = arg_value(argc, argv, "--drive");
        if (!race_init(&r, atoi(race), car ? atoi(car) : 0)) return 1;
        if (drive) { r.test_gas = 100; r.test_steer = (float)atof(drive); }
        const char *secs = arg_value(argc, argv, "--seconds");
        double t0 = plat_time(), until = secs ? atof(secs) : 0;
        while (plat_poll()) {
            int alive = race_frame(&r, plat_time());
            if (until > 0 && plat_time() - t0 >= until) {       /* --seconds T [--shot file]: stop after T s */
                g_shot_frames = 1;
                if (g_shot) shot_frame();
                break;
            }
            plat_swap();
            if (!alive) break;
        }
        printf("speed %d km/h, gear %d, chunk %d\n", r.speed_kmh, r.gear + 1, r.obj[r.plr_obj].chunk);
        race_free(&r);
    }
    const char *trk = arg_value(argc, argv, "--track");
    if (trk) {
        const char *ch = arg_value(argc, argv, "--chunk");
        rc = view_track(atoi(trk), ch ? atoi(ch) : 0);
    }
    const char *img = arg_value(argc, argv, "--image");
    if (img) rc = view_image(img, arg_value(argc, argv, "--archive"));
    if (!race && !trk && !img && !film) rc = game_run();     /* the game: intro, menu, races */
    models_free();
    plat_shutdown();
    return rc;
}
