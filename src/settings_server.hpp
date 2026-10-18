#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <process.h>
#include <cmath>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include "config.hpp"
#include "crypto_utils.hpp"

namespace mouseinc {

class LocalSettingsServer {
    SOCKET listen_sock_ = INVALID_SOCKET;
    int port_ = 0;
    AppConfig* cfg_ = nullptr;
    HWND main_hwnd_ = nullptr;
    UINT reload_msg_ = 0;
    std::string cached_settings_html_;

    static void load_embedded_settings_html(std::string& out_html) {
        HMODULE hMod = GetModuleHandleW(nullptr);
        HRSRC hRes = FindResourceW(hMod, L"SETTINGS", L"HTML");
        if (!hRes) hRes = FindResourceW(hMod, L"SETTINGS", RT_HTML);
        if (hRes) {
            HGLOBAL hMem = LoadResource(hMod, hRes);
            DWORD sz = SizeofResource(hMod, hRes);
            if (hMem && sz > 0) {
                const char* ptr = static_cast<const char*>(LockResource(hMem));
                if (ptr) {
                    out_html.assign(ptr, sz);
                }
            }
        }
    }

    static std::string build_placeholder_svg_uri() {
        std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" width=\"64\" height=\"64\">"
            "<rect x=\"6\" y=\"6\" width=\"88\" height=\"88\" rx=\"16\" fill=\"none\" stroke=\"#61afef\" stroke-width=\"5\" stroke-dasharray=\"8 6\"/>"
            "<circle cx=\"50\" cy=\"50\" r=\"8\" fill=\"#e47542\"/>"
            "</svg>";
        return "data:image/svg+xml;base64," + base64_encode(svg);
    }

    static std::string build_gesture_svg_uri(const std::vector<int>& data) {
        if (data.size() < 2) return build_placeholder_svg_uri();
        std::vector<std::pair<double, double>> pts;
        bool is_packed = false;
        for (int v : data) {
            if (v > 255) { is_packed = true; break; }
        }
        double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
        if (is_packed) {
            for (int v : data) {
                double x = (double)((v >> 8) & 0xFF);
                double y = (double)(v & 0xFF);
                pts.push_back({x, y});
                if (x < min_x) min_x = x;
                if (y < min_y) min_y = y;
                if (x > max_x) max_x = x;
                if (y > max_y) max_y = y;
            }
        } else {
            for (size_t i = 0; i + 1 < data.size(); i += 2) {
                double x = (double)data[i];
                double y = (double)data[i + 1];
                pts.push_back({x, y});
                if (x < min_x) min_x = x;
                if (y < min_y) min_y = y;
                if (x > max_x) max_x = x;
                if (y > max_y) max_y = y;
            }
        }
        if (pts.size() < 2) return build_placeholder_svg_uri();

        // Normalize into [16..84] box inside 100x100 viewBox so all gestures look centered & crisp
        double span_x = std::max(1.0, max_x - min_x);
        double span_y = std::max(1.0, max_y - min_y);
        double span = std::max(span_x, span_y);
        double scale = (span > 1.0) ? (66.0 / span) : 1.0;
        double cx = (min_x + max_x) * 0.5;
        double cy = (min_y + max_y) * 0.5;

        for (auto& p : pts) {
            p.first = 50.0 + (p.first - cx) * scale;
            p.second = 50.0 + (p.second - cy) * scale;
        }

        std::ostringstream ss;
        ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" width=\"64\" height=\"64\">";
        ss << "<polyline fill=\"none\" stroke=\"#E47542\" stroke-width=\"6.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";
        for (size_t i = 0; i < pts.size(); i++) {
            if (i > 0) ss << " ";
            ss << (int)std::round(pts[i].first) << "," << (int)std::round(pts[i].second);
        }
        ss << "\"/>";

        // Draw start round anchor dot
        ss << "<circle cx=\"" << (int)std::round(pts[0].first)
           << "\" cy=\"" << (int)std::round(pts[0].second)
           << "\" r=\"5.5\" fill=\"#E47542\"/>";

        // Draw directional arrowhead at the end
        size_t n = pts.size();
        size_t prev_idx = (n >= 2) ? (n - 2) : 0;
        double dx = pts[n - 1].first - pts[prev_idx].first;
        double dy = pts[n - 1].second - pts[prev_idx].second;
        if (std::hypot(dx, dy) < 1.0 && n >= 2) {
            dx = pts[n - 1].first - pts[0].first;
            dy = pts[n - 1].second - pts[0].second;
        }
        if (std::hypot(dx, dy) >= 1.0) {
            double angle = std::atan2(dy, dx);
            double tip_x = pts[n - 1].first + 3.0 * std::cos(angle);
            double tip_y = pts[n - 1].second + 3.0 * std::sin(angle);
            double len = 15.0;
            double wing = 0.55;
            double x1 = tip_x - len * std::cos(angle - wing);
            double y1 = tip_y - len * std::sin(angle - wing);
            double x2 = tip_x - len * std::cos(angle + wing);
            double y2 = tip_y - len * std::sin(angle + wing);
            ss << "<polygon fill=\"#E47542\" points=\""
               << (int)std::round(tip_x) << "," << (int)std::round(tip_y) << " "
               << (int)std::round(x1) << "," << (int)std::round(y1) << " "
               << (int)std::round(x2) << "," << (int)std::round(y2) << "\"/>";
        }
        ss << "</svg>";
        return "data:image/svg+xml;base64," + base64_encode(ss.str());
    }

