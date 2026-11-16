/* Contiguous STFS extractor for NBA Unrivaled (title 584109D4).
 * Algorithm from tools/XBLA-Extract/stfs_extract.py / prepare_game.py. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

#include "stfs_nba.h"

#define STFS_BLOCK 0x1000
#define STFS_MAX_ENTRIES 4096

static unsigned get_cluster(unsigned startclust, unsigned offset)
{
    unsigned rst = 0;
    while (startclust >= 170) {
        startclust /= 170;
        rst += (startclust + 1) * offset;
    }
    return rst;
}

static int read_at(HANDLE f, unsigned long long off, void *buf, DWORD n)
{
    LARGE_INTEGER li;
    DWORD got = 0;
    li.QuadPart = (LONGLONG)off;
    if (!SetFilePointerEx(f, li, NULL, FILE_BEGIN)) return 0;
    return ReadFile(f, buf, n, &got, NULL) && got == n;
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

static int write_all(const WCHAR *path, const unsigned char *data, size_t n)
{
    HANDLE f;
    DWORD wrote = 0;
    if (!make_dirs_for(path)) return 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    while (wrote < n) {
        DWORD chunk = 0, want = (DWORD)((n - wrote) > 1u << 20 ? 1u << 20 : (n - wrote));
        if (!WriteFile(f, data + wrote, want, &chunk, NULL) || !chunk) {
            CloseHandle(f);
            return 0;
        }
        wrote += chunk;
    }
    CloseHandle(f);
    return 1;
}

int nba_stfs_is_title_package(const WCHAR *path)
{
    HANDLE f;
    unsigned char magic[4], tid[4];
    LARGE_INTEGER sz;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0xD000) {
        CloseHandle(f);
        return 0;
    }
    if (!read_at(f, 0, magic, 4)) {
        CloseHandle(f);
        return 0;
    }
    if (!(memcmp(magic, "LIVE", 4) == 0 || memcmp(magic, "PIRS", 4) == 0 ||
          memcmp(magic, "CON ", 4) == 0)) {
        CloseHandle(f);
        return 0;
    }
    if (!read_at(f, 0x360, tid, 4)) {
        CloseHandle(f);
        return 0;
    }
    CloseHandle(f);
    return tid[0] == 0x58 && tid[1] == 0x41 && tid[2] == 0x09 && tid[3] == 0xD4;
}

int nba_stfs_extract(const WCHAR *package, const WCHAR *dest_dir,
                      void (*progress)(double frac, const char *text))
{
    HANDLE f;
    LARGE_INTEGER sz;
    unsigned char *raw = NULL;
    unsigned start, offset, max_ft_blocks, firstclust;
    unsigned char *ft;
    unsigned i, files_done = 0, files_total = 0;
    WCHAR (*parent_paths)[MAX_PATH] = NULL;
    unsigned char *parent_valid = NULL;
    int r = 3;

    if (!nba_stfs_is_title_package(package)) {
        printf("ERROR That STFS package is not NBA Unrivaled (title 584109D4).\n");
        return 3;
    }

    f = CreateFileW(package, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return 6;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0xD000 || sz.QuadPart > 512ULL << 20) {
        CloseHandle(f);
        return 3;
    }
    raw = (unsigned char *)malloc((size_t)sz.QuadPart);
    if (!raw) {
        CloseHandle(f);
        return 6;
    }
    {
        size_t off = 0;
        while (off < (size_t)sz.QuadPart) {
            DWORD chunk = 0, want = (DWORD)(((size_t)sz.QuadPart - off) > 1u << 20
                                               ? 1u << 20
                                               : ((size_t)sz.QuadPart - off));
            if (!ReadFile(f, raw + off, want, &chunk, NULL) || !chunk) {
                free(raw);
                CloseHandle(f);
                return 6;
            }
            off += chunk;
            if (progress && (off & ((8u << 20) - 1)) == 0)
                progress(0.05 + 0.05 * (double)off / (double)sz.QuadPart, "Reading STFS package");
        }
    }
    CloseHandle(f);

    if (raw[0xC032] == 0xFF && raw[0xC033] == 0xFF)
        start = 0xC000;
    else
        start = 0xD000;
    offset = (start == 0xC000) ? 0x1000u : 0x2000u;
    firstclust = (unsigned)raw[start + 0x2F] | ((unsigned)raw[start + 0x30] << 8);
    max_ft_blocks = firstclust > 16 ? firstclust : 16;
    if ((unsigned long long)start + (unsigned long long)max_ft_blocks * STFS_BLOCK >
        (unsigned long long)sz.QuadPart) {
        free(raw);
        return 3;
    }
    ft = raw + start;

    parent_paths = (WCHAR (*)[MAX_PATH])calloc(STFS_MAX_ENTRIES, MAX_PATH * sizeof(WCHAR));
    parent_valid = (unsigned char *)calloc(STFS_MAX_ENTRIES, 1);
    if (!parent_paths || !parent_valid) {
        free(parent_paths);
        free(parent_valid);
        free(raw);
        return 6;
    }

    SHCreateDirectoryExW(NULL, dest_dir, NULL);

    for (i = 0; i < (max_ft_blocks * STFS_BLOCK) / 64; i++) {
        unsigned char *cur = ft + i * 64;
        unsigned name_len = cur[40] & 0x3F;
        int is_dir = (cur[40] & 0x80) != 0;
        if (name_len == 0) break;
        if (name_len < 1 || name_len > 40) continue;
        if (!is_dir) files_total++;
    }

    for (i = 0; i < (max_ft_blocks * STFS_BLOCK) / 64; i++) {
        unsigned char *cur = ft + i * 64;
        unsigned name_len = cur[40] & 0x3F;
        int is_dir = (cur[40] & 0x80) != 0;
        int is_contiguous = (cur[40] & 0x40) != 0;
        char name_a[41];
        WCHAR name_w[41], rel[MAX_PATH], out_path[MAX_PATH], parent[MAX_PATH];
        unsigned startclust, filelen, pathind, count, n;
        unsigned char *data;
        size_t wrote;

        if (name_len == 0) break;
        if (name_len < 1 || name_len > 40) continue;
        memcpy(name_a, cur, name_len);
        name_a[name_len] = 0;
        MultiByteToWideChar(CP_ACP, 0, name_a, -1, name_w, 41);

        startclust = (unsigned)cur[47] | ((unsigned)cur[48] << 8) | ((unsigned)cur[49] << 16);
        pathind = ((unsigned)cur[50] << 8) | (unsigned)cur[51];
        filelen = ((unsigned)cur[52] << 24) | ((unsigned)cur[53] << 16) |
                  ((unsigned)cur[54] << 8) | (unsigned)cur[55];

        if (pathind == 0xFFFF)
            parent[0] = 0;
        else if (pathind < STFS_MAX_ENTRIES && parent_valid[pathind])
            wcscpy_s(parent, MAX_PATH, parent_paths[pathind]);
        else
            parent[0] = 0;

        if (is_dir) {
            if (parent[0])
                swprintf_s(rel, MAX_PATH, L"%s\\%s", parent, name_w);
            else
                wcscpy_s(rel, MAX_PATH, name_w);
            if (i < STFS_MAX_ENTRIES) {
                wcscpy_s(parent_paths[i], MAX_PATH, rel);
                parent_valid[i] = 1;
            }
            swprintf_s(out_path, MAX_PATH, L"%s\\%s", dest_dir, rel);
            SHCreateDirectoryExW(NULL, out_path, NULL);
            continue;
        }

        if (!is_contiguous) {
            printf("ERROR STFS file is fragmented (not supported): %s\n", name_a);
            r = 5;
            goto done;
        }

        if (parent[0])
            swprintf_s(rel, MAX_PATH, L"%s\\%s", parent, name_w);
        else
            wcscpy_s(rel, MAX_PATH, name_w);
        swprintf_s(out_path, MAX_PATH, L"%s\\%s", dest_dir, rel);

        count = (filelen + STFS_BLOCK - 1) / STFS_BLOCK;
        data = (unsigned char *)malloc(filelen ? filelen : 1);
        if (!data) {
            r = 6;
            goto done;
        }
        wrote = 0;
        for (n = 0; n < count; n++) {
            unsigned cl = startclust + n;
            unsigned long long off =
                (unsigned long long)start + (unsigned long long)cl * STFS_BLOCK +
                get_cluster(cl, offset);
            unsigned take = STFS_BLOCK;
            if (wrote + take > filelen) take = (unsigned)(filelen - wrote);
            if (off + take > (unsigned long long)sz.QuadPart) {
                free(data);
                printf("ERROR STFS read past end of package for %s\n", name_a);
                r = 5;
                goto done;
            }
            memcpy(data + wrote, raw + (size_t)off, take);
            wrote += take;
        }
        if (!write_all(out_path, data, filelen)) {
            free(data);
            printf("ERROR Could not write extracted file: %s\n", name_a);
            r = 6;
            goto done;
        }
        free(data);
        files_done++;
        if (progress && files_total)
            progress(0.10 + 0.55 * (double)files_done / (double)files_total,
                     "Extracting STFS package");
    }

    r = 0;
done:
    free(parent_paths);
    free(parent_valid);
    free(raw);
    return r;
}
