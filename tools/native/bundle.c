/* bundle.c - the single-file launcher of make_standalone.bat (not part of the engine). tools/native/pack.c appends
 * A2RacerIIRE.exe and the game files to this exe; at start it unpacks them once to %LOCALAPPDATA%\A2RacerRE (again only
 * when the bundle changes), then runs A2RacerIIRE.exe from there with that data folder and passes its own command line on
 * (e.g. "A2RacerIIRE-standalone.exe --windowed"). The settings and saves stay in the user folder of A2RacerIIRE.exe
 * (%APPDATA%\A2RacerRE), shared with a normal install. The data inside makes the result for your own use only: never
 * share it. A2_BUNDLE_DIR unpacks somewhere else (tests); A2_HIDDEN shows no progress window.
 *
 * Payload layout (after the launcher itself): the file bodies, then the index (per entry: u32 name length, name with
 * '/', u64 offset from the start of the exe, u64 size), then the trailer: u64 index offset, u32 count, u64 bundle id,
 * "A2RCPK01". Entry 0 is A2RacerIIRE.exe, the others data/<name>.
 *
 * Build (build.bat standalone): zig cc -std=c99 -O2 -Wl,--subsystem,windows tools/native/bundle.c res/a2racerre.rc
 * -luser32 -lgdi32
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

#pragma pack(push, 1)
typedef struct { uint64_t index_off; uint32_t count; uint64_t id; char magic[8]; } Trailer;
#pragma pack(pop)

static HWND g_win, g_text;
static int g_console;

static void fail(const wchar_t *what)
{
    wchar_t msg[1024];
    swprintf(msg, 1024, L"%ls (error %lu)", what, GetLastError());
    if (g_console || GetEnvironmentVariableW(L"A2_HIDDEN", NULL, 0)) fwprintf(stderr, L"A2RacerIIRE: %ls\n", msg);
    else MessageBoxW(NULL, msg, L"A2RacerIIRE", MB_ICONERROR);
    ExitProcess(1);
}

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}

static void progress(uint64_t done, uint64_t total)
{
    static int last = -1;
    int pct = total ? (int)(done * 100 / total) : 100;
    if (pct == last) return;
    last = pct;
    wchar_t s[128];
    swprintf(s, 128, L"Unpacking the game files (first start only)... %d%%", pct);
    if (g_text) { SetWindowTextW(g_text, s); pump(); }
    if (g_console) { fwprintf(stderr, L"\r%ls", s); if (pct == 100) fputwc(L'\n', stderr); }
}

static void show_window(void)
{
    if (g_console || GetEnvironmentVariableW(L"A2_HIDDEN", NULL, 0)) return;
    g_win = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"A2RacerIIRE", WS_POPUP | WS_BORDER | WS_VISIBLE,
                            (GetSystemMetrics(SM_CXSCREEN) - 440) / 2, (GetSystemMetrics(SM_CYSCREEN) - 70) / 2, 440, 70,
                            NULL, NULL, NULL, NULL);
    g_text = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, 440, 70, g_win,
                             NULL, NULL, NULL);
    SendMessageW(g_text, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
    pump();
}

static void make_dirs(wchar_t *path)          /* creates every parent directory of path */
{
    for (wchar_t *p = path; *p; p++)
        if (*p == L'\\' && p > path + 2) { *p = 0; CreateDirectoryW(path, NULL); *p = L'\\'; }
}

static int read_at(HANDLE f, uint64_t off, void *buf, DWORD n)
{
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    DWORD got;
    return SetFilePointerEx(f, li, NULL, FILE_BEGIN) && ReadFile(f, buf, n, &got, NULL) && got == n;
}

