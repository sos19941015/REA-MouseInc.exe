#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <appmodel.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <objidl.h>
#include <gdiplus.h>
#include <roapi.h>
#include <winstring.h>
#include <process.h>
#include <unknwn.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.Ocr.h>

#include <string>
#include <sstream>
#include <vector>
#include "config.hpp"

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "runtimeobject.lib")

namespace mouseinc {
namespace win_ai_ocr {

// ============================================================================
// 1. Win11 OneOCR (Windows 11 SnippingTool oneocr.dll + oneocr.onemodel)
//    Reference: https://github.com/b1tg/win11-oneocr
// ============================================================================

#pragma pack(push, 1)
struct OneOcrImg {
    int32_t t;        // 3 = BGRA (4 channels)
    int32_t col;      // width in pixels
    int32_t row;      // height in pixels
    int32_t _unk;     // 0
    int64_t step;     // row stride in bytes
    int64_t data_ptr; // pointer to pixel data
};
#pragma pack(pop)

using CreateOcrInitOptions_t = int64_t(__cdecl*)(int64_t*);
using OcrInitOptionsSetUseModelDelayLoad_t = int64_t(__cdecl*)(int64_t, char);
using CreateOcrPipeline_t = int64_t(__cdecl*)(const char*, const char*, int64_t, int64_t*);
using CreateOcrProcessOptions_t = int64_t(__cdecl*)(int64_t*);
using OcrProcessOptionsSetMaxRecognitionLineCount_t = int64_t(__cdecl*)(int64_t, int64_t);
using RunOcrPipeline_t = int64_t(__cdecl*)(int64_t, OneOcrImg*, int64_t, int64_t*);
using GetOcrLineCount_t = int64_t(__cdecl*)(int64_t, int64_t*);
using GetOcrLine_t = int64_t(__cdecl*)(int64_t, int64_t, int64_t*);
using GetOcrLineContent_t = int64_t(__cdecl*)(int64_t, int64_t*);

inline bool has_oneocr_files(const std::wstring& dir) {
    if (dir.empty()) return false;
    std::wstring dll = dir + L"\\oneocr.dll";
    std::wstring mdl = dir + L"\\oneocr.onemodel";
    return PathFileExistsW(dll.c_str()) && PathFileExistsW(mdl.c_str());
}

// Automatically discover the installed Microsoft.ScreenSketch SnippingTool folder on any Windows 11 PC
// regardless of package version (e.g. 11.2409.25.0, 11.2607.23.0, etc.)
inline std::wstring find_oneocr_source_directory() {
    // Tier 1: Check MouseInc application directory first (allows portable override)
    std::wstring app_dir = AppConfig::get_app_dir();
    if (has_oneocr_files(app_dir)) return app_dir;
    if (has_oneocr_files(app_dir + L"\\oneocr")) return app_dir + L"\\oneocr";
    if (has_oneocr_files(app_dir + L"\\SnippingTool")) return app_dir + L"\\SnippingTool";

    // Tier 2: Win32 AppModel GetPackagesByPackageFamily("Microsoft.ScreenSketch_8wekyb3d8bbwe")
    {
        UINT32 count = 0;
        UINT32 bufLen = 0;
        const wchar_t* family = L"Microsoft.ScreenSketch_8wekyb3d8bbwe";
        LONG rc = GetPackagesByPackageFamily(family, &count, nullptr, &bufLen, nullptr);
        if (rc == ERROR_INSUFFICIENT_BUFFER && count > 0 && bufLen > 0) {
            std::vector<PWSTR> names(count, nullptr);
            std::vector<wchar_t> buffer(bufLen, 0);
            if (GetPackagesByPackageFamily(family, &count, names.data(), &bufLen, buffer.data()) == ERROR_SUCCESS) {
                for (UINT32 i = 0; i < count; i++) {
                    if (!names[i]) continue;
                    UINT32 pathLen = 0;
                    if (GetPackagePathByFullName(names[i], &pathLen, nullptr) == ERROR_INSUFFICIENT_BUFFER && pathLen > 0) {
                        std::vector<wchar_t> pathBuf(pathLen, 0);
                        if (GetPackagePathByFullName(names[i], &pathLen, pathBuf.data()) == ERROR_SUCCESS) {
                            std::wstring pkgRoot(pathBuf.data());
                            if (has_oneocr_files(pkgRoot + L"\\SnippingTool")) return pkgRoot + L"\\SnippingTool";
                            if (has_oneocr_files(pkgRoot)) return pkgRoot;
                        }
                    }
                }
            }
        }
    }

    // Tier 3: Check AppModel Repository Registry under HKCU
    {
        HKEY hPackages = nullptr;
        const wchar_t* regPath = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages";
        if (RegOpenKeyExW(HKEY_CURRENT_USER, regPath, 0, KEY_READ, &hPackages) == ERROR_SUCCESS) {
            wchar_t subKeyName[256];
            for (DWORD idx = 0; ; idx++) {
                DWORD nameLen = 256;
                if (RegEnumKeyExW(hPackages, idx, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                    break;
                }
                if (wcsncmp(subKeyName, L"Microsoft.ScreenSketch_", 23) == 0) {
                    wchar_t rootFolder[MAX_PATH] = {0};
                    DWORD sz = sizeof(rootFolder);
                    if (RegGetValueW(hPackages, subKeyName, L"PackageRootFolder", RRF_RT_REG_SZ, nullptr, rootFolder, &sz) == ERROR_SUCCESS && rootFolder[0]) {
                        std::wstring pkgRoot(rootFolder);
                        if (has_oneocr_files(pkgRoot + L"\\SnippingTool")) {
                            RegCloseKey(hPackages);
                            return pkgRoot + L"\\SnippingTool";
                        }
                        if (has_oneocr_files(pkgRoot)) {
                            RegCloseKey(hPackages);
                            return pkgRoot;
                        }
                    }
                }
            }
            RegCloseKey(hPackages);
        }
    }

    // Tier 4: Wildcard search in C:\Program Files\WindowsApps\Microsoft.ScreenSketch_*
    {
        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = FindFirstFileW(L"C:\\Program Files\\WindowsApps\\Microsoft.ScreenSketch_*", &fd);
        std::wstring best_dir;
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    std::wstring base = std::wstring(L"C:\\Program Files\\WindowsApps\\") + fd.cFileName;
                    if (has_oneocr_files(base + L"\\SnippingTool")) best_dir = base + L"\\SnippingTool";
                    else if (has_oneocr_files(base)) best_dir = base;
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
        if (!best_dir.empty()) return best_dir;
    }

    return L"";
}

// WindowsApps folders have conditional ACLs (WIN://SYSAPPID) that allow reading/copying files
// via CopyFileW, but block direct LoadLibraryW (ERROR_ACCESS_DENIED = 5) from non-packaged Win32 apps.
// This helper automatically stages oneocr.dll, oneocr.onemodel, and onnxruntime.dll into %appdir%\oneocr
// (or %TEMP%\MouseInc_OneOCR) so LoadLibraryW succeeds on every Windows 11 PC without manual copying.
inline std::wstring ensure_loadable_oneocr_directory() {
    std::wstring app_dir = AppConfig::get_app_dir();
    if (has_oneocr_files(app_dir)) return app_dir;

    std::wstring local_cache = app_dir + L"\\oneocr";
    if (has_oneocr_files(local_cache)) return local_cache;

    std::wstring src_dir = find_oneocr_source_directory();
    if (src_dir.empty()) return L"";

    // If src_dir is not inside WindowsApps, we can load from it directly
    std::wstring lower_src = to_lower_wide(src_dir);
    if (lower_src.find(L"windowsapps") == std::wstring::npos) {
        return src_dir;
    }

    auto try_stage_to = [&](const std::wstring& dst_dir) -> bool {
        CreateDirectoryW(dst_dir.c_str(), nullptr);
        const wchar_t* files[] = { L"oneocr.dll", L"oneocr.onemodel", L"onnxruntime.dll" };
        for (const wchar_t* f : files) {
            std::wstring s = src_dir + L"\\" + f;
            std::wstring d = dst_dir + L"\\" + f;
            if (!PathFileExistsW(d.c_str()) && PathFileExistsW(s.c_str())) {
                CopyFileW(s.c_str(), d.c_str(), FALSE);
            }
        }
        return has_oneocr_files(dst_dir);
    };

    if (try_stage_to(local_cache)) {
        return local_cache;
    }

    wchar_t temp_path[MAX_PATH] = {0};
    if (GetTempPathW(MAX_PATH, temp_path) > 0) {
        std::wstring temp_cache = std::wstring(temp_path) + L"MouseInc_OneOCR";
        if (try_stage_to(temp_cache)) {
            return temp_cache;
        }
    }

    return src_dir;
}

inline std::wstring perform_ocr_win11_oneocr(const std::string& png_bytes) {
    if (png_bytes.empty()) return L"";

    static HMODULE s_hOneOcr = nullptr;
    static int64_t s_ctx = 0;
    static int64_t s_pipeline = 0;
    static int64_t s_opt = 0;
    static RunOcrPipeline_t s_RunOcrPipeline = nullptr;
    static GetOcrLineCount_t s_GetOcrLineCount = nullptr;
    static GetOcrLine_t s_GetOcrLine = nullptr;
    static GetOcrLineContent_t s_GetOcrLineContent = nullptr;

    if (!s_pipeline) {
        std::wstring dir = ensure_loadable_oneocr_directory();
        if (dir.empty()) return L"";

        SetDllDirectoryW(dir.c_str());
        LoadLibraryW((dir + L"\\onnxruntime.dll").c_str());
        s_hOneOcr = LoadLibraryW((dir + L"\\oneocr.dll").c_str());
        if (!s_hOneOcr) return L"";

        auto fnCreateOcrInitOptions = (CreateOcrInitOptions_t)GetProcAddress(s_hOneOcr, "CreateOcrInitOptions");
        auto fnSetDelayLoad = (OcrInitOptionsSetUseModelDelayLoad_t)GetProcAddress(s_hOneOcr, "OcrInitOptionsSetUseModelDelayLoad");
        auto fnCreatePipeline = (CreateOcrPipeline_t)GetProcAddress(s_hOneOcr, "CreateOcrPipeline");
        auto fnCreateProcessOpts = (CreateOcrProcessOptions_t)GetProcAddress(s_hOneOcr, "CreateOcrProcessOptions");
        auto fnSetMaxLines = (OcrProcessOptionsSetMaxRecognitionLineCount_t)GetProcAddress(s_hOneOcr, "OcrProcessOptionsSetMaxRecognitionLineCount");

        s_RunOcrPipeline = (RunOcrPipeline_t)GetProcAddress(s_hOneOcr, "RunOcrPipeline");
        s_GetOcrLineCount = (GetOcrLineCount_t)GetProcAddress(s_hOneOcr, "GetOcrLineCount");
        s_GetOcrLine = (GetOcrLine_t)GetProcAddress(s_hOneOcr, "GetOcrLine");
        s_GetOcrLineContent = (GetOcrLineContent_t)GetProcAddress(s_hOneOcr, "GetOcrLineContent");

        if (!fnCreateOcrInitOptions || !fnSetDelayLoad || !fnCreatePipeline ||
            !fnCreateProcessOpts || !fnSetMaxLines || !s_RunOcrPipeline ||
            !s_GetOcrLineCount || !s_GetOcrLine || !s_GetOcrLineContent) {
            return L"";
        }

        if (fnCreateOcrInitOptions(&s_ctx) != 0) return L"";
        fnSetDelayLoad(s_ctx, 0);

        const char* key = "kj)TGtrK>f]b[Piow.gU+nC@s\"\"\"\"\"\"4";
        std::string model_path = wide_to_utf8(dir + L"\\oneocr.onemodel");
        int64_t res = fnCreatePipeline(model_path.c_str(), key, s_ctx, &s_pipeline);
        if (res != 0 || !s_pipeline) {
            wchar_t prevCwd[MAX_PATH] = {0};
            GetCurrentDirectoryW(MAX_PATH, prevCwd);
            SetCurrentDirectoryW(dir.c_str());
            res = fnCreatePipeline("oneocr.onemodel", key, s_ctx, &s_pipeline);
            if (prevCwd[0]) SetCurrentDirectoryW(prevCwd);
        }
        if (res != 0 || !s_pipeline) {
            s_pipeline = 0;
            return L"";
        }

        if (fnCreateProcessOpts(&s_opt) != 0) {
            s_pipeline = 0;
            return L"";
        }
        fnSetMaxLines(s_opt, 1000);
    }

    // Decode PNG bytes to 32-bit BGRA bitmap buffer via GDI+ (no OpenCV needed!)
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, png_bytes.size());
    if (!hMem) return L"";
    void* pLock = GlobalLock(hMem);
    if (pLock) {
        memcpy(pLock, png_bytes.data(), png_bytes.size());
        GlobalUnlock(hMem);
    }
    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
        GlobalFree(hMem);
        return L"";
    }

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        return L"";
    }

