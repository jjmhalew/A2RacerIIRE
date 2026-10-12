/* datasetup.c - see datasetup.h.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "datasetup.h"
#include "datafiles.h"
#include "gfx.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir_p(p) _mkdir(p)
#else
#include <dirent.h>
#include <unistd.h>
#define mkdir_p(p) mkdir(p, 0755)
#endif

static char g_found[1024];

/* ---- checks ---------------------------------------------------------------------------------------------------- */
static int has(const char *dir, const char *name)
{
    char p[1100];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = plat_fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* a folder with the CD's data files: the race archive, a track and the films */
static int is_data_dir(const char *dir)
{
    return dir && *dir && has(dir, "RCS.IMG") && has(dir, "RCS.IND") && has(dir, "etappe0.rmp") && has(dir, "Raser.ini");
}

/* the CD (or a copy of it): data/ inside it */
static int is_cd_root(const char *root, char *data, size_t cap)
{
    snprintf(data, cap, "%s%sdata", root, root[0] && root[strlen(root) - 1] != '/' && root[strlen(root) - 1] != '\\' ? "/" : "");
    return is_data_dir(data);
}

static uint32_t crc_table[256];
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int j = 0; j < 8; j++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1;
            crc_table[i] = k;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}

int data_verify(const char *dir)
{
    int bad = 0;
    uint8_t *buf = malloc(1 << 20);
    for (int i = 0; i < DATA_FILE_COUNT; i++) {
        const DataFile *df = &DATA_FILES[i];
        char p[1100];
        snprintf(p, sizeof p, "%s/%s", dir, df->name);
        FILE *f = plat_fopen(p, "rb");
        if (!f) {
            if (df->required) { printf("missing:   %s\n", df->name); bad++; }
            continue;
        }
        uint32_t c = 0;
        size_t n, total = 0;
        while ((n = fread(buf, 1, 1 << 20, f)) > 0) { c = crc32_update(c, buf, n); total += n; }
        fclose(f);
        if (total != df->size || c != df->crc) {
            printf("different: %s (%u bytes, crc %08x; the 1.01 CD has %u bytes, crc %08x)\n", df->name, (unsigned)total, c,
                   df->size, df->crc);
            if (df->required) bad++;
        }
    }
    free(buf);
    printf(bad ? "%d required file(s) missing or different.\n" : "All files match the A2 Racer II CD (Dutch, 1.01).\n", bad);
    return bad;
}

/* ---- progress ---------------------------------------------------------------------------------------------------- */
/* 0 = the window was closed (on Android: the app is ending), stop copying */
static int progress(double done, double total)
{
    static double last;
    double now = plat_time();
    if (now - last < 0.05 && done < total) return 1;
    last = now;
    if (!plat_poll()) return 0;
    int w, h;
    plat_size(&w, &h);
    gfx_begin(w, h, 0.08f, 0.08f, 0.1f);
    gfx_2d_begin(640, 480);
    gfx_2d_rect(120, 228, 400, 24, 0x404048ffu);
    gfx_2d_rect(124, 232, (float)(392 * (total > 0 ? done / total : 0)), 16, 0xce1126ffu);
    plat_swap();
    return 1;
}

/* ---- copying ----------------------------------------------------------------------------------------------------- */
static void make_dirs(const char *path)
{
    char p[1024];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/' || *s == '\\') {
            char c = *s;
            *s = 0;
            mkdir_p(p);
            *s = c;
        }
    mkdir_p(p);
}

static int copy_stream(FILE *in, long long size, const char *dest, const char *name, double *done, double total)
{
    char p[1100], part[1110];
    snprintf(p, sizeof p, "%s/%s", dest, name);
    snprintf(part, sizeof part, "%s.part", p);
    FILE *out = fopen(part, "wb");
    if (!out) return 0;
    uint8_t *buf = malloc(1 << 20);
    long long left = size;
    int ok = buf != NULL;
    while (ok && left > 0) {
        size_t n = (size_t)(left < (1 << 20) ? left : (1 << 20));
        if (fread(buf, 1, n, in) != n || fwrite(buf, 1, n, out) != n) { ok = 0; break; }
        left -= (long long)n;
        *done += (double)n;
        if (!progress(*done, total)) ok = 0;
    }
    free(buf);
    if (fclose(out) != 0) ok = 0;
    remove(p);
    if (ok && rename(part, p) != 0) ok = 0;
    if (!ok) remove(part);
    return ok;
}

