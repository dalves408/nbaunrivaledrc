/* nba_import: copies validated NBA Unrivaled game files from a player-supplied
 * ZIP or folder into the install tree.
 *
 *   nba_import <source.zip|folder> <dest_dir>
 *   nba_import --prepare <source> <dest_dir>
 *
 * Accepted sources (same content prepared with scripts/prepare_game.py):
 *   - Extracted game tree with default.xex (+ data.zip, …) at the root or nested
 *   - Xbox LIVE/STFS package tree: …/584109D4/000D0000/<content> (~169 MB LIVE)
 *   - ZIP of either layout (one nested root folder is fine)
 *
 * Exit codes: 0 ok, 2 source not found, 3 not a usable ZIP/folder of game
 * files, 4 wrong title/version (default.xex), 5 incomplete/damaged, 6 write. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bcrypt.h>

#include "checklist.h"
#include "stfs_nba.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

static WCHAR g_dest[MAX_PATH];
static int g_sdk;
static unsigned long long g_copied;
static unsigned g_files;

static void progress_frac(double f, const char *text)
{
    if (g_sdk)
        printf("progress %.4f %s\n", f, text);
    else
        printf("PROGRESS %.0f 100 %s\n", f * 100.0, text);
    fflush(stdout);
}

static void progress_cb(double f, const char *text)
{
    progress_frac(f, text);
}

static int make_dirs_for(const WCHAR *file)
{
    WCHAR dir[MAX_PATH], *sl;
    wcscpy_s(dir, MAX_PATH, file);
    sl = wcsrchr(dir, L'\\');
    if (!sl) return 1;
    *sl = 0;
    return SHCreateDirectoryExW(NULL, dir, NULL) == ERROR_SUCCESS ||
           GetFileAttributesW(dir) != INVALID_FILE_ATTRIBUTES;
}

static void remove_tree(const WCHAR *dir)
{
    WCHAR buf[MAX_PATH + 2];
    SHFILEOPSTRUCTW op;
    size_t n = wcslen(dir);
    if (n == 0 || n >= MAX_PATH) return;
    memcpy(buf, dir, (n + 1) * sizeof(WCHAR));
    buf[n + 1] = 0;
    memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE;
    op.pFrom = buf;
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

static int sha256_file(const WCHAR *path, unsigned char out[32])
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hh = NULL;
    HANDLE f;
    static unsigned char buf[1 << 20];
    DWORD n;
    int ok = 0;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) goto done;
    if (BCryptCreateHash(alg, &hh, NULL, 0, NULL, 0, 0)) goto done;
    while (ReadFile(f, buf, sizeof buf, &n, NULL) && n) {
        if (BCryptHashData(hh, buf, n, 0)) goto done;
    }
    ok = !BCryptFinishHash(hh, out, 32, 0);
done:
    if (hh) BCryptDestroyHash(hh);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return ok;
}

static int xex_ok(const WCHAR *path)
{
    HANDLE f;
    unsigned char magic[4], h[32];
    DWORD n;
    LARGE_INTEGER sz;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(f, magic, 4, &n, NULL) || n != 4 || memcmp(magic, "XEX2", 4) != 0) {
        CloseHandle(f);
        return 0;
    }
    if (!GetFileSizeEx(f, &sz) || (unsigned long long)sz.QuadPart != 1335296ULL) {
        CloseHandle(f);
        return 0;
    }
    CloseHandle(f);
    return sha256_file(path, h) && memcmp(h, XEX_SHA256, 32) == 0;
}

static int file_size_is(const WCHAR *path, unsigned long long want)
{
    HANDLE f;
    LARGE_INTEGER sz;
    int ok;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = GetFileSizeEx(f, &sz) && (unsigned long long)sz.QuadPart == want;
    CloseHandle(f);
    return ok;
}

static void path_join(WCHAR *out, const WCHAR *a, const char *rel)
{
    WCHAR w[MAX_PATH], *p;
    MultiByteToWideChar(CP_ACP, 0, rel, -1, w, MAX_PATH);
    for (p = w; *p; p++) if (*p == L'/') *p = L'\\';
    swprintf_s(out, MAX_PATH, L"%s\\%s", a, w);
}

/* Recursively find default.xex (depth-limited). Returns folder that contains it. */
static int find_xex_root(const WCHAR *dir, WCHAR *root, int depth)
{
    WCHAR pat[MAX_PATH], t[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    swprintf_s(t, MAX_PATH, L"%s\\default.xex", dir);
    if (GetFileAttributesW(t) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(root, MAX_PATH, dir);
        return 1;
    }
    if (depth <= 0) return 0;

    swprintf_s(pat, MAX_PATH, L"%s\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
            continue;
        swprintf_s(t, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (find_xex_root(t, root, depth - 1)) {
            FindClose(h);
            return 1;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

static int basename_is_package_content(const WCHAR *path)
{
    const WCHAR *base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    return _wcsicmp(base, NBA_PACKAGE_CONTENT_NAME) == 0;
}

/* Prefer …\584109D4\000D0000\<content>, else recursive search by exact name. */
static int find_named_content(const WCHAR *dir, WCHAR *out, int depth)
{
    WCHAR preferred[MAX_PATH], pat[MAX_PATH], t[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    swprintf_s(preferred, MAX_PATH, L"%s\\584109D4\\000D0000\\%s", dir, NBA_PACKAGE_CONTENT_NAME);
    if (GetFileAttributesW(preferred) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(out, MAX_PATH, preferred);
        return 1;
    }
    swprintf_s(preferred, MAX_PATH, L"%s\\000D0000\\%s", dir, NBA_PACKAGE_CONTENT_NAME);
    if (GetFileAttributesW(preferred) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(out, MAX_PATH, preferred);
        return 1;
    }
    swprintf_s(preferred, MAX_PATH, L"%s\\%s", dir, NBA_PACKAGE_CONTENT_NAME);
    if (GetFileAttributesW(preferred) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(out, MAX_PATH, preferred);
        return 1;
    }
    if (depth <= 0) return 0;

    swprintf_s(pat, MAX_PATH, L"%s\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
            continue;
        swprintf_s(t, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (find_named_content(t, out, depth - 1)) {
                FindClose(h);
                return 1;
            }
        } else if (_wcsicmp(fd.cFileName, NBA_PACKAGE_CONTENT_NAME) == 0) {
            wcscpy_s(out, MAX_PATH, t);
            FindClose(h);
            return 1;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

/* Exit 4 = wrong package (hash/name), matching wrong-title default.xex. */
static int verify_package_content(const WCHAR *path)
{
    unsigned char h[32];
    if (!basename_is_package_content(path)) {
        printf("ERROR Expected LIVE content file named %ls.\n", NBA_PACKAGE_CONTENT_NAME);
        return 4;
    }
    if (!nba_stfs_is_title_package(path)) {
        printf("ERROR %ls is not a LIVE/PIRS/CON package for title 584109D4.\n",
               NBA_PACKAGE_CONTENT_NAME);
        return 4;
    }
    progress_frac(0.04, "Verifying LIVE package hash");
    if (!sha256_file(path, h)) {
        printf("ERROR Could not read LIVE package %ls.\n", NBA_PACKAGE_CONTENT_NAME);
        return 4;
    }
    for (size_t i = 0; i < sizeof NBA_PACKAGE_SHA256 / sizeof NBA_PACKAGE_SHA256[0]; i++) {
        if (memcmp(h, NBA_PACKAGE_SHA256[i], sizeof h) == 0) return 0;
    }
    printf("ERROR Unsupported SHA-256 for the inner LIVE package %ls.\n",
           NBA_PACKAGE_CONTENT_NAME);
    return 4;
}

/* Accept the content file itself as the source path (name + hash gated). */
static int source_is_package_content(const WCHAR *path)
{
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    return basename_is_package_content(path);
}

static int validate_root(const WCHAR *root)
{
    int i;
    WCHAR path[MAX_PATH], xex[MAX_PATH];
    swprintf_s(xex, MAX_PATH, L"%s\\default.xex", root);
    if (!xex_ok(xex)) {
        printf("ERROR default.xex is missing or is not the supported NBA Unrivaled build.\n");
        return 4;
    }
    for (i = 0; i < NBA_CHECKLIST_COUNT; i++) {
        path_join(path, root, NBA_CHECKLIST[i].path);
        if (!file_size_is(path, NBA_CHECKLIST[i].size)) {
            printf("ERROR Missing or wrong size: %s\n", NBA_CHECKLIST[i].path);
            return 5;
        }
    }
    return 0;
}

static int copy_file(const WCHAR *src, const WCHAR *dst)
{
    if (!make_dirs_for(dst)) return 0;
    return CopyFileW(src, dst, FALSE) != 0;
}

static int copy_tree(const WCHAR *src_root, const WCHAR *dst_root, const WCHAR *rel)
{
    WCHAR src[MAX_PATH], dst[MAX_PATH], pat[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    swprintf_s(src, MAX_PATH, L"%s\\%s", src_root, rel);
    swprintf_s(dst, MAX_PATH, L"%s\\%s", dst_root, rel);
    swprintf_s(pat, MAX_PATH, L"%s\\*", src);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 6;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
            continue;
        if (rel[0])
            swprintf_s(child, MAX_PATH, L"%s\\%s", rel, fd.cFileName);
        else
            swprintf_s(child, MAX_PATH, L"%s", fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (copy_tree(src_root, dst_root, child)) {
                FindClose(h);
                return 6;
            }
        } else {
            WCHAR s[MAX_PATH], d[MAX_PATH];
            swprintf_s(s, MAX_PATH, L"%s\\%s", src_root, child);
            swprintf_s(d, MAX_PATH, L"%s\\%s", dst_root, child);
            if (!copy_file(s, d)) {
                FindClose(h);
                return 6;
            }
            g_files++;
            g_copied += ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if ((g_files % 25) == 0)
                progress_frac(0.70 + 0.25 * (double)g_files / (double)NBA_EXPECTED_FILE_COUNT,
                              "Copying game files");
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

static int looks_like_zip(const WCHAR *path)
{
    size_t n = wcslen(path);
    return n > 4 && (!_wcsicmp(path + n - 4, L".zip"));
}

static int escape_powershell_path(const WCHAR *path, WCHAR *out, size_t capacity)
{
    size_t offset = 0;
    while (*path) {
        size_t chars = *path == L'\'' ? 2 : 1;
        if (offset + chars >= capacity) return 0;
        if (chars == 2) out[offset++] = L'\'';
        out[offset++] = *path++;
    }
    out[offset] = 0;
    return 1;
}

static int expand_zip(const WCHAR *zip, const WCHAR *dest)
{
    WCHAR cmd[2048], quoted_zip[2 * MAX_PATH], quoted_dest[2 * MAX_PATH];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    if (!escape_powershell_path(zip, quoted_zip, _countof(quoted_zip)) ||
        !escape_powershell_path(dest, quoted_dest, _countof(quoted_dest))) return 3;
    SHCreateDirectoryExW(NULL, dest, NULL);
    if (swprintf_s(cmd, _countof(cmd),
                   L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
                   L"\"$ErrorActionPreference = 'Stop'; Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
                   quoted_zip, quoted_dest) < 0) return 3;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 3;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0 ? 0 : 3;
}

static int run_import(const WCHAR *source, const WCHAR *destination)
{
    DWORD attr;
    WCHAR root[MAX_PATH], stage_zip[MAX_PATH], stage_stfs[MAX_PATH], pkg[MAX_PATH];
    int r;
    const WCHAR *work = source;
    int from_stfs = 0;

    g_copied = 0;
    g_files = 0;
    stage_zip[0] = 0;
    stage_stfs[0] = 0;
    wcscpy_s(g_dest, MAX_PATH, destination);
    {
        size_t n = wcslen(g_dest);
        while (n > 3 && (g_dest[n - 1] == L'\\' || g_dest[n - 1] == L'/')) g_dest[--n] = 0;
    }

    attr = GetFileAttributesW(source);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("ERROR The ZIP or folder was not found.\n");
        return 2;
    }

    /* Single LIVE content file (exact content name). */
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY) && source_is_package_content(source)) {
        r = verify_package_content(source);
        if (r) return r;
        swprintf_s(stage_stfs, MAX_PATH, L"%s.stfs-stage-%lu", g_dest,
                   (unsigned long)GetCurrentProcessId());
        remove_tree(stage_stfs);
        progress_frac(0.05, "Extracting Xbox LIVE package");
        r = nba_stfs_extract(source, stage_stfs, progress_cb);
        if (r) {
            remove_tree(stage_stfs);
            return r;
        }
        work = stage_stfs;
        from_stfs = 1;
    } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!looks_like_zip(source)) {
            printf("ERROR Supply a .zip archive, an extracted game folder (default.xex), "
                   "or the Xbox LIVE content file %ls (title 584109D4).\n",
                   NBA_PACKAGE_CONTENT_NAME);
            return 3;
        }
        swprintf_s(stage_zip, MAX_PATH, L"%s.zip-stage-%lu", g_dest,
                   (unsigned long)GetCurrentProcessId());
        remove_tree(stage_zip);
        progress_frac(0.03, "Extracting ZIP");
        r = expand_zip(source, stage_zip);
        if (r) {
            remove_tree(stage_zip);
            printf("ERROR The ZIP could not be extracted.\n");
            return 3;
        }
        work = stage_zip;
    }

    if (!from_stfs && !find_xex_root(work, root, 6)) {
        /* No extracted dump. Locate the LIVE content by exact name, then verify it. */
        if (!find_named_content(work, pkg, 8)) {
            printf("ERROR No usable NBA Unrivaled content found.\n"
                   "Point Setup at either:\n"
                   "  - an extracted game folder (or ZIP) with default.xex, or\n"
                   "  - an Xbox LIVE package tree containing:\n"
                   "      ...\\584109D4\\000D0000\\%ls\n"
                   "    (the inner file must match a supported SHA-256).\n",
                   NBA_PACKAGE_CONTENT_NAME);
            if (stage_zip[0]) remove_tree(stage_zip);
            return 3;
        }
        r = verify_package_content(pkg);
        if (r) {
            if (stage_zip[0]) remove_tree(stage_zip);
            return r;
        }
        swprintf_s(stage_stfs, MAX_PATH, L"%s.stfs-stage-%lu", g_dest,
                   (unsigned long)GetCurrentProcessId());
        remove_tree(stage_stfs);
        progress_frac(0.05, "Extracting Xbox LIVE package");
        r = nba_stfs_extract(pkg, stage_stfs, progress_cb);
        if (stage_zip[0]) remove_tree(stage_zip);
        stage_zip[0] = 0;
        if (r) {
            remove_tree(stage_stfs);
            return r;
        }
        work = stage_stfs;
        from_stfs = 1;
    }

    if (from_stfs && !find_xex_root(work, root, 2)) {
        printf("ERROR STFS extraction did not produce default.xex.\n");
        remove_tree(stage_stfs);
        return 5;
    }

    progress_frac(0.68, "Validating game files");
    r = validate_root(root);
    if (r) {
        if (stage_zip[0]) remove_tree(stage_zip);
        if (stage_stfs[0]) remove_tree(stage_stfs);
        return r;
    }
    progress_frac(0.70, "Copying game files");
    r = copy_tree(root, g_dest, L"");
    if (stage_zip[0]) remove_tree(stage_zip);
    if (stage_stfs[0]) remove_tree(stage_stfs);
    if (r) {
        printf("ERROR Could not write the game files.\n");
        return 6;
    }
    if (g_files < (unsigned)(NBA_EXPECTED_FILE_COUNT / 2)) {
        printf("ERROR Only %u files were copied (expected about %d). The dump looks incomplete.\n",
               g_files, NBA_EXPECTED_FILE_COUNT);
        return 5;
    }
    r = validate_root(g_dest);
    if (r) return r;
    printf("OK %u files%s\n", g_files, from_stfs ? " (from STFS package)" : "");
    return 0;
}

static int patch_title_menu(const WCHAR *destination);

static int sdk_prepare(const WCHAR *source, const WCHAR *destination, int with_launcher_patch)
{
    WCHAR dest[MAX_PATH], stage[MAX_PATH], backup[MAX_PATH], parent[MAX_PATH], *sl;
    DWORD n = GetFullPathNameW(destination, MAX_PATH, dest, NULL), attr;
    int r, had_old = 0;

    g_sdk = 1;
    while (n > 3 && (dest[n - 1] == L'\\' || dest[n - 1] == L'/')) dest[--n] = 0;
    if (!n || n >= MAX_PATH - 64) {
        printf("ERROR The game folder path is not usable.\n");
        return 6;
    }
    swprintf_s(stage, MAX_PATH, L"%s.stage-%lu", dest, (unsigned long)GetCurrentProcessId());
    swprintf_s(backup, MAX_PATH, L"%s.old-%lu", dest, (unsigned long)GetCurrentProcessId());
    wcscpy_s(parent, MAX_PATH, dest);
    sl = wcsrchr(parent, L'\\');
    if (sl) {
        *sl = 0;
        SHCreateDirectoryExW(NULL, parent, NULL);
    }
    progress_frac(0.02, "Checking your game source");
    remove_tree(stage);
    r = run_import(source, stage);
    if (r == 0 && with_launcher_patch) r = patch_title_menu(stage);
    if (r) {
        remove_tree(stage);
        return r;
    }
    attr = GetFileAttributesW(dest);
    if (attr != INVALID_FILE_ATTRIBUTES) {
        if (!(attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
            printf("ERROR The game folder is not a plain folder.\n");
            remove_tree(stage);
            return 6;
        }
        if (!MoveFileW(dest, backup)) {
            printf("ERROR Could not replace the old game files.\n");
            remove_tree(stage);
            return 6;
        }
        had_old = 1;
    }
    if (!MoveFileW(stage, dest)) {
        printf("ERROR Could not put the game files in place.\n");
        if (had_old) MoveFileW(backup, dest);
        remove_tree(stage);
        return 6;
    }
    if (had_old) remove_tree(backup);
    progress_frac(1.0, "Game files are ready");
    return 0;
}

/* RecompLauncher imports the same game tree as Setup and applies the same patch. */
static int patch_title_menu(const WCHAR *destination)
{
    WCHAR helper[MAX_PATH], cmd[2 * MAX_PATH + 120], *slash;
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    DWORD length = GetModuleFileNameW(NULL, helper, MAX_PATH);
    if (!length || length >= MAX_PATH || !(slash = wcsrchr(helper, L'\\'))) return 7;
    *slash = 0;
    if (swprintf_s(cmd, _countof(cmd),
                   L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass "
                   L"-File \"%s\\launcher-support\\Apply-TitleMenuPatches.ps1\" -GameRoot \"%s\"",
                   helper, destination) < 0) return 7;

    progress_frac(0.98, "Applying title-menu patch");
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 7;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        printf("ERROR Title-menu patch failed (PowerShell exit code %lu).\n", (unsigned long)code);
        return 7;
    }
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 4 && !wcscmp(argv[1], L"--launcher-prepare"))
        return sdk_prepare(argv[2], argv[3], 1);
    if (argc == 4 && !wcscmp(argv[1], L"--prepare"))
        return sdk_prepare(argv[2], argv[3], 0);
    if (argc < 3) {
        printf("usage: nba_import <zip|folder|LIVE package> <dest folder> | "
               "--prepare <zip|folder|LIVE package> <dest folder> | "
               "--launcher-prepare <zip|folder|LIVE package> <dest folder>\n");
        return 2;
    }
    return run_import(argv[1], argv[2]);
}
