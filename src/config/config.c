/* config.exe -- player settings for NBA Unrivaled (ReXGlue).
 *
 * Video tab mirrors the in-game F1 host overlay exactly (same shared option list,
 * host_video_options.h), saved to %LOCALAPPDATA%\NBAUnrivaled\settings.ini [display].
 * Controls tab: keyboard remapper for keybind_* cvars, saved under [keybinds].
 * nbaunrivaled.exe itself, Launch-Game.ps1 and the F1 overlay all read this file.
 * UX follows NHL Hitz Pro. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../host_video_options.h"
#include "../host_keybinds.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum {
  ID_TAB = 100, ID_LIST, ID_CHANGE, ID_ALT, ID_CLEAR,
  ID_RESET = 110, ID_SAVE, ID_CLOSE, ID_STATUS,
  ID_VIDEO0 = 200, ID_SCROLL = 400
};
enum { PAGE_VIDEO, PAGE_CONTROLS, PAGE_ABOUT };

static int s_vsel[HOST_VIDEO_OPTS_COUNT];
static char s_kprim[HOST_KEYBIND_COUNT][64];
static char s_kalt[HOST_KEYBIND_COUNT][96];
static int s_dirty;
static int s_wait_row = -1, s_wait_slot;
static int s_page = PAGE_VIDEO;
static HWND s_main, s_tab, s_status, s_scroll, s_list;
static HWND s_vcombo[HOST_VIDEO_OPTS_COUNT];
static HWND s_vpage[96], s_cpage[16], s_apage[16];
static int s_nv, s_nc, s_na;
static int s_content_h;

static void ini_path(char *out, size_t n, int create_dir)
{
    char base[MAX_PATH];
    if (!GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base))
        strcpy(base, ".");
    snprintf(out, n, "%s\\%s", base, HOST_SETTINGS_DIR_NAME);
    if (create_dir) CreateDirectoryA(out, NULL);
    snprintf(out, n, "%s\\%s\\%s", base, HOST_SETTINGS_DIR_NAME, HOST_SETTINGS_FILE_NAME);
}

static void load_video(void)
{
    char path[MAX_PATH], buf[256];
    int i;
    ini_path(path, sizeof path, 0);
    HostIniMigrate(path); /* drops hidden/legacy keys from older settings.ini */
    for (i = 0; i < HOST_VIDEO_OPTS_COUNT; i++) {
        GetPrivateProfileStringA(HOST_SETTINGS_SECTION, kHostVideoOpts[i].cvar,
                                 kHostVideoOpts[i].values[kHostVideoOpts[i].def],
                                 buf, sizeof buf, path);
        s_vsel[i] = HostVideoFindChoice(&kHostVideoOpts[i], buf);
    }
}

static void load_keys(void)
{
    char path[MAX_PATH], buf[192];
    int i;
    ini_path(path, sizeof path, 0);
    for (i = 0; i < HOST_KEYBIND_COUNT; i++) {
        GetPrivateProfileStringA(HOST_KEYBINDS_SECTION, kHostKeybinds[i].cvar,
                                 kHostKeybinds[i].def, buf, sizeof buf, path);
        HostKeybindSplit(HostKeybindFromIni(buf), s_kprim[i], sizeof s_kprim[i], s_kalt[i], sizeof s_kalt[i]);
    }
}

static int save_video(void)
{
    char path[MAX_PATH];
    int i, ok = 1;
    ini_path(path, sizeof path, 1);
    HostIniMigrate(path);
    ok &= WritePrivateProfileStringA(HOST_SETTINGS_SECTION, HOST_SETTINGS_SCHEMA_KEY,
                                     HOST_SETTINGS_SCHEMA_VALUE, path);
    for (i = 0; i < HOST_VIDEO_OPTS_COUNT; i++) {
        const char *value = kHostVideoOpts[i].values[s_vsel[i]];
        ok &= WritePrivateProfileStringA(HOST_SETTINGS_SECTION, kHostVideoOpts[i].cvar,
                                         value, path);
    }
    return ok;
}
static int save_keys(void)
{
    char path[MAX_PATH], joined[192];
    int i, ok = 1;
    ini_path(path, sizeof path, 1);
    for (i = 0; i < HOST_KEYBIND_COUNT; i++) {
        HostKeybindJoin(s_kprim[i], s_kalt[i], joined, sizeof joined);
        ok &= WritePrivateProfileStringA(HOST_KEYBINDS_SECTION, kHostKeybinds[i].cvar,
                                         joined[0] ? joined : HOST_KEYBIND_NONE, path);
    }
    return ok;
}