static int copy_folder(const char *src, const char *dest)
{
    make_dirs(dest);
    double total = 0, done = 0;
    for (int i = 0; i < DATA_FILE_COUNT; i++) total += DATA_FILES[i].size;
    for (int i = 0; i < DATA_FILE_COUNT; i++) {
        char p[1100];
        snprintf(p, sizeof p, "%s/%s", src, DATA_FILES[i].name);
        FILE *f = plat_fopen(p, "rb");
        if (!f) {
            if (DATA_FILES[i].required) { fprintf(stderr, "%s missing on the CD\n", p); return 0; }
            continue;
        }
        fseek(f, 0, SEEK_END);
        long long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        int ok = copy_stream(f, n, dest, DATA_FILES[i].name, &done, total);
        fclose(f);
        if (!ok) return 0;
    }
    return 1;
}

/* ISO 9660 with the Joliet names when the image has them (the CD's names in their real case) */
typedef struct { uint32_t lba, size; int dir; char name[128]; } IsoEnt;

static int iso_read(FILE *f, uint32_t lba, uint32_t off, void *buf, size_t n)
{
#ifdef _WIN32
    if (_fseeki64(f, (long long)lba * 2048 + off, SEEK_SET)) return 0;
#else
    if (fseeko(f, (off_t)lba * 2048 + off, SEEK_SET)) return 0;
#endif
    return fread(buf, 1, n, f) == n;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* the entries of a directory; returns their number (max cap) */
static int iso_list(FILE *f, uint32_t lba, uint32_t size, int joliet, IsoEnt *out, int cap)
{
    if (size > (1u << 20)) return 0;            /* a damaged image; the CD's directories are a few KB */
    uint8_t *d = malloc(size + 1);
    int n = 0;
    if (!d || !iso_read(f, lba, 0, d, size)) { free(d); return 0; }
    for (uint32_t p = 0; p < size && n < cap;) {
        uint8_t len = d[p];
        if (!len) { p = (p / 2048 + 1) * 2048; continue; }       /* records do not cross sectors */
        if (p + len > size || len < 34) break;
        const uint8_t *r = d + p;
        int nl = r[32];
        if (!(nl == 1 && (r[33] == 0 || r[33] == 1))) {          /* skip . and .. */
            IsoEnt *e = &out[n++];
            e->lba = le32(r + 2);
            e->size = le32(r + 10);
            e->dir = (r[25] & 2) != 0;
            int k = 0;
            if (joliet)
                for (int i = 0; i + 1 < nl && k < 126; i += 2) {
                    unsigned ch = (unsigned)r[33 + i] << 8 | r[34 + i];
                    e->name[k++] = (char)(ch < 256 ? ch : '_');
                }
            else
                for (int i = 0; i < nl && k < 126; i++) e->name[k++] = (char)r[33 + i];
            e->name[k] = 0;
            char *semi = strchr(e->name, ';');
            if (semi) *semi = 0;
            size_t l = strlen(e->name);
            if (l && e->name[l - 1] == '.') e->name[l - 1] = 0;
            /* a file name, never a path: the names are joined to the destination folder */
            if (!e->name[0] || strpbrk(e->name, "/:") || strchr(e->name, 92) || !strcmp(e->name, "..")) n--;
        }
        p += len;
    }
    free(d);
    return n;
}

static int copy_iso_file(FILE *f, const char *dest)
{
    uint8_t vd[2048];
    uint32_t root_lba = 0, root_size = 0;
    int joliet = 0;
    for (uint32_t s = 16; s < 32; s++) {
        if (!iso_read(f, s, 0, vd, 2048) || memcmp(vd + 1, "CD001", 5)) break;
        if (vd[0] == 255) break;
        int is_joliet = vd[0] == 2 && vd[88] == 0x25 && vd[89] == 0x2f && (vd[90] == 0x40 || vd[90] == 0x43 || vd[90] == 0x45);
        if (vd[0] == 1 && !joliet) { root_lba = le32(vd + 156 + 2); root_size = le32(vd + 156 + 10); }
        if (is_joliet) { root_lba = le32(vd + 156 + 2); root_size = le32(vd + 156 + 10); joliet = 1; }
    }
    if (!root_lba) return 0;
    IsoEnt *ent = malloc(sizeof *ent * 512);
    if (!ent) return 0;
    int n = iso_list(f, root_lba, root_size, joliet, ent, 512), ok = 0;
    for (int i = 0; i < n; i++)
        if (ent[i].dir && !_stricmp(ent[i].name, "data")) {
            uint32_t lba = ent[i].lba, size = ent[i].size;
            n = iso_list(f, lba, size, joliet, ent, 512);
            double total = 0, done = 0;
            int found = 0;
            for (int k = 0; k < n; k++) {
                if (ent[k].dir) continue;
                total += ent[k].size;
                if (!_stricmp(ent[k].name, "RCS.IMG")) found = 1;
            }
            if (!found) break;
            make_dirs(dest);
            ok = 1;
            for (int k = 0; k < n && ok; k++) {
                if (ent[k].dir) continue;
                ok = iso_read(f, ent[k].lba, 0, vd, 0) &&
                     copy_stream(f, ent[k].size, dest, ent[k].name, &done, total);
            }
            break;
        }
    free(ent);
    return ok;
}

int data_copy_iso(const char *iso_path, const char *dest)
{
    FILE *f = plat_fopen(iso_path, "rb");
    if (!f) return 0;
    int ok = copy_iso_file(f, dest);
    fclose(f);
    return ok;
}

int data_copy_iso_fd(int fd, const char *dest)
{
#ifdef _WIN32
    (void)fd; (void)dest;
    return 0;
#else
    FILE *f = fdopen(fd, "rb");
    if (!f) return 0;
    int ok = copy_iso_file(f, dest);
    fclose(f);
    return ok;
#endif
}

/* ---- looking for the CD ------------------------------------------------------------------------------------------ */
static int find_cd(char *data, size_t cap)
{
#ifdef _WIN32
    char drives[256];
    DWORD n = GetLogicalDriveStringsA(sizeof drives, drives);
    for (char *d = drives; n && *d; d += strlen(d) + 1) {
        UINT t = GetDriveTypeA(d);
        if (t == DRIVE_CDROM || t == DRIVE_REMOVABLE || t == DRIVE_FIXED || t == DRIVE_REMOTE) {
            if (t == DRIVE_FIXED && (d[0] == 'C' || d[0] == 'c')) continue;   /* the system disk has no CD on its root */
            if (is_cd_root(d, data, cap)) return 1;
        }
    }
#elif defined __SWITCH__
    (void)data; (void)cap;                     /* no CD drive: an ISO image in the folder of the .nro (data_find) */
#else
    const char *roots[] = { "/media", "/run/media", "/mnt", "/media/cdrom", "/Volumes" };
    const char *user = getenv("USER");
    for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        char base[512];
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 0) snprintf(base, sizeof base, "%s", roots[i]);
            else if (user) snprintf(base, sizeof base, "%s/%s", roots[i], user);
            else continue;
            if (is_cd_root(base, data, cap)) return 1;
            DIR *dd = opendir(base);
            struct dirent *e;
            if (!dd) continue;
            while ((e = readdir(dd))) {
                if (e->d_name[0] == '.') continue;
                char sub[1024];
                snprintf(sub, sizeof sub, "%s/%s", base, e->d_name);
                if (is_cd_root(sub, data, cap)) { closedir(dd); return 1; }
            }
            closedir(dd);
        }
    }
