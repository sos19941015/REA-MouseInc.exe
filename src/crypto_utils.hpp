#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace mouseinc {

inline std::string base64_encode(const std::string& in) {
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kTable[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(kTable[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

inline std::string base64_decode(const std::string& in) {
    std::vector<int> T(256, -1);
    static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) T[(unsigned char)kTable[i]] = i;
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

inline std::string url_encode(const std::string& in) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << (char)c;
        } else {
            escaped << '%' << std::setw(2) << int(c);
        }
    }
    return escaped.str();
}

inline std::string url_decode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size() && std::isxdigit((unsigned char)in[i + 1]) && std::isxdigit((unsigned char)in[i + 2])) {
            std::string hex = in.substr(i + 1, 2);
            out.push_back((char)std::strtol(hex.c_str(), nullptr, 16));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

inline std::string win_crypto_hash(const std::string& data, ALG_ID alg_id) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string hex_out;

    DWORD prov_type = (alg_id == CALG_SHA_256) ? PROV_RSA_AES : PROV_RSA_FULL;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, prov_type, CRYPT_VERIFYCONTEXT)) {
        return "";
    }
    if (CryptCreateHash(hProv, alg_id, 0, 0, &hHash)) {
        if (CryptHashData(hHash, reinterpret_cast<const BYTE*>(data.data()), (DWORD)data.size(), 0)) {
            BYTE hash_buf[64] = {0};
            DWORD hash_len = sizeof(hash_buf);
            if (CryptGetHashParam(hHash, HP_HASHVAL, hash_buf, &hash_len, 0)) {
                std::ostringstream oss;
                oss << std::hex << std::setfill('0');
                for (DWORD i = 0; i < hash_len; i++) {
                    oss << std::setw(2) << (int)hash_buf[i];
                }
                hex_out = oss.str();
            }
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
    return hex_out;
}

inline std::string md5_hex(const std::string& data) {
    return win_crypto_hash(data, CALG_MD5);
}

inline std::string sha1_hex(const std::string& data) {
    return win_crypto_hash(data, CALG_SHA1);
}

inline std::string sha256_hex(const std::string& data) {
    return win_crypto_hash(data, CALG_SHA_256);
}

} // namespace mouseinc
