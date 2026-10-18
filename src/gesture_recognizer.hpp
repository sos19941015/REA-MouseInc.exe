#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "config.hpp"

namespace mouseinc {

struct PointF2D {
    double x = 0.0;
    double y = 0.0;
};

class GestureRecognizer {
public:
    static double path_length(const std::vector<PointF2D>& pts) {
        double d = 0.0;
        for (size_t i = 1; i < pts.size(); i++) {
            d += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        }
        return d;
    }

    static std::vector<PointF2D> resample(const std::vector<PointF2D>& pts, int n = 32) {
        if (pts.empty()) return {};
        if (pts.size() == 1) return std::vector<PointF2D>(n, pts[0]);
        double total = path_length(pts);
        if (total < 1e-5) return std::vector<PointF2D>(n, pts[0]);

        double interval = total / (n - 1);
        double D = 0.0;
        std::vector<PointF2D> src = pts;
        std::vector<PointF2D> out;
        out.reserve(n);
        out.push_back(src[0]);

        for (size_t i = 1; i < src.size(); i++) {
            double dx = src[i].x - src[i - 1].x;
            double dy = src[i].y - src[i - 1].y;
            double d = std::hypot(dx, dy);
            if (D + d >= interval && d > 1e-6) {
                double qx = src[i - 1].x + ((interval - D) / d) * dx;
                double qy = src[i - 1].y + ((interval - D) / d) * dy;
                PointF2D q{qx, qy};
                out.push_back(q);
                src.insert(src.begin() + i, q);
                D = 0.0;
                if ((int)out.size() == n - 1) break;
            } else {
                D += d;
            }
        }
        while ((int)out.size() < n) {
            out.push_back(pts.back());
        }
        return out;
    }

