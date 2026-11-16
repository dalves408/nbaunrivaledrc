/* Shared F1 / config.exe video settings for NBA Unrivaled (ReXGlue / Xenia GPU).
 *
 * ONE definition used by BOTH the in-game F1 overlay (host_overlay.cpp) and
 * config.exe (config/config.c) so the two always show exactly the same list.
 * C-compatible on purpose (config.c is plain C).
 *
 * Only the 7 options below are shown to the player. Everything else is an
 * emulator-accuracy lever that is locked to a good default (see
 * kHostHiddenKeys / host_settings.cpp) and can still be overridden by hand in
 * %LOCALAPPDATA%\NBAUnrivaled\settings.ini [display] (raw cvar name = value).
 *
 * Some entries are "virtual" ini keys that expand into several cvars:
 *   display_mode -> fullscreen + fullscreen_exclusive
 *   frame_rate   -> nba_frame_limit + video_mode_refresh_rate
 *   anti_aliasing-> swap_post_effect (FXAA) + native_2x_msaa
 *   resolution_scale also switches the two resolution-scale fixes on above 1x.
 */
#pragma once

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef struct HostVideoOpt {
  const char *label;
  const char *cvar; /* settings.ini [display] key (may be a virtual key) */
  int count;
  const char *const *texts;
  const char *const *values;
  int def; /* default choice index */
} HostVideoOpt;

static const char *const kHostOffOnT[] = {"Off", "On"};
static const char *const kHostOffOnV[] = {"false", "true"};

/* Single dropdown covering windowed + fullscreen (+ exclusive). */
static const char *const kHostDisplayModeT[] = {
    "Windowed", "Borderless fullscreen", "Exclusive fullscreen"};
static const char *const kHostDisplayModeV[] = {"windowed", "borderless", "exclusive"};

static const char *const kHostFrameRateT[] = {"30", "60", "Unlimited"};
static const char *const kHostFrameRateV[] = {"30", "60", "unlimited"};

/* Player-facing labels only; no multiplier numbers. */
static const char *const kHostScaleT[] = {"Low", "Medium", "High"};
static const char *const kHostScaleV[] = {"1", "2", "3"};

static const char *const kHostAaT[] = {"Off", "FXAA", "FXAA Extreme", "2x MSAA"};
static const char *const kHostAaV[] = {"off", "fxaa", "fxaa_extreme", "msaa2x"};

static const char *const kHostAnisoT[] = {"Off", "2x", "4x", "8x", "16x"};
static const char *const kHostAnisoV[] = {"0", "2", "3", "4", "5"};

#define HOST_DISPLAY_MODE_CVAR "display_mode"
#define HOST_FRAME_RATE_KEY "frame_rate"
#define HOST_ANTI_ALIASING_KEY "anti_aliasing"
#define HOST_RESOLUTION_SCALE_KEY "resolution_scale"

#define HOST_VIDEO_OPTS_COUNT 7

static const HostVideoOpt kHostVideoOpts[HOST_VIDEO_OPTS_COUNT] = {
    {"Display mode", HOST_DISPLAY_MODE_CVAR, 3, kHostDisplayModeT, kHostDisplayModeV, 1},
    {"Frame rate", HOST_FRAME_RATE_KEY, 3, kHostFrameRateT, kHostFrameRateV, 1},
    {"VSync", "vsync", 2, kHostOffOnT, kHostOffOnV, 0},
    {"Internal resolution", HOST_RESOLUTION_SCALE_KEY, 3, kHostScaleT, kHostScaleV, 0},
    {"Anti-aliasing", HOST_ANTI_ALIASING_KEY, 4, kHostAaT, kHostAaV, 3},
    {"Texture filtering", "anisotropic_override", 5, kHostAnisoT, kHostAnisoV, 2},
    {"Keep aspect ratio", "present_letterbox", 2, kHostOffOnT, kHostOffOnV, 1},
};

