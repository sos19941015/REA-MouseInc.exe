#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <wininet.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include "config.hpp"
#include "crypto_utils.hpp"
#include "osd_overlay.hpp"
#include "win_ai_ocr.hpp"

namespace mouseinc {

inline bool get_encoder_clsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<BYTE> buf(size);
    auto* pInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, pInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pInfo[j].MimeType, format) == 0) {
            *pClsid = pInfo[j].Clsid;
            return true;
        }
    }
    return false;
}

inline std::string hbitmap_to_png_bytes(HBITMAP hbm) {
    std::string out;
    Gdiplus::Bitmap bmp(hbm, nullptr);
    CLSID pngClsid;
    if (!get_encoder_clsid(L"image/png", &pngClsid)) return out;
    IStream* pStream = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &pStream))) {
        if (bmp.Save(pStream, &pngClsid, nullptr) == Gdiplus::Ok) {
            HGLOBAL hGlobal = nullptr;
            if (SUCCEEDED(GetHGlobalFromStream(pStream, &hGlobal))) {
                SIZE_T sz = GlobalSize(hGlobal);
                const char* ptr = (const char*)GlobalLock(hGlobal);
                if (ptr && sz > 0) {
                    out.assign(ptr, sz);
                    GlobalUnlock(hGlobal);
                }
            }
        }
        pStream->Release();
    }
    return out;
}

inline void copy_text_to_clipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* p = GlobalLock(hMem);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

inline void copy_hbitmap_to_clipboard(HBITMAP hbm) {
    if (!hbm || !OpenClipboard(nullptr)) return;
    EmptyClipboard();
    BITMAP bm = {};
    GetObjectW(hbm, sizeof(bm), &bm);
    HDC hdc = GetDC(nullptr);
    HDC hdcSrc = CreateCompatibleDC(hdc);
    HDC hdcDst = CreateCompatibleDC(hdc);
    HBITMAP hCopy = CreateCompatibleBitmap(hdc, bm.bmWidth, bm.bmHeight);
    HGDIOBJ old1 = SelectObject(hdcSrc, hbm);
    HGDIOBJ old2 = SelectObject(hdcDst, hCopy);
    BitBlt(hdcDst, 0, 0, bm.bmWidth, bm.bmHeight, hdcSrc, 0, 0, SRCCOPY);
    SelectObject(hdcSrc, old1);
    SelectObject(hdcDst, old2);
    DeleteDC(hdcSrc);
    DeleteDC(hdcDst);
    ReleaseDC(nullptr, hdc);
    SetClipboardData(CF_BITMAP, hCopy);
    CloseClipboard();
}

struct StickWindowState {
    HBITMAP hbm = nullptr;
    int orig_w = 0;
    int orig_h = 0;
    double scale = 1.0;
};

struct PipWindowState {
    HWND src_hwnd = nullptr;
    RECT src_rect = {};
    HTHUMBNAIL thumb = nullptr;
    int orig_w = 0;
    int orig_h = 0;
    double scale = 1.0;
};

class SnapshotManager {
public:
    static constexpr int kShadowMargin = 10;

    static void render_stick_layered(HWND hwnd, StickWindowState* st) {
        if (!st || !st->hbm) return;
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int imgW = std::max(16, (int)(st->orig_w * st->scale));
        int imgH = std::max(16, (int)(st->orig_h * st->scale));
        int totalW = imgW + kShadowMargin * 2;
        int totalH = imgH + kShadowMargin * 2;

        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = totalW;
        bmi.bmiHeader.biHeight = -totalH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pvBits = nullptr;
        HBITMAP hDib = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
        HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hDib);

        {
            Gdiplus::Graphics g(hdcMem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            // Render 9-patch or stretched SHADOW.png resource around the image
            static Gdiplus::Bitmap* s_shadow = load_embedded_png_resource(L"SHADOW");
            if (s_shadow && s_shadow->GetWidth() > 0 && s_shadow->GetHeight() > 0) {
                g.DrawImage(s_shadow, Gdiplus::Rect(0, 0, totalW, totalH),
                    0, 0, (INT)s_shadow->GetWidth(), (INT)s_shadow->GetHeight(), Gdiplus::UnitPixel);
            } else {
                Gdiplus::SolidBrush shd(Gdiplus::Color(90, 0, 0, 0));
                g.FillRectangle(&shd, 4, 4, totalW - 8, totalH - 8);
            }

            Gdiplus::Bitmap contentBmp(st->hbm, nullptr);
            g.DrawImage(&contentBmp, Gdiplus::Rect(kShadowMargin, kShadowMargin, imgW, imgH),
                0, 0, st->orig_w, st->orig_h, Gdiplus::UnitPixel);

            Gdiplus::Pen borderPen(Gdiplus::Color(235, 228, 117, 66), 1.5f);
            g.DrawRectangle(&borderPen, kShadowMargin, kShadowMargin, imgW - 1, imgH - 1);
        }

        POINT ptSrc = { 0, 0 };
        POINT ptDst = { rc.left, rc.top };
        SIZE szWnd = { totalW, totalH };
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

        UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

        SelectObject(hdcMem, hOld);
        DeleteObject(hDib);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
    }

