#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "miniz.h"
#include "MacroEngine.h"
#include "VisionEngine.h"

namespace hm {
namespace FileIO {
template<typename T> inline void Write(std::ofstream& f, const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
template<typename T> inline void Read(std::ifstream& f, T& v) { f.read(reinterpret_cast<char*>(&v), sizeof(T)); }

inline void WriteString(std::ofstream& f, const std::string& s) {
    uint16_t len = (uint16_t)std::min<size_t>(0xFFFF, s.size());
    Write(f, len); if (len > 0) f.write(s.data(), len);
}
inline void ReadString(std::ifstream& f, std::string& s) {
    uint16_t len = 0; Read(f, len); s.resize(len); if (len > 0) f.read(&s[0], len);
}

inline void EnforceSizeLimit(std::vector<uint8_t>& pixels, int& w, int& h, size_t maxRaw = 8'000'000) {
    if (pixels.size() > maxRaw) {
        float scale = std::sqrt((float)maxRaw / (float)pixels.size());
        int nw = std::max(1, (int)(w * scale));
        int nh = std::max(1, (int)(h * scale));
        std::vector<uint8_t> out(nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                memcpy(&out[(y * nw + x) * 4], &pixels[(int)(y / scale) * w * 4 + (int)(x / scale) * 4], 4);
        pixels = std::move(out); w = nw; h = nh;
    }
}

// FIXED #2: Atomic save via .tmp + rename to prevent data loss on failure
inline bool SaveMacro(const std::string& path, const std::vector<MacroAction>& actions, const RECT& recRect) {
    std::string tmpPath = path + ".tmp";
    std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    f.write("HMACV3", 6); f.write("\0\0", 2);
    Write(f, (uint32_t)3);
    Write(f, recRect.left); Write(f, recRect.top);
    Write(f, recRect.right); Write(f, recRect.bottom);
    Write(f, (uint32_t)actions.size());

    for (const auto& a : actions) {
        Write(f, (uint8_t)a.type); Write(f, a.tsMs);
        Write(f, (int32_t)a.x); Write(f, (int32_t)a.y);
        Write(f, (uint8_t)a.button); Write(f, (uint16_t)a.vkCode);
        Write(f, (int32_t)a.delayMs); Write(f, (int32_t)a.visionTargetId);
        WriteString(f, a.keyName);
    }

    auto& vision = VisionEngine::Get();
    auto& targets = vision.GetTargets();
    Write(f, (uint32_t)targets.size());

    for (const auto& t : targets) {
        Write(f, (int32_t)t.id); WriteString(f, t.name);
        Write(f, (int32_t)t.width); Write(f, (int32_t)t.height);
        Write(f, (int32_t)t.captureX); Write(f, (int32_t)t.captureY);
        Write(f, t.confidence);
        Write(f, (int32_t)t.clickOffsetX); Write(f, (int32_t)t.clickOffsetY);
        Write(f, (int32_t)t.timeoutMs);

        std::vector<uint8_t> pixels = t.pixels;
        int w = t.width, h = t.height;
        EnforceSizeLimit(pixels, w, h);
        unsigned long compSize = compressBound(pixels.size());
        std::vector<uint8_t> comp(compSize);
        int res = compress2(comp.data(), &compSize, pixels.data(), pixels.size(), Z_BEST_SPEED);
        if (res != Z_OK) { f.close(); std::remove(tmpPath.c_str()); return false; }

        Write(f, (uint32_t)pixels.size()); Write(f, (uint32_t)compSize);
        f.write(reinterpret_cast<const char*>(comp.data()), compSize);
    }
    f.close();
    std::remove(path.c_str());
    return std::rename(tmpPath.c_str(), path.c_str()) == 0;
}

inline bool LoadMacro(const std::string& path, std::vector<MacroAction>& actions, RECT& recRect) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[8]; f.read(magic, 8);
    if (std::string(magic, 6) != "HMACV3") return false;
    uint32_t version; Read(f, version);
    Read(f, recRect.left); Read(f, recRect.top);
    Read(f, recRect.right); Read(f, recRect.bottom);

    uint32_t actCount; Read(f, actCount);
    actions.resize(actCount);
    for (uint32_t i = 0; i < actCount; i++) {
        auto& a = actions[i];
        uint8_t type, button; uint16_t vk;
        Read(f, type); a.type = (ActionType)type;
        Read(f, a.tsMs);
        int32_t x, y; Read(f, x); Read(f, y); a.x = x; a.y = y;
        Read(f, button); a.button = button;
        Read(f, vk); a.vkCode = (WORD)vk;
        Read(f, a.delayMs); Read(f, a.visionTargetId);
        ReadString(f, a.keyName);
    }

    auto& vision = VisionEngine::Get();
    vision.ClearTargets();
    uint32_t visCount; Read(f, visCount);
    for (uint32_t i = 0; i < visCount; i++) {
        ImageTarget t;
        Read(f, t.id); ReadString(f, t.name);
        int32_t w, h; Read(f, w); Read(f, h); t.width = w; t.height = h;
        Read(f, t.captureX); Read(f, t.captureY);
        Read(f, t.confidence);
        Read(f, t.clickOffsetX); Read(f, t.clickOffsetY);
        Read(f, t.timeoutMs);
        uint32_t rawSize, compSize;
        Read(f, rawSize); Read(f, compSize);
        std::vector<uint8_t> comp(compSize);
        f.read(reinterpret_cast<char*>(comp.data()), compSize);
        t.pixels.resize(rawSize);
        unsigned long decSize = rawSize;
        int res = uncompress(t.pixels.data(), &decSize, comp.data(), compSize);
        if (res != Z_OK || decSize != rawSize) continue;
        t.glTexture = VisionEngine::CreateGLTexture(t.pixels.data(), t.width, t.height);
        vision.GetTargets().push_back(std::move(t));
    }
    return true;
}
} // namespace FileIO
} // namespace hm