#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "config.hpp"

namespace mouseinc {

inline Gdiplus::Bitmap* load_embedded_png_resource(const wchar_t* res_name) {
    HMODULE hMod = GetModuleHandleW(nullptr);
    HRSRC hRes = FindResourceW(hMod, res_name, L"PNG");
    if (!hRes) return nullptr;
    DWORD sz = SizeofResource(hMod, hRes);
    HGLOBAL hMem = LoadResource(hMod, hRes);
    if (!hMem || sz == 0) return nullptr;
    const void* pData = LockResource(hMem);
    if (!pData) return nullptr;

    HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!hCopy) return nullptr;
    void* pLock = GlobalLock(hCopy);
    if (pLock) {
        memcpy(pLock, pData, sz);
        GlobalUnlock(hCopy);
    }
    IStream* pStream = nullptr;
    Gdiplus::Bitmap* bmp = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(hCopy, TRUE, &pStream))) {
        bmp = Gdiplus::Bitmap::FromStream(pStream);
        pStream->Release();
    } else {
        GlobalFree(hCopy);
    }
    return bmp;
}

class OsdOverlayManager {
    HWND hwnd_trace_ = nullptr;
    HWND hwnd_banner_ = nullptr;
    HWND hwnd_ime_ = nullptr;
    HWND hwnd_caps_ = nullptr;
    Gdiplus::Bitmap* shadow_bmp_ = nullptr;

    // Persistent DIBSection for zero-allocation gesture trace rendering
    HDC trace_dc_ = nullptr;
    HBITMAP trace_bmp_ = nullptr;
    HBITMAP trace_old_bmp_ = nullptr;
    void* trace_bits_ = nullptr;
    int trace_w_ = 0;
    int trace_h_ = 0;
    DWORD current_rand_color_ = 0;