    static LRESULT CALLBACK StickWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* st = reinterpret_cast<StickWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
            case WM_NCHITTEST:
                return HTCAPTION;
            case WM_RBUTTONUP:
            case WM_NCLBUTTONDBLCLK:
            case WM_NCRBUTTONUP:
                DestroyWindow(hwnd);
                return 0;
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) {
                    DestroyWindow(hwnd);
                    return 0;
                }
                if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) && st && st->hbm) {
                    copy_hbitmap_to_clipboard(st->hbm);
                    return 0;
                }
                if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000) && st && st->hbm) {
                    wchar_t file[MAX_PATH] = L"MouseInc_Snap.png";
                    OPENFILENAMEW ofn = { sizeof(ofn) };
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0";
                    ofn.lpstrFile = file;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                    if (GetSaveFileNameW(&ofn)) {
                        Gdiplus::Bitmap bmp(st->hbm, nullptr);
                        CLSID clsid;
                        if (get_encoder_clsid(L"image/png", &clsid)) {
                            bmp.Save(file, &clsid, nullptr);
                        }
                    }
                    return 0;
                }
                break;
            case WM_MOUSEWHEEL: {
                if (!st) break;
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                st->scale = std::clamp(st->scale + (delta > 0 ? 0.1 : -0.1), 0.2, 4.0);
                render_stick_layered(hwnd, st);
                return 0;
            }
            case WM_DESTROY:
                if (st) {
                    if (st->hbm) DeleteObject(st->hbm);
                    delete st;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                }
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK PipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* st = reinterpret_cast<PipWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
            case WM_NCHITTEST:
                return HTCAPTION;
            case WM_RBUTTONUP:
            case WM_NCRBUTTONUP:
            case WM_NCLBUTTONDBLCLK:
                DestroyWindow(hwnd);
                return 0;
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) {
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;
            case WM_MOUSEWHEEL: {
                if (!st) break;
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                st->scale = std::clamp(st->scale + (delta > 0 ? 0.1 : -0.1), 0.2, 4.0);
                RECT rc;
                GetWindowRect(hwnd, &rc);
                int nw = (int)(st->orig_w * st->scale);
                int nh = (int)(st->orig_h * st->scale);
                SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top, nw, nh, SWP_NOACTIVATE);
                if (st->thumb) {
                    DWM_THUMBNAIL_PROPERTIES props = {};
                    props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
                    props.fVisible = TRUE;
                    props.opacity = 255;
                    props.rcDestination = { 0, 0, nw, nh };
                    props.rcSource = st->src_rect;
                    DwmUpdateThumbnailProperties(st->thumb, &props);
                }
                return 0;
            }
            case WM_DESTROY:
                if (st) {
                    if (st->thumb) DwmUnregisterThumbnail(st->thumb);
                    delete st;
                    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                }
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static void register_classes(HINSTANCE hInst) {
        static bool registered = false;
        if (registered) return;
        registered = true;

        WNDCLASSEXW wcStick = { sizeof(WNDCLASSEXW) };
        wcStick.lpfnWndProc = StickWndProc;
        wcStick.hInstance = hInst;
        wcStick.lpszClassName = L"MouseIncStickWnd";
        wcStick.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
        wcStick.style = CS_DBLCLKS;
        RegisterClassExW(&wcStick);

        WNDCLASSEXW wcPip = { sizeof(WNDCLASSEXW) };
        wcPip.lpfnWndProc = PipWndProc;
        wcPip.hInstance = hInst;
        wcPip.lpszClassName = L"MouseIncPipWnd";
        wcPip.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
        wcPip.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wcPip.style = CS_DBLCLKS;
        RegisterClassExW(&wcPip);
    }

    static void create_stick_window(HBITMAP hbm, int x, int y, int w, int h) {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        register_classes(hInst);
        auto* st = new StickWindowState();
        st->hbm = hbm;
        st->orig_w = w;
        st->orig_h = h;
        st->scale = 1.0;

        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"MouseIncStickWnd", L"MouseInc Stick",
            WS_POPUP | WS_VISIBLE,
            x - kShadowMargin, y - kShadowMargin,
            w + kShadowMargin * 2, h + kShadowMargin * 2,
            nullptr, nullptr, hInst, nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        render_stick_layered(hwnd, st);
    }

    static void create_pip_window(HWND src_hwnd, RECT screen_rect) {
        if (!src_hwnd || !IsWindow(src_hwnd)) return;
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        register_classes(hInst);

        RECT winRect = {};
        GetWindowRect(src_hwnd, &winRect);
        int w = screen_rect.right - screen_rect.left;
        int h = screen_rect.bottom - screen_rect.top;
        if (w < 10 || h < 10) return;

        auto* st = new PipWindowState();
        st->src_hwnd = src_hwnd;
        st->orig_w = w;
        st->orig_h = h;
        st->src_rect = {
            screen_rect.left - winRect.left,
            screen_rect.top - winRect.top,
            screen_rect.right - winRect.left,
            screen_rect.bottom - winRect.top
        };

        HWND hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"MouseIncPipWnd", L"MouseInc PiP",
            WS_POPUP | WS_VISIBLE,
            screen_rect.left, screen_rect.top, w, h,
            nullptr, nullptr, hInst, nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        if (SUCCEEDED(DwmRegisterThumbnail(hwnd, src_hwnd, &st->thumb))) {
            DWM_THUMBNAIL_PROPERTIES props = {};
            props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
            props.fVisible = TRUE;
            props.opacity = 255;
            props.rcDestination = { 0, 0, w, h };
            props.rcSource = st->src_rect;
            DwmUpdateThumbnailProperties(st->thumb, &props);
        }
    }

    static void stick_from_clipboard() {
        if (!IsClipboardFormatAvailable(CF_BITMAP)) return;
        if (!OpenClipboard(nullptr)) return;
        HBITMAP hClip = (HBITMAP)GetClipboardData(CF_BITMAP);
        if (hClip) {
            BITMAP bm = {};
            GetObjectW(hClip, sizeof(bm), &bm);
            if (bm.bmWidth > 0 && bm.bmHeight > 0) {
                HDC hdc = GetDC(nullptr);
                HDC hdcSrc = CreateCompatibleDC(hdc);
                HDC hdcDst = CreateCompatibleDC(hdc);
                HBITMAP hCopy = CreateCompatibleBitmap(hdc, bm.bmWidth, bm.bmHeight);
                HGDIOBJ o1 = SelectObject(hdcSrc, hClip);
                HGDIOBJ o2 = SelectObject(hdcDst, hCopy);
                BitBlt(hdcDst, 0, 0, bm.bmWidth, bm.bmHeight, hdcSrc, 0, 0, SRCCOPY);
                SelectObject(hdcSrc, o1);
                SelectObject(hdcDst, o2);
                DeleteDC(hdcSrc);
                DeleteDC(hdcDst);
                ReleaseDC(nullptr, hdc);

                POINT pt;
                GetCursorPos(&pt);
                create_stick_window(hCopy, pt.x - bm.bmWidth / 2, pt.y - bm.bmHeight / 2, bm.bmWidth, bm.bmHeight);
            }
        }
        CloseClipboard();
    }

    static std::wstring perform_ocr(const std::string& png_bytes, int ocr_service) {
        std::wstring res;
        if (ocr_service == 1 || ocr_service == 3) {
            // 1: 微軟AI OCR (Microsoft.Windows.AI.Imaging.TextRecognizer — Windows App SDK NPU AI OCR)
            res = win_ai_ocr::perform_ocr_microsoft_ai_ocr(png_bytes);
            if (res.empty()) res = win_ai_ocr::perform_ocr_microsoft_ocr(png_bytes);
        } else {
            // 0: 微軟OCR (Windows.Media.Ocr.OcrEngine — 系統原生內建輕量離線 OCR)
            res = win_ai_ocr::perform_ocr_microsoft_ocr(png_bytes);
        }
        return res;
    }

    static HBITMAP capture_screen_rect(RECT rc, bool draw_cursor = false) {
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return nullptr;
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
        HGDIOBJ old = SelectObject(hdcMem, hbm);
        BitBlt(hdcMem, 0, 0, w, h, hdcScreen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
        if (draw_cursor) {
            CURSORINFO ci = { sizeof(ci) };
            if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
                DrawIconEx(hdcMem, ci.ptScreenPos.x - rc.left, ci.ptScreenPos.y - rc.top, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
            }
        }
        SelectObject(hdcMem, old);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return hbm;
    }
};