static void status(const char *s) { SetWindowTextA(s_status, s); }

static void refresh_row(int i)
{
    char t[96];
    HostKeybindDisplayName(s_kprim[i], t, sizeof t);
    ListView_SetItemText(s_list, i, 1, t);
    HostKeybindDisplayName(s_kalt[i], t, sizeof t);
    ListView_SetItemText(s_list, i, 2, t);
}

static void refresh_all(void)
{
    int i;
    for (i = 0; i < HOST_KEYBIND_COUNT; i++) refresh_row(i);
}

static void begin_wait(int slot)
{
    char m[200];
    int row = ListView_GetNextItem(s_list, -1, LVNI_SELECTED);
    if (row < 0) { status("Select an action in the list first."); return; }
    s_wait_row = row;
    s_wait_slot = slot;
    snprintf(m, sizeof m, "Press the new %s for \"%s\"  (Esc cancels).",
             slot ? "alternate key" : "key", kHostKeybinds[row].label);
    status(m);
    SetFocus(s_main);
}

static void clear_slot(void)
{
    int row = ListView_GetNextItem(s_list, -1, LVNI_SELECTED);
    if (row < 0) { status("Select an action in the list first."); return; }
    s_kprim[row][0] = 0;
    s_kalt[row][0] = 0;
    refresh_row(row);
    s_dirty = 1;
    status("Cleared. Click Save to keep the change.");
}

static void assign_key(int vk)
{
    char m[220], name[64];
    int i, moved = 0, shift;
    if (vk == VK_ESCAPE) { s_wait_row = -1; status("Cancelled."); return; }
    if (HostKeybindReservedVk(vk)) {
        HostVkToRexName(vk, 0, name, sizeof name);
        snprintf(m, sizeof m,
                 "%s is used by the host (F1 Settings, Esc quit, F4 SDK, Alt+Enter) and can't be assigned.",
                 name);
        status(m);
        return;
    }
    shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (!HostVkToRexName(vk, shift, name, sizeof name) || !name[0]) {
        status("That key can't be mapped.");
        return;
    }
    /* One key drives one action: clear duplicates. */
    for (i = 0; i < HOST_KEYBIND_COUNT; i++) {
        if (i == s_wait_row) continue;
        if (!_stricmp(s_kprim[i], name)) {
            s_kprim[i][0] = 0;
            refresh_row(i);
            moved = 1;
        }
        if (!_stricmp(s_kalt[i], name)) {
            s_kalt[i][0] = 0;
            refresh_row(i);
            moved = 1;
        }
    }
    if (s_wait_slot == 0)
        snprintf(s_kprim[s_wait_row], sizeof s_kprim[s_wait_row], "%s", name);
    else
        snprintf(s_kalt[s_wait_row], sizeof s_kalt[s_wait_row], "%s", name);
    refresh_row(s_wait_row);
    if (moved)
        snprintf(m, sizeof m, "%s was moved here.", name);
    else
        snprintf(m, sizeof m, "\"%s\" is now %s.", kHostKeybinds[s_wait_row].label, name);
    status(m);
    s_wait_row = -1;
    s_dirty = 1;
}

