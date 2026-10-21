#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <wincrypt.h>
#include <wininet.h>
#include <objidl.h>
#include <gdiplus.h>

#include "resource.h"
#include "config.hpp"
#include "crypto_utils.hpp"
#include "sound_player.hpp"
#include "gesture_recognizer.hpp"
#include "osd_overlay.hpp"
#include "snapshot_windows.hpp"
#include "actions.hpp"
#include "settings_server.hpp"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "imm32.lib")

#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif
#ifndef IMC_GETOPENSTATUS
#define IMC_GETOPENSTATUS 0x0005
#endif

namespace mouseinc {

constexpr UINT WM_TRAYICON = WM_APP + 101;
constexpr UINT WM_RELOAD_CFG = WM_APP + 102;
constexpr UINT WM_EXEC_DEFERRED = WM_APP + 103;
constexpr UINT WM_SHOW_CLIPMENU = WM_APP + 104;
constexpr UINT WM_SHOW_ADD_GESTURE = WM_APP + 105;
constexpr UINT WM_CHECK_IME_STATUS = WM_APP + 106;
constexpr UINT WM_RENDER_TRACE = WM_APP + 107;
constexpr UINT WM_UPDATE_CAPSLOCK = WM_APP + 108;

constexpr UINT_PTR TIMER_CAPS_UNLOCK = 9101;
constexpr UINT_PTR TIMER_CHECK_IME = 9102;

enum TrayCmdId {
    ID_TRAY_PAUSE = 1001,
    ID_TRAY_SETTINGS = 1002,
    ID_TRAY_RELOAD = 1003,
    ID_TRAY_AUTORUN = 1004,
    ID_TRAY_MANUAL = 1005,
    ID_TRAY_UPDATE = 1006,
    ID_TRAY_ABOUT = 1007,
    ID_TRAY_EXIT = 1008,
    ID_CLIP_BASE = 2000
};

struct GlobalState {
    HINSTANCE hInst = nullptr;
    HWND main_hwnd = nullptr;
    HHOOK mouse_hook = nullptr;
    HHOOK kbd_hook = nullptr;
    UINT msg_taskbar_created = 0;

    AppConfig cfg;
    OsdOverlayManager osd;
    AppRuntimeCallbacks rt;
    LocalSettingsServer settings_srv;

    bool paused = false;
    bool tray_added = false;
    NOTIFYICONDATAW nid = {};

    // Gesture state
    bool rbutton_down = false;
    bool gesture_active = false;
    bool wheel_switched_during_rbutton = false;
    bool injecting_rbutton = false;
    bool fast_paste_triggered = false;
    bool trace_render_pending = false;
    DWORD rbutton_down_tick = 0;
    DWORD last_move_tick = 0;
    POINT rbutton_start_pt = {};
    HWND gesture_target_hwnd = nullptr;
    std::wstring gesture_target_exe;
    std::vector<POINT> gesture_points;
    std::vector<POINT> last_gesture_points;

    // AltDrag & WheelAltControl state
    bool alt_mbutton_down = false;
    bool alt_mbutton_dragged = false;
    bool alt_lbutton_dragging = false;
    bool alt_rbutton_resizing = false;
    HWND alt_drag_hwnd = nullptr;
    POINT alt_drag_start_pt = {};
    RECT alt_drag_start_rc = {};

    // AutoClip & FastPaste LButton state
    bool lbutton_down = false;
    POINT lbutton_start_pt = {};
    DWORD lbutton_down_tick = 0;

    // HotCorner state
    int active_corner = -1; // 0=TL, 1=TR, 2=BL, 3=BR

    // Double Ctrl+C clipboard menu state
    DWORD last_ctrl_c_tick = 0;

    // ShowIme state
    int last_ime_chinese = -1;

    // QuickJump state
    HWND last_quickjump_dlg = nullptr;

    // Deferred action queue
    ActionList pending_actions;
    HWND pending_target_hwnd = nullptr;
};

static GlobalState g_app;

// Query current foreground window's IME conversion status via ImmGetDefaultIMEWnd (matching original 0x00453946)
inline void query_and_show_ime_indicator(bool silent_init) {
    if (!g_app.cfg.show_ime || g_app.paused) return;
    HWND hFg = GetForegroundWindow();
    if (!hFg) return;
    HWND hIme = ImmGetDefaultIMEWnd(hFg);
    if (!hIme) return;

    DWORD_PTR convMode = 0;
    if (!SendMessageTimeoutW(hIme, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0, SMTO_ABORTIFHUNG, 60, &convMode)) {
        return;
    }

    int is_zh = (convMode & 0x0001) ? 1 : 0;
    if (silent_init || g_app.last_ime_chinese == -1) {
        g_app.last_ime_chinese = is_zh;
        return;
    }
    if (is_zh != g_app.last_ime_chinese) {
        g_app.last_ime_chinese = is_zh;
        g_app.osd.show_cursor_ime_tip(is_zh ? L"中" : L"英");
    }
}

// Helper for QuickJump: find active/topmost File Explorer (CabinetWClass) folder path
inline std::wstring find_top_explorer_folder_path() {
    std::wstring found_path;
    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        if (!IsWindowVisible(hWnd)) return TRUE;
        wchar_t cls[64] = {0};
        GetClassNameW(hWnd, cls, 64);
        if (wcscmp(cls, L"CabinetWClass") != 0 && wcscmp(cls, L"ExploreWClass") != 0) return TRUE;

        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lp) -> BOOL {
            wchar_t ccls[64] = {0};
            GetClassNameW(hChild, ccls, 64);
            if (wcscmp(ccls, L"ToolbarWindow32") == 0) {
                wchar_t txt[MAX_PATH + 64] = {0};
                GetWindowTextW(hChild, txt, MAX_PATH + 60);
                std::wstring s(txt);
                size_t colon = s.find(L": ");
                if (colon != std::wstring::npos && colon + 2 < s.size()) {
                    std::wstring candidate = s.substr(colon + 2);
                    if (PathIsDirectoryW(candidate.c_str())) {
                        *reinterpret_cast<std::wstring*>(lp) = candidate;
                        return FALSE;
                    }
                }
            }
            return TRUE;
        }, lParam);

        return reinterpret_cast<std::wstring*>(lParam)->empty() ? TRUE : FALSE;
    }, reinterpret_cast<LPARAM>(&found_path));
    return found_path;
}