    int w = (int)bmp->GetWidth();
    int h = (int)bmp->GetHeight();
    if (w <= 0 || h <= 0) {
        delete bmp;
        return L"";
    }

    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData bd = {};
    if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
        delete bmp;
        return L"";
    }

    OneOcrImg img = {};
    img.t = 3;
    img.col = w;
    img.row = h;
    img._unk = 0;
    img.step = (int64_t)bd.Stride;
    img.data_ptr = (int64_t)reinterpret_cast<uintptr_t>(bd.Scan0);

    int64_t instance = 0;
    std::wstring output;
    if (s_RunOcrPipeline(s_pipeline, &img, s_opt, &instance) == 0 && instance != 0) {
        int64_t line_count = 0;
        if (s_GetOcrLineCount(instance, &line_count) == 0 && line_count > 0) {
            std::string joined;
            for (int64_t i = 0; i < line_count; i++) {
                int64_t line = 0;
                s_GetOcrLine(instance, i, &line);
                if (!line) continue;
                int64_t content_ptr = 0;
                s_GetOcrLineContent(line, &content_ptr);
                if (content_ptr) {
                    const char* utf8_line = reinterpret_cast<const char*>((uintptr_t)content_ptr);
                    if (utf8_line && utf8_line[0]) {
                        if (!joined.empty()) joined += "\r\n";
                        joined += utf8_line;
                    }
                }
            }
            output = utf8_to_wide(joined);
        }
    }

    bmp->UnlockBits(&bd);
    delete bmp;
    return output;
}

