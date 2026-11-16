/* Shared keyboard remaps for F1 "Redefine Keys" and config.exe Controls tab.
 *
 * Values are ReXGlue keybind_* cvar strings (comma = alternate keys), matching
 * Launch-Game.ps1 Keyboard defaults. Stored in
 * %LOCALAPPDATA%\NBAUnrivaled\settings.ini under [keybinds]. */
#pragma once

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef struct HostKeybind {
  const char *cvar;  /* e.g. keybind_a */
  const char *label; /* player-facing action name */
  const char *def;   /* default "Primary,Alt,..." string */
} HostKeybind;

#define HOST_KEYBINDS_SECTION "keybinds"
#define HOST_KEYBIND_COUNT 24
/* settings.ini value for an input the player deliberately cleared (an empty
 * value means "not set" = use the default). */
#define HOST_KEYBIND_NONE "(none)"

static const HostKeybind kHostKeybinds[HOST_KEYBIND_COUNT] = {
    {"keybind_a", "A (confirm)", "Space,Semicolon"},
    {"keybind_b", "B (back / cancel)", "Backspace,Quote"},
    {"keybind_x", "X", "L"},
    {"keybind_y", "Y", "P"},
    {"keybind_start", "Start", "2,X,Return"},
    {"keybind_back", "Back", "Z,Tab"},
    {"keybind_left_trigger", "Left trigger", "Q,I"},
    {"keybind_right_trigger", "Right trigger", "E,O"},
    {"keybind_left_shoulder", "Left shoulder", "1"},
    {"keybind_right_shoulder", "Right shoulder", "3"},
    {"keybind_lstick_up", "Left stick up", "W,Up"},
    {"keybind_lstick_down", "Left stick down", "S,Down"},
    {"keybind_lstick_left", "Left stick left", "A,Left"},
    {"keybind_lstick_right", "Left stick right", "D,Right"},
    {"keybind_lstick_press", "Left stick click", "F"},
    {"keybind_rstick_press", "Right stick click", "K"},
    {"keybind_dpad_up", "D-pad up", "Shift+Up"},
    {"keybind_dpad_down", "D-pad down", "Shift+Down"},
    {"keybind_dpad_left", "D-pad left", "Shift+Left"},
    {"keybind_dpad_right", "D-pad right", "Shift+Right"},
    {"keybind_rstick_up", "Right stick up", "Numpad8"},
    {"keybind_rstick_down", "Right stick down", "Numpad2"},
    {"keybind_rstick_left", "Right stick left", "Numpad4"},
    {"keybind_rstick_right", "Right stick right", "Numpad6"},
};

/* Stored ini value -> cvar value ("(none)" -> empty). */
static const char *HostKeybindFromIni(const char *v) {
  return (v && !strcmp(v, HOST_KEYBIND_NONE)) ? "" : v;
}

/* Split "Primary,Alt,..." into key + alternate (alternate may still contain commas). */
static void HostKeybindSplit(const char *value, char *key, size_t kn, char *alt,
                             size_t an) {
  const char *comma;
  size_t n;
  if (!value) value = "";
  comma = strchr(value, ',');
  if (!comma) {
    snprintf(key, kn, "%s", value);
    if (an) alt[0] = 0;
    return;
  }
  n = (size_t)(comma - value);
  if (n >= kn) n = kn ? kn - 1 : 0;
  memcpy(key, value, n);
  key[n] = 0;
  snprintf(alt, an, "%s", comma + 1);
}

/* Join key + alt into a cvar value. Empty slots omitted. */
static void HostKeybindJoin(const char *key, const char *alt, char *out,
                            size_t n) {
  if (!key) key = "";
  if (!alt) alt = "";
  if (!key[0] && !alt[0]) {
    if (n) out[0] = 0;
  } else if (!alt[0]) {
    snprintf(out, n, "%s", key);
  } else if (!key[0]) {
    snprintf(out, n, "%s", alt);
  } else {
    snprintf(out, n, "%s,%s", key, alt);
  }
}