#endif
    return 0;
}

static const char *exe_dir(void)
{
    static char d[1024];
    if (!d[0]) {
        const char *b = plat_base_dir();
        snprintf(d, sizeof d, "%s", b ? b : "./");
    }
    return d;
}

static int writable(const char *dir)
{
    char p[1100];
    snprintf(p, sizeof p, "%s/.a2racerre_write_test", dir);
    FILE *f = fopen(p, "wb");
    if (!f) return 0;
    fclose(f);
    remove(p);
    return 1;
}

const char *data_find(const char *arg)
{
    char p[1100];
    /* 1. the command line: a folder (the data folder or the CD root) or an ISO */
    if (arg && *arg) {
        if (is_data_dir(arg)) { snprintf(g_found, sizeof g_found, "%s", arg); return g_found; }
        if (is_cd_root(arg, g_found, sizeof g_found)) return g_found;
    }
    const char *env = getenv("A2_DATA");
    if (env && is_data_dir(env)) { snprintf(g_found, sizeof g_found, "%s", env); return g_found; }
    snprintf(p, sizeof p, "%sdata", exe_dir());
    if (is_data_dir(p)) { snprintf(g_found, sizeof g_found, "%s", p); plat_set_user_dir(exe_dir()); return g_found; }
    if (is_data_dir("extract/data")) { snprintf(g_found, sizeof g_found, "extract/data"); return g_found; }
    snprintf(p, sizeof p, "%sdata", plat_user_dir());
    if (is_data_dir(p)) { snprintf(g_found, sizeof g_found, "%s", p); return g_found; }

    /* 2. first start: copy them */
    char dest[1100];
    snprintf(dest, sizeof dest, "%sdata", plat_user_dir());
    char src[1100];
    int ok = 0;
    const char *iso = arg && *arg ? arg : NULL;
    if (iso && plat_exists(iso) && !is_data_dir(iso)) ok = data_copy_iso(iso, dest);
#ifdef __SWITCH__
    /* no CD drive and no file picker: an ISO image of the CD in sdmc:/switch/a2racerre is unpacked into data/ (once;
     * the ISO can be deleted afterwards) */
    if (!ok) {
        DIR *d = opendir(plat_user_dir());
        struct dirent *e;
        while (d && !ok && (e = readdir(d))) {
            size_t n = strlen(e->d_name);
            if (n < 5 || _stricmp(e->d_name + n - 4, ".iso")) continue;
            snprintf(src, sizeof src, "%s%s", plat_user_dir(), e->d_name);
            printf("unpacking %s\n", src);
            ok = data_copy_iso(src, dest);
        }
        if (d) closedir(d);
    }
    if (!ok) {
        plat_message("A2RacerIIRE needs the files of your A2 Racer II CD (they are not included).\n\nPut an ISO image of "
                     "the CD into /switch/a2racerre/ on the SD card, or copy the CD's data folder to "
                     "/switch/a2racerre/data, and start A2RacerIIRE again.", 1);
        return NULL;
    }
#endif
    while (!ok) {
        if (find_cd(src, sizeof src)) {
            int r = plat_ask("A2RacerIIRE needs the files of your A2 Racer II CD. Copy them from the CD now (about 115 MB)?",
                             "Copy", "Choose an ISO / folder", "Quit");
            if (r < 0) return NULL;
            if (r == 1) { ok = copy_folder(src, dest); if (!ok) plat_message("Copying from the CD failed.", 1); continue; }
        } else {
            int r = plat_ask("A2RacerIIRE needs the files of your A2 Racer II CD (they are not included).\n\n"
                             "Insert the CD (or mount its ISO image) and choose Retry, or choose an ISO image or a folder "
                             "with a copy of the CD.", "Retry", "Choose an ISO / folder", "Quit");
            if (r < 0) return NULL;
            if (r == 1) continue;
        }
        char pick[1024];
        int fd = -1;
        int kind = plat_pick_data(pick, sizeof pick, &fd, dest);
        if (kind == PICK_NONE) continue;
        if (kind == PICK_FD) ok = data_copy_iso_fd(fd, dest);
        else if (kind == PICK_COPIED) ok = 1;
        else if (is_data_dir(pick)) ok = copy_folder(pick, dest);
        else if (is_cd_root(pick, src, sizeof src)) ok = copy_folder(src, dest);
        else ok = data_copy_iso(pick, dest);
        if (!ok) plat_message("That is not the A2 Racer II CD (no data folder with RCS.IMG found), or copying failed.", 1);
    }
    if (!is_data_dir(dest)) { plat_message("The copy is incomplete.", 1); return NULL; }
    if (data_verify(dest)) plat_message("Some game files differ from the A2 Racer II CD this port was made with "
                                         "(Dutch, version 1.01). The game may still work.", 1);
    (void)writable;
    snprintf(g_found, sizeof g_found, "%s", dest);
    return g_found;
}
