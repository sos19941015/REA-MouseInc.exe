#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>
#include "json_mini.hpp"

namespace mouseinc {

inline std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], len);
    return wstr;
}

inline std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string str(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], len, nullptr, nullptr);
    return str;
}

inline std::wstring to_lower_wide(std::wstring s) {
    for (auto& ch : s) ch = (wchar_t)towlower(ch);
    return s;
}

inline std::string to_lower_ascii(std::string s) {
    for (auto& ch : s) ch = (char) std::tolower((unsigned char)ch);
    return s;
}

inline DWORD parse_argb_color(const std::string& hex, BYTE default_alpha = 224) {
    std::string s = hex;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    unsigned long val = 0;
    try {
        val = std::stoul(s, nullptr, 16);
    } catch (...) {
        return ((DWORD)default_alpha << 24) | 0x00E47542;
    }
    if (s.size() <= 6) {
        return ((DWORD)default_alpha << 24) | (val & 0x00FFFFFF);
    }
    return (DWORD)val;
}

using ActionCommand = std::vector<std::string>;
using ActionList = std::vector<ActionCommand>;

struct MenuItemConfig {
    std::wstring name;
    bool valid = true;
    ActionList actions;
};

struct GestureTemplate {
    std::string sign;
    std::vector<int> data; // x0, y0, x1, y1, ... in 0..100
};

struct GestureRule {
    std::wstring name;
    std::string sign;
    bool valid = true;
    ActionList actions;
};

struct CustomMatchGroup {
    std::wstring name;
    bool ignore_global = false;
    std::vector<std::wstring> match_exes;
    std::vector<GestureRule> list;
};

struct HotCornerSide {
    std::wstring name;
    bool valid = false;
    ActionList actions;
};

struct HotkeyRule {
    std::wstring name;
    std::string keys;
    bool valid = true;
    ActionList actions;
};

struct WheelEdgeSide {
    std::wstring name;
    bool valid = false;
    ActionList up_actions;
    ActionList down_actions;
    ActionList press_actions;
};

struct AppConfig {
    mini_json::Value raw_json;
    std::string raw_default_json_text;

    bool administrator = false;
    bool alt_drag = true;
    bool auto_clip = false;
    bool caps_lock_led = false;
    bool caps_unlock = false;
    bool fast_paste = false;
    bool ignore_full_screen = true;
    bool key_sound = false;
    int key_sound_index = 0;
    int ocr_service = 0;
    bool quick_jump = false;
    bool show_ime = false;
    bool show_tray_icon = true;
    bool volume_control_sound = true;
    int volume_sound_index = 0;
    bool wheel_alt_control = true;
    bool wheel_natural = false;
    bool wheel_through = false;

    // ClipboardManager
    bool clipboard_manager_open = true;
    std::vector<MenuItemConfig> clipboard_menu;

    // Excludes
    std::vector<std::wstring> excludes;

    // Gestures templates
    std::vector<GestureTemplate> gestures;

    // MatchGlobal & MatchCustom
    std::vector<GestureRule> match_global;
    std::vector<CustomMatchGroup> match_custom;

    // MouseGesture settings
    struct {
        bool add_mode = false;
        DWORD draw_color = 0xE0E47542;
        bool draw_result = true;
        bool draw_trace = true;
        DWORD fail_color = 0xE0CAD0D3;
        int font_size = 26;
        int offset = 150;
        bool open = true;
        bool rand_color = false;
        bool restore_event = false;
        int sensitive = 50;
        int start_distance = 5;
        int timeout = 1000;
        bool trace_arrow = true;
        int trace_width = 3;
        bool wheel_switch = true;
    } mouse_gesture;

    // Keycast settings
    struct {
        DWORD background_color = 0xB4353535;
        int fade = 5;
        int font_size = 24;
        bool ignore_single = false;
        bool open = false;
        int space = 10;
        DWORD text_color = 0xFFFFFFFF;
        DWORD text_shadow_color = 0xB4141414;
        int x = 40;
        int y = 84;
    } keycast;

    // HotCorner
    bool hot_corner_open = false;
    HotCornerSide hot_corner_top_left;
    HotCornerSide hot_corner_top_right;
    HotCornerSide hot_corner_bottom_left;
    HotCornerSide hot_corner_bottom_right;

    // Hotkeys
    std::vector<HotkeyRule> hotkeys;

    // WheelEdge
    bool wheel_edge_open = true;
    WheelEdgeSide wheel_edge_left;
    WheelEdgeSide wheel_edge_right;
    WheelEdgeSide wheel_edge_top;
    WheelEdgeSide wheel_edge_bottom;