/* Hidden, locked settings. Not shown anywhere in the UI. Locked defaults
 * (value non-NULL) are applied at startup unless the command line or a
 * hand-written settings.ini entry says otherwise. NULL value = leave at the
 * emulator's own default. The two *_scale_* fixes are switched on
 * automatically whenever Internal resolution is above Low (see
 * host_settings.cpp) unless set by hand. */
typedef struct HostHiddenOpt {
  const char *cvar;
  const char *locked; /* NULL = emulator default */
} HostHiddenOpt;

#define HOST_HIDDEN_COUNT 14
static const HostHiddenOpt kHostHiddenOpts[HOST_HIDDEN_COUNT] = {
    {"render_target_path_d3d12", NULL},
    {"gamma_render_target_as_unorm16", "true"},
    {"snorm16_render_target_full_range", NULL},
    {"use_fuzzy_alpha_epsilon", "true"},
    {"readback_resolve", NULL},
    {"direct_host_resolve", NULL},
    {"non_seamless_cube_map", NULL},
    {"draw_resolution_scaled_texture_offsets", NULL}, /* Scale texture coords */
    {"resolve_resolution_scale_fill_half_pixel_offset", NULL}, /* Scale fill fix */
    {"present_allow_overscan_cutoff", NULL},
    {"present_dither", NULL},
    {"swap_post_effect", NULL}, /* driven by Anti-aliasing */
    {"half_pixel_offset", NULL}, /* "Sharper 2D": accuracy lever, not an upscale option */
    {"native_2x_msaa", NULL},    /* driven by Anti-aliasing */
};

/* Keys written by older builds that are now derived/hidden. Removed from
 * settings.ini once (schema marker below) so stale old defaults cannot
 * override the new locked ones. */
#define HOST_LEGACY_KEYS_COUNT 4
static const char *const kHostLegacyKeys[HOST_LEGACY_KEYS_COUNT] = {
    "fullscreen", "fullscreen_exclusive", "video_mode_refresh_rate", "nba_frame_limit"};

/* Keys from older builds that are no longer shown and must not be applied,
 * even as a hand-written raw override. An existing settings.ini entry is
 * left in place and ignored, so the SDK's own default (off / bilinear)
 * stays in effect. The prebuilt SDK has no FidelityFX. */
#define HOST_IGNORED_KEYS_COUNT 1
static const char *const kHostIgnoredKeys[HOST_IGNORED_KEYS_COUNT] = {
    "present_effect", /* former "Upscale & sharpen" option */
};

#define HOST_SETTINGS_DIR_NAME "NBAUnrivaled"
#define HOST_SETTINGS_FILE_NAME "settings.ini"
#define HOST_SETTINGS_SECTION "display"
#define HOST_SETTINGS_SCHEMA_KEY "schema"
#define HOST_SETTINGS_SCHEMA_VALUE "2"

/* F1 action rows after the option list. */
enum { kHostVideoClose = -2, kHostVideoQuit = -1 };

static int HostDisplayModeIs(const char *cvar) {
  return cvar && strcmp(cvar, HOST_DISPLAY_MODE_CVAR) == 0;
}

/* Map display_mode value -> fullscreen / fullscreen_exclusive. */
static void HostDisplayModeExpand(const char *mode, const char **fullscreen,
                                  const char **exclusive) {
  if (mode && !_stricmp(mode, "exclusive")) {
    *fullscreen = "true";
    *exclusive = "true";
  } else if (mode && !_stricmp(mode, "borderless")) {
    *fullscreen = "true";
    *exclusive = "false";
  } else {
    *fullscreen = "false";
    *exclusive = "false";
  }
}