// ============================================================================
// 2. Microsoft.Windows.AI.Imaging.TextRecognizer & Windows.Media.Ocr.OcrEngine
//    Reference: https://learn.microsoft.com/zh-tw/windows/ai/apis/text-recognition
// ============================================================================

enum class AIFeatureReadyState : int32_t {
    Ready = 0,
    NotReady = 1,
    NotSupportedOnCurrentSystem = 2,
    DisabledByUser = 3
};

enum class AIFeatureReadyResultState : int32_t {
    InProgress = 0,
    Success = 1,
    Failure = 2
};

struct __declspec(uuid("38595062-5201-5690-9A0A-70456E780E8E")) IAIFeatureReadyResult : ::IInspectable {
    virtual HRESULT __stdcall get_Error(::HRESULT* value) = 0;
    virtual HRESULT __stdcall get_ErrorDisplayText(HSTRING* value) = 0;
    virtual HRESULT __stdcall get_ExtendedError(::HRESULT* value) = 0;
    virtual HRESULT __stdcall get_Status(AIFeatureReadyResultState* value) = 0;
};

struct __declspec(uuid("3C92A3D7-8A86-56B0-806D-A51C360C188E")) IImageBufferStatics : ::IInspectable {
    virtual HRESULT __stdcall CreateForBuffer(::IInspectable* buffer, int32_t pixelFormat, int32_t width, int32_t height, int32_t rowStride, ::IInspectable** result) = 0;
    virtual HRESULT __stdcall CreateForSoftwareBitmap(::IInspectable* softwareBitmap, ::IInspectable** result) = 0;
};