    void ensure_trace_surface(int vw, int vh) {
        if (trace_dc_ && trace_bmp_ && trace_w_ == vw && trace_h_ == vh) {
            memset(trace_bits_, 0, (size_t)vw * (size_t)vh * 4);
            return;
        }
        if (trace_dc_) {
            if (trace_old_bmp_) SelectObject(trace_dc_, trace_old_bmp_);
            if (trace_bmp_) DeleteObject(trace_bmp_);
            DeleteDC(trace_dc_);
            trace_dc_ = nullptr;
            trace_bmp_ = nullptr;
            trace_old_bmp_ = nullptr;
            trace_bits_ = nullptr;
        }
        HDC hdcScreen = GetDC(nullptr);
        trace_dc_ = CreateCompatibleDC(hdcScreen);
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = vw;
        bmi.bmiHeader.biHeight = -vh;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        trace_bmp_ = CreateDIBSection(trace_dc_, &bmi, DIB_RGB_COLORS, &trace_bits_, nullptr, 0);
        trace_old_bmp_ = (HBITMAP)SelectObject(trace_dc_, trace_bmp_);
        trace_w_ = vw;
        trace_h_ = vh;
        ReleaseDC(nullptr, hdcScreen);
    }

    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_TIMER) {
            if (wParam == 9001 || wParam == 9002 || wParam == 9003) {
                KillTimer(hwnd, wParam);
                if (wParam == 9002) KillTimer(hwnd, 9010);
                if (wParam == 9003) KillTimer(hwnd, 9011);
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            } else if (wParam == 9010) {
                if (!IsWindowVisible(hwnd)) {
                    KillTimer(hwnd, 9010);
                    return 0;
                }
                POINT pt;
                GetCursorPos(&pt);
                SetWindowPos(hwnd, HWND_TOPMOST, pt.x + 16, pt.y + 16, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                return 0;
            } else if (wParam == 9011) {
                if (!IsWindowVisible(hwnd)) {
                    KillTimer(hwnd, 9011);
                    return 0;
                }
                POINT pt;
                GetCursorPos(&pt);
                HWND hIme = FindWindowW(L"MouseIncOverlayWnd", L"MouseIncImeTip");
                int offsetY = (hIme && IsWindowVisible(hIme)) ? 54 : 16;
                SetWindowPos(hwnd, HWND_TOPMOST, pt.x + 16, pt.y + offsetY, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

public:
    void init(HINSTANCE hInst) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"MouseIncOverlayWnd";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);

        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        hwnd_trace_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"MouseIncOverlayWnd", L"MouseIncTrace",
            WS_POPUP, vx, vy, vw, vh,
            nullptr, nullptr, hInst, nullptr);

        hwnd_banner_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"MouseIncOverlayWnd", L"MouseIncBanner",
            WS_POPUP, 0, 0, 400, 100,
            nullptr, nullptr, hInst, nullptr);

        hwnd_ime_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"MouseIncOverlayWnd", L"MouseIncImeTip",
            WS_POPUP, 0, 0, 80, 48,
            nullptr, nullptr, hInst, nullptr);

        hwnd_caps_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"MouseIncOverlayWnd", L"MouseIncCapsLed",
            WS_POPUP, 0, 0, 160, 48,
            nullptr, nullptr, hInst, nullptr);

        shadow_bmp_ = load_embedded_png_resource(L"SHADOW");
        ensure_trace_surface(vw, vh);
    }

    Gdiplus::Bitmap* get_shadow_bitmap() const {
        return shadow_bmp_;
    }

    void reset_gesture_color(const AppConfig& cfg) {
        if (cfg.mouse_gesture.rand_color) {
            current_rand_color_ = 0xE8000000 | ((rand() % 180 + 50) << 16) | ((rand() % 180 + 50) << 8) | (rand() % 180 + 50);
        } else {
            current_rand_color_ = cfg.mouse_gesture.draw_color;
        }
    }

    static Gdiplus::GraphicsPath* create_round_rect(Gdiplus::RectF r, float radius) {
        auto* path = new Gdiplus::GraphicsPath();
        float d = radius * 2.0f;
        path->AddArc(r.X, r.Y, d, d, 180, 90);
        path->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
        path->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
        path->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
        path->CloseFigure();
        return path;
    }

    void render_gesture_trace(
        const std::vector<POINT>& points,
        const std::wstring& result_text,
        bool matched,
        const AppConfig& cfg) {
        if (!hwnd_trace_) return;
        if (!cfg.mouse_gesture.draw_trace && !cfg.mouse_gesture.draw_result) return;

        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);

        ensure_trace_surface(vw, vh);
        if (!trace_dc_) return;

        DWORD argb = (cfg.mouse_gesture.rand_color && current_rand_color_ != 0)
            ? current_rand_color_
            : cfg.mouse_gesture.draw_color;

        {
            Gdiplus::Graphics g(trace_dc_);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            if (cfg.mouse_gesture.draw_trace && points.size() >= 2) {
                Gdiplus::Color penColor((Gdiplus::ARGB)argb);
                float tw = (float)std::max(1, cfg.mouse_gesture.trace_width);
                Gdiplus::Pen pen(penColor, tw);
                pen.SetLineJoin(Gdiplus::LineJoinRound);
                pen.SetStartCap(Gdiplus::LineCapRoundAnchor);
                if (cfg.mouse_gesture.trace_arrow) {
                    Gdiplus::AdjustableArrowCap arrowCap(4.2f, 4.0f, TRUE);
                    pen.SetCustomEndCap(&arrowCap);
                } else {
                    pen.SetEndCap(Gdiplus::LineCapRound);
                }

                std::vector<Gdiplus::Point> gpts;
                gpts.reserve(points.size());
                for (const auto& p : points) {
                    gpts.emplace_back(p.x - vx, p.y - vy);
                }
                if (gpts.size() >= 3) {
                    g.DrawCurve(&pen, gpts.data(), (INT)gpts.size(), 0.25f);
                } else {
                    g.DrawLines(&pen, gpts.data(), (INT)gpts.size());
                }
            }

            // Original MouseInc.exe style (docs/image/3.png):
            // Large outlined/stroked text (white interior + thick DrawColor/FailColor outline),
            // positioned at (sw / 2, sh - Offset) from the bottom of the screen!
            if (cfg.mouse_gesture.draw_result && !result_text.empty() && !points.empty()) {
                Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
                if (!fontFamily.IsAvailable()) {
                    Gdiplus::FontFamily fallback(L"Microsoft YaHei");
                }
                Gdiplus::StringFormat fmt;
                fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                float emSize = (float)std::max(14, cfg.mouse_gesture.font_size) * 1.45f;
                float cx = (float)(sw / 2 - vx);
                float cy = (float)(sh - std::clamp(cfg.mouse_gesture.offset, 30, sh - 60) - vy);

                Gdiplus::RectF textRect(cx - 600.0f, cy - 80.0f, 1200.0f, 160.0f);
                Gdiplus::GraphicsPath textPath;
                textPath.AddString(
                    result_text.c_str(),
                    -1,
                    &fontFamily,
                    Gdiplus::FontStyleBold,
                    emSize,
                    textRect,
                    &fmt);

                DWORD outlineArgb = matched ? argb : cfg.mouse_gesture.fail_color;
                Gdiplus::Color outlineColor((Gdiplus::ARGB)outlineArgb);
                float strokeWidth = std::max(5.0f, emSize * 0.20f);
                Gdiplus::Pen outlinePen(outlineColor, strokeWidth);
                outlinePen.SetLineJoin(Gdiplus::LineJoinRound);
                g.DrawPath(&outlinePen, &textPath);

                Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 255, 255, 255));
                g.FillPath(&fillBrush, &textPath);
            }
        }

        HDC hdcScreen = GetDC(nullptr);
        POINT ptSrc = { 0, 0 };
        POINT ptDst = { vx, vy };
        SIZE szWnd = { vw, vh };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

        UpdateLayeredWindow(hwnd_trace_, hdcScreen, &ptDst, &szWnd, trace_dc_, &ptSrc, 0, &blend, ULW_ALPHA);
        if (!IsWindowVisible(hwnd_trace_)) {
            ShowWindow(hwnd_trace_, SW_SHOWNOACTIVATE);
        }
        ReleaseDC(nullptr, hdcScreen);
    }

    void hide_gesture_trace() {
        if (hwnd_trace_ && IsWindowVisible(hwnd_trace_)) {
            ShowWindow(hwnd_trace_, SW_HIDE);
        }
    }

    void show_banner(
        const std::wstring& text,
        const AppConfig& cfg,
        int duration_ms = 2500,
        bool use_keycast_pos = false,
        const std::wstring& custom_title = L"MouseInc") {
        if (!hwnd_banner_ || text.empty()) return;

        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);

        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        int maxW = 900, maxH = 180;
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = maxW;
        bmi.bmiHeader.biHeight = -maxH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pvBits = nullptr;
        HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);

        int wndW = 300, wndH = 72;
        int posX = 0, posY = 0;

        if (use_keycast_pos) {
            // Keycast OSD style
            int font_sz = cfg.keycast.font_size;
            DWORD bg = cfg.keycast.background_color;
            DWORD fg = cfg.keycast.text_color;
            DWORD shd = cfg.keycast.text_shadow_color;

            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
            Gdiplus::Font font(&fontFamily, (Gdiplus::REAL)font_sz, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::RectF layoutRect(0, 0, (Gdiplus::REAL)maxW, (Gdiplus::REAL)maxH);
            Gdiplus::RectF bound;
            g.MeasureString(text.c_str(), -1, &font, layoutRect, &fmt, &bound);

            wndW = std::min(maxW, (int)std::ceil(bound.Width + 36.0f));
            wndH = std::min(maxH, (int)std::ceil(bound.Height + 22.0f));

            Gdiplus::RectF pill(1.0f, 1.0f, (Gdiplus::REAL)(wndW - 2), (Gdiplus::REAL)(wndH - 2));
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color((Gdiplus::ARGB)bg));
            Gdiplus::SolidBrush shdBrush(Gdiplus::Color((Gdiplus::ARGB)shd));
            Gdiplus::SolidBrush fgBrush(Gdiplus::Color((Gdiplus::ARGB)fg));

            auto* path = create_round_rect(pill, 10.0f);
            g.FillPath(&bgBrush, path);
            delete path;

            Gdiplus::RectF shadowRect = pill;
            shadowRect.X += 1.5f;
            shadowRect.Y += 1.5f;
            g.DrawString(text.c_str(), -1, &font, shadowRect, &fmt, &shdBrush);
            g.DrawString(text.c_str(), -1, &font, pill, &fmt, &fgBrush);

            posX = (sw * std::clamp(cfg.keycast.x, 5, 95)) / 100 - wndW / 2;
            posY = (sh * std::clamp(cfg.keycast.y, 5, 95)) / 100 - wndH / 2;
        } else {
            // Original MouseInc.exe bottom-right dark notification toast (docs/image/6.png):
            // Dark #1F1F1F box above the system tray with bold "MouseInc" title and body text
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
            Gdiplus::Font titleFont(&fontFamily, 15.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::Font bodyFont(&fontFamily, 14.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentNear);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);

            std::wstring title = custom_title.empty() ? L"MouseInc" : custom_title;
            Gdiplus::RectF layoutRect(0, 0, 360.0f, 120.0f);
            Gdiplus::RectF boundTitle, boundBody;
            g.MeasureString(title.c_str(), -1, &titleFont, layoutRect, &fmt, &boundTitle);
            g.MeasureString(text.c_str(), -1, &bodyFont, layoutRect, &fmt, &boundBody);

            wndW = std::clamp((int)std::ceil(std::max(boundTitle.Width, boundBody.Width) + 36.0f), 220, 390);
            wndH = std::clamp((int)std::ceil(boundTitle.Height + boundBody.Height + 28.0f), 68, 160);

            Gdiplus::RectF box(0.0f, 0.0f, (Gdiplus::REAL)wndW, (Gdiplus::REAL)wndH);
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(242, 31, 31, 31));
            Gdiplus::Pen borderPen(Gdiplus::Color(255, 58, 58, 58), 1.0f);
            g.FillRectangle(&bgBrush, box);
            g.DrawRectangle(&borderPen, 0.5f, 0.5f, (Gdiplus::REAL)(wndW - 1), (Gdiplus::REAL)(wndH - 1));

            Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::SolidBrush bodyBrush(Gdiplus::Color(255, 215, 215, 215));

            Gdiplus::PointF ptTitle(16.0f, 11.0f);
            g.DrawString(title.c_str(), -1, &titleFont, ptTitle, &fmt, &titleBrush);

            Gdiplus::RectF bodyRect(16.0f, 13.0f + boundTitle.Height + 3.0f, (Gdiplus::REAL)(wndW - 32), (Gdiplus::REAL)(wndH - 20));
            g.DrawString(text.c_str(), -1, &bodyFont, bodyRect, &fmt, &bodyBrush);

            RECT rcWork = { 0, 0, sw, sh };
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
            posX = rcWork.right - wndW - 10;
            posY = rcWork.bottom - wndH - 10;
        }

        POINT ptSrc = { 0, 0 };
        POINT ptDst = { posX, posY };
        SIZE szWnd = { wndW, wndH };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

        UpdateLayeredWindow(hwnd_banner_, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
        ShowWindow(hwnd_banner_, SW_SHOWNOACTIVATE);
        SetTimer(hwnd_banner_, 9001, (UINT)std::max(duration_ms, 400), nullptr);

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
    }

    // Cursor-following CapsLock indicator ("A" / "a", same duration & style as "中" / "英")
    void update_capslock_led(bool caps_on, bool enabled, bool show_tip = false) {
        if (!hwnd_caps_) return;
        if (!enabled || !show_tip) {
            KillTimer(hwnd_caps_, 9003);
            KillTimer(hwnd_caps_, 9011);
            if (IsWindowVisible(hwnd_caps_)) {
                ShowWindow(hwnd_caps_, SW_HIDE);
            }
            return;
        }

        POINT pt;
        GetCursorPos(&pt);
        int wndW = 46, wndH = 34;
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = wndW;
        bmi.bmiHeader.biHeight = -wndH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pvBits = nullptr;
        HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);

        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            Gdiplus::RectF pill(1.0f, 1.0f, (Gdiplus::REAL)(wndW - 2), (Gdiplus::REAL)(wndH - 2));
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(220, 228, 117, 66));
            Gdiplus::SolidBrush fgBrush(Gdiplus::Color(255, 255, 255, 255));
            auto* path = create_round_rect(pill, 8.0f);
            g.FillPath(&bgBrush, path);
            delete path;

            Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
            Gdiplus::Font font(&fontFamily, 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(caps_on ? L"A" : L"a", -1, &font, pill, &fmt, &fgBrush);
        }

        int offsetY = (hwnd_ime_ && IsWindowVisible(hwnd_ime_)) ? 54 : 16;
        POINT ptSrc = { 0, 0 };
        POINT ptDst = { pt.x + 16, pt.y + offsetY };
        SIZE szWnd = { wndW, wndH };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

        UpdateLayeredWindow(hwnd_caps_, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
        ShowWindow(hwnd_caps_, SW_SHOWNOACTIVATE);
        SetTimer(hwnd_caps_, 9011, 16, nullptr);
        SetTimer(hwnd_caps_, 9003, 950, nullptr);

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
    }

    // Cursor-adjacent IME indicator ("中" / "英") for ShowIme
    void show_cursor_ime_tip(const std::wstring& label) {
        if (!hwnd_ime_ || label.empty()) return;
        POINT pt;
        GetCursorPos(&pt);

        int wndW = 46, wndH = 34;
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = wndW;
        bmi.bmiHeader.biHeight = -wndH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pvBits = nullptr;
        HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);

        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            Gdiplus::RectF pill(1.0f, 1.0f, (Gdiplus::REAL)(wndW - 2), (Gdiplus::REAL)(wndH - 2));
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(220, 228, 117, 66));
            Gdiplus::SolidBrush fgBrush(Gdiplus::Color(255, 255, 255, 255));
            auto* path = create_round_rect(pill, 8.0f);
            g.FillPath(&bgBrush, path);
            delete path;

            Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
            Gdiplus::Font font(&fontFamily, 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(label.c_str(), -1, &font, pill, &fmt, &fgBrush);
        }

        POINT ptSrc = { 0, 0 };
        POINT ptDst = { pt.x + 16, pt.y + 16 };
        SIZE szWnd = { wndW, wndH };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

        UpdateLayeredWindow(hwnd_ime_, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
        ShowWindow(hwnd_ime_, SW_SHOWNOACTIVATE);
        SetTimer(hwnd_ime_, 9010, 16, nullptr);
        SetTimer(hwnd_ime_, 9002, 950, nullptr);

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
    }
};

} // namespace mouseinc
