# REA-MouseInc.exe

透過 [REA (morluto/rea)](https://github.com/morluto/rea) 逆向工程證據完整復刻的 **MouseInc 2.13.4**（Native C++17 / Win32 API / GDI+），並內嵌 100% 本地離線版官方設定介面（改編自 [shuax/MouseInc.Settings](https://github.com/shuax/MouseInc.Settings)，Vue 3.5 + TypeScript + Vite + Element Plus）。

## ✨ 專案特色

1. **100% 原生 Win32 + GDI+ 架構**
   - 全域低階滑鼠與鍵盤鉤子（`WH_MOUSE_LL` / `WH_KEYBOARD_LL`）
   - 非同步合併繪製（`WM_RENDER_TRACE`）與常駐全螢幕 DIBSection，畫手勢零延遲
   - 原版 GDI+ `GraphicsPath` 白字粗框描邊手勢結果提示、起點圓點與終點方向箭頭
2. **100% 內嵌離線版 Vue 3 設定介面（無須連網）**
   - 將官方 `shuax/MouseInc.Settings` 升級為 Vue 3.5 + Element Plus 單一 HTML 套件（`res/SETTINGS.html`），直接封裝進 `MouseInc.exe` 的 PE `RT_HTML` 資源（`SETTINGS`）
   - 點擊系統托盤「打開設置(S)...」即可透過本機 `127.0.0.1:<port>` 秒開完整繁體中文設定介面
3. **完整功能復刻**
   - 滑鼠手勢（DTW + 方向鏈混合演算法、支援特定程式規則、手勢新增模式 `AddMode`、按住右鍵滾輪切換 `WheelSwitch`）
   - 手勢區域截圖（`Screenshot`）與十字框選截圖（`Snapshot`）、獨立釘選貼圖視窗（`Stick`）、畫中畫（`PiP`）、OCR 文字辨識、透明網格高清截圖（`ScreenshotHQ`）
   - 邊緣滾動（`WheelEdge`）、觸發角（`HotCorner`）、複製增強子選單（雙擊 `Ctrl+C` `ClipboardManager`）
   - 按鍵回顯（`Keycast`）、打字音效（`KeySound`）、音量音效（`VolumeControlSound`）
   - 快速音量調整（`WheelAltControl`：`Alt + 滾輪` 調音量、`Alt + 中鍵` 靜音）、快速移動視窗（`AltDrag`：`Alt + 中鍵拖曳`）
   - 選取文字自動複製（`AutoClip`）、按住左鍵點右鍵快速貼上（`FastPaste`）、穿透滾動（`WheelThrough`）、自然滾動（`WheelNatural`）
   - 輸入法中/英游標狀態提示（`ShowIme`）、檔案對話框快速跳轉（`QuickJump`）、右上角大寫鎖定指示器（`CapsLockLed`）與 30 秒 / `Esc` 自動解除大寫（`CapsUnlock`）
   - 內建演算法（`b64encode` / `b64decode` / `urlencode` / `urldecode` / `md5` / `sha1` / `sha256` / `qrcode` 貼圖顯示 / `CyberChef`）

## 📂 目錄結構

```text
MouseInc-Rebuild/
├── MouseInc.exe               # 編譯完成的單一執行檔（內嵌所有圖示、音效、DEFAULT.json 與離線設定 GUI）
├── MouseInc.Settings.html     # 獨立離線版 Vue 3 設定介面單檔
├── build.bat                  # Visual Studio MSVC (x86) 一鍵編譯腳本
├── src/
│   ├── main.cpp               # 主程式、托盤選單、WH_MOUSE_LL / WH_KEYBOARD_LL 與非同步訊息迴圈
│   ├── config.hpp             # 零依賴 JSON 解析器/序列化器與 MouseInc.json 結構綁定
│   ├── gesture_recognizer.hpp # 滑鼠手勢方向鏈 + DTW 演算法與新增手勢對話框
│   ├── osd_overlay.hpp        # GDI+ 分層透明視窗（手勢軌跡、描邊文字、右下角通知、Keycast、IME、CapsLock）
│   ├── snapshot_windows.hpp   # 截圖框選器、Stick 貼圖視窗、PiP 畫中畫視窗與 OCR
│   ├── actions.hpp            # 完整動作分派引擎（SendKeys、Window、Internal、Screenshot、Algorithm 等）
│   ├── settings_server.hpp    # 本機 127.0.0.1 HTTP 設定伺服器與即時手勢 SVG 預覽產生器
│   ├── sound_player.hpp       # 內嵌 WAVE 資源記憶體播放
│   ├── crypto_utils.hpp       # WinCrypt MD5/SHA1/SHA256、Base64 與 URL 編解碼
│   └── resource.h             # 資源 ID 定義
└── res/
    ├── MouseInc.rc            # PE 資源描述檔
    ├── DEFAULT.json           # 內嵌預設設定與多語系字串表
    ├── SETTINGS.html          # 內嵌離線版 Vue 3 + Element Plus 設定介面
    ├── MouseInc.ico           # 應用程式與系統托盤圖示
    ├── SHADOW.png             # 視窗陰影九宮格邊框素材
    └── KEY0..4.wav / VOLUME0..1.wav # 內嵌按鍵與音量音效
```

## 🛠️ 編譯方式

需要安裝 Visual Studio 2022（含 C++ 桌面開發工具集），直接執行：

```bat
build.bat
```