    mini_json::Value build_ws_settings_payload(const std::string& type) const {
        mini_json::Value gestures_map;
        gestures_map.type = mini_json::Type::Object;
        gestures_map["placeholder"] = build_placeholder_svg_uri();

        if (cfg_) {
            if (cfg_->raw_json.contains("Gestures") && cfg_->raw_json["Gestures"].is_array()) {
                for (const auto& g : cfg_->raw_json["Gestures"].arr_val) {
                    std::string sign = g["Sign"].get_string();
                    std::vector<int> data;
                    if (g.contains("Data") && g["Data"].is_array()) {
                        for (const auto& dv : g["Data"].arr_val) {
                            data.push_back(dv.get_int());
                        }
                    }
                    if (!sign.empty()) {
                        gestures_map[sign] = build_gesture_svg_uri(data);
                    }
                }
            } else {
                for (const auto& gt : cfg_->gestures) {
                    if (!gt.sign.empty()) {
                        gestures_map[gt.sign] = build_gesture_svg_uri(gt.data);
                    }
                }
            }
        }

        int is_admin = IsUserAnAdmin() ? 1 : 0;

        mini_json::Value data_obj;
        data_obj.type = mini_json::Type::Object;
        data_obj["version"] = "2.13.4";
        data_obj["admin"] = is_admin;
        data_obj["cfg"] = cfg_ ? cfg_->raw_json : mini_json::Value();
        data_obj["gestures"] = gestures_map;

        mini_json::Value resp;
        resp.type = mini_json::Type::Object;
        resp["type"] = type;
        resp["data"] = data_obj;
        return resp;
    }

    static std::string compute_ws_accept(const std::string& client_key) {
        std::string raw = client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        std::string sha1_bytes;
        if (CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
                if (CryptHashData(hHash, reinterpret_cast<const BYTE*>(raw.data()), (DWORD)raw.size(), 0)) {
                    BYTE buf[20] = {0};
                    DWORD len = 20;
                    if (CryptGetHashParam(hHash, HP_HASHVAL, buf, &len, 0)) {
                        sha1_bytes.assign(reinterpret_cast<const char*>(buf), len);
                    }
                }
                CryptDestroyHash(hHash);
            }
            CryptReleaseContext(hProv, 0);
        }
        return base64_encode(sha1_bytes);
    }

    static bool send_all(SOCKET s, const char* data, int total_len) {
        int sent = 0;
        while (sent < total_len) {
            int n = send(s, data + sent, total_len - sent, 0);
            if (n <= 0) return false;
            sent += n;
        }
        return true;
    }

    static bool recv_exact(SOCKET s, unsigned char* buf, size_t len) {
        size_t got = 0;
        while (got < len) {
            int r = recv(s, reinterpret_cast<char*>(buf + got), (int)(len - got), 0);
            if (r <= 0) return false;
            got += (size_t)r;
        }
        return true;
    }