struct __declspec(uuid("B48E2431-3D38-5A3C-828D-9C640F202D94")) IRecognizedLine : ::IInspectable {
    virtual HRESULT __stdcall get_Text(HSTRING* value) = 0;
};

struct __declspec(uuid("AE12A99E-4642-5028-922F-7A1E0185A0D3")) IRecognizedText : ::IInspectable {
    virtual HRESULT __stdcall get_Lines(::IInspectable** value) = 0;
};

struct __declspec(uuid("7782C095-0327-576A-B217-6863E19F0F3E")) ITextRecognizer : ::IInspectable {
    virtual HRESULT __stdcall RecognizeTextFromImageAsync(::IInspectable* imageBuffer, ::IInspectable** operation) = 0;
    virtual HRESULT __stdcall RecognizeTextFromImage(::IInspectable* imageBuffer, IRecognizedText** result) = 0;
};

struct __declspec(uuid("3788C29B-4861-537F-B75A-5C5F3B7F2E3B")) ITextRecognizerStatics : ::IInspectable {
    virtual HRESULT __stdcall GetReadyState(AIFeatureReadyState* result) = 0;
    virtual HRESULT __stdcall EnsureReadyAsync(::IInspectable** operation) = 0;
    virtual HRESULT __stdcall CreateAsync(::IInspectable** operation) = 0;
};