/* Map a Win32 VK (+ optional Shift) to a ReXGlue keybind name used by MNK. */
static int HostVkToRexName(int vk, int shift_held, char *out, size_t n) {
  if (!vk) {
    if (n) out[0] = 0;
    return 0;
  }
  if (shift_held) {
    switch (vk) {
    case VK_UP: snprintf(out, n, "Shift+Up"); return 1;
    case VK_DOWN: snprintf(out, n, "Shift+Down"); return 1;
    case VK_LEFT: snprintf(out, n, "Shift+Left"); return 1;
    case VK_RIGHT: snprintf(out, n, "Shift+Right"); return 1;
    }
  }
  switch (vk) {
  case VK_SPACE: snprintf(out, n, "Space"); return 1;
  case VK_BACK: snprintf(out, n, "Backspace"); return 1;
  case VK_TAB: snprintf(out, n, "Tab"); return 1;
  case VK_RETURN: snprintf(out, n, "Return"); return 1;
  case VK_ESCAPE: snprintf(out, n, "Escape"); return 1;
  case VK_UP: snprintf(out, n, "Up"); return 1;
  case VK_DOWN: snprintf(out, n, "Down"); return 1;
  case VK_LEFT: snprintf(out, n, "Left"); return 1;
  case VK_RIGHT: snprintf(out, n, "Right"); return 1;
  case VK_PRIOR: snprintf(out, n, "PageUp"); return 1;
  case VK_NEXT: snprintf(out, n, "PageDown"); return 1;
  case VK_HOME: snprintf(out, n, "Home"); return 1;
  case VK_END: snprintf(out, n, "End"); return 1;
  case VK_INSERT: snprintf(out, n, "Insert"); return 1;
  case VK_DELETE: snprintf(out, n, "Delete"); return 1;
  case VK_OEM_1: snprintf(out, n, "Semicolon"); return 1;
  case VK_OEM_7: snprintf(out, n, "Quote"); return 1;
  case VK_OEM_COMMA: snprintf(out, n, "Comma"); return 1;
  case VK_OEM_PERIOD: snprintf(out, n, "Period"); return 1;
  case VK_OEM_2: snprintf(out, n, "Slash"); return 1;
  case VK_OEM_5: snprintf(out, n, "Backslash"); return 1;
  case VK_OEM_4: snprintf(out, n, "LeftBracket"); return 1;
  case VK_OEM_6: snprintf(out, n, "RightBracket"); return 1;
  case VK_OEM_MINUS: snprintf(out, n, "Minus"); return 1;
  case VK_OEM_PLUS: snprintf(out, n, "Equals"); return 1;
  case VK_OEM_3: snprintf(out, n, "Backtick"); return 1;
  case VK_NUMPAD0: snprintf(out, n, "Numpad0"); return 1;
  case VK_NUMPAD1: snprintf(out, n, "Numpad1"); return 1;
  case VK_NUMPAD2: snprintf(out, n, "Numpad2"); return 1;
  case VK_NUMPAD3: snprintf(out, n, "Numpad3"); return 1;
  case VK_NUMPAD4: snprintf(out, n, "Numpad4"); return 1;
  case VK_NUMPAD5: snprintf(out, n, "Numpad5"); return 1;
  case VK_NUMPAD6: snprintf(out, n, "Numpad6"); return 1;
  case VK_NUMPAD7: snprintf(out, n, "Numpad7"); return 1;
  case VK_NUMPAD8: snprintf(out, n, "Numpad8"); return 1;
  case VK_NUMPAD9: snprintf(out, n, "Numpad9"); return 1;
  case VK_MULTIPLY: snprintf(out, n, "NumpadMultiply"); return 1;
  case VK_ADD: snprintf(out, n, "NumpadPlus"); return 1;
  case VK_SUBTRACT: snprintf(out, n, "NumpadMinus"); return 1;
  case VK_DECIMAL: snprintf(out, n, "NumpadDecimal"); return 1;
  case VK_DIVIDE: snprintf(out, n, "NumpadDivide"); return 1;
  case VK_F1: case VK_F2: case VK_F3: case VK_F4:
  case VK_F5: case VK_F6: case VK_F7: case VK_F8:
  case VK_F9: case VK_F10: case VK_F11: case VK_F12:
    snprintf(out, n, "F%d", vk - VK_F1 + 1);
    return 1;
  case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    snprintf(out, n, "Shift");
    return 1;
  case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    snprintf(out, n, "Ctrl");
    return 1;
  case VK_MENU: case VK_LMENU: case VK_RMENU:
    snprintf(out, n, "Alt");
    return 1;
  default:
    if (vk >= '0' && vk <= '9') {
      snprintf(out, n, "%c", (char)vk);
      return 1;
    }
    if (vk >= 'A' && vk <= 'Z') {
      snprintf(out, n, "%c", (char)vk);
      return 1;
    }
    snprintf(out, n, "Key%d", vk);
    return 1;
  }
}

/* Keys reserved by the host overlay / SDK and not assignable. */
static int HostKeybindReservedVk(int vk) {
  return vk == VK_ESCAPE || vk == VK_F1 || vk == VK_F4 || vk == VK_F11 ||
         vk == VK_LWIN || vk == VK_RWIN || vk == VK_MENU || vk == VK_LMENU ||
         vk == VK_RMENU;
}

/* Pretty label for a ReXGlue key name (empty → "(none)"). */
static void HostKeybindDisplayName(const char *rex, char *out, size_t n) {
  if (!rex || !rex[0]) {
    snprintf(out, n, "(none)");
    return;
  }
  snprintf(out, n, "%s", rex);
}