    static void ws_send_text(SOCKET s, const std::string& payload) {
        std::vector<unsigned char> frame;
        frame.push_back(0x81); // FIN + Text
        if (payload.size() < 126) {
            frame.push_back((unsigned char)payload.size());
        } else if (payload.size() <= 0xFFFF) {
            frame.push_back(126);
            frame.push_back((unsigned char)((payload.size() >> 8) & 0xFF));
            frame.push_back((unsigned char)(payload.size() & 0xFF));
        } else {
            frame.push_back(127);
            uint64_t len = payload.size();
            for (int i = 7; i >= 0; i--) {
                frame.push_back((unsigned char)((len >> (i * 8)) & 0xFF));
            }
        }
        frame.insert(frame.end(), payload.begin(), payload.end());
        send_all(s, reinterpret_cast<const char*>(frame.data()), (int)frame.size());
    }

    void handle_ws_message(SOCKET s, const std::string& msg_text) {
        if (!cfg_) return;
        auto req = mini_json::parse(msg_text);
        std::string type = req["type"].get_string();
        if (type == "load_settings") {
            auto resp = build_ws_settings_payload("load_settings");
            ws_send_text(s, mini_json::dump(resp, 0));
        } else if (type == "save_settings") {
            if (req.contains("config")) {
                std::string dumped;
                if (req["config"].is_string()) {
                    dumped = req["config"].get_string();
                } else if (req["config"].is_object()) {
                    dumped = mini_json::dump(req["config"], 1);
                }
                if (!dumped.empty()) {
                    cfg_->load_from_json_string(dumped);
                    cfg_->save();
                    if (main_hwnd_ && reload_msg_) PostMessageW(main_hwnd_, reload_msg_, 0, 0);
                    auto resp = build_ws_settings_payload("save_settings");
                    ws_send_text(s, mini_json::dump(resp, 0));
                }
            }
        } else if (type == "reset_settings") {
            if (!cfg_->raw_default_json_text.empty()) {
                cfg_->load_from_json_string(cfg_->raw_default_json_text);
                cfg_->save();
                if (main_hwnd_ && reload_msg_) PostMessageW(main_hwnd_, reload_msg_, 0, 0);
                auto resp = build_ws_settings_payload("reset_settings");
                ws_send_text(s, mini_json::dump(resp, 0));
            }
        }
    }

    void handle_client(SOCKET c) {
        char buf[8192];
        int n = recv(c, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            closesocket(c);
            return;
        }
        buf[n] = '\0';
        std::string req(buf, n);

        // Check WebSocket upgrade
        size_t key_pos = req.find("Sec-WebSocket-Key:");
        if (key_pos == std::string::npos) {
            key_pos = req.find("sec-websocket-key:");
        }
        if (key_pos != std::string::npos) {
            size_t line_end = req.find("\r\n", key_pos);
            std::string key = req.substr(key_pos + 18, line_end - (key_pos + 18));
            while (!key.empty() && key.front() == ' ') key.erase(key.begin());
            while (!key.empty() && (key.back() == ' ' || key.back() == '\r')) key.pop_back();
            std::string accept = compute_ws_accept(key);

            std::string hs =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
            send_all(c, hs.data(), (int)hs.size());

            // WebSocket frame loop (handles fragmentation + large payloads reliably)
            std::string message_buffer;
            int message_opcode = 0;
            while (true) {
                unsigned char hdr[2];
                if (!recv_exact(c, hdr, 2)) break;
                bool fin = (hdr[0] & 0x80) != 0;
                int opcode = hdr[0] & 0x0F;
                if (opcode == 8) break; // CLOSE
                bool masked = (hdr[1] & 0x80) != 0;
                uint64_t len = hdr[1] & 0x7F;
                if (len == 126) {
                    unsigned char ext[2];
                    if (!recv_exact(c, ext, 2)) break;
                    len = ((uint64_t)ext[0] << 8) | ext[1];
                } else if (len == 127) {
                    unsigned char ext[8];
                    if (!recv_exact(c, ext, 8)) break;
                    len = 0;
                    for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
                }
                unsigned char mask[4] = {0};
                if (masked && !recv_exact(c, mask, 4)) break;
                if (len > 16 * 1024 * 1024) break;
                std::string payload((size_t)len, '\0');
                if (len > 0 && !recv_exact(c, reinterpret_cast<unsigned char*>(&payload[0]), (size_t)len)) break;
                if (masked) {
                    for (size_t i = 0; i < payload.size(); i++) {
                        payload[i] ^= mask[i % 4];
                    }
                }
                if (opcode == 9) { // PING -> PONG
                    unsigned char pong[2] = { 0x8A, 0x00 };
                    send_all(c, reinterpret_cast<const char*>(pong), 2);
                    continue;
                }
                if (opcode != 0) {
                    message_opcode = opcode;
                    message_buffer = std::move(payload);
                } else {
                    message_buffer += payload;
                }
                if (fin && message_opcode == 1) {
                    handle_ws_message(c, message_buffer);
                    message_buffer.clear();
                }
            }
            closesocket(c);
            return;
        }

        // Standard HTTP GET -> serve embedded MouseInc.Settings (Vue 3 + Element Plus) single-file HTML
        const std::string& html = cached_settings_html_;
        std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-cache\r\n"
            "Content-Length: " + std::to_string(html.size()) + "\r\n"
            "Connection: close\r\n\r\n";
        send_all(c, resp.data(), (int)resp.size());
        if (!html.empty()) {
            send_all(c, html.data(), (int)html.size());
        }
        closesocket(c);
    }

