#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <string>

namespace mouseinc {

inline void play_embedded_wave(const wchar_t* res_name) {
    HMODULE hMod = GetModuleHandleW(nullptr);
    HRSRC hRes = FindResourceW(hMod, res_name, L"WAVE");
    if (!hRes) return;
    HGLOBAL hMem = LoadResource(hMod, hRes);
    if (!hMem) return;
    const void* pData = LockResource(hMem);
    if (!pData) return;
    PlaySoundW(reinterpret_cast<LPCWSTR>(pData), hMod, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

inline void play_key_sound(int index) {
    if (index < 0) index = 0;
    if (index > 4) index = index % 5;
    wchar_t name[16] = {0};
    wsprintfW(name, L"KEY%d", index);
    play_embedded_wave(name);
}

inline void play_volume_sound(int index) {
    if (index < 0) index = 0;
    if (index > 1) index = index % 2;
    wchar_t name[16] = {0};
    wsprintfW(name, L"VOLUME%d", index);
    play_embedded_wave(name);
}

} // namespace mouseinc