inline bool is_microsoft_ai_ocr_supported() {
    HSTRING hClassName = nullptr;
    if (FAILED(WindowsCreateString(L"Microsoft.Windows.AI.Imaging.TextRecognizer", 43, &hClassName))) {
        return false;
    }
    winrt::com_ptr<ITextRecognizerStatics> recognizerStatics;
    HRESULT hr = RoGetActivationFactory(hClassName, __uuidof(ITextRecognizerStatics), recognizerStatics.put_void());
    WindowsDeleteString(hClassName);
    if (FAILED(hr) || !recognizerStatics) return false;

    AIFeatureReadyState readyState = AIFeatureReadyState::NotSupportedOnCurrentSystem;
    if (FAILED(recognizerStatics->GetReadyState(&readyState))) return false;
    return readyState != AIFeatureReadyState::NotSupportedOnCurrentSystem &&
           readyState != AIFeatureReadyState::DisabledByUser;
}

inline winrt::Windows::Graphics::Imaging::SoftwareBitmap decode_png_to_software_bitmap(const std::string& png_bytes) {
    using namespace winrt::Windows::Storage::Streams;
    using namespace winrt::Windows::Graphics::Imaging;

    InMemoryRandomAccessStream stream;
    DataWriter writer(stream);
    writer.WriteBytes(winrt::array_view<const uint8_t>(
        reinterpret_cast<const uint8_t*>(png_bytes.data()),
        reinterpret_cast<const uint8_t*>(png_bytes.data() + png_bytes.size())));
    writer.StoreAsync().get();
    writer.FlushAsync().get();
    (void)writer.DetachStream();
    stream.Seek(0);

    BitmapDecoder decoder = BitmapDecoder::CreateAsync(stream).get();
    SoftwareBitmap bitmap = decoder.GetSoftwareBitmapAsync().get();
    if (!bitmap) return nullptr;

    if (bitmap.BitmapPixelFormat() != BitmapPixelFormat::Bgra8 ||
        bitmap.BitmapAlphaMode() == BitmapAlphaMode::Straight) {
        bitmap = SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied);
    }
    return bitmap;
}