struct RegionSelectorContext {
    HBITMAP hScreenBmp = nullptr;
    int vx = 0, vy = 0, vw = 0, vh = 0;
    bool dragging = false;
    bool accepted = false;
    POINT start_pt = {};
    POINT end_pt = {};
    HWND target_hwnd = nullptr;
};

inline LRESULT CALLBACK RegionSelectorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<RegionSelectorContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!ctx) break;
            ctx->dragging = true;
            GetCursorPos(&ctx->start_pt);
            ctx->end_pt = ctx->start_pt;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!ctx || !ctx->dragging) break;
            GetCursorPos(&ctx->end_pt);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!ctx || !ctx->dragging) break;
            ctx->dragging = false;
            GetCursorPos(&ctx->end_pt);
            ReleaseCapture();
            ctx->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_RBUTTONUP:
        case WM_KEYDOWN:
            if (msg == WM_RBUTTONUP || wParam == VK_ESCAPE) {
                if (ctx) ctx->accepted = false;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (ctx && ctx->hScreenBmp) {
                HDC hdcBuf = CreateCompatibleDC(hdc);
                HBITMAP hbmBuf = CreateCompatibleBitmap(hdc, ctx->vw, ctx->vh);
                HGDIOBJ oldBuf = SelectObject(hdcBuf, hbmBuf);

                HDC hdcSrc = CreateCompatibleDC(hdc);
                HGDIOBJ oldSrc = SelectObject(hdcSrc, ctx->hScreenBmp);
                BitBlt(hdcBuf, 0, 0, ctx->vw, ctx->vh, hdcSrc, 0, 0, SRCCOPY);
                SelectObject(hdcSrc, oldSrc);
                DeleteDC(hdcSrc);

                if (ctx->dragging) {
                    int x1 = std::min(ctx->start_pt.x, ctx->end_pt.x) - ctx->vx;
                    int y1 = std::min(ctx->start_pt.y, ctx->end_pt.y) - ctx->vy;
                    int x2 = std::max(ctx->start_pt.x, ctx->end_pt.x) - ctx->vx;
                    int y2 = std::max(ctx->start_pt.y, ctx->end_pt.y) - ctx->vy;
                    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(228, 117, 66));
                    HGDIOBJ oldPen = SelectObject(hdcBuf, hPen);
                    HGDIOBJ oldBr = SelectObject(hdcBuf, GetStockObject(NULL_BRUSH));
                    Rectangle(hdcBuf, x1, y1, x2, y2);
                    SelectObject(hdcBuf, oldPen);
                    SelectObject(hdcBuf, oldBr);
                    DeleteObject(hPen);
                }

                BitBlt(hdc, 0, 0, ctx->vw, ctx->vh, hdcBuf, 0, 0, SRCCOPY);
                SelectObject(hdcBuf, oldBuf);
                DeleteObject(hbmBuf);
                DeleteDC(hdcBuf);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

inline bool select_screen_region(RECT& out_rect, HWND& out_underlying_hwnd) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool reg = false;
    if (!reg) {
        reg = true;
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = RegionSelectorWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"MouseIncRegionSelector";
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        RegisterClassExW(&wc);
    }

    RegionSelectorContext ctx;
    ctx.vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    ctx.vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    ctx.vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    ctx.vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    RECT full = { ctx.vx, ctx.vy, ctx.vx + ctx.vw, ctx.vy + ctx.vh };
    ctx.hScreenBmp = SnapshotManager::capture_screen_rect(full, false);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MouseIncRegionSelector", L"Select Region",
        WS_POPUP | WS_VISIBLE,
        ctx.vx, ctx.vy, ctx.vw, ctx.vh,
        nullptr, nullptr, hInst, nullptr);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    SetForegroundWindow(hwnd);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (ctx.hScreenBmp) DeleteObject(ctx.hScreenBmp);
    if (!ctx.accepted) return false;

    out_rect.left = std::min(ctx.start_pt.x, ctx.end_pt.x);
    out_rect.top = std::min(ctx.start_pt.y, ctx.end_pt.y);
    out_rect.right = std::max(ctx.start_pt.x, ctx.end_pt.x);
    out_rect.bottom = std::max(ctx.start_pt.y, ctx.end_pt.y);
    if (out_rect.right - out_rect.left < 5 || out_rect.bottom - out_rect.top < 5) return false;

    POINT center = { (out_rect.left + out_rect.right) / 2, (out_rect.top + out_rect.bottom) / 2 };
    out_underlying_hwnd = GetAncestor(WindowFromPoint(center), GA_ROOT);
    return true;
}

} // namespace mouseinc