static void show_page(int page)
{
    int i;
    s_page = page;
    s_wait_row = -1;
    for (i = 0; i < s_nv; i++) ShowWindow(s_vpage[i], page == PAGE_VIDEO ? SW_SHOW : SW_HIDE);
    for (i = 0; i < s_nc; i++) ShowWindow(s_cpage[i], page == PAGE_CONTROLS ? SW_SHOW : SW_HIDE);
    for (i = 0; i < s_na; i++) ShowWindow(s_apage[i], page == PAGE_ABOUT ? SW_SHOW : SW_HIDE);
    ShowWindow(s_scroll, page == PAGE_VIDEO ? SW_SHOW : SW_HIDE);
    if (page == PAGE_VIDEO)
        status("Video: the same options as the in-game F1 menu. Click Save; they apply the next time the game starts.");
    else if (page == PAGE_CONTROLS)
        status("Controls: select an action, click Change key (or double-click), then press the key you want.");
    else
        status("About: NBA Unrivaled settings.");
}

static void do_reset(void)
{
    int i;
    if (s_page == PAGE_CONTROLS) {
        for (i = 0; i < HOST_KEYBIND_COUNT; i++)
            HostKeybindSplit(kHostKeybinds[i].def, s_kprim[i], sizeof s_kprim[i],
                             s_kalt[i], sizeof s_kalt[i]);
        refresh_all();
        s_wait_row = -1;
        status("Default keys restored. Click Save to keep them.");
    } else if (s_page == PAGE_VIDEO) {
        for (i = 0; i < HOST_VIDEO_OPTS_COUNT; i++) {
            s_vsel[i] = kHostVideoOpts[i].def;
            SendMessageA(s_vcombo[i], CB_SETCURSEL, (WPARAM)s_vsel[i], 0);
        }
        status("Default video settings restored. Click Save to keep them.");
    } else {
        status("Nothing to reset on this tab.");
        return;
    }
    s_dirty = 1;
}

static int do_save(void)
{
    if (!save_video() || !save_keys()) {
        MessageBoxA(s_main, "The settings could not be saved.", "NBA Unrivaled", MB_ICONERROR);
        return 0;
    }
    s_dirty = 0;
    status("Saved. The game reads %LOCALAPPDATA%\\NBAUnrivaled\\settings.ini when it starts.");
    return 1;
}

static HWND mk(HWND parent, const char *cls, const char *text, DWORD style,
               int x, int y, int w, int h, int id, HFONT f)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | style, x, y, w, h, parent,
                             (HMENU)(INT_PTR)id, NULL, NULL);
    if (f) SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

static void layout_scroll(void)
{
    RECT rc;
    SCROLLINFO si;
    GetClientRect(s_scroll, &rc);
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = s_content_h;
    si.nPage = (UINT)(rc.bottom - rc.top);
    si.nPos = 0;
    SetScrollInfo(s_scroll, SB_VERT, &si, TRUE);
}