inline std::wstring try_microsoft_windows_ai_text_recognizer(
    winrt::Windows::Graphics::Imaging::SoftwareBitmap const& bitmap) {
    try {
        HSTRING hClassName = nullptr;
        if (FAILED(WindowsCreateString(L"Microsoft.Windows.AI.Imaging.TextRecognizer", 43, &hClassName))) {
            return L"";
        }
        winrt::com_ptr<ITextRecognizerStatics> recognizerStatics;
        HRESULT hr = RoGetActivationFactory(hClassName, __uuidof(ITextRecognizerStatics), recognizerStatics.put_void());
        WindowsDeleteString(hClassName);
        if (FAILED(hr) || !recognizerStatics) {
            return L"";
        }

        AIFeatureReadyState readyState = AIFeatureReadyState::NotSupportedOnCurrentSystem;
        if (FAILED(recognizerStatics->GetReadyState(&readyState))) {
            return L"";
        }
        if (readyState == AIFeatureReadyState::NotSupportedOnCurrentSystem ||
            readyState == AIFeatureReadyState::DisabledByUser) {
            return L"";
        }

        if (readyState == AIFeatureReadyState::NotReady) {
            winrt::com_ptr<::IInspectable> ensureOpInsp;
            if (FAILED(recognizerStatics->EnsureReadyAsync(ensureOpInsp.put())) || !ensureOpInsp) {
                return L"";
            }
            auto ensureOp = ensureOpInsp.try_as<winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::IInspectable>>();
            if (ensureOp) {
                auto loadResInsp = ensureOp.get();
                auto loadRes = loadResInsp.try_as<IAIFeatureReadyResult>();
                if (loadRes) {
                    AIFeatureReadyResultState status = AIFeatureReadyResultState::Failure;
                    loadRes->get_Status(&status);
                    if (status != AIFeatureReadyResultState::Success) {
                        return L"";
                    }
                }
            }
        }

        HSTRING hImgBufClass = nullptr;
        if (FAILED(WindowsCreateString(L"Microsoft.Graphics.Imaging.ImageBuffer", 38, &hImgBufClass))) {
            return L"";
        }
        winrt::com_ptr<IImageBufferStatics> imgBufStatics;
        hr = RoGetActivationFactory(hImgBufClass, __uuidof(IImageBufferStatics), imgBufStatics.put_void());
        WindowsDeleteString(hImgBufClass);
        if (FAILED(hr) || !imgBufStatics) {
            return L"";
        }

        winrt::com_ptr<::IInspectable> imageBuffer;
        hr = imgBufStatics->CreateForSoftwareBitmap(
            static_cast<::IInspectable*>(winrt::get_abi(bitmap)),
            imageBuffer.put());
        if (FAILED(hr) || !imageBuffer) {
            return L"";
        }

        winrt::com_ptr<::IInspectable> createOpInsp;
        if (FAILED(recognizerStatics->CreateAsync(createOpInsp.put())) || !createOpInsp) {
            return L"";
        }
        auto createOp = createOpInsp.try_as<winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::IInspectable>>();
        if (!createOp) return L"";

        auto recognizerInsp = createOp.get();
        auto textRecognizer = recognizerInsp.try_as<ITextRecognizer>();
        if (!textRecognizer) return L"";

        winrt::com_ptr<IRecognizedText> recognizedText;
        if (FAILED(textRecognizer->RecognizeTextFromImage(imageBuffer.get(), recognizedText.put())) || !recognizedText) {
            return L"";
        }

        winrt::com_ptr<::IInspectable> linesInsp;
        if (FAILED(recognizedText->get_Lines(linesInsp.put())) || !linesInsp) {
            return L"";
        }
        auto linesVec = linesInsp.try_as<winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::Foundation::IInspectable>>();
        if (!linesVec) return L"";

        std::wstringstream ss;
        bool first = true;
        for (auto const& item : linesVec) {
            auto line = item.try_as<IRecognizedLine>();
            if (line) {
                HSTRING hTxt = nullptr;
                if (SUCCEEDED(line->get_Text(&hTxt)) && hTxt) {
                    uint32_t len = 0;
                    const wchar_t* raw = WindowsGetStringRawBuffer(hTxt, &len);
                    if (raw && len > 0) {
                        if (!first) ss << L"\r\n";
                        ss.write(raw, len);
                        first = false;
                    }
                    WindowsDeleteString(hTxt);
                }
            }
        }
        return ss.str();
    } catch (...) {
        return L"";
    }
}