    struct ClientWorkerArgs {
        LocalSettingsServer* self;
        SOCKET client;
    };

    static unsigned __stdcall ClientThreadProc(void* param) {
        auto* args = reinterpret_cast<ClientWorkerArgs*>(param);
        LocalSettingsServer* self = args->self;
        SOCKET c = args->client;
        delete args;
        self->handle_client(c);
        return 0;
    }

    static unsigned __stdcall ServerThreadProc(void* param) {
        auto* self = reinterpret_cast<LocalSettingsServer*>(param);
        while (self->listen_sock_ != INVALID_SOCKET) {
            SOCKET c = accept(self->listen_sock_, nullptr, nullptr);
            if (c == INVALID_SOCKET) break;
            auto* args = new ClientWorkerArgs{ self, c };
            uintptr_t th = _beginthreadex(nullptr, 0, ClientThreadProc, args, 0, nullptr);
            if (th) {
                CloseHandle((HANDLE)th);
            } else {
                delete args;
                closesocket(c);
            }
        }
        return 0;
    }

    static std::wstring find_chromium_browser_exe() {
        const wchar_t* candidates[] = {
            L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
            L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
            L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
            L"C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe"
        };
        for (const wchar_t* p : candidates) {
            if (PathFileExistsW(p)) return p;
        }
        wchar_t buf[MAX_PATH] = {0};
        DWORD sz = sizeof(buf);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe", nullptr, RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS && PathFileExistsW(buf)) {
            return buf;
        }
        sz = sizeof(buf);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe", nullptr, RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS && PathFileExistsW(buf)) {
            return buf;
        }
        return L"";
    }

public:
    void start(AppConfig* cfg, HWND main_hwnd, UINT reload_msg) {
        cfg_ = cfg;
        main_hwnd_ = main_hwnd;
        reload_msg_ = reload_msg;

        load_embedded_settings_html(cached_settings_html_);

        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);

        listen_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_sock_ == INVALID_SOCKET) return;

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // ephemeral port on 127.0.0.1:0 just like original MouseInc.exe
        if (bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            int len = sizeof(addr);
            if (getsockname(listen_sock_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
                port_ = ntohs(addr.sin_port);
            }
            listen(listen_sock_, 16);
            uintptr_t th = _beginthreadex(nullptr, 0, ServerThreadProc, this, 0, nullptr);
            if (th) CloseHandle((HANDLE)th);
        }
    }

    int get_port() const {
        return port_;
    }

    void open_settings_ui() const {
        if (port_ > 0) {
            std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port_) + L"/?port=" + std::to_wstring(port_);
            std::wstring browser = find_chromium_browser_exe();
            if (!browser.empty()) {
                std::wstring args = L"--app=" + url + L" --window-size=1180,780 --disable-extensions --disable-features=TranslateUI";
                ShellExecuteW(nullptr, L"open", browser.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
            } else {
                ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        } else {
            std::wstring cfg_path = AppConfig::get_config_path();
            ShellExecuteW(nullptr, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOWNORMAL);
        }
    }
};

} // namespace mouseinc