static void make_ui(HWND h)
{
    TCITEMA ti;
    LVCOLUMNA c;
    LVITEMA it;
    int i, j;
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    struct { int id; const char *t; int x; int w; } kb[] = {
        { ID_CHANGE, "Change key", 20, 100 },
        { ID_ALT, "Change alternate", 126, 120 },
        { ID_CLEAR, "Clear", 252, 70 } };
    struct { int id; const char *t; int x; int w; } bb[] = {
        { ID_RESET, "Reset to defaults", 10, 130 },
        { ID_SAVE, "Save", 444, 70 },
        { ID_CLOSE, "Close", 520, 70 } };

    s_tab = CreateWindowExA(0, WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                            10, 10, 580, 500, h, (HMENU)(INT_PTR)ID_TAB, NULL, NULL);
    SendMessageA(s_tab, WM_SETFONT, (WPARAM)f, TRUE);
    memset(&ti, 0, sizeof ti);
    ti.mask = TCIF_TEXT;
    ti.pszText = "Video"; SendMessageA(s_tab, TCM_INSERTITEMA, 0, (LPARAM)&ti);
    ti.pszText = "Controls"; SendMessageA(s_tab, TCM_INSERTITEMA, 1, (LPARAM)&ti);
    ti.pszText = "About"; SendMessageA(s_tab, TCM_INSERTITEMA, 2, (LPARAM)&ti);

    s_scroll = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC", "",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                               20, 48, 560, 420, h, (HMENU)(INT_PTR)ID_SCROLL, NULL, NULL);
    s_vpage[s_nv++] = s_scroll;

    s_vpage[s_nv++] = mk(s_scroll, "STATIC",
                         "These options match the in-game F1 menu. Everything else is set automatically.",
                         SS_LEFT, 8, 8, 520, 36, 0, f);

    for (i = 0; i < HOST_VIDEO_OPTS_COUNT; i++) {
        int y = 52 + i * 36;
        HWND lab = mk(s_scroll, "STATIC", kHostVideoOpts[i].label, SS_LEFT | SS_NOPREFIX, 12, y + 4, 220, 22, 0, f);
        s_vcombo[i] = mk(s_scroll, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                         240, y, 290, 220, ID_VIDEO0 + i, f);
        for (j = 0; j < kHostVideoOpts[i].count; j++)
            SendMessageA(s_vcombo[i], CB_ADDSTRING, 0, (LPARAM)kHostVideoOpts[i].texts[j]);
        SendMessageA(s_vcombo[i], CB_SETCURSEL, (WPARAM)s_vsel[i], 0);
        s_vpage[s_nv++] = lab;
        s_vpage[s_nv++] = s_vcombo[i];
    }
    s_content_h = 52 + HOST_VIDEO_OPTS_COUNT * 36 + 16;
    layout_scroll();

    s_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                             WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                             20, 48, 560, 400, h, (HMENU)(INT_PTR)ID_LIST, NULL, NULL);
    SendMessageA(s_list, WM_SETFONT, (WPARAM)f, TRUE);
    ListView_SetExtendedListViewStyle(s_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    memset(&c, 0, sizeof c);
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = "Action"; c.cx = 210; SendMessageA(s_list, LVM_INSERTCOLUMNA, 0, (LPARAM)&c);
    c.pszText = "Key"; c.cx = 165; SendMessageA(s_list, LVM_INSERTCOLUMNA, 1, (LPARAM)&c);
    c.pszText = "Alternate key"; c.cx = 165; SendMessageA(s_list, LVM_INSERTCOLUMNA, 2, (LPARAM)&c);
    for (i = 0; i < HOST_KEYBIND_COUNT; i++) {
        memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (char *)kHostKeybinds[i].label;
        SendMessageA(s_list, LVM_INSERTITEMA, 0, (LPARAM)&it);
    }
    refresh_all();
    ListView_SetItemState(s_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    s_cpage[s_nc++] = s_list;
    for (i = 0; i < 3; i++)
        s_cpage[s_nc++] = mk(h, "BUTTON", kb[i].t, WS_TABSTOP | BS_PUSHBUTTON,
                             kb[i].x, 458, kb[i].w, 28, kb[i].id, f);

    s_apage[s_na++] = mk(h, "STATIC",
                         "NBA Unrivaled settings (config.exe).\r\n\r\n"
                         "F1 opens the Settings menu in-game (video + Redefine Keys); "
                         "Esc opens the quit prompt (NHL Hitz Pro UX).\r\n\r\n"
                         "Controls writes keybind_* values to settings.ini [keybinds]. "
                         "The game applies them on the next start; F1 applies changes live.",
                         SS_LEFT, 30, 60, 540, 220, 0, f);

    s_status = mk(h, "STATIC", "", SS_LEFT | WS_VISIBLE, 10, 520, 580, 36, ID_STATUS, f);
    for (i = 0; i < 3; i++)
        mk(h, "BUTTON", bb[i].t, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
           bb[i].x, 560, bb[i].w, 28, bb[i].id, f);
    {
        char path[MAX_PATH], base[MAX_PATH];
        int start = PAGE_VIDEO;
        if (GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base)) {
            snprintf(path, sizeof path, "%s\\%s\\open-controls.flag", base,
                     HOST_SETTINGS_DIR_NAME);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
                DeleteFileA(path);
                start = PAGE_CONTROLS;
                SendMessageA(s_tab, TCM_SETCURSEL, (WPARAM)PAGE_CONTROLS, 0);
            }
        }
        show_page(start);
    }
}