// Apply QuickJump to a #32770 File Open/Save dialog
inline void try_quick_jump_dialog(HWND hDlg) {
    if (!g_app.cfg.quick_jump || !hDlg || hDlg == g_app.last_quickjump_dlg) return;
    wchar_t cls[64] = {0};
    GetClassNameW(hDlg, cls, 64);
    if (wcscmp(cls, L"#32770") != 0) return;

    bool is_file_dlg = (FindWindowExW(hDlg, nullptr, L"DUIViewWndClassName", nullptr) != nullptr) ||
                       (FindWindowExW(hDlg, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr);
    if (!is_file_dlg) return;

    std::wstring folder = find_top_explorer_folder_path();
    if (folder.empty()) return;

    HWND hEdit = GetDlgItem(hDlg, 1148);
    if (!hEdit) {
        HWND hComboEx = FindWindowExW(hDlg, nullptr, L"ComboBoxEx32", nullptr);
        if (hComboEx) {
            HWND hCombo = FindWindowExW(hComboEx, nullptr, L"ComboBox", nullptr);
            if (hCombo) hEdit = FindWindowExW(hCombo, nullptr, L"Edit", nullptr);
        }
    }
    if (hEdit) {
        g_app.last_quickjump_dlg = hDlg;
        wchar_t prev[MAX_PATH] = {0};
        SendMessageW(hEdit, WM_GETTEXT, MAX_PATH, reinterpret_cast<LPARAM>(prev));
        SendMessageW(hEdit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(folder.c_str()));
        SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hDlg, IDOK)));
        SendMessageW(hEdit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(prev));
    }
}

inline bool is_window_fullscreen(HWND hwnd) {
    if (!hwnd) return false;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root) return false;
    wchar_t cls[64] = {0};
    GetClassNameW(root, cls, 64);
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) return false;

    RECT rcWnd = {};
    if (!GetWindowRect(root, &rcWnd)) return false;
    HMONITOR hMon = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(hMon, &mi)) return false;

    return rcWnd.left <= mi.rcMonitor.left &&
           rcWnd.top <= mi.rcMonitor.top &&
           rcWnd.right >= mi.rcMonitor.right &&
           rcWnd.bottom >= mi.rcMonitor.bottom;
}

inline bool should_bypass_target(HWND hwnd) {
    if (g_app.paused) return true;
    std::wstring exe = get_window_exe_name(hwnd);
    if (!exe.empty() && g_app.cfg.is_excluded(exe)) return true;
    if (g_app.cfg.ignore_full_screen && is_window_fullscreen(hwnd)) return true;
    return false;
}

inline bool is_autorun_enabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0, sz = 0;
    bool exists = (RegQueryValueExW(hKey, L"MouseInc", nullptr, &type, nullptr, &sz) == ERROR_SUCCESS && sz > 0);
    RegCloseKey(hKey);
    return exists;
}