/* Derive display_mode from the two SDK flags (true/false strings). */
static const char *HostDisplayModeFromFlags(const char *fullscreen,
                                            const char *exclusive) {
  const int fs = fullscreen && (!_stricmp(fullscreen, "true") ||
                                !_stricmp(fullscreen, "1") ||
                                !_stricmp(fullscreen, "on"));
  const int ex = exclusive && (!_stricmp(exclusive, "true") ||
                               !_stricmp(exclusive, "1") ||
                               !_stricmp(exclusive, "on"));
  if (ex) return "exclusive";
  if (fs) return "borderless";
  return "windowed";
}

/* frame_rate value -> nba_frame_limit (0 = off) and guest refresh rate. */
static void HostFrameRateExpand(const char *rate, const char **limit,
                                const char **refresh) {
  if (rate && !_stricmp(rate, "30")) {
    *limit = "30";
    *refresh = "30";
  } else if (rate && !_stricmp(rate, "unlimited")) {
    *limit = "0";
    *refresh = "120";
  } else {
    *limit = "60";
    *refresh = "60";
  }
}

/* anti_aliasing value -> swap_post_effect + native_2x_msaa. */
static void HostAntiAliasExpand(const char *aa, const char **swap_fx,
                                const char **msaa) {
  *swap_fx = "none";
  *msaa = "false";
  if (aa && !_stricmp(aa, "fxaa")) {
    *swap_fx = "fxaa";
  } else if (aa && !_stricmp(aa, "fxaa_extreme")) {
    *swap_fx = "fxaa_extreme";
  } else if (aa && !_stricmp(aa, "msaa2x")) {
    *msaa = "true";
  }
}

/* Pick the choice matching a stored value (exact, then nearest number). */
static int HostVideoFindChoice(const HostVideoOpt *opt, const char *cur) {
  int i, best = opt->def;
  long want, v, d, best_d = 1L << 30;
  char *end = NULL;
  if (!cur) return opt->def;
  for (i = 0; i < opt->count; i++)
    if (!_stricmp(opt->values[i], cur)) return i;
  want = strtol(cur, &end, 10);
  if (end && end != cur && !*end) {
    for (i = 0; i < opt->count; i++) {
      v = strtol(opt->values[i], NULL, 10);
      d = labs(v - want);
      if (d < best_d) {
        best_d = d;
        best = i;
      }
    }
    return best;
  }
  return opt->def;
}

static int HostIsHiddenKey(const char *key) {
  int i;
  for (i = 0; i < HOST_HIDDEN_COUNT; i++)
    if (!_stricmp(kHostHiddenOpts[i].cvar, key)) return 1;
  return 0;
}

static int HostIsLegacyKey(const char *key) {
  int i;
  for (i = 0; i < HOST_LEGACY_KEYS_COUNT; i++)
    if (!_stricmp(kHostLegacyKeys[i], key)) return 1;
  return HostIsHiddenKey(key);
}

static int HostIsIgnoredKey(const char *key) {
  int i;
  if (!key) return 0;
  for (i = 0; i < HOST_IGNORED_KEYS_COUNT; i++)
    if (!_stricmp(kHostIgnoredKeys[i], key)) return 1;
  return 0;
}

#ifdef _WIN32
/* Carry forward the old fullscreen flags without dropping saved settings. */
static void HostIniMigrate(const char *path) {
  char mode[16], fullscreen[16], exclusive[16];
  if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
  GetPrivateProfileStringA(HOST_SETTINGS_SECTION, HOST_DISPLAY_MODE_CVAR, "", mode,
                           sizeof mode, path);
  if (mode[0]) return;
  GetPrivateProfileStringA(HOST_SETTINGS_SECTION, "fullscreen", "", fullscreen,
                           sizeof fullscreen, path);
  if (!fullscreen[0]) return;
  GetPrivateProfileStringA(HOST_SETTINGS_SECTION, "fullscreen_exclusive", "", exclusive,
                           sizeof exclusive, path);
  WritePrivateProfileStringA(HOST_SETTINGS_SECTION, HOST_DISPLAY_MODE_CVAR,
                             HostDisplayModeFromFlags(fullscreen, exclusive), path);
}
#endif