static const wchar_t *skip_argv0(const wchar_t *c)
{
    if (*c == L'"') { c++; while (*c && *c != L'"') c++; if (*c) c++; }
    else while (*c && *c != L' ' && *c != L'\t') c++;
    while (*c == L' ' || *c == L'\t') c++;
    return c;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    (void)hi; (void)hp; (void)cmd; (void)show;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) { g_console = 1; freopen("CONOUT$", "w", stderr); }

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    HANDLE f = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) fail(L"cannot open the exe itself");
    LARGE_INTEGER fsz;
    GetFileSizeEx(f, &fsz);
    Trailer t;
    if (!read_at(f, (uint64_t)fsz.QuadPart - sizeof t, &t, sizeof t) || memcmp(t.magic, "A2RCPK01", 8)) {
        SetLastError(0);
        fail(L"no game files attached to this exe (make it with make_standalone.bat)");
    }

    wchar_t base[MAX_PATH], idpath[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"A2_BUNDLE_DIR", base, MAX_PATH);
    if (!n || n >= MAX_PATH - 64) {
        n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        if (!n || n >= MAX_PATH - 64) fail(L"LOCALAPPDATA is not set");
        wcscat(base, L"\\A2RacerRE");   /* the folder keeps the project's first name */
    }
    CreateDirectoryW(base, NULL);
    swprintf(idpath, MAX_PATH, L"%ls\\.bundle_id", base);

    uint64_t have = 0;
    FILE *idf = _wfopen(idpath, L"rb");
    if (idf) { if (fread(&have, 8, 1, idf) != 1) have = 0; fclose(idf); }
    if (have != t.id) {
        DeleteFileW(idpath);
        uint64_t isz = (uint64_t)fsz.QuadPart - sizeof t - t.index_off;
        unsigned char *idx = malloc((size_t)isz), *buf = malloc(4 << 20);
        if (!idx || !buf || !read_at(f, t.index_off, idx, (DWORD)isz)) fail(L"cannot read the bundle index");
        uint64_t total = 0, done = 0;
        unsigned char *q = idx;
        for (uint32_t i = 0; i < t.count; i++) {
            uint32_t nl;
            memcpy(&nl, q, 4);
            q += 4 + nl;
            uint64_t sz;
            memcpy(&sz, q + 8, 8);
            total += sz;
            q += 16;
        }
        show_window();
        progress(0, total);
        q = idx;
        for (uint32_t i = 0; i < t.count; i++) {
            uint32_t nl;
            memcpy(&nl, q, 4);
            q += 4;
            wchar_t name[MAX_PATH], out[MAX_PATH * 2];
            int wl = MultiByteToWideChar(CP_UTF8, 0, (const char *)q, (int)nl, name, MAX_PATH - 1);
            name[wl] = 0;
            q += nl;
            uint64_t off, sz;
            memcpy(&off, q, 8);
            memcpy(&sz, q + 8, 8);
            q += 16;
            for (wchar_t *c = name; *c; c++) if (*c == L'/') *c = L'\\';
            swprintf(out, MAX_PATH * 2, L"%ls\\%ls", base, name);
            make_dirs(out);
            HANDLE o = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (o == INVALID_HANDLE_VALUE) fail(out);
            LARGE_INTEGER li;
            li.QuadPart = (LONGLONG)off;
            SetFilePointerEx(f, li, NULL, FILE_BEGIN);
            while (sz) {
                DWORD chunk = sz > (4u << 20) ? (4u << 20) : (DWORD)sz, got, put;
                if (!ReadFile(f, buf, chunk, &got, NULL) || got != chunk) fail(L"cannot read the bundle");
                if (!WriteFile(o, buf, chunk, &put, NULL) || put != chunk) fail(out);
                sz -= chunk;
                done += chunk;
                progress(done, total);
            }
            CloseHandle(o);
        }
        free(idx);
        free(buf);
        idf = _wfopen(idpath, L"wb");
        if (idf) { fwrite(&t.id, 8, 1, idf); fclose(idf); }
        if (g_win) DestroyWindow(g_win);
    }
    CloseHandle(f);

    /* A2RacerIIRE.exe <base>\data [the arguments this exe got], run in <base> */
    const wchar_t *args = skip_argv0(GetCommandLineW());
    size_t cl = wcslen(args) + 3 * MAX_PATH;
    wchar_t *line = malloc(cl * sizeof *line), exe[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\A2RacerIIRE.exe", base);
    swprintf(line, cl, L"\"%ls\" \"%ls\\data\" %ls", exe, base, args);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, base, &si, &pi)) fail(L"cannot start A2RacerIIRE.exe");
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    return (int)code;
}
