#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "runtimeobject.lib")

namespace mouseinc {
namespace win_ai_ocr {

// ============================================================================
// Microsoft.Windows.AI.Imaging.TextRecognizer & Microsoft.Graphics.Imaging.ImageBuffer
// WinRT ABI definitions matching:
// https://learn.microsoft.com/zh-tw/windows/ai/apis/text-recognition
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

// IInspectable-derived raw WinRT interfaces for dynamic activation on Copilot+ NPU systems
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

// Decode PNG bytes in memory into a Windows::Graphics::Imaging::SoftwareBitmap
// following the exact steps in https://learn.microsoft.com/zh-tw/windows/ai/apis/text-recognition
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

// Step A: Try Windows App SDK AI TextRecognizer (Microsoft.Windows.AI.Imaging.TextRecognizer)
// Reference: https://learn.microsoft.com/zh-tw/windows/ai/apis/text-recognition
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

        // ImageBuffer::CreateForSoftwareBitmap(bitmap)
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

        // TextRecognizer::CreateAsync().get()
        winrt::com_ptr<::IInspectable> createOpInsp;
        if (FAILED(recognizerStatics->CreateAsync(createOpInsp.put())) || !createOpInsp) {
            return L"";
        }
        auto createOp = createOpInsp.try_as<winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::IInspectable>>();
        if (!createOp) return L"";

        auto recognizerInsp = createOp.get();
        auto textRecognizer = recognizerInsp.try_as<ITextRecognizer>();
        if (!textRecognizer) return L"";

        // RecognizeTextFromImage(imageBuffer)
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

// Step B: Windows Platform SDK Windows.Media.Ocr.OcrEngine on the SoftwareBitmap
// Built into all Windows 10 / 11 systems (works on both NPU and non-NPU PCs offline)
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
            // Remove unwanted spaces between CJK characters that Windows.Media.Ocr sometimes inserts
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

    // 1. Try User Profile Languages first
    try {
        OcrEngine userEngine = OcrEngine::TryCreateFromUserProfileLanguages();
        std::wstring text = recognize_with_engine(userEngine);
        if (!text.empty()) return text;
    } catch (...) {}

    // 2. Try Traditional Chinese, Simplified Chinese, English, Japanese in order
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
                // "微軟AI OCR" (OcrService == 3): Microsoft.Windows.AI.Imaging.TextRecognizer (NPU)
                ctx->result = try_microsoft_windows_ai_text_recognizer(bitmap);
            } else {
                // "微軟OCR" (OcrService == 0): Windows.Media.Ocr.OcrEngine (Built-in lightweight Win10/11 OCR)
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

// OcrService == 3: 微軟AI OCR (Microsoft.Windows.AI.Imaging.TextRecognizer, Copilot+ NPU OCR)
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