inline void toggle_autorun() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_READ, &hKey) != ERROR_SUCCESS) {
        return;
    }
    if (is_autorun_enabled()) {
        RegDeleteValueW(hKey, L"MouseInc");
    } else {
        wchar_t exe[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        PathQuoteSpacesW(exe);
        RegSetValueExW(hKey, L"MouseInc", 0, REG_SZ, reinterpret_cast<const BYTE*>(exe), (DWORD)((wcslen(exe) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(hKey);
}

inline void update_tray_icon() {
    g_app.nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_app.nid.hWnd = g_app.main_hwnd;
    g_app.nid.uID = 1;
    g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon = (HICON)LoadImageW(
        g_app.hInst,
        MAKEINTRESOURCEW(IDI_MAIN_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR);
    if (!g_app.nid.hIcon) g_app.nid.hIcon = LoadIconW(g_app.hInst, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    if (!g_app.nid.hIcon) g_app.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_app.nid.szTip, g_app.paused ? L"MouseInc (Paused)" : L"MouseInc");

    if (g_app.cfg.show_tray_icon) {
        if (!g_app.tray_added) {
            Shell_NotifyIconW(NIM_ADD, &g_app.nid);
            g_app.tray_added = true;
        } else {
            Shell_NotifyIconW(NIM_MODIFY, &g_app.nid);
        }
    } else if (g_app.tray_added) {
        Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
        g_app.tray_added = false;
    }
}

inline void register_configured_hotkeys() {
    for (int i = 0; i < 128; i++) {
        UnregisterHotKey(g_app.main_hwnd, 4000 + i);
    }
    for (size_t i = 0; i < g_app.cfg.hotkeys.size() && i < 128; i++) {
        const auto& hk = g_app.cfg.hotkeys[i];
        if (!hk.valid || hk.keys.empty()) continue;

        UINT mods = MOD_NOREPEAT;
        WORD vk = 0;
        std::stringstream ss(hk.keys);
        std::string part;
        while (std::getline(ss, part, '+')) {
            std::string low = to_lower_ascii(part);
            if (low == "ctrl" || low == "control") mods |= MOD_CONTROL;
            else if (low == "alt") mods |= MOD_ALT;
            else if (low == "shift") mods |= MOD_SHIFT;
            else if (low == "win") mods |= MOD_WIN;
            else vk = parse_single_vk(part);
        }
        if (vk != 0) {
            RegisterHotKey(g_app.main_hwnd, (int)(4000 + i), mods, vk);
        }
    }
}

inline void queue_deferred_actions(const ActionList& actions, HWND target_hwnd) {
    if (actions.empty()) return;
    g_app.pending_actions = actions;
    g_app.pending_target_hwnd = target_hwnd;
    PostMessageW(g_app.main_hwnd, WM_EXEC_DEFERRED, 0, 0);
}

inline void record_gesture_geometry(const std::vector<POINT>& pts) {
    if (pts.empty()) return;
    g_app.rt.gesture_start_pt = pts.front();
    g_app.rt.gesture_end_pt = pts.back();
    LONG min_x = pts[0].x, max_x = pts[0].x;
    LONG min_y = pts[0].y, max_y = pts[0].y;
    for (const auto& p : pts) {
        if (p.x < min_x) min_x = p.x;
        if (p.x > max_x) max_x = p.x;
        if (p.y < min_y) min_y = p.y;
        if (p.y > max_y) max_y = p.y;
    }
    g_app.rt.last_gesture_rect = { min_x, min_y, max_x, max_y };
    g_app.rt.has_gesture_rect = true;
}

inline int detect_screen_edge(POINT pt) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (pt.x <= 2) return 0;          // Left
    if (pt.x >= sw - 3) return 1;     // Right
    if (pt.y <= 2) return 2;          // Top
    if (pt.y >= sh - 3) return 3;     // Bottom
    return -1;
}

inline int detect_screen_corner(POINT pt) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (pt.x <= 2 && pt.y <= 2) return 0;             // TopLeft
    if (pt.x >= sw - 3 && pt.y <= 2) return 1;        // TopRight
    if (pt.x <= 2 && pt.y >= sh - 3) return 2;        // BottomLeft
    if (pt.x >= sw - 3 && pt.y >= sh - 3) return 3;   // BottomRight
    return -1;
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        POINT pt = ms->pt;

        // Pass through our own synthesized mouse events
        if (g_app.injecting_rbutton || (ms->flags & LLMHF_INJECTED)) {
            return CallNextHookEx(g_app.mouse_hook, nCode, wParam, lParam);
        }

        // Track left mouse button state for AutoClip and FastPaste
        if (wParam == WM_LBUTTONDOWN) {
            g_app.lbutton_down = true;
            g_app.lbutton_start_pt = pt;
            g_app.lbutton_down_tick = GetTickCount();
            if (!g_app.paused && g_app.cfg.quick_jump) {
                HWND hRoot = GetAncestor(WindowFromPoint(pt), GA_ROOT);
                if (hRoot) try_quick_jump_dialog(hRoot);
            }
        } else if (wParam == WM_LBUTTONUP) {
            bool was_down = g_app.lbutton_down;
            g_app.lbutton_down = false;
            if (!g_app.paused && g_app.cfg.auto_clip && was_down && !g_app.fast_paste_triggered) {
                double dist = std::hypot(pt.x - g_app.lbutton_start_pt.x, pt.y - g_app.lbutton_start_pt.y);
                if (dist > 25.0 && (GetTickCount() - g_app.lbutton_down_tick) > 120) {
                    ActionList copy_act = { {"SendKeys", "Ctrl+C", "NOACTIVATE"} };
                    queue_deferred_actions(copy_act, GetForegroundWindow());
                }
            }
            g_app.fast_paste_triggered = false;
        }

        // FastPaste ("按住左鍵點右鍵貼上", zh-TW.js line 86 & faq.md line 131)
        if (!g_app.paused && g_app.cfg.fast_paste && g_app.lbutton_down) {
            if (wParam == WM_RBUTTONDOWN) {
                HWND hUnder = GetAncestor(WindowFromPoint(pt), GA_ROOT);
                if (!should_bypass_target(hUnder)) {
                    g_app.fast_paste_triggered = true;
                    ActionList paste_act = { {"SendKeys", "Ctrl+V", "NOACTIVATE"} };
                    queue_deferred_actions(paste_act, hUnder);
                    return 1;
                }
            } else if (wParam == WM_RBUTTONUP && g_app.fast_paste_triggered) {
                return 1;
            }
        }
        if (wParam == WM_RBUTTONUP && g_app.fast_paste_triggered) {
            g_app.fast_paste_triggered = false;
            return 1;
        }

        // 1. AltDrag & WheelAltControl (Alt + MButton drag moves window; Alt + MButton click mutes volume; Alt + Wheel adjusts volume)
        bool alt_held = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        if (!g_app.paused && alt_held) {
            if (wParam == WM_MBUTTONDOWN && (g_app.cfg.alt_drag || g_app.cfg.wheel_alt_control)) {
                HWND hTarget = GetAncestor(WindowFromPoint(pt), GA_ROOT);
                if (hTarget && !should_bypass_target(hTarget)) {
                    g_app.alt_mbutton_down = true;
                    g_app.alt_mbutton_dragged = false;
                    g_app.alt_drag_hwnd = hTarget;
                    g_app.alt_drag_start_pt = pt;
                    GetWindowRect(hTarget, &g_app.alt_drag_start_rc);
                    return 1;
                }
            } else if (wParam == WM_MOUSEWHEEL && g_app.cfg.wheel_alt_control) {
                short delta = (short)HIWORD(ms->mouseData);
                ActionList vol_act = { {"SendKeys", (delta > 0) ? "VolumeUp" : "VolumeDown", "NOACTIVATE"} };
                queue_deferred_actions(vol_act, GetForegroundWindow());
                return 1;
            }
        }

        if (g_app.alt_mbutton_down) {
            if (wParam == WM_MOUSEMOVE) {
                int dx = pt.x - g_app.alt_drag_start_pt.x;
                int dy = pt.y - g_app.alt_drag_start_pt.y;
                if (!g_app.alt_mbutton_dragged && std::hypot(dx, dy) >= 4.0) {
                    g_app.alt_mbutton_dragged = true;
                }
                if (g_app.alt_mbutton_dragged && g_app.cfg.alt_drag && g_app.alt_drag_hwnd && !IsZoomed(g_app.alt_drag_hwnd)) {
                    SetWindowPos(
                        g_app.alt_drag_hwnd, nullptr,
                        g_app.alt_drag_start_rc.left + dx,
                        g_app.alt_drag_start_rc.top + dy,
                        0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            } else if (wParam == WM_MBUTTONUP) {
                bool dragged = g_app.alt_mbutton_dragged;
                g_app.alt_mbutton_down = false;
                g_app.alt_mbutton_dragged = false;
                g_app.alt_drag_hwnd = nullptr;
                if (!dragged && g_app.cfg.wheel_alt_control) {
                    ActionList mute_act = { {"SendKeys", "VolumeMute", "NOACTIVATE"} };
                    queue_deferred_actions(mute_act, GetForegroundWindow());
                }
                return 1;
            }
        }

        // 2. MouseGesture (Right-button gesture & WheelSwitch & AddMode)
        if (!g_app.paused && g_app.cfg.mouse_gesture.open) {
            if (wParam == WM_RBUTTONDOWN) {
                HWND hUnder = GetAncestor(WindowFromPoint(pt), GA_ROOT);
                if (!should_bypass_target(hUnder)) {
                    g_app.rbutton_down = true;
                    g_app.gesture_active = false;
                    g_app.wheel_switched_during_rbutton = false;
                    g_app.rbutton_down_tick = GetTickCount();
                    g_app.last_move_tick = g_app.rbutton_down_tick;
                    g_app.rbutton_start_pt = pt;
                    g_app.gesture_target_hwnd = hUnder;
                    g_app.gesture_target_exe = get_window_exe_name(hUnder);
                    g_app.gesture_points.clear();
                    g_app.gesture_points.push_back(pt);
                    g_app.osd.reset_gesture_color(g_app.cfg);
                    return 1; // Suppress initial right down until we know if it's a click or gesture
                }
            } else if (wParam == WM_MOUSEMOVE && g_app.rbutton_down) {
                double dist = std::hypot(pt.x - g_app.rbutton_start_pt.x, pt.y - g_app.rbutton_start_pt.y);
                if (!g_app.gesture_active && dist >= g_app.cfg.mouse_gesture.start_distance) {
                    g_app.gesture_active = true;
                }
                if (g_app.gesture_active) {
                    g_app.last_move_tick = GetTickCount();
                    if (g_app.gesture_points.empty() ||
                        std::hypot(pt.x - g_app.gesture_points.back().x, pt.y - g_app.gesture_points.back().y) >= 3.0) {
                        g_app.gesture_points.push_back(pt);
                    }
                    // Coalesce OSD trace rendering asynchronously on main_hwnd so WH_MOUSE_LL has zero lag!
                    if (!g_app.trace_render_pending) {
                        g_app.trace_render_pending = true;
                        PostMessageW(g_app.main_hwnd, WM_RENDER_TRACE, 0, 0);
                    }
                }
            } else if (wParam == WM_MOUSEWHEEL && g_app.rbutton_down && g_app.cfg.mouse_gesture.wheel_switch) {
                short delta = (short)HIWORD(ms->mouseData);
                std::string sign = (delta > 0) ? "WheelSwitchUp" : "WheelSwitchDown";
                const GestureRule* rule = GestureRecognizer::match_rule(sign, g_app.gesture_target_exe, g_app.cfg);
                if (rule) {
                    g_app.wheel_switched_during_rbutton = true;
                    queue_deferred_actions(rule->actions, g_app.gesture_target_hwnd);
                    return 1;
                }
            } else if (wParam == WM_RBUTTONUP && g_app.rbutton_down) {
                g_app.rbutton_down = false;
                g_app.trace_render_pending = false;
                g_app.osd.hide_gesture_trace();

                if (g_app.wheel_switched_during_rbutton) {
                    g_app.wheel_switched_during_rbutton = false;
                    return 1;
                }

                if (g_app.gesture_active) {
                    g_app.gesture_active = false;
                    // Timeout is elapsed time since last mouse movement ("滑鼠超過這個時間未移動，會中斷手勢")
                    DWORD idle_ms = GetTickCount() - g_app.last_move_tick;
                    if (g_app.cfg.mouse_gesture.timeout <= 0 || (int)idle_ms <= g_app.cfg.mouse_gesture.timeout) {
                        record_gesture_geometry(g_app.gesture_points);
                        if (g_app.cfg.mouse_gesture.add_mode) {
                            g_app.last_gesture_points = g_app.gesture_points;
                            PostMessageW(g_app.main_hwnd, WM_SHOW_ADD_GESTURE, 0, 0);
                        } else {
                            std::string sign = GestureRecognizer::recognize(g_app.gesture_points, g_app.cfg);
                            const GestureRule* rule = GestureRecognizer::match_rule(sign, g_app.gesture_target_exe, g_app.cfg);
                            if (rule) {
                                queue_deferred_actions(rule->actions, g_app.gesture_target_hwnd);
                            }
                        }
                    }
                    return 1;
                } else {
                    // Replay normal right click
                    g_app.injecting_rbutton = true;
                    INPUT in[2] = {};
                    in[0].type = INPUT_MOUSE;
                    in[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
                    in[1].type = INPUT_MOUSE;
                    in[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                    SendInput(2, in, sizeof(INPUT));
                    g_app.injecting_rbutton = false;
                    return 1;
                }
            }
        }

        // 3. WheelEdge (Scroll or Middle-Click on screen edges)
        if (!g_app.paused && g_app.cfg.wheel_edge_open && (wParam == WM_MOUSEWHEEL || wParam == WM_MBUTTONDOWN)) {
            int edge = detect_screen_edge(pt);
            const WheelEdgeSide* side = nullptr;
            if (edge == 0) side = &g_app.cfg.wheel_edge_left;
            else if (edge == 1) side = &g_app.cfg.wheel_edge_right;
            else if (edge == 2) side = &g_app.cfg.wheel_edge_top;
            else if (edge == 3) side = &g_app.cfg.wheel_edge_bottom;

            if (side && side->valid) {
                HWND hUnder = GetAncestor(WindowFromPoint(pt), GA_ROOT);
                if (wParam == WM_MOUSEWHEEL) {
                    short delta = (short)HIWORD(ms->mouseData);
                    const ActionList& acts = (delta > 0) ? side->up_actions : side->down_actions;
                    if (!acts.empty()) {
                        queue_deferred_actions(acts, hUnder);
                        return 1;
                    }
                } else if (wParam == WM_MBUTTONDOWN && !side->press_actions.empty()) {
                    queue_deferred_actions(side->press_actions, hUnder);
                    return 1;
                }
            }
        }

        // 4. HotCorner
        if (!g_app.paused && g_app.cfg.hot_corner_open && wParam == WM_MOUSEMOVE) {
            int c = detect_screen_corner(pt);
            if (c != g_app.active_corner) {
                g_app.active_corner = c;
                const HotCornerSide* hc = nullptr;
                if (c == 0) hc = &g_app.cfg.hot_corner_top_left;
                else if (c == 1) hc = &g_app.cfg.hot_corner_top_right;
                else if (c == 2) hc = &g_app.cfg.hot_corner_bottom_left;
                else if (c == 3) hc = &g_app.cfg.hot_corner_bottom_right;
                if (hc && hc->valid && !hc->actions.empty()) {
                    queue_deferred_actions(hc->actions, GetForegroundWindow());
                }
            }
        }

        // 5. WheelThrough & WheelNatural
        if (!g_app.paused && wParam == WM_MOUSEWHEEL && !g_app.rbutton_down) {
            short delta = (short)HIWORD(ms->mouseData);
            if (g_app.cfg.wheel_natural) delta = -delta;
            if (g_app.cfg.wheel_through) {
                HWND hUnder = WindowFromPoint(pt);
                if (hUnder && hUnder != GetForegroundWindow()) {
                    WORD keys = 0;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keys |= MK_CONTROL;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keys |= MK_SHIFT;
                    PostMessageW(hUnder, WM_MOUSEWHEEL, MAKEWPARAM(keys, delta), MAKELPARAM(pt.x, pt.y));
                    return 1;
                }
            } else if (g_app.cfg.wheel_natural) {
                HWND hUnder = WindowFromPoint(pt);
                if (hUnder) {
                    WORD keys = 0;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keys |= MK_CONTROL;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keys |= MK_SHIFT;
                    PostMessageW(hUnder, WM_MOUSEWHEEL, MAKEWPARAM(keys, delta), MAKELPARAM(pt.x, pt.y));
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_app.mouse_hook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (!(kb->flags & LLKHF_INJECTED) && !g_app.paused) {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                // 1. KeySound
                if (g_app.cfg.key_sound) {
                    play_key_sound(g_app.cfg.key_sound_index);
                }

                // 2. Double Ctrl+C -> ClipboardManager popup menu
                if (kb->vkCode == 'C' && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && g_app.cfg.clipboard_manager_open) {
                    DWORD now = GetTickCount();
                    if (now - g_app.last_ctrl_c_tick <= 450) {
                        g_app.last_ctrl_c_tick = 0;
                        PostMessageW(g_app.main_hwnd, WM_SHOW_CLIPMENU, 0, 0);
                    } else {
                        g_app.last_ctrl_c_tick = now;
                    }
                }

                // 3. Keycast OSD
                if (g_app.cfg.keycast.open) {
                    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool win = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;

                    if (kb->vkCode != VK_CONTROL && kb->vkCode != VK_LCONTROL && kb->vkCode != VK_RCONTROL &&
                        kb->vkCode != VK_MENU && kb->vkCode != VK_LMENU && kb->vkCode != VK_RMENU &&
                        kb->vkCode != VK_SHIFT && kb->vkCode != VK_LSHIFT && kb->vkCode != VK_RSHIFT &&
                        kb->vkCode != VK_LWIN && kb->vkCode != VK_RWIN) {
                        bool has_mod = ctrl || alt || shift || win;
                        if (!g_app.cfg.keycast.ignore_single || has_mod) {
                            wchar_t keyName[64] = {0};
                            LONG lParamKey = (LONG)(kb->scanCode << 16);
                            if (kb->flags & LLKHF_EXTENDED) lParamKey |= (1 << 24);
                            GetKeyNameTextW(lParamKey, keyName, 64);

                            std::wstring chord;
                            if (ctrl) chord += L"Ctrl + ";
                            if (alt) chord += L"Alt + ";
                            if (shift) chord += L"Shift + ";
                            if (win) chord += L"Win + ";
                            chord += keyName[0] ? keyName : L"Key";
                            g_app.osd.show_banner(chord, g_app.cfg, std::max(1, g_app.cfg.keycast.fade) * 400, true);
                        }
                    }
                }

                // 4. CapsLock LED & CapsUnlock (30-second auto-unlock + Esc unlock)
                if (kb->vkCode == VK_CAPITAL) {
                    PostMessageW(g_app.main_hwnd, WM_UPDATE_CAPSLOCK, 0, 0);
                } else if (kb->vkCode == VK_ESCAPE && g_app.cfg.caps_unlock && (GetKeyState(VK_CAPITAL) & 0x0001) != 0) {
                    INPUT in[2] = {};
                    in[0].type = INPUT_KEYBOARD;
                    in[0].ki.wVk = VK_CAPITAL;
                    in[1].type = INPUT_KEYBOARD;
                    in[1].ki.wVk = VK_CAPITAL;
                    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, in, sizeof(INPUT));
                    PostMessageW(g_app.main_hwnd, WM_UPDATE_CAPSLOCK, 0, 0);
                }
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                if (kb->vkCode == VK_CAPITAL) {
                    PostMessageW(g_app.main_hwnd, WM_UPDATE_CAPSLOCK, 0, 0);
                }
            }
        }
    }
    return CallNextHookEx(g_app.kbd_hook, nCode, wParam, lParam);
}

// Build and show ClipboardManager popup menu matching docs/image/5.png:
// Items preceding an item with Actions==[] and non-empty Name are grouped into a popup submenu!
inline void show_clipboard_popup_menu() {
    if (g_app.cfg.clipboard_menu.empty()) return;
    HMENU hRoot = CreatePopupMenu();

    // Check if there are any group container entries (Actions==[] && !Name.empty())
    std::vector<std::pair<size_t, std::wstring>> pending_group;

    for (size_t i = 0; i < g_app.cfg.clipboard_menu.size(); i++) {
        const auto& item = g_app.cfg.clipboard_menu[i];
        if (!item.valid) continue;

        if (item.actions.empty()) {
            if (item.name.empty()) {
                // Flush any pending items to root before separator
                for (const auto& p : pending_group) {
                    AppendMenuW(hRoot, MF_STRING, ID_CLIP_BASE + p.first, p.second.c_str());
                }
                pending_group.clear();
                AppendMenuW(hRoot, MF_SEPARATOR, 0, nullptr);
            } else {
                // Submenu container for all pending_group items (e.g. "搜索翻译", "编码解码" in docs/image/5.png)
                HMENU hSub = CreatePopupMenu();
                for (const auto& p : pending_group) {
                    AppendMenuW(hSub, MF_STRING, ID_CLIP_BASE + p.first, p.second.c_str());
                }
                pending_group.clear();
                AppendMenuW(hRoot, MF_POPUP, reinterpret_cast<UINT_PTR>(hSub), item.name.c_str());
            }
        } else {
            pending_group.push_back({i, item.name});
        }
    }

    for (const auto& p : pending_group) {
        AppendMenuW(hRoot, MF_STRING, ID_CLIP_BASE + p.first, p.second.c_str());
    }

    POINT pt;
    GetCursorPos(&pt);
    HWND hTarget = GetForegroundWindow();
    SetForegroundWindow(g_app.main_hwnd);
    UINT cmd = TrackPopupMenu(hRoot, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_app.main_hwnd, nullptr);
    DestroyMenu(hRoot);

    if (cmd >= ID_CLIP_BASE && cmd < ID_CLIP_BASE + g_app.cfg.clipboard_menu.size()) {
        size_t idx = cmd - ID_CLIP_BASE;
        execute_action_list(g_app.cfg.clipboard_menu[idx].actions, hTarget, g_app.cfg, g_app.osd, g_app.rt);
    }
}

// Tray context menu matching docs/image/2.png order & separators
inline void show_tray_context_menu() {
    HMENU hMenu = CreatePopupMenu();
    HMENU hHelpMenu = CreatePopupMenu();

    AppendMenuW(hHelpMenu, MF_STRING, ID_TRAY_MANUAL, g_app.cfg.tr("Manual", L"Manual(&M)...").c_str());
    AppendMenuW(hHelpMenu, MF_STRING, ID_TRAY_UPDATE, g_app.cfg.tr("CheckUpdate", L"Check Update(&U)").c_str());
    AppendMenuW(hHelpMenu, MF_STRING, ID_TRAY_ABOUT, g_app.cfg.tr("About", L"About(&A)...").c_str());

    AppendMenuW(hMenu, MF_STRING | (is_autorun_enabled() ? MF_CHECKED : 0), ID_TRAY_AUTORUN, g_app.cfg.tr("Autorun", L"Start on boot(&A)").c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, g_app.cfg.tr("Settings", L"Settings(&S)...").c_str());
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RELOAD, g_app.cfg.tr("ReloadConfig", L"Reload Config(&C)").c_str());
    AppendMenuW(hMenu, MF_STRING | (g_app.paused ? MF_CHECKED : 0), ID_TRAY_PAUSE, g_app.cfg.tr("Pause", L"Pause(&P)").c_str());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hHelpMenu), g_app.cfg.tr("Help", L"Help(&H)").c_str());
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, g_app.cfg.tr("Exit", L"Exit(&X)").c_str());

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_app.main_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_app.main_hwnd, nullptr);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_app.msg_taskbar_created && g_app.msg_taskbar_created != 0) {
        g_app.tray_added = false;
        update_tray_icon();
        return 0;
    }

    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) {
                show_tray_context_menu();
            } else if (lParam == WM_LBUTTONDBLCLK) {
                // If windows are hidden via HideTray, restore them; otherwise open Settings UI
                if (!g_app.rt.hidden_tray_windows.empty()) {
                    for (HWND h : g_app.rt.hidden_tray_windows) {
                        if (IsWindow(h)) {
                            ShowWindow(h, SW_SHOW);
                            SetForegroundWindow(h);
                        }
                    }
                    g_app.rt.hidden_tray_windows.clear();
                } else {
                    g_app.settings_srv.open_settings_ui();
                }
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_PAUSE:
                    g_app.paused = !g_app.paused;
                    update_tray_icon();
                    break;
                case ID_TRAY_SETTINGS:
                    g_app.settings_srv.open_settings_ui();
                    break;
                case ID_TRAY_RELOAD:
                    g_app.cfg.load();
                    register_configured_hotkeys();
                    update_tray_icon();
                    g_app.osd.update_capslock_led((GetKeyState(VK_CAPITAL) & 0x0001) != 0, g_app.cfg.caps_lock_led);
    query_and_show_ime_indicator(true);
    SetTimer(g_app.main_hwnd, TIMER_CHECK_IME, 100, nullptr);
                    g_app.osd.show_banner(g_app.cfg.tr("ReloadSucess", L"Configuration reloaded"), g_app.cfg, 2500, false);
                    break;
                case ID_TRAY_AUTORUN:
                    toggle_autorun();
                    break;
                case ID_TRAY_MANUAL:
                    ShellExecuteW(nullptr, L"open", L"https://docs.shuax.com/MouseInc/", nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                case ID_TRAY_UPDATE:
                    MessageBoxW(hwnd, g_app.cfg.tr("UpdateCheckLatest", L"You are using the latest version 2.13.4").c_str(), L"MouseInc", MB_OK | MB_ICONINFORMATION);
                    break;
                case ID_TRAY_ABOUT: {
                    std::wstring about = g_app.cfg.tr("AboutContent", L"MouseInc - 2.13.4\nhttps://shuax.com");
                    size_t p = about.find(L"{0}");
                    if (p != std::wstring::npos) about.replace(p, 3, L"2.13.4");
                    MessageBoxW(hwnd, about.c_str(), L"MouseInc", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                case ID_TRAY_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            return 0;

        case WM_RENDER_TRACE: {
            g_app.trace_render_pending = false;
            if (!g_app.rbutton_down || !g_app.gesture_active) return 0;

            DWORD idle_ms = GetTickCount() - g_app.last_move_tick;
            if (g_app.cfg.mouse_gesture.timeout > 0 && (int)idle_ms > g_app.cfg.mouse_gesture.timeout) {
                g_app.osd.render_gesture_trace(g_app.gesture_points, g_app.cfg.tr("NoMatch", L"NoMatch"), false, g_app.cfg);
                return 0;
            }

            std::string sign = GestureRecognizer::recognize(g_app.gesture_points, g_app.cfg);
            const GestureRule* rule = GestureRecognizer::match_rule(sign, g_app.gesture_target_exe, g_app.cfg);
            std::wstring label;
            bool matched = false;
            if (g_app.cfg.mouse_gesture.add_mode) {
                label = g_app.cfg.tr("AddMode", L"AddMode");
                matched = true;
            } else if (rule && !rule->name.empty()) {
                // Match docs/image/3.png: display ONLY the action name (e.g. "搜索选中文字")
                label = rule->name;
                matched = true;
            } else if (!sign.empty()) {
                label = g_app.cfg.tr("NoAction", L"NoAction");
            } else {
                label = g_app.cfg.tr("NoMatch", L"NoMatch");
            }
            g_app.osd.render_gesture_trace(g_app.gesture_points, label, matched, g_app.cfg);
            return 0;
        }

        case WM_UPDATE_CAPSLOCK: {
            bool caps_on = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
            g_app.osd.update_capslock_led(caps_on, g_app.cfg.caps_lock_led);
            if (caps_on && g_app.cfg.caps_unlock) {
                SetTimer(hwnd, TIMER_CAPS_UNLOCK, 30000, nullptr);
            } else {
                KillTimer(hwnd, TIMER_CAPS_UNLOCK);
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_CAPS_UNLOCK) {
                KillTimer(hwnd, TIMER_CAPS_UNLOCK);
                if (g_app.cfg.caps_unlock && (GetKeyState(VK_CAPITAL) & 0x0001) != 0) {
                    INPUT in[2] = {};
                    in[0].type = INPUT_KEYBOARD;
                    in[0].ki.wVk = VK_CAPITAL;
                    in[1].type = INPUT_KEYBOARD;
                    in[1].ki.wVk = VK_CAPITAL;
                    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, in, sizeof(INPUT));
                    PostMessageW(hwnd, WM_UPDATE_CAPSLOCK, 0, 0);
                }
                return 0;
            } else if (wParam == TIMER_CHECK_IME) {
                query_and_show_ime_indicator(false);
                return 0;
            }
            break;

        case WM_HOTKEY: {
            int idx = (int)wParam - 4000;
            if (!g_app.paused && idx >= 0 && idx < (int)g_app.cfg.hotkeys.size()) {
                execute_action_list(g_app.cfg.hotkeys[idx].actions, GetForegroundWindow(), g_app.cfg, g_app.osd, g_app.rt);
            } else if (idx >= 0 && idx < (int)g_app.cfg.hotkeys.size()) {
                const auto& acts = g_app.cfg.hotkeys[idx].actions;
                if (!acts.empty() && acts[0].size() >= 2 &&
                    to_lower_ascii(acts[0][0]) == "internal" &&
                    to_lower_ascii(acts[0][1]) == "pause") {
                    g_app.paused = !g_app.paused;
                    update_tray_icon();
                }
            }
            return 0;
        }

        case WM_RELOAD_CFG:
            register_configured_hotkeys();
            update_tray_icon();
            g_app.osd.update_capslock_led((GetKeyState(VK_CAPITAL) & 0x0001) != 0, g_app.cfg.caps_lock_led);
    query_and_show_ime_indicator(true);
    SetTimer(g_app.main_hwnd, TIMER_CHECK_IME, 100, nullptr);
            g_app.osd.show_banner(g_app.cfg.tr("ReloadSucess", L"Configuration reloaded"), g_app.cfg, 2500, false);
            return 0;

        case WM_EXEC_DEFERRED: {
            ActionList list = std::move(g_app.pending_actions);
            HWND target = g_app.pending_target_hwnd;
            execute_action_list(list, target, g_app.cfg, g_app.osd, g_app.rt);
            return 0;
        }

        case WM_SHOW_CLIPMENU:
            show_clipboard_popup_menu();
            return 0;

        case WM_SHOW_ADD_GESTURE:
            if (show_new_gesture_dialog(g_app.last_gesture_points, g_app.cfg)) {
                g_app.osd.show_banner(g_app.cfg.tr("AddGestureSuccess", L"New gesture saved to MouseInc.json"), g_app.cfg, 2200, false);
            }
            return 0;

        case WM_CHECK_IME_STATUS:
            query_and_show_ime_indicator(false);
            return 0;

        case WM_DESTROY:
            if (g_app.tray_added) {
                Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
                g_app.tray_added = false;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace mouseinc

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    using namespace mouseinc;

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MouseInc_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    g_app.hInst = hInstance;
    g_app.cfg.load();

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wc.lpszClassName = L"MouseIncMainHiddenWnd";
    RegisterClassExW(&wc);

    g_app.msg_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    g_app.main_hwnd = CreateWindowExW(0, L"MouseIncMainHiddenWnd", L"MouseInc", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    g_app.osd.init(hInstance);

    g_app.rt.main_hwnd = g_app.main_hwnd;
    g_app.rt.paused = &g_app.paused;
    g_app.rt.on_reload_config = []() {
        PostMessageW(g_app.main_hwnd, WM_COMMAND, ID_TRAY_RELOAD, 0);
    };
    g_app.rt.on_open_settings = []() {
        g_app.settings_srv.open_settings_ui();
    };
    g_app.rt.on_show_clipboard_menu = []() {
        PostMessageW(g_app.main_hwnd, WM_SHOW_CLIPMENU, 0, 0);
    };
    g_app.rt.on_toggle_tray_icon = []() {
        g_app.cfg.show_tray_icon = !g_app.cfg.show_tray_icon;
        update_tray_icon();
    };

    update_tray_icon();
    register_configured_hotkeys();
    g_app.settings_srv.start(&g_app.cfg, g_app.main_hwnd, WM_RELOAD_CFG);
    g_app.rt.settings_port = g_app.settings_srv.get_port();

    g_app.osd.update_capslock_led((GetKeyState(VK_CAPITAL) & 0x0001) != 0, g_app.cfg.caps_lock_led);
    query_and_show_ime_indicator(true);
    SetTimer(g_app.main_hwnd, TIMER_CHECK_IME, 100, nullptr);

    g_app.mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);
    g_app.kbd_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.mouse_hook) UnhookWindowsHookEx(g_app.mouse_hook);
    if (g_app.kbd_hook) UnhookWindowsHookEx(g_app.kbd_hook);
    g_app.rt.brightness.restore_default();
    for (HWND h : g_app.rt.hidden_tray_windows) {
        if (IsWindow(h)) ShowWindow(h, SW_SHOW);
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    if (hMutex) CloseHandle(hMutex);
    return 0;
}