    // Locales
    std::string language = "auto";
    std::map<std::string, std::map<std::string, std::wstring>> locales;

    static ActionList parse_action_list(const mini_json::Value& v) {
        ActionList list;
        if (!v.is_array()) return list;
        for (const auto& cmd_val : v.arr_val) {
            if (!cmd_val.is_array()) continue;
            ActionCommand cmd;
            for (const auto& arg : cmd_val.arr_val) {
                if (arg.is_string()) cmd.push_back(arg.str_val);
                else if (arg.is_number()) cmd.push_back(std::to_string(arg.get_int()));
            }
            if (!cmd.empty()) list.push_back(std::move(cmd));
        }
        return list;
    }

    static HotCornerSide parse_hot_corner_side(const mini_json::Value& v) {
        HotCornerSide side;
        if (!v.is_object()) return side;
        side.name = utf8_to_wide(v["Name"].get_string());
        side.valid = v["Valid"].get_bool(false);
        side.actions = parse_action_list(v["Actions"]);
        return side;
    }

    static WheelEdgeSide parse_wheel_edge_side(const mini_json::Value& v) {
        WheelEdgeSide side;
        if (!v.is_object()) return side;
        side.name = utf8_to_wide(v["Name"].get_string());
        side.valid = v["Valid"].get_bool(false);
        side.up_actions = parse_action_list(v["UpActions"]);
        side.down_actions = parse_action_list(v["DownActions"]);
        side.press_actions = parse_action_list(v["PressActions"]);
        return side;
    }

    static std::string load_embedded_default_json() {
        HRSRC hRes = FindResourceW(GetModuleHandleW(nullptr), L"DEFAULT", L"JSON");
        if (!hRes) return "";
        HGLOBAL hMem = LoadResource(GetModuleHandleW(nullptr), hRes);
        if (!hMem) return "";
        DWORD sz = SizeofResource(GetModuleHandleW(nullptr), hRes);
        const char* ptr = (const char*)LockResource(hMem);
        if (!ptr || sz == 0) return "";
        return std::string(ptr, sz);
    }

