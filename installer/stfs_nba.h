/* Contiguous STFS (LIVE/PIRS/CON) extractor for NBA Unrivaled title 584109D4.
 * Matches scripts/prepare_game.py + tools/XBLA-Extract/stfs_extract.py. */
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Title ID bytes at package offset 0x360 (big-endian). */
#define NBA_TITLE_ID_BE 0x584109D4u

/* Returns 1 if path is a LIVE/PIRS/CON package for this title. */
int nba_stfs_is_title_package(const WCHAR *path);

/* Extract all contiguous file entries into dest_dir. Returns 0 on success,
 * 3 bad/unsupported package, 5 fragmented/incomplete, 6 I/O. */
int nba_stfs_extract(const WCHAR *package, const WCHAR *dest_dir,
                     void (*progress)(double frac, const char *text));

#ifdef __cplusplus
}
#endif
