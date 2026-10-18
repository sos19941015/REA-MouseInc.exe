#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wininet.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include "config.hpp"
#include "crypto_utils.hpp"
#include "sound_player.hpp"
#include "osd_overlay.hpp"
#include "snapshot_windows.hpp"

namespace mouseinc {

inline std::wstring read_clipboard_text() {
    std::wstring res;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return res;
    if (!OpenClipboard(nullptr)) return res;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(hData));
        if (p) {
            res = p;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return res;
}

inline HBITMAP read_clipboard_bitmap_copy() {
    if (!IsClipboardFormatAvailable(CF_BITMAP)) return nullptr;
    if (!OpenClipboard(nullptr)) return nullptr;
    HBITMAP hClip = (HBITMAP)GetClipboardData(CF_BITMAP);
    HBITMAP hCopy = nullptr;
    if (hClip) {
        hCopy = (HBITMAP)CopyImage(hClip, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    }
    CloseClipboard();
    return hCopy;
}

inline std::wstring get_window_exe_path(HWND hwnd) {
    if (!hwnd) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return L"";
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return L"";
    wchar_t buf[MAX_PATH] = {0};
    DWORD sz = MAX_PATH;
    QueryFullProcessImageNameW(hProc, 0, buf, &sz);
    CloseHandle(hProc);
    return buf;
}

inline std::wstring get_window_exe_name(HWND hwnd) {
    std::wstring full = get_window_exe_path(hwnd);
    if (full.empty()) return L"";
    return PathFindFileNameW(full.c_str());
}

inline DWORD get_window_pid(HWND hwnd) {
    if (!hwnd) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

inline WORD parse_single_vk(const std::string& token_raw) {
    std::string t = to_lower_ascii(token_raw);
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (t.empty()) return 0;

    if (t == "ctrl" || t == "control" || t == "crtl") return VK_CONTROL;
    if (t == "lctrl" || t == "lcontrol") return VK_LCONTROL;
    if (t == "rctrl" || t == "rcontrol") return VK_RCONTROL;
    if (t == "shift" || t == "shfit") return VK_SHIFT;
    if (t == "lshift") return VK_LSHIFT;
    if (t == "rshift") return VK_RSHIFT;
    if (t == "alt") return VK_MENU;
    if (t == "lalt") return VK_LMENU;
    if (t == "ralt") return VK_RMENU;
    if (t == "win" || t == "lwin") return VK_LWIN;
    if (t == "rwin") return VK_RWIN;
    if (t == "esc" || t == "escape") return VK_ESCAPE;
    if (t == "enter" || t == "return") return VK_RETURN;
    if (t == "space") return VK_SPACE;
    if (t == "tab") return VK_TAB;
    if (t == "backspace") return VK_BACK;
    if (t == "back" || t == "browserback") return VK_BROWSER_BACK;
    if (t == "forward" || t == "browserforward") return VK_BROWSER_FORWARD;
    if (t == "refresh" || t == "browserrefresh") return VK_BROWSER_REFRESH;
    if (t == "up") return VK_UP;
    if (t == "down") return VK_DOWN;
    if (t == "left") return VK_LEFT;
    if (t == "right") return VK_RIGHT;
    if (t == "home") return VK_HOME;
    if (t == "end") return VK_END;
    if (t == "pgup" || t == "pageup") return VK_PRIOR;
    if (t == "pgdn" || t == "pagedown") return VK_NEXT;
    if (t == "insert" || t == "ins") return VK_INSERT;
    if (t == "delete" || t == "del") return VK_DELETE;
    if (t == "prtsc" || t == "printscreen") return VK_SNAPSHOT;
    if (t == "capslock") return VK_CAPITAL;
    if (t == "numlock") return VK_NUMLOCK;
    if (t == "scroll" || t == "scrolllock") return VK_SCROLL;
    if (t == "pause" || t == "break") return VK_PAUSE;
    if (t == "volumemute") return VK_VOLUME_MUTE;
    if (t == "volumedown") return VK_VOLUME_DOWN;
    if (t == "volumeup") return VK_VOLUME_UP;
    if (t == "medianext") return VK_MEDIA_NEXT_TRACK;
    if (t == "mediaprev") return VK_MEDIA_PREV_TRACK;
    if (t == "mediaplaypause") return VK_MEDIA_PLAY_PAUSE;
    if (t == "plus") return VK_OEM_PLUS;
    if (t == "minus") return VK_OEM_MINUS;

    if (t.rfind("num ", 0) == 0 && t.size() == 5 && t[4] >= '0' && t[4] <= '9') {
        return (WORD)(VK_NUMPAD0 + (t[4] - '0'));
    }
    if (t.rfind("num", 0) == 0 && t.size() == 4 && t[3] >= '0' && t[3] <= '9') {
        return (WORD)(VK_NUMPAD0 + (t[3] - '0'));
    }

    if (t.size() == 1) {
        char c = t[0];
        if (c >= 'a' && c <= 'z') return (WORD)('A' + (c - 'a'));
        if (c >= '0' && c <= '9') return (WORD)c;
    }
    if (t.size() >= 2 && t[0] == 'f' && std::isdigit((unsigned char)t[1])) {
        int fn = std::atoi(t.c_str() + 1);
        if (fn >= 1 && fn <= 24) return (WORD)(VK_F1 + fn - 1);
    }
    return 0;
}

inline void send_key_chord(const std::string& keys_expr, AppConfig& cfg, int mode = 0 /* 0=both, 1=down, 2=up */) {
    std::vector<WORD> vks;
    std::stringstream ss(keys_expr);
    std::string item;
    while (std::getline(ss, item, '+')) {
        WORD vk = parse_single_vk(item);
        if (vk != 0) vks.push_back(vk);
    }
    if (vks.empty()) return;

    for (WORD vk : vks) {
        if ((vk == VK_VOLUME_UP || vk == VK_VOLUME_DOWN || vk == VK_VOLUME_MUTE) && cfg.volume_control_sound) {
            play_volume_sound(cfg.volume_sound_index);
        }
    }

    std::vector<INPUT> inputs;
    if (mode == 0 || mode == 1) {
        for (WORD vk : vks) {
            INPUT in = {};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = vk;
            if (vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END || vk == VK_HOME ||
                vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN ||
                vk == VK_INSERT || vk == VK_DELETE || vk == VK_LWIN || vk == VK_RWIN) {
                in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }
            inputs.push_back(in);
        }
    }
    if (mode == 0 || mode == 2) {
        for (auto it = vks.rbegin(); it != vks.rend(); ++it) {
            WORD vk = *it;
            INPUT in = {};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = vk;
            in.ki.dwFlags = KEYEVENTF_KEYUP;
            if (vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END || vk == VK_HOME ||
                vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN ||
                vk == VK_INSERT || vk == VK_DELETE || vk == VK_LWIN || vk == VK_RWIN) {
                in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }
            inputs.push_back(in);
        }
    }
    if (!inputs.empty()) {
        SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
    }
}

class BrightnessController {
    int current_pct_ = 100;
    bool modified_ = false;
public:
    int adjust(const std::string& dir, const AppConfig& cfg, OsdOverlayManager& osd) {
        std::string d = to_lower_ascii(dir);
        if (d == "up") current_pct_ = std::min(100, current_pct_ + 10);
        else if (d == "down") current_pct_ = std::max(0, current_pct_ - 10);
        else current_pct_ = 100;

        apply_gamma(current_pct_);
        modified_ = (current_pct_ != 100);

        std::wstring tip = cfg.tr("BrightnessTips", L"Current brightness {0}%");
        size_t pos = tip.find(L"{0}");
        if (pos != std::wstring::npos) {
            tip.replace(pos, 3, std::to_wstring(current_pct_));
        }
        osd.show_banner(tip, cfg, 1800, false);
        return current_pct_;
    }

    void restore_default() {
        if (modified_) {
            apply_gamma(100);
            modified_ = false;
        }
    }

private:
    static void apply_gamma(int pct) {
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            WORD ramp[3][256];
            double factor = std::clamp(pct, 10, 100) / 100.0;
            for (int i = 0; i < 256; i++) {
                WORD val = (WORD)std::clamp((int)(i * 256 * factor), 0, 65535);
                ramp[0][i] = ramp[1][i] = ramp[2][i] = val;
            }
            SetDeviceGammaRamp(hdc, ramp);
            ReleaseDC(nullptr, hdc);
        }
    }
};

struct AppRuntimeCallbacks {
    HWND main_hwnd = nullptr;
    bool* paused = nullptr;
    int settings_port = 0;
    std::wstring tempstr;
    POINT gesture_start_pt = {};
    POINT gesture_end_pt = {};
    RECT last_gesture_rect = {};
    bool has_gesture_rect = false;
    std::vector<HWND> hidden_tray_windows;
    BrightnessController brightness;
    void (*on_reload_config)() = nullptr;
    void (*on_open_settings)() = nullptr;
    void (*on_show_clipboard_menu)() = nullptr;
    void (*on_toggle_tray_icon)() = nullptr;
};

inline std::wstring expand_placeholders(
    const std::wstring& input,
    HWND target_hwnd,
    const AppRuntimeCallbacks& rt,
    bool auto_url_encode = false) {
    std::wstring s = input;
    bool is_http = auto_url_encode &&
                   (s.rfind(L"http://", 0) == 0 || s.rfind(L"https://", 0) == 0);

    auto encode_if_url = [&](const std::wstring& val) -> std::wstring {
        if (!is_http) return val;
        return utf8_to_wide(url_encode(wide_to_utf8(val)));
    };

    auto replace_all = [&](const std::wstring& from, const std::wstring& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::wstring::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    std::wstring app_dir = AppConfig::get_app_dir() + L"\\";
    std::wstring full_path = get_window_exe_path(target_hwnd);
    std::wstring file_name = get_window_exe_name(target_hwnd);
    std::wstring pid_str = std::to_wstring(get_window_pid(target_hwnd));
    std::wstring port_str = std::to_wstring(rt.settings_port);

    replace_all(L"%clipboard%", encode_if_url(read_clipboard_text()));
    replace_all(L"%tempstr%", encode_if_url(rt.tempstr));
    replace_all(L"%appdir%", app_dir);
    replace_all(L"%filepath%", full_path);
    replace_all(L"%fullpath%", full_path);
    replace_all(L"%filename%", file_name);
    replace_all(L"%pid%", pid_str);
    replace_all(L"%port%", port_str);

    wchar_t expanded[4096] = {0};
    if (ExpandEnvironmentStringsW(s.c_str(), expanded, 4096) > 0) {
        s = expanded;
    }
    return s;
}

inline HBITMAP fetch_qrcode_bitmap(const std::string& utf8_text) {
    std::string url = "https://api.qrserver.com/v1/create-qr-code/?margin=10&size=260x260&data=" + url_encode(utf8_text);
    HINTERNET hInet = InternetOpenW(L"MouseInc/2.13.4", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hInet) return nullptr;
    HINTERNET hUrl = InternetOpenUrlA(hInet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInet);
        return nullptr;
    }
    std::vector<BYTE> buf;
    BYTE chunk[4096];
    DWORD readBytes = 0;
    while (InternetReadFile(hUrl, chunk, sizeof(chunk), &readBytes) && readBytes > 0) {
        buf.insert(buf.end(), chunk, chunk + readBytes);
    }
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInet);
    if (buf.empty()) return nullptr;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, buf.size());
    if (!hMem) return nullptr;
    void* pLock = GlobalLock(hMem);
    if (pLock) {
        memcpy(pLock, buf.data(), buf.size());
        GlobalUnlock(hMem);
    }
    IStream* pStream = nullptr;
    HBITMAP hbmResult = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
        Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            bmp->GetHBITMAP(Gdiplus::Color(255, 255, 255, 255), &hbmResult);
        }
        delete bmp;
        pStream->Release();
    } else {
        GlobalFree(hMem);
    }
    return hbmResult;
}