    static std::wstring get_app_dir() {
        wchar_t path[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        PathRemoveFileSpecW(path);
        return path;
    }

    static std::wstring get_config_path() {
        return get_app_dir() + L"\\MouseInc.json";
    }

    bool load_from_json_string(const std::string& json_str) {
        mini_json::Value root = mini_json::parse(json_str);
        if (!root.is_object()) return false;
        raw_json = root;

        administrator = root["Administrator"].get_bool(false);
        alt_drag = root["AltDrag"].get_bool(true);
        auto_clip = root["AutoClip"].get_bool(false);
        caps_lock_led = root["CapsLockLed"].get_bool(false);
        caps_unlock = root["CapsUnlock"].get_bool(false);
        fast_paste = root["FastPaste"].get_bool(false);
        ignore_full_screen = root["IgnoreFullScreen"].get_bool(true);
        key_sound = root["KeySound"].get_bool(false);
        key_sound_index = root["KeySoundIndex"].get_int(0);
        ocr_service = root["OcrService"].get_int(0);
        quick_jump = root["QuickJump"].get_bool(false);
        show_ime = root["ShowIme"].get_bool(false);
        show_tray_icon = root["ShowTrayIcon"].get_bool(true);
        volume_control_sound = root["VolumeControlSound"].get_bool(true);
        volume_sound_index = root["VolumeSoundIndex"].get_int(0);
        wheel_alt_control = root["WheelAltControl"].get_bool(true);
        wheel_natural = root["WheelNatural"].get_bool(false);
        wheel_through = root["WheelThrough"].get_bool(false);
        language = root["Language"].get_string("auto");

        // ClipboardManager
        clipboard_menu.clear();
        const auto& cm = root["ClipboardManager"];
        if (cm.is_object()) {
            clipboard_manager_open = cm["Open"].get_bool(true);
            const auto& menu = cm["Menu"];
            if (menu.is_array()) {
                for (const auto& item : menu.arr_val) {
                    MenuItemConfig mic;
                    mic.name = utf8_to_wide(item["Name"].get_string());
                    mic.valid = item["Valid"].get_bool(true);
                    mic.actions = parse_action_list(item["Actions"]);
                    clipboard_menu.push_back(std::move(mic));
                }
            }
        }

        // Excludes
        excludes.clear();
        const auto& ex = root["Excludes"];
        if (ex.is_array()) {
            for (const auto& item : ex.arr_val) {
                if (item.is_string()) excludes.push_back(to_lower_wide(utf8_to_wide(item.str_val)));
            }
        }

        // Gestures
        gestures.clear();
        const auto& gs = root["Gestures"];
        if (gs.is_array()) {
            for (const auto& g : gs.arr_val) {
                GestureTemplate gt;
                gt.sign = g["Sign"].get_string();
                const auto& d = g["Data"];
                if (d.is_array()) {
                    for (const auto& pt : d.arr_val) gt.data.push_back(pt.get_int());
                }
                if (!gt.sign.empty() && gt.data.size() >= 4) {
                    gestures.push_back(std::move(gt));
                }
            }
        }

        // MatchGlobal
        match_global.clear();
        const auto& mg = root["MatchGlobal"];
        if (mg.is_array()) {
            for (const auto& r : mg.arr_val) {
                GestureRule gr;
                gr.name = utf8_to_wide(r["Name"].get_string());
                gr.sign = r["Sign"].get_string();
                gr.valid = r["Valid"].get_bool(true);
                gr.actions = parse_action_list(r["Actions"]);
                match_global.push_back(std::move(gr));
            }
        }

        // MatchCustom
        match_custom.clear();
        const auto& mc = root["MatchCustom"];
        if (mc.is_array()) {
            for (const auto& grp : mc.arr_val) {
                CustomMatchGroup cmg;
                cmg.name = utf8_to_wide(grp["Name"].get_string());
                cmg.ignore_global = grp["IgnoreGlobal"].get_bool(false);
                const auto& mlist = grp["Match"];
                if (mlist.is_array()) {
                    for (const auto& m : mlist.arr_val) {
                        if (m.is_string()) cmg.match_exes.push_back(to_lower_wide(utf8_to_wide(m.str_val)));
                    }
                }
                const auto& l = grp["List"];
                if (l.is_array()) {
                    for (const auto& r : l.arr_val) {
                        GestureRule gr;
                        gr.name = utf8_to_wide(r["Name"].get_string());
                        gr.sign = r["Sign"].get_string();
                        gr.valid = r["Valid"].get_bool(true);
                        gr.actions = parse_action_list(r["Actions"]);
                        cmg.list.push_back(std::move(gr));
                    }
                }
                match_custom.push_back(std::move(cmg));
            }
        }

        // MouseGesture
        const auto& mcfg = root["MouseGesture"];
        if (mcfg.is_object()) {
            mouse_gesture.add_mode = mcfg["AddMode"].get_bool(false);
            mouse_gesture.draw_color = parse_argb_color(mcfg["DrawColor"].get_string("#E47542"), 0xE0);
            mouse_gesture.draw_result = mcfg["DrawResult"].get_bool(true);
            mouse_gesture.draw_trace = mcfg["DrawTrace"].get_bool(true);
            mouse_gesture.fail_color = parse_argb_color(mcfg["FailColor"].get_string("#CAD0D3"), 0xE0);
            mouse_gesture.font_size = mcfg["FontSize"].get_int(26);
            mouse_gesture.offset = mcfg["Offset"].get_int(150);
            mouse_gesture.open = mcfg["Open"].get_bool(true);
            mouse_gesture.rand_color = mcfg["RandColor"].get_bool(false);
            mouse_gesture.restore_event = mcfg["RestoreEvent"].get_bool(false);
            mouse_gesture.sensitive = mcfg["Sensitive"].get_int(50);
            mouse_gesture.start_distance = mcfg["StartDistance"].get_int(5);
            mouse_gesture.timeout = mcfg["Timeout"].get_int(1000);
            mouse_gesture.trace_arrow = mcfg["TraceArrow"].get_bool(true);
            mouse_gesture.trace_width = mcfg["TraceWidth"].get_int(3);
            mouse_gesture.wheel_switch = mcfg["WheelSwitch"].get_bool(true);
        }

        // Keycast
        const auto& kcfg = root["Keycast"];
        if (kcfg.is_object()) {
            keycast.background_color = parse_argb_color(kcfg["BackgroundColor"].get_string("#353535"), 0xB4);
            keycast.fade = kcfg["Fade"].get_int(5);
            keycast.font_size = kcfg["FontSize"].get_int(24);
            keycast.ignore_single = kcfg["IgnoreSingle"].get_bool(false);
            keycast.open = kcfg["Open"].get_bool(false);
            keycast.space = kcfg["Space"].get_int(10);
            keycast.text_color = parse_argb_color(kcfg["TextColor"].get_string("#FFFFFF"), 0xFF);
            keycast.text_shadow_color = parse_argb_color(kcfg["TextShadowColor"].get_string("#141414"), 0xB4);
            keycast.x = kcfg["X"].get_int(40);
            keycast.y = kcfg["Y"].get_int(84);
        }

        // HotCorner
        const auto& hc = root["HotCorner"];
        if (hc.is_object()) {
            hot_corner_open = hc["Open"].get_bool(false);
            hot_corner_top_left = parse_hot_corner_side(hc["TopLeft"]);
            hot_corner_top_right = parse_hot_corner_side(hc["TopRight"]);
            hot_corner_bottom_left = parse_hot_corner_side(hc["BottomLeft"]);
            hot_corner_bottom_right = parse_hot_corner_side(hc["BottomRight"]);
        }

        // Hotkeys
        hotkeys.clear();
        const auto& hks = root["Hotkeys"];
        if (hks.is_array()) {
            for (const auto& hk : hks.arr_val) {
                HotkeyRule hr;
                hr.name = utf8_to_wide(hk["Name"].get_string());
                hr.keys = hk["Keys"].get_string();
                hr.valid = hk["Valid"].get_bool(true);
                hr.actions = parse_action_list(hk["Actions"]);
                hotkeys.push_back(std::move(hr));
            }
        }

        // WheelEdge
        const auto& we = root["WheelEdge"];
        if (we.is_object()) {
            wheel_edge_open = we["Open"].get_bool(true);
            wheel_edge_left = parse_wheel_edge_side(we["Left"]);
            wheel_edge_right = parse_wheel_edge_side(we["Right"]);
            wheel_edge_top = parse_wheel_edge_side(we["Top"]);
            wheel_edge_bottom = parse_wheel_edge_side(we["Bottom"]);
        }

        // Locales
        locales.clear();
        const auto& locs = root["Locales"];
        if (locs.is_object()) {
            for (const auto& lang_kv : locs.obj_val) {
                if (!lang_kv.second.is_object()) continue;
                auto& table = locales[lang_kv.first];
                for (const auto& str_kv : lang_kv.second.obj_val) {
                    table[str_kv.first] = utf8_to_wide(str_kv.second.get_string());
                }
            }
        }
        return true;
    }

    bool load() {
        raw_default_json_text = load_embedded_default_json();
        std::wstring cfg_path = get_config_path();
        if (!PathFileExistsW(cfg_path.c_str()) && !raw_default_json_text.empty()) {
            std::ofstream out(cfg_path, std::ios::binary);
            if (out) {
                out.write(raw_default_json_text.data(), raw_default_json_text.size());
            }
        }
        std::ifstream in(cfg_path, std::ios::binary);
        std::string content;
        if (in) {
            content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        if (content.empty()) content = raw_default_json_text;
        return load_from_json_string(content);
    }

    bool save() const {
        std::wstring cfg_path = get_config_path();
        std::ofstream out(cfg_path, std::ios::binary);
        if (!out) return false;
        std::string dumped = mini_json::dump(raw_json, 1);
        out.write(dumped.data(), dumped.size());
        return true;
    }

    std::string active_locale_key() const {
        if (language != "auto" && locales.count(language)) return language;
        LANGID lid = GetUserDefaultUILanguage();
        WORD prim = PRIMARYLANGID(lid);
        WORD sub = SUBLANGID(lid);
        if (prim == LANG_CHINESE) {
            if (sub == SUBLANG_CHINESE_TRADITIONAL || sub == SUBLANG_CHINESE_HONGKONG || sub == SUBLANG_CHINESE_MACAU) {
                if (locales.count("zh-TW")) return "zh-TW";
            }
            if (locales.count("zh-CN")) return "zh-CN";
        }
        if (locales.count("en-US")) return "en-US";
        if (!locales.empty()) return locales.begin()->first;
        return "zh-TW";
    }

    std::wstring tr(const std::string& key, const std::wstring& fallback = L"") const {
        std::string lk = active_locale_key();
        auto it = locales.find(lk);
        if (it != locales.end()) {
            auto sit = it->second.find(key);
            if (sit != it->second.end()) return sit->second;
        }
        return fallback.empty() ? utf8_to_wide(key) : fallback;
    }

    bool is_excluded(const std::wstring& exe_name) const {
        std::wstring lower = to_lower_wide(exe_name);
        for (const auto& ex : excludes) {
            if (ex == lower) return true;
        }
        return false;
    }
};

} // namespace mouseinc