    static std::vector<PointF2D> normalize_100(const std::vector<PointF2D>& pts) {
        if (pts.empty()) return {};
        double min_x = pts[0].x, max_x = pts[0].x;
        double min_y = pts[0].y, max_y = pts[0].y;
        for (const auto& p : pts) {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
        double w = max_x - min_x;
        double h = max_y - min_y;
        double span = std::max({w, h, 1.0});
        std::vector<PointF2D> out;
        out.reserve(pts.size());
        for (const auto& p : pts) {
            double nx = (w / span < 0.28) ? ((p.x - min_x) / span * 100.0) : ((p.x - min_x) / std::max(w, 1.0) * 100.0);
            double ny = (h / span < 0.28) ? ((p.y - min_x * 0.0 - min_y) / span * 100.0) : ((p.y - min_y) / std::max(h, 1.0) * 100.0);
            out.push_back({nx, ny});
        }
        return out;
    }

    // Quantize raw drawn points into a compact MouseInc.json Data array [x0, y0, x1, y1, ...] in 0..100
    static std::vector<int> encode_to_template_data(const std::vector<POINT>& raw_points) {
        std::vector<PointF2D> fpts;
        for (const auto& p : raw_points) fpts.push_back({(double)p.x, (double)p.y});
        auto norm = normalize_100(resample(fpts, 16));
        std::vector<int> data;
        for (const auto& p : norm) {
            data.push_back((int)std::llround(std::clamp(p.x, 0.0, 100.0)));
            data.push_back((int)std::llround(std::clamp(p.y, 0.0, 100.0)));
        }
        return data;
    }

    // Dynamic Time Warping distance between two normalized polylines
    static double dtw_distance(const std::vector<PointF2D>& a, const std::vector<PointF2D>& b) {
        int n = (int)a.size();
        int m = (int)b.size();
        if (n == 0 || m == 0) return 1e18;
        std::vector<double> dp((n + 1) * (m + 1), 1e18);
        dp[0] = 0.0;
        int window = std::max(6, std::abs(n - m) + 6);
        for (int i = 1; i <= n; i++) {
            int j_start = std::max(1, i - window);
            int j_end = std::min(m, i + window);
            for (int j = j_start; j <= j_end; j++) {
                double cost = std::hypot(a[i - 1].x - b[j - 1].x, a[i - 1].y - b[j - 1].y);
                double prev = std::min({
                    dp[(i - 1) * (m + 1) + j],
                    dp[i * (m + 1) + (j - 1)],
                    dp[(i - 1) * (m + 1) + (j - 1)]
                });
                dp[i * (m + 1) + j] = cost + prev;
            }
        }
        return dp[n * (m + 1) + m] / std::max(n, m);
    }

    static std::string classify_direction_chain(const std::vector<POINT>& raw_points) {
        if (raw_points.size() < 2) return "";
        std::vector<std::string> dirs;
        POINT anchor = raw_points[0];
        for (size_t i = 1; i < raw_points.size(); i++) {
            double dx = raw_points[i].x - anchor.x;
            double dy = raw_points[i].y - anchor.y;
            double dist = std::hypot(dx, dy);
            if (dist < 24.0) continue;
            double angle = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
            std::string seg;
            if (dirs.empty() && i + 3 >= raw_points.size()) {
                if (angle >= -67.5 && angle < -22.5) seg = "/ UP";
                else if (angle >= 22.5 && angle < 67.5) seg = "\\ DOWN";
                else if (angle >= 112.5 && angle < 157.5) seg = "/ DOWN";
                else if (angle >= -157.5 && angle < -112.5) seg = "\\ UP";
            }
            if (seg.empty()) {
                if (angle >= -45.0 && angle < 45.0) seg = "RIGHT";
                else if (angle >= 45.0 && angle < 135.0) seg = "DOWN";
                else if (angle >= -135.0 && angle < -45.0) seg = "UP";
                else seg = "LEFT";
            }
            if (dirs.empty() || dirs.back() != seg) {
                dirs.push_back(seg);
            }
            anchor = raw_points[i];
        }
        if (dirs.empty()) {
            double dx = raw_points.back().x - raw_points.front().x;
            double dy = raw_points.back().y - raw_points.front().y;
            if (std::hypot(dx, dy) < 6.0) return "";
            double angle = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
            if (angle >= -67.5 && angle < -22.5) return "/ UP";
            if (angle >= 22.5 && angle < 67.5) return "\\ DOWN";
            if (angle >= 112.5 && angle < 157.5) return "/ DOWN";
            if (angle >= -157.5 && angle < -112.5) return "\\ UP";
            if (angle >= -45.0 && angle < 45.0) return "RIGHT";
            if (angle >= 45.0 && angle < 135.0) return "DOWN";
            if (angle >= -135.0 && angle < -45.0) return "UP";
            return "LEFT";
        }
        if (dirs.size() == 1) {
            double dx = raw_points.back().x - raw_points.front().x;
            double dy = raw_points.back().y - raw_points.front().y;
            double angle = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
            if (angle >= -65.0 && angle < -25.0) return "/ UP";
            if (angle >= 25.0 && angle < 65.0) return "\\ DOWN";
            if (angle >= 115.0 && angle < 155.0) return "/ DOWN";
            if (angle >= -155.0 && angle < -115.0) return "\\ UP";
            return dirs[0];
        }
        if (dirs.size() <= 3) {
            std::string joined = dirs[0];
            for (size_t i = 1; i < dirs.size(); i++) {
                if (dirs[i].find(' ') != std::string::npos) continue;
                joined += "-" + dirs[i];
            }
            return joined;
        }
        if (dirs.size() == 4) {
            if (dirs[0] == "DOWN" && dirs[1] == "RIGHT" && dirs[2] == "UP" && dirs[3] == "LEFT") return "SQUARE";
            if (dirs[0] == "DOWN" && dirs[1] == "LEFT" && dirs[2] == "UP" && dirs[3] == "RIGHT") return "SQUARE 2";
        }
        return "";
    }

    static std::string recognize(const std::vector<POINT>& raw_points, const AppConfig& cfg) {
        if (raw_points.size() < 2) return "";

        std::vector<PointF2D> user_pts;
        user_pts.reserve(raw_points.size());
        for (const auto& p : raw_points) {
            user_pts.push_back({(double)p.x, (double)p.y});
        }
        if (path_length(user_pts) < (double)std::max(cfg.mouse_gesture.start_distance, 5)) {
            return "";
        }

        std::string dir_sign = classify_direction_chain(raw_points);

        constexpr int kSampleCount = 32;
        auto user_norm = normalize_100(resample(user_pts, kSampleCount));

        std::string best_sign;
        double best_score = 1e18;

        for (const auto& tmpl : cfg.gestures) {
            std::vector<PointF2D> tpts;
            for (size_t i = 0; i + 1 < tmpl.data.size(); i += 2) {
                tpts.push_back({(double)tmpl.data[i], (double)tmpl.data[i + 1]});
            }
            auto tmpl_norm = normalize_100(resample(tpts, kSampleCount));
            if (tmpl_norm.size() != user_norm.size()) continue;

            double sum_euclid = 0.0;
            for (int i = 0; i < kSampleCount; i++) {
                sum_euclid += std::hypot(user_norm[i].x - tmpl_norm[i].x, user_norm[i].y - tmpl_norm[i].y);
            }
            double avg_euclid = sum_euclid / kSampleCount;
            double dtw = dtw_distance(user_norm, tmpl_norm);
            double combined = avg_euclid * 0.55 + dtw * 0.45;

            if (combined < best_score) {
                best_score = combined;
                best_sign = tmpl.sign;
            }
        }

        double threshold = 22.0 + (100.0 - std::clamp(cfg.mouse_gesture.sensitive, 0, 100)) * 0.28;
        if (!dir_sign.empty()) {
            for (const auto& tmpl : cfg.gestures) {
                if (tmpl.sign == dir_sign && best_score > 16.0) {
                    return dir_sign;
                }
            }
        }
        if (best_score <= threshold) {
            return best_sign;
        }
        return dir_sign;
    }

    static const GestureRule* match_rule(const std::string& sign, const std::wstring& fg_exe_name, const AppConfig& cfg) {
        if (sign.empty()) return nullptr;
        std::wstring lower_exe = to_lower_wide(fg_exe_name);
        bool ignore_global = false;

        for (const auto& grp : cfg.match_custom) {
            bool matched_exe = false;
            for (const auto& m : grp.match_exes) {
                if (m == lower_exe) {
                    matched_exe = true;
                    break;
                }
            }
            if (!matched_exe) continue;
            if (grp.ignore_global) ignore_global = true;
            for (const auto& r : grp.list) {
                if (r.valid && r.sign == sign) {
                    return &r;
                }
            }
        }

        if (!ignore_global) {
            for (const auto& r : cfg.match_global) {
                if (r.valid && r.sign == sign) {
                    return &r;
                }
            }
        }
        return nullptr;
    }
};

// Native AddMode Dialog ("NewGesture") for recording and saving a new gesture into MouseInc.json
struct NewGestureDialogContext {
    std::vector<int> data;
    AppConfig* cfg = nullptr;
    HWND hEdit = nullptr;
    std::wstring entered_name;
    bool accepted = false;
};

inline LRESULT CALLBACK NewGestureDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<NewGestureDialogContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            ctx = reinterpret_cast<NewGestureDialogContext*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

