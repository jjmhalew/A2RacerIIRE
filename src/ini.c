/* ini.c - see ini.h. The file is kept as its lines; a lookup walks them. Values are trimmed of white space, as
 * GetPrivateProfileString does (which does not strip a trailing "; comment": GetPrivateProfileInt reads the leading
 * number, so "car1 = 15000 ; Trabant" is 15000 - ini_int does the same).
 *
 * This file is part of A2RacerRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#include "ini.h"
#include "plat.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Ini {
    char **lines;
    int n, cap;
};

static void add_line(Ini *ini, int at, const char *s, size_t len)
{
    if (ini->n == ini->cap) {
        ini->cap = ini->cap ? ini->cap * 2 : 64;
        ini->lines = realloc(ini->lines, sizeof *ini->lines * (size_t)ini->cap);
    }
    char *c = malloc(len + 1);
    memcpy(c, s, len);
    c[len] = 0;
    memmove(ini->lines + at + 1, ini->lines + at, sizeof *ini->lines * (size_t)(ini->n - at));
    ini->lines[at] = c;
    ini->n++;
}

Ini *ini_parse(const char *t, size_t n)
{
    Ini *ini = calloc(1, sizeof *ini);
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && t[j] != '\n') j++;
        size_t e = j;
        if (e > i && t[e - 1] == '\r') e--;
        add_line(ini, ini->n, t + i, e - i);
        i = j + 1;
    }
    return ini;
}

Ini *ini_load(const char *path)
{
    FILE *f = plat_fopen(path, "rb");
    if (!f) return ini_parse("", 0);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *d = malloc((size_t)n + 1);
    size_t got = fread(d, 1, (size_t)n, f);
    fclose(f);
    Ini *ini = ini_parse(d, got);
    free(d);
    return ini;
}

void ini_free(Ini *ini)
{
    if (!ini) return;
    for (int i = 0; i < ini->n; i++) free(ini->lines[i]);
    free(ini->lines);
    free(ini);
}

int ini_save(const Ini *ini, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    for (int i = 0; i < ini->n; i++) fprintf(f, "%s\r\n", ini->lines[i]);
    return fclose(f) == 0;
}

static const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

/* section name of a "[name]" line into buf; 0 = not a section line */
static int section_of(const char *line, char *buf, size_t cap)
{
    const char *s = skip_ws(line);
    if (*s != '[') return 0;
    const char *e = strchr(s, ']');
    if (!e) return 0;
    size_t n = (size_t)(e - s - 1);
    if (n >= cap) n = cap - 1;
    memcpy(buf, s + 1, n);
    buf[n] = 0;
    return 1;
}

/* key of a "key = value" line matches? -> pointer to the value */
static const char *key_match(const char *line, const char *key)
{
    const char *s = skip_ws(line);
    if (*s == ';' || *s == '[' || !*s) return NULL;
    const char *eq = strchr(s, '=');
    if (!eq) return NULL;
    const char *ke = eq;
    while (ke > s && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
    size_t kl = strlen(key);
    if ((size_t)(ke - s) != kl || _strnicmp(s, key, kl)) return NULL;
    return skip_ws(eq + 1);
}

/* line index of key in sec, -1 when absent; *sec_end = index after the section's last line (-1 = no section) */
static int find(const Ini *ini, const char *sec, const char *key, int *sec_end)
{
    char cur[128] = "";
    int in = 0, found = -1;
    if (sec_end) *sec_end = -1;
    for (int i = 0; i < ini->n; i++) {
        char s[128];
        if (section_of(ini->lines[i], s, sizeof s)) {
            if (in && sec_end) *sec_end = i;
            in = !_stricmp(s, sec);
            snprintf(cur, sizeof cur, "%s", s);
            if (in && sec_end) *sec_end = i + 1;
            continue;
        }
        if (in) {
            if (sec_end && ini->lines[i][0] && *skip_ws(ini->lines[i]) != ';') *sec_end = i + 1;
            if (found < 0 && key && key_match(ini->lines[i], key)) found = i;
        }
    }
    return found;
}

static char g_val[1024];

const char *ini_get(const Ini *ini, const char *sec, const char *key)
{
    int i = find(ini, sec, key, NULL);
    if (i < 0) return NULL;
    const char *v = key_match(ini->lines[i], key);
    snprintf(g_val, sizeof g_val, "%s", v);
    size_t n = strlen(g_val);
    while (n && (g_val[n - 1] == ' ' || g_val[n - 1] == '\t')) g_val[--n] = 0;
    /* GetPrivateProfileString strips one pair of surrounding quotes */
    if (n >= 2 && (g_val[0] == '"' || g_val[0] == '\'') && g_val[n - 1] == g_val[0]) {
        memmove(g_val, g_val + 1, n - 2);
        g_val[n - 2] = 0;
    }
    return g_val;
}

int ini_int(const Ini *ini, const char *sec, const char *key, int def)
{
    const char *v = ini_get(ini, sec, key);
    if (!v) return def;
    char *e;
    long r = strtol(v, &e, 10);
    return e == v ? 0 : (int)r;
}

float ini_float(const Ini *ini, const char *sec, const char *key, float def)
{
    const char *v = ini_get(ini, sec, key);
    return v ? (float)atof(v) : def;
}

void ini_str(const Ini *ini, const char *sec, const char *key, const char *def, char *out, size_t cap)
{
    const char *v = ini_get(ini, sec, key);
    snprintf(out, cap, "%s", v ? v : def);
}

void ini_set(Ini *ini, const char *sec, const char *key, const char *value)
{
    char line[1100];
    snprintf(line, sizeof line, "%s=%s", key, value);
    int end, i = find(ini, sec, key, &end);
    if (i >= 0) {
        free(ini->lines[i]);
        ini->lines[i] = malloc(strlen(line) + 1);
        strcpy(ini->lines[i], line);
        return;
    }
    if (end < 0) {
        char s[140];
        snprintf(s, sizeof s, "[%s]", sec);
        add_line(ini, ini->n, s, strlen(s));
        end = ini->n;
    }
    add_line(ini, end, line, strlen(line));
}

void ini_set_int(Ini *ini, const char *sec, const char *key, int value)
{
    char b[32];
    snprintf(b, sizeof b, "%d", value);
    ini_set(ini, sec, key, b);
}