static void scroll_by(int delta)
{
    SCROLLINFO si;
    int old, pos, max_pos;
    RECT rc;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_ALL;
    GetScrollInfo(s_scroll, SB_VERT, &si);
    old = si.nPos;
    GetClientRect(s_scroll, &rc);
    max_pos = (int)si.nMax - (int)si.nPage;
    if (max_pos < 0) max_pos = 0;
    pos = old + delta;
    if (pos < 0) pos = 0;
    if (pos > max_pos) pos = max_pos;
    if (pos == old) return;
    si.fMask = SIF_POS;
    si.nPos = pos;
    SetScrollInfo(s_scroll, SB_VERT, &si, TRUE);
    ScrollWindowEx(s_scroll, 0, old - pos, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        s_main = h;
        make_ui(h);
        return 0;
    case WM_VSCROLL:
        if ((HWND)l == s_scroll || GetDlgCtrlID((HWND)l) == ID_SCROLL) {
            int delta = 0;
            switch (LOWORD(w)) {
            case SB_LINEUP: delta = -24; break;
            case SB_LINEDOWN: delta = 24; break;
            case SB_PAGEUP: delta = -120; break;
            case SB_PAGEDOWN: delta = 120; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO si;
                memset(&si, 0, sizeof si);
                si.cbSize = sizeof si; si.fMask = SIF_TRACKPOS | SIF_POS;
                GetScrollInfo(s_scroll, SB_VERT, &si);
                delta = (int)si.nTrackPos - (int)si.nPos;
                break;
            }
            }
            if (delta) scroll_by(delta);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (s_page == PAGE_VIDEO) {
            scroll_by(-(GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA) * 36);
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (((LPNMHDR)l)->idFrom == ID_TAB && ((LPNMHDR)l)->code == TCN_SELCHANGE)
            show_page((int)SendMessageA(s_tab, TCM_GETCURSEL, 0, 0));
        if (((LPNMHDR)l)->idFrom == ID_LIST && ((LPNMHDR)l)->code == NM_DBLCLK)
            begin_wait(0);
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) >= ID_VIDEO0 && LOWORD(w) < ID_VIDEO0 + HOST_VIDEO_OPTS_COUNT) {
            if (HIWORD(w) == CBN_SELCHANGE) {
                int i = LOWORD(w) - ID_VIDEO0;
                s_vsel[i] = (int)SendMessageA(s_vcombo[i], CB_GETCURSEL, 0, 0);
                s_dirty = 1;
            }
            return 0;
        }
        switch (LOWORD(w)) {
        case ID_CHANGE: begin_wait(0); return 0;
        case ID_ALT: begin_wait(1); return 0;
        case ID_CLEAR: clear_slot(); return 0;
        case ID_RESET: do_reset(); return 0;
        case ID_SAVE: do_save(); return 0;
        case ID_CLOSE:
            if (s_dirty) {
                int r = MessageBoxA(h, "Save changes before closing?", "NBA Unrivaled",
                                    MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL) return 0;
                if (r == IDYES && !do_save()) return 0;
            }
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (s_wait_row >= 0) {
            if (l & (1 << 30)) return 0; /* ignore auto-repeat */
            assign_key((int)w);
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
        if (s_wait_row >= 0) {
            assign_key((int)w);
            return 0;
        }
        break;
    case WM_CLOSE:
        SendMessageA(h, WM_COMMAND, ID_CLOSE, 0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    HWND h;
    MSG msg;
    INITCOMMONCONTROLSEX icc;
    (void)prev; (void)cmd;
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);
    load_video();
    load_keys();
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "NbaUnrivaledConfig";
    RegisterClassA(&wc);
    h = CreateWindowExA(0, wc.lpszClassName, "NBA Unrivaled Settings",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                        CW_USEDEFAULT, CW_USEDEFAULT, 620, 640, NULL, NULL, inst, NULL);
    ShowWindow(h, show);
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (s_wait_row >= 0) {
            /* Route key messages to our window while capturing. */
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
                if (msg.lParam & (1 << 30)) continue; /* ignore auto-repeat */
                assign_key((int)msg.wParam);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