            std::wstring desc = ctx->cfg->tr("NewGestureDesc", L"Don't repeat existing gesture");
            std::wstring lbl = ctx->cfg->tr("NewGestureName", L"Name:");
            std::wstring okTxt = ctx->cfg->tr("NewGestureOK", L"OK");

            HWND hDesc = CreateWindowExW(0, L"STATIC", desc.c_str(), WS_CHILD | WS_VISIBLE, 20, 155, 260, 22, hwnd, nullptr, cs->hInstance, nullptr);
            HWND hLbl = CreateWindowExW(0, L"STATIC", lbl.c_str(), WS_CHILD | WS_VISIBLE, 20, 185, 60, 24, hwnd, nullptr, cs->hInstance, nullptr);
            ctx->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 85, 182, 195, 24, hwnd, (HMENU)301, cs->hInstance, nullptr);
            HWND hBtn = CreateWindowExW(0, L"BUTTON", okTxt.c_str(), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 100, 220, 100, 30, hwnd, (HMENU)IDOK, cs->hInstance, nullptr);

            SendMessageW(hDesc, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(ctx->hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            SetFocus(ctx->hEdit);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK && ctx) {
                wchar_t buf[128] = {0};
                GetWindowTextW(ctx->hEdit, buf, 128);
                ctx->entered_name = buf;
                if (!ctx->entered_name.empty()) {
                    ctx->accepted = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (ctx && ctx->data.size() >= 4) {
                Gdiplus::Graphics g(hdc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::SolidBrush boxBg(Gdiplus::Color(255, 245, 247, 250));
                g.FillRectangle(&boxBg, 80, 15, 140, 130);
                Gdiplus::Pen borderPen(Gdiplus::Color(255, 210, 215, 220), 1.0f);
                g.DrawRectangle(&borderPen, 80, 15, 140, 130);

                Gdiplus::Pen strokePen(Gdiplus::Color(255, 228, 117, 66), 3.0f);
                strokePen.SetStartCap(Gdiplus::LineCapRound);
                Gdiplus::AdjustableArrowCap arrow(4.0f, 4.0f, TRUE);
                strokePen.SetCustomEndCap(&arrow);

                std::vector<Gdiplus::PointF> pts;
                for (size_t i = 0; i + 1 < ctx->data.size(); i += 2) {
                    float px = 95.0f + (ctx->data[i] / 100.0f) * 110.0f;
                    float py = 25.0f + (ctx->data[i + 1] / 100.0f) * 110.0f;
                    pts.emplace_back(px, py);
                }
                if (pts.size() >= 2) {
                    g.DrawLines(&strokePen, pts.data(), (INT)pts.size());
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

inline bool show_new_gesture_dialog(const std::vector<POINT>& raw_points, AppConfig& cfg) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool reg = false;
    if (!reg) {
        reg = true;
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = NewGestureDlgProc;
        wc.hInstance = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"MouseIncNewGestureDlg";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
    }

    NewGestureDialogContext ctx;
    ctx.data = GestureRecognizer::encode_to_template_data(raw_points);
    ctx.cfg = &cfg;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    std::wstring title = cfg.tr("NewGesture", L"Add new gesture");

    HWND hDlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        L"MouseIncNewGestureDlg", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        (sw - 310) / 2, (sh - 300) / 2, 310, 300,
        nullptr, nullptr, hInst, &ctx);
    SetForegroundWindow(hDlg);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(hDlg, WM_COMMAND, IDOK, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            DestroyWindow(hDlg);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (ctx.accepted && !ctx.entered_name.empty()) {
        std::string sign_utf8 = wide_to_utf8(ctx.entered_name);
        GestureTemplate gt;
        gt.sign = sign_utf8;
        gt.data = ctx.data;
        cfg.gestures.push_back(gt);

        mini_json::Value new_g;
        new_g["Sign"] = sign_utf8;
        mini_json::Value arr;
        arr.type = mini_json::Type::Array;
        for (int v : ctx.data) arr.arr_val.push_back(mini_json::Value(v));
        new_g["Data"] = arr;

        if (!cfg.raw_json["Gestures"].is_array()) {
            cfg.raw_json["Gestures"].type = mini_json::Type::Array;
        }
        cfg.raw_json["Gestures"].arr_val.push_back(new_g);
        cfg.mouse_gesture.add_mode = false;
        cfg.raw_json["MouseGesture"]["AddMode"] = mini_json::Value(false);
        cfg.save();
        return true;
    }
    return false;
}

} // namespace mouseinc