inline std::wstring run_windows_media_ocr_engine(
    winrt::Windows::Graphics::Imaging::SoftwareBitmap const& bitmap) {
    using namespace winrt::Windows::Media::Ocr;
    using namespace winrt::Windows::Globalization;

    auto recognize_with_engine = [&](OcrEngine const& engine) -> std::wstring {
        if (!engine) return L"";
        OcrResult result = engine.RecognizeAsync(bitmap).get();
        if (!result) return L"";

        std::wstringstream ss;
        bool first = true;
        for (auto const& line : result.Lines()) {
            std::wstring line_str(line.Text().c_str());
            std::wstring cleaned;
            cleaned.reserve(line_str.size());
            for (size_t i = 0; i < line_str.size(); i++) {
                if (line_str[i] == L' ' && i > 0 && i + 1 < line_str.size()) {
                    wchar_t prev = line_str[i - 1];
                    wchar_t next = line_str[i + 1];
                    if (prev >= 0x2E80 && next >= 0x2E80) {
                        continue;
                    }
                }
                cleaned.push_back(line_str[i]);
            }
            if (!cleaned.empty()) {
                if (!first) ss << L"\r\n";
                ss << cleaned;
                first = false;
            }
        }
        return ss.str();
    };

    try {
        OcrEngine userEngine = OcrEngine::TryCreateFromUserProfileLanguages();
        std::wstring text = recognize_with_engine(userEngine);
        if (!text.empty()) return text;
    } catch (...) {}

    const wchar_t* lang_tags[] = {
        L"zh-Hant-TW", L"zh-TW", L"zh-Hant",
        L"zh-Hans-CN", L"zh-CN", L"zh-Hans",
        L"en-US", L"ja"
    };
    for (const wchar_t* tag : lang_tags) {
        try {
            Language lang(tag);
            if (OcrEngine::IsLanguageSupported(lang)) {
                OcrEngine eng = OcrEngine::TryCreateFromLanguage(lang);
                std::wstring text = recognize_with_engine(eng);
                if (!text.empty()) return text;
            }
        } catch (...) {}
    }
    return L"";
}

struct OcrWorkerContext {
    const std::string* png_bytes = nullptr;
    bool use_npu_ai = false;
    std::wstring result;
};

inline unsigned __stdcall OcrMtaWorkerProc(void* arg) {
    auto* ctx = static_cast<OcrWorkerContext*>(arg);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
        auto bitmap = decode_png_to_software_bitmap(*ctx->png_bytes);
        if (bitmap) {
            if (ctx->use_npu_ai) {
                ctx->result = try_microsoft_windows_ai_text_recognizer(bitmap);
            } else {
                ctx->result = run_windows_media_ocr_engine(bitmap);
            }
        }
    } catch (...) {
        ctx->result.clear();
    }
    winrt::uninit_apartment();
    return 0;
}

// OcrService == 0: 微軟OCR (Windows.Media.Ocr.OcrEngine, 100% lightweight built-in Win10/11 OCR)
inline std::wstring perform_ocr_microsoft_ocr(const std::string& png_bytes) {
    if (png_bytes.empty()) return L"";
    OcrWorkerContext ctx;
    ctx.png_bytes = &png_bytes;
    ctx.use_npu_ai = false;
    uintptr_t hThread = _beginthreadex(nullptr, 0, OcrMtaWorkerProc, &ctx, 0, nullptr);
    if (!hThread) return L"";
    WaitForSingleObject(reinterpret_cast<HANDLE>(hThread), 15000);
    CloseHandle(reinterpret_cast<HANDLE>(hThread));
    return ctx.result;
}

// OcrService == 1: 微軟AI OCR (Microsoft.Windows.AI.Imaging.TextRecognizer, Copilot+ NPU OCR)
inline std::wstring perform_ocr_microsoft_ai_ocr(const std::string& png_bytes) {
    if (png_bytes.empty()) return L"";
    OcrWorkerContext ctx;
    ctx.png_bytes = &png_bytes;
    ctx.use_npu_ai = true;
    uintptr_t hThread = _beginthreadex(nullptr, 0, OcrMtaWorkerProc, &ctx, 0, nullptr);
    if (!hThread) return L"";
    WaitForSingleObject(reinterpret_cast<HANDLE>(hThread), 15000);
    CloseHandle(reinterpret_cast<HANDLE>(hThread));
    return ctx.result;
}

} // namespace win_ai_ocr
} // namespace mouseinc
