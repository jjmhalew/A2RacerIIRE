/* ini.h - Windows-style .ini files as the original reads and writes them with GetPrivateProfileString /
 * WritePrivateProfileString: [SECTION] and KEY=value, ';' comments, names compared ignoring case. Writing keeps
 * the file's comments and order (the CD's Game.ini and Raser.ini document every setting in their comments).
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_INI_H
#define A2_INI_H
#include <stddef.h>

typedef struct Ini Ini;
Ini *ini_load(const char *path);                 /* an empty Ini when the file does not exist */
Ini *ini_parse(const char *text, size_t n);
void ini_free(Ini *ini);
int  ini_save(const Ini *ini, const char *path);
const char *ini_get(const Ini *ini, const char *sec, const char *key);   /* NULL when absent; trimmed (static buffer) */
int  ini_int(const Ini *ini, const char *sec, const char *key, int def);
float ini_float(const Ini *ini, const char *sec, const char *key, float def);
void ini_str(const Ini *ini, const char *sec, const char *key, const char *def, char *out, size_t cap);
void ini_set(Ini *ini, const char *sec, const char *key, const char *value);
void ini_set_int(Ini *ini, const char *sec, const char *key, int value);
#endif