inline void save_hbitmap_dialog(HBITMAP hbm, const std::wstring& default_name = L"MouseInc_Screenshot.png") {
    wchar_t file[MAX_PATH] = {0};
    wcsncpy_s(file, default_name.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) {
        Gdiplus::Bitmap bmp(hbm, nullptr);
        CLSID clsid;
        if (get_encoder_clsid(L"image/png", &clsid)) {
            bmp.Save(file, &clsid, nullptr);
        }
    }
}

inline void execute_action_command(
    const ActionCommand& cmd,
    HWND target_hwnd,
    AppConfig& cfg,
    OsdOverlayManager& osd,
    AppRuntimeCallbacks& rt) {
    if (cmd.empty()) return;
    std::string type = to_lower_ascii(cmd[0]);
    std::string arg1 = cmd.size() > 1 ? cmd[1] : "";
    std::string arg2 = cmd.size() > 2 ? cmd[2] : "";
    std::string arg3 = cmd.size() > 3 ? cmd[3] : "";

    if (!target_hwnd || !IsWindow(target_hwnd)) {
        target_hwnd = GetForegroundWindow();
    }
    HWND root_hwnd = GetAncestor(target_hwnd, GA_ROOT);
    if (!root_hwnd) root_hwnd = target_hwnd;

    if (type == "sendkeys") {
        // Activate target window unless "NOACTIVATE" is specified (docs/docs/action.md line 165 & 196)
        if (to_lower_ascii(arg2) != "noactivate" && root_hwnd && root_hwnd != GetForegroundWindow()) {
            SetForegroundWindow(root_hwnd);
            Sleep(15);
        }
        send_key_chord(arg1, cfg, 0);
    } else if (type == "sendkeydown") {
        send_key_chord(arg1, cfg, 1);
    } else if (type == "sendkeyup") {
        send_key_chord(arg1, cfg, 2);
    } else if (type == "sendclick") {
        std::string btn = to_lower_ascii(arg1);
        INPUT in[2] = {};
        in[0].type = INPUT_MOUSE;
        in[1].type = INPUT_MOUSE;
        if (btn == "right") {
            in[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            in[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        } else if (btn == "middle") {
            in[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            in[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
        } else if (btn == "x1") {
            in[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
            in[0].mi.mouseData = XBUTTON1;
            in[1].mi.dwFlags = MOUSEEVENTF_XUP;
            in[1].mi.mouseData = XBUTTON1;
        } else if (btn == "x2") {
            in[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
            in[0].mi.mouseData = XBUTTON2;
            in[1].mi.dwFlags = MOUSEEVENTF_XUP;
            in[1].mi.mouseData = XBUTTON2;
        } else {
            in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        }
        SendInput(2, in, sizeof(INPUT));
    } else if (type == "mousemove") {
        auto resolve_coord = [&](const std::string& token, int fallback) -> int {
            std::string low = to_lower_ascii(token);
            if (low == "start_x") return rt.gesture_start_pt.x;
            if (low == "start_y") return rt.gesture_start_pt.y;
            if (low == "end_x") return rt.gesture_end_pt.x;
            if (low == "end_y") return rt.gesture_end_pt.y;
            return token.empty() ? fallback : std::atoi(token.c_str());
        };
        POINT cur = {};
        GetCursorPos(&cur);
        int x = resolve_coord(arg1, cur.x);
        int y = resolve_coord(arg2, cur.y);
        SetCursorPos(x, y);
    } else if (type == "activate") {
        if (root_hwnd) SetForegroundWindow(root_hwnd);
    } else if (type == "postmessage") {
        UINT m = (UINT)std::strtoul(arg1.c_str(), nullptr, 0);
        WPARAM wp = (WPARAM)std::strtoull(arg2.c_str(), nullptr, 0);
        LPARAM lp = (LPARAM)std::strtoull(arg3.c_str(), nullptr, 0);
        if (target_hwnd) PostMessageW(target_hwnd, m, wp, lp);
    } else if (type == "sendmessage") {
        UINT m = (UINT)std::strtoul(arg1.c_str(), nullptr, 0);
        WPARAM wp = (WPARAM)std::strtoull(arg2.c_str(), nullptr, 0);
        LPARAM lp = (LPARAM)std::strtoull(arg3.c_str(), nullptr, 0);
        if (target_hwnd) SendMessageTimeoutW(target_hwnd, m, wp, lp, SMTO_ABORTIFHUNG, 1000, nullptr);
    } else if (type == "setclipboard") {
        std::wstring text = expand_placeholders(utf8_to_wide(arg1), target_hwnd, rt, false);
        copy_text_to_clipboard(text);
    } else if (type == "getclipboard") {
        std::string sub = to_lower_ascii(arg1);
        if (sub == "stick") {
            SnapshotManager::stick_from_clipboard();
        } else if (sub == "tofile") {
            HBITMAP hbm = read_clipboard_bitmap_copy();
            if (hbm) {
                save_hbitmap_dialog(hbm);
                DeleteObject(hbm);
            }
        } else if (sub == "ocr") {
            HBITMAP hbm = read_clipboard_bitmap_copy();
            if (hbm) {
                std::string png = hbitmap_to_png_bytes(hbm);
                DeleteObject(hbm);
                std::wstring err_msg;
                std::wstring title = SnapshotManager::get_ocr_engine_title(cfg.ocr_service);
                std::wstring text = SnapshotManager::perform_ocr(png, cfg.ocr_service, &err_msg);
                if (!text.empty()) {
                    copy_text_to_clipboard(text);
                    osd.show_banner(cfg.tr("SuccessTips", L"OCR result copied to clipboard"), cfg, 2200, false, title);
                } else {
                    std::wstring fail = err_msg.empty() ? cfg.tr("FailedTips", L"OCR failed") : err_msg;
                    osd.show_banner(fail, cfg, 2800, false, title);
                }
            }
        }
    } else if (type == "execute" || type == "execute2") {
        std::wstring raw_cmd = expand_placeholders(utf8_to_wide(arg1), target_hwnd, rt, true);
        std::string flags = to_lower_ascii(arg2);
        bool run_admin = (type == "execute") && (flags.find("admin") != std::string::npos);
        bool wait_proc = (type == "execute") && (flags.find("wait") != std::string::npos);
        bool hide_wnd = (type == "execute") && (flags.find("hide") != std::string::npos);

        std::wstring file = raw_cmd;
        std::wstring params;
        if (raw_cmd.rfind(L"http://", 0) != 0 && raw_cmd.rfind(L"https://", 0) != 0) {
            int argc = 0;
            LPWSTR* argv = CommandLineToArgvW(raw_cmd.c_str(), &argc);
            if (argv && argc >= 1) {
                file = argv[0];
                size_t pos = raw_cmd.find(file);
                if (pos != std::wstring::npos) {
                    size_t after = pos + file.size();
                    if (after < raw_cmd.size() && raw_cmd[after] == L'"') after++;
                    while (after < raw_cmd.size() && raw_cmd[after] == L' ') after++;
                    params = raw_cmd.substr(after);
                }
                LocalFree(argv);
            }
        }

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = run_admin ? L"runas" : L"open";
        sei.lpFile = file.c_str();
        sei.lpParameters = params.empty() ? nullptr : params.c_str();
        sei.nShow = hide_wnd ? SW_HIDE : SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            if (wait_proc && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, INFINITE);
            }
            if (sei.hProcess) CloseHandle(sei.hProcess);
        }
    } else if (type == "window") {
        std::string sub = to_lower_ascii(arg1);
        HWND root = root_hwnd;
        if (sub == "close") {
            PostMessageW(root, WM_SYSCOMMAND, SC_CLOSE, 0);
        } else if (sub == "closesimilar") {
            wchar_t target_cls[128] = {0};
            if (root && GetClassNameW(root, target_cls, 128) > 0) {
                struct CloseClassCtx { std::wstring cls; };
                CloseClassCtx cctx{target_cls};
                EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                    if (!IsWindowVisible(h)) return TRUE;
                    auto* p = reinterpret_cast<CloseClassCtx*>(lp);
                    wchar_t c[128] = {0};
                    if (GetClassNameW(h, c, 128) > 0 && p->cls == c) {
                        PostMessageW(h, WM_SYSCOMMAND, SC_CLOSE, 0);
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&cctx));
            }
        } else if (sub == "maximize") {
            if (IsZoomed(root)) ShowWindow(root, SW_RESTORE);
            else ShowWindow(root, SW_MAXIMIZE);
        } else if (sub == "minimize") {
            ShowWindow(root, SW_MINIMIZE);
        } else if (sub == "center") {
            RECT rc;
            if (GetWindowRect(root, &rc)) {
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;
                int sw = GetSystemMetrics(SM_CXSCREEN);
                int sh = GetSystemMetrics(SM_CYSCREEN);
                SetWindowPos(root, nullptr, (sw - w) / 2, (sh - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
        } else if (sub == "top") {
            LONG_PTR ex = GetWindowLongPtrW(root, GWL_EXSTYLE);
            bool is_top = (ex & WS_EX_TOPMOST) != 0;
            SetWindowPos(root, is_top ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        } else if (sub == "hidetray") {
            if (root && IsWindowVisible(root)) {
                ShowWindow(root, SW_HIDE);
                rt.hidden_tray_windows.push_back(root);
            }
        } else if (sub == "showtray") {
            for (HWND h : rt.hidden_tray_windows) {
                if (IsWindow(h)) {
                    ShowWindow(h, SW_SHOW);
                    SetForegroundWindow(h);
                }
            }
            rt.hidden_tray_windows.clear();
        } else if (sub == "toggletray") {
            if (!rt.hidden_tray_windows.empty()) {
                for (HWND h : rt.hidden_tray_windows) {
                    if (IsWindow(h)) {
                        ShowWindow(h, SW_SHOW);
                        SetForegroundWindow(h);
                    }
                }
                rt.hidden_tray_windows.clear();
            } else if (root && IsWindowVisible(root)) {
                ShowWindow(root, SW_HIDE);
                rt.hidden_tray_windows.push_back(root);
            }
        }
    } else if (type == "internal") {
        std::string sub = to_lower_ascii(arg1);
        if (sub == "exit") {
            PostQuitMessage(0);
        } else if (sub == "pause") {
            if (rt.paused) *rt.paused = !(*rt.paused);
        } else if (sub == "icon") {
            if (rt.on_toggle_tray_icon) rt.on_toggle_tray_icon();
        } else if (sub == "clipboardmenu") {
            if (rt.on_show_clipboard_menu) rt.on_show_clipboard_menu();
        } else if (sub == "reload") {
            if (rt.on_reload_config) rt.on_reload_config();
        } else if (sub == "settings") {
            if (rt.on_open_settings) rt.on_open_settings();
        } else if (sub == "config") {
            // ["Internal", "Config", "Keycast.Open", "true"]
            if (!arg2.empty() && !arg3.empty()) {
                mini_json::Value parsed_val = mini_json::parse(arg3);
                size_t dot = arg2.find('.');
                if (dot == std::string::npos) {
                    cfg.raw_json[arg2] = parsed_val;
                } else {
                    std::string k1 = arg2.substr(0, dot);
                    std::string k2 = arg2.substr(dot + 1);
                    cfg.raw_json[k1][k2] = parsed_val;
                }
                cfg.save();
                if (rt.on_reload_config) rt.on_reload_config();
            }
        } else if (sub == "exclude") {
            std::wstring exe = get_window_exe_name(target_hwnd);
            if (!exe.empty()) {
                std::wstring tip = cfg.tr("ExcludeTips", L"Exclude {0}?");
                size_t p = tip.find(L"{0}");
                if (p != std::wstring::npos) tip.replace(p, 3, exe);
                if (MessageBoxW(nullptr, tip.c_str(), L"MouseInc", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES) {
                    cfg.excludes.push_back(to_lower_wide(exe));
                    if (cfg.raw_json["Excludes"].is_array()) {
                        cfg.raw_json["Excludes"].arr_val.push_back(mini_json::Value(wide_to_utf8(exe)));
                        cfg.save();
                    }
                }
            }
        } else if (sub == "delay") {
            Sleep((DWORD)std::max(0, std::atoi(arg2.c_str())));
        } else if (sub == "showtips" || sub == "print") {
            if (sub == "showtips" && !arg3.empty()) {
                std::wstring title = expand_placeholders(utf8_to_wide(arg2), target_hwnd, rt, false);
                std::wstring msg = expand_placeholders(utf8_to_wide(arg3), target_hwnd, rt, false);
                osd.show_banner(msg, cfg, 3000, false, title);
            } else {
                std::wstring msg = expand_placeholders(utf8_to_wide(arg2), target_hwnd, rt, false);
                osd.show_banner(msg, cfg, 3000, false, L"MouseInc");
            }
        } else if (sub == "tempstr") {
            // Save previous clipboard text, send Ctrl+C, store into rt.tempstr, and restore clipboard!
            std::wstring prev_clip = read_clipboard_text();
            if (root_hwnd && root_hwnd != GetForegroundWindow()) {
                SetForegroundWindow(root_hwnd);
                Sleep(15);
            }
            send_key_chord("Ctrl+C", cfg, 0);
            Sleep(65);
            rt.tempstr = read_clipboard_text();
            if (!prev_clip.empty()) {
                copy_text_to_clipboard(prev_clip);
            }
        }
    } else if (type == "algorithm") {
        std::string alg = to_lower_ascii(arg1);
        std::wstring raw_in = expand_placeholders(utf8_to_wide(arg2.empty() ? "%clipboard%" : arg2), target_hwnd, rt, false);
        std::string utf8_in = wide_to_utf8(raw_in);
        std::string out;

        if (alg == "b64encode") out = base64_encode(utf8_in);
        else if (alg == "b64decode") out = base64_decode(utf8_in);
        else if (alg == "urlencode") out = url_encode(utf8_in);
        else if (alg == "urldecode") out = url_decode(utf8_in);
        else if (alg == "md5") out = md5_hex(utf8_in);
        else if (alg == "sha1") out = sha1_hex(utf8_in);
        else if (alg == "sha256") out = sha256_hex(utf8_in);
        else if (alg == "cyberchef") {
            std::string url = "https://gchq.github.io/CyberChef/#input=" + url_encode(base64_encode(utf8_in));
            ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        } else if (alg == "qrcode") {
            // Generate QR code bitmap, copy to clipboard, AND display as a pinned Stick window!
            HBITMAP hbm = fetch_qrcode_bitmap(utf8_in);
            if (hbm) {
                copy_hbitmap_to_clipboard(hbm);
                int sw = GetSystemMetrics(SM_CXSCREEN);
                int sh = GetSystemMetrics(SM_CYSCREEN);
                SnapshotManager::create_stick_window(hbm, (sw - 260) / 2, (sh - 260) / 2, 260, 260);
            } else {
                std::string url = "https://api.qrserver.com/v1/create-qr-code/?size=260x260&data=" + url_encode(utf8_in);
                ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return;
        }

        if (!out.empty()) {
            copy_text_to_clipboard(utf8_to_wide(out));
            osd.show_banner(cfg.tr("SuccessTips", L"Copied to clipboard"), cfg, 2200, false);
        }
    } else if (type == "explorer") {
        std::string sub = to_lower_ascii(arg1);
        if (sub == "select") {
            std::wstring path = expand_placeholders(utf8_to_wide(arg2.empty() ? "%fullpath%" : arg2), target_hwnd, rt, false);
            if (!path.empty()) {
                PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
                if (pidl) {
                    SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
                    ILFree(pidl);
                }
            }
        } else if (sub == "recyclebin") {
            DWORD flags = 0;
            std::string opts = to_lower_ascii(arg2);
            if (opts.find("noconfirmation") != std::string::npos) flags |= SHERB_NOCONFIRMATION;
            if (opts.find("noprogressui") != std::string::npos) flags |= SHERB_NOPROGRESSUI;
            SHEmptyRecycleBinW(nullptr, nullptr, flags);
        } else if (sub == "turnoffmonitor") {
            SendMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
        } else if (sub == "toggledesktopicons") {
            HWND hProgman = FindWindowW(L"Progman", nullptr);
            HWND hDefView = FindWindowExW(hProgman, nullptr, L"SHELLDLL_DefView", nullptr);
            if (!hDefView) {
                EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                    HWND dv = FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr);
                    if (dv) {
                        *reinterpret_cast<HWND*>(lp) = dv;
                        return FALSE;
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&hDefView));
            }
            if (hDefView) {
                SendMessageW(hDefView, WM_COMMAND, 0x7402, 0);
            }
        }
    } else if (type == "setbrightness") {
        rt.brightness.adjust(arg1, cfg, osd);
    } else if (type == "screenshot" || type == "snapshot" || type == "screenshothq") {
        std::string mode = to_lower_ascii(arg1);
        std::string flags = to_lower_ascii(arg2);
        bool draw_cursor = flags.find("drawcursor") != std::string::npos;

        RECT rc = {};
        HWND underlying = root_hwnd;

        if (type == "snapshot") {
            // Snapshot opens the interactive crosshair region selector (docs/docs/action.md line 373)
            if (!select_screen_region(rc, underlying)) return;
        } else if (type == "screenshothq") {
            // ScreenshotHQ captures the active/foreground window (docs/docs/action.md line 448)
            HWND fg = GetForegroundWindow();
            if (fg) underlying = GetAncestor(fg, GA_ROOT);
            if (!underlying || !GetWindowRect(underlying, &rc)) return;
        } else {
            // Screenshot automatically uses the bounding box of the drawn mouse gesture (docs/docs/action.md line 292)
            if (rt.has_gesture_rect &&
                (rt.last_gesture_rect.right - rt.last_gesture_rect.left) >= 16 &&
                (rt.last_gesture_rect.bottom - rt.last_gesture_rect.top) >= 16) {
                rc = rt.last_gesture_rect;
            } else if (!underlying || !GetWindowRect(underlying, &rc)) {
                return;
            }
        }

        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w < 4 || h < 4) return;

        if (mode == "pip") {
            SnapshotManager::create_pip_window(underlying, rc);
            return;
        }
        HBITMAP hbm = SnapshotManager::capture_screen_rect(rc, draw_cursor);
        if (!hbm) return;

        if (mode == "stick" || mode == "reference") {
            SnapshotManager::create_stick_window(hbm, rc.left, rc.top, w, h);
        } else if (mode == "ocr") {
            std::string png = hbitmap_to_png_bytes(hbm);
            DeleteObject(hbm);
            std::wstring err_msg;
            std::wstring title = SnapshotManager::get_ocr_engine_title(cfg.ocr_service);
            std::wstring text = SnapshotManager::perform_ocr(png, cfg.ocr_service, &err_msg);
            if (!text.empty()) {
                copy_text_to_clipboard(text);
                osd.show_banner(cfg.tr("SuccessTips", L"OCR result copied to clipboard"), cfg, 2200, false, title);
            } else {
                std::wstring fail = err_msg.empty() ? cfg.tr("FailedTips", L"OCR failed") : err_msg;
                osd.show_banner(fail, cfg, 2800, false, title);
            }
        } else if (mode == "tofile") {
            save_hbitmap_dialog(hbm);
            DeleteObject(hbm);
        } else {
            copy_hbitmap_to_clipboard(hbm);
            DeleteObject(hbm);
            osd.show_banner(cfg.tr("SuccessTips", L"Screenshot copied to clipboard"), cfg, 2000, false);
        }
    } else if (type == "regset") {
        std::wstring key_full = utf8_to_wide(arg1);
        std::wstring val_name = utf8_to_wide(arg2);
        std::wstring val_data = utf8_to_wide(arg3);
        HKEY root = HKEY_CURRENT_USER;
        size_t slash = key_full.find(L'\\');
        if (slash != std::wstring::npos) {
            std::wstring prefix = to_lower_wide(key_full.substr(0, slash));
            std::wstring subkey = key_full.substr(slash + 1);
            if (prefix == L"hklm" || prefix == L"hkey_local_machine") root = HKEY_LOCAL_MACHINE;
            else if (prefix == L"hkcr" || prefix == L"hkey_classes_root") root = HKEY_CLASSES_ROOT;
            else if (prefix == L"hku" || prefix == L"hkey_users") root = HKEY_USERS;
            else if (prefix == L"hkcc" || prefix == L"hkey_current_config") root = HKEY_CURRENT_CONFIG;
            HKEY hKey = nullptr;
            if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
                if (cmd.size() <= 3 || arg3.empty()) {
                    RegDeleteValueW(hKey, val_name.c_str());
                } else {
                    RegSetValueExW(hKey, val_name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(val_data.c_str()), (DWORD)((val_data.size() + 1) * sizeof(wchar_t)));
                }
                RegCloseKey(hKey);
            }
        }
    }
}

inline void execute_action_list(
    const ActionList& list,
    HWND target_hwnd,
    AppConfig& cfg,
    OsdOverlayManager& osd,
    AppRuntimeCallbacks& rt) {
    for (const auto& cmd : list) {
        execute_action_command(cmd, target_hwnd, cfg, osd, rt);
    }
}

} // namespace mouseinc
