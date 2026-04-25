#include "VisionEngine.h"
#include <GL/gl.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <fstream>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

namespace hm {
VisionEngine& VisionEngine::Get() { static VisionEngine inst; return inst; }

VisionEngine::~VisionEngine() {
    for (auto& t : m_targets) {
        if (t.glTexture) {
            DeleteGLTexture(t.glTexture);
            t.glTexture = 0;
        }
    }
    if (m_fullTex) {
        DeleteGLTexture(m_fullTex);
        m_fullTex = 0;
    }
}

bool VisionEngine::SaveVisionData(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int count = static_cast<int>(m_targets.size());
    f.write(reinterpret_cast<char*>(&count), sizeof(count));
    for (auto& t : m_targets) {
        f.write(reinterpret_cast<char*>(&t.id), sizeof(t.id));
        int nlen = static_cast<int>(t.name.size());
        f.write(reinterpret_cast<char*>(&nlen), sizeof(nlen));
        f.write(t.name.c_str(), nlen);
        f.write(reinterpret_cast<char*>(&t.width), sizeof(t.width));
        f.write(reinterpret_cast<char*>(&t.height), sizeof(t.height));
        f.write(reinterpret_cast<char*>(&t.captureX), sizeof(t.captureX));
        f.write(reinterpret_cast<char*>(&t.captureY), sizeof(t.captureY));
        f.write(reinterpret_cast<char*>(&t.confidence), sizeof(t.confidence));
        f.write(reinterpret_cast<char*>(&t.clickOffsetX), sizeof(t.clickOffsetX));
        f.write(reinterpret_cast<char*>(&t.clickOffsetY), sizeof(t.clickOffsetY));
        f.write(reinterpret_cast<char*>(&t.timeoutMs), sizeof(t.timeoutMs));
        size_t psize = t.pixels.size();
        f.write(reinterpret_cast<char*>(&psize), sizeof(psize));
        if (psize > 0) f.write(reinterpret_cast<char*>(t.pixels.data()), psize);
    }
    return true;
}

bool VisionEngine::LoadVisionData(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    ClearTargets();
    int count = 0;
    if (!f.read(reinterpret_cast<char*>(&count), sizeof(count))) return false;
    int maxId = -1;
    for (int i = 0; i < count; ++i) {
        ImageTarget t;
        f.read(reinterpret_cast<char*>(&t.id), sizeof(t.id));
        if (t.id > maxId) maxId = t.id;
        int nlen = 0;
        f.read(reinterpret_cast<char*>(&nlen), sizeof(nlen));
        t.name.resize(nlen);
        f.read(&t.name[0], nlen);
        f.read(reinterpret_cast<char*>(&t.width), sizeof(t.width));
        f.read(reinterpret_cast<char*>(&t.height), sizeof(t.height));
        f.read(reinterpret_cast<char*>(&t.captureX), sizeof(t.captureX));
        f.read(reinterpret_cast<char*>(&t.captureY), sizeof(t.captureY));
        f.read(reinterpret_cast<char*>(&t.confidence), sizeof(t.confidence));
        f.read(reinterpret_cast<char*>(&t.clickOffsetX), sizeof(t.clickOffsetX));
        f.read(reinterpret_cast<char*>(&t.clickOffsetY), sizeof(t.clickOffsetY));
        f.read(reinterpret_cast<char*>(&t.timeoutMs), sizeof(t.timeoutMs));
        size_t psize = 0;
        f.read(reinterpret_cast<char*>(&psize), sizeof(psize));
        if (psize > 0) {
            t.pixels.resize(psize);
            f.read(reinterpret_cast<char*>(t.pixels.data()), psize);
            t.glTexture = CreateGLTexture(t.pixels.data(), t.width, t.height);
            t.textureDirty = false;
        }
        m_targets.push_back(t);
    }
    m_nextId = maxId + 1;
    return true;
}

void VisionEngine::ClearTargets() {
    for (auto& t : m_targets) { if (t.glTexture) DeleteGLTexture(t.glTexture); }
    m_targets.clear();
}

bool VisionEngine::CaptureScreen(std::vector<uint8_t>& outBgra, int& outW, int& outH, HWND hwnd, bool background) {
    HDC hScreen = GetDC(hwnd);
    if (!hScreen) return false;
    int w = 0, h = 0;
    if (hwnd && background) { RECT rc; GetClientRect(hwnd, &rc); w = rc.right - rc.left; h = rc.bottom - rc.top; }
    else { w = GetSystemMetrics(SM_CXSCREEN); h = GetSystemMetrics(SM_CYSCREEN); }
    if (w <= 0 || h <= 0) { ReleaseDC(hwnd, hScreen); return false; }

    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreen, w, h);
    HBITMAP hOld = static_cast<HBITMAP>(SelectObject(hDC, hBmp));
    if (hwnd && background) PrintWindow(hwnd, hDC, PW_CLIENTONLY | 0x00000002);
    else BitBlt(hDC, 0, 0, w, h, hScreen, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi); bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = BI_RGB;
    outW = w; outH = h;
    outBgra.resize(static_cast<size_t>(w) * h * 4);
    GetDIBits(hDC, hBmp, 0, static_cast<UINT>(h), outBgra.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    SelectObject(hDC, hOld); DeleteObject(hBmp); DeleteDC(hDC); ReleaseDC(hwnd, hScreen);
    return true;
}

bool VisionEngine::CaptureFullScreen(HWND hwnd, bool background) {
    if (!CaptureScreen(m_fullPixels, m_fullW, m_fullH, hwnd, background)) return false;
    if (m_fullTex) UpdateGLTexture(m_fullTex, m_fullPixels.data(), m_fullW, m_fullH);
    else m_fullTex = CreateGLTexture(m_fullPixels.data(), m_fullW, m_fullH);
    return true;
}

int VisionEngine::CropAndAddTarget(int rx, int ry, int rw, int rh, const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    rx = std::max(0, std::min(rx, m_fullW - 1)); ry = std::max(0, std::min(ry, m_fullH - 1));
    rw = std::min(rw, m_fullW - rx); rh = std::min(rh, m_fullH - ry);
    if (rw <= 0 || rh <= 0) return -1;
    int id = AddTarget(name);
    ImageTarget* t = GetTarget(id);
    t->width = rw; t->height = rh; t->captureX = rx; t->captureY = ry;
    t->pixels.resize(static_cast<size_t>(rw) * rh * 4);
    for (int row = 0; row < rh; ++row) {
        const uint8_t* src = m_fullPixels.data() + ((ry + row) * m_fullW + rx) * 4;
        uint8_t* dst = t->pixels.data() + row * rw * 4;
        memcpy(dst, src, rw * 4);
    }
    if (t->glTexture) DeleteGLTexture(t->glTexture);
    t->glTexture = CreateGLTexture(t->pixels.data(), rw, rh);
    t->textureDirty = false;
    return id;
}

int VisionEngine::AddTarget(const std::string& name) {
    ImageTarget t; t.id = m_nextId++;
    t.name = name.empty() ? "Target " + std::to_string(t.id) : name;
    m_targets.push_back(t); return t.id;
}

void VisionEngine::RemoveTarget(int id) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto it = m_targets.begin(); it != m_targets.end(); ++it) {
        if (it->id == id) { if (it->glTexture) DeleteGLTexture(it->glTexture); m_targets.erase(it); return; }
    }
}

ImageTarget* VisionEngine::GetTarget(int id) {
    for (auto& t : m_targets) { if (t.id == id) return &t; }
    return nullptr;
}

std::vector<ImageTarget>& VisionEngine::GetTargets() { return m_targets; }

MatchResult VisionEngine::MatchTemplate(const std::vector<uint8_t>& screen, int sw, int sh, const std::vector<uint8_t>& tpl, int tw, int th, float threshold) {
    MatchResult best; best.found = false; best.score = 0.0f;
    if (screen.empty() || tpl.empty() || sw <= 0 || sh <= 0 || tw <= 0 || th <= 0 || tw > sw || th > sh) return best;

    int coarseStep = 4; int bestCX = 0, bestCY = 0; float bestCScore = 0.0f;
    for (int y = 0; y <= sh - th; y += coarseStep) {
        for (int x = 0; x <= sw - tw; x += coarseStep) {
            float score = 0.0f; int count = 0;
            for (int ty = 0; ty < th; ty += 4) {
                for (int tx = 0; tx < tw; tx += 4) {
                    const uint8_t* sp = screen.data() + ((y + ty) * sw + (x + tx)) * 4;
                    const uint8_t* tp = tpl.data() + (ty * tw + tx) * 4;
                    float diff = 0.0f;
                    for (int c = 0; c < 3; ++c) diff += static_cast<float>(std::abs(static_cast<int>(sp[c]) - static_cast<int>(tp[c])));
                    score += 1.0f - diff / (3.0f * 255.0f); count++;
                }
            }
            if (count > 0) score /= static_cast<float>(count);
            if (score > bestCScore) { bestCScore = score; bestCX = x; bestCY = y; }
        }
    }

    int searchR = 8;
    int y0 = std::max(0, bestCY - searchR), y1 = std::min(sh - th, bestCY + searchR);
    int x0 = std::max(0, bestCX - searchR), x1 = std::min(sw - tw, bestCX + searchR);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float score = 0.0f; int count = 0;
            for (int ty = 0; ty < th; ty += 2) {
                for (int tx = 0; tx < tw; tx += 2) {
                    const uint8_t* sp = screen.data() + ((y + ty) * sw + (x + tx)) * 4;
                    const uint8_t* tp = tpl.data() + (ty * tw + tx) * 4;
                    float diff = 0.0f;
                    for (int c = 0; c < 3; ++c) diff += static_cast<float>(std::abs(static_cast<int>(sp[c]) - static_cast<int>(tp[c])));
                    score += 1.0f - diff / (3.0f * 255.0f); count++;
                }
            }
            if (count > 0) score /= static_cast<float>(count);
            if (score > best.score) { best.score = score; best.x = x; best.y = y; }
        }
    }
    best.found = (best.score >= threshold);
    if (best.found) { best.x += (tw / 2); best.y += (th / 2); }
    return best;
}

MatchResult VisionEngine::WaitForTarget(int targetId, int timeoutMs, HWND hwnd, bool background, const std::atomic<bool>* cancelFlag) {
    std::vector<uint8_t> tplPixels;
    int tw = 0, th = 0;
    float confidence = 0.80f;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        ImageTarget* t = GetTarget(targetId);
        if (!t || t->pixels.empty()) return {};
        tplPixels   = t->pixels;
        tw          = t->width;
        th          = t->height;
        confidence  = t->confidence;
    }

    std::vector<uint8_t> screen;
    int sw = 0, sh = 0;

    DWORD start = GetTickCount();
    while (true) {
        if (cancelFlag && cancelFlag->load()) break;
        if (GetTickCount() - start > static_cast<DWORD>(timeoutMs)) break;

        if (!CaptureScreen(screen, sw, sh, hwnd, background)) {
            Sleep(50); continue;
        }

        // Guard against degenerate captures
        if (sw < tw || sh < th || screen.empty()) {
            Sleep(50); continue;
        }

        MatchResult result = MatchTemplate(screen, sw, sh, tplPixels, tw, th, confidence);
        if (result.found) return result;
        Sleep(16);
    }
    return {};
}


unsigned int VisionEngine::CreateGLTexture(const uint8_t* bgra, int w, int h) {
    unsigned int tex = 0; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    glBindTexture(GL_TEXTURE_2D, 0); return tex;
}

void VisionEngine::UpdateGLTexture(unsigned int tex, const uint8_t* bgra, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, tex); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, bgra); glBindTexture(GL_TEXTURE_2D, 0);
}

void VisionEngine::DeleteGLTexture(unsigned int tex) { if (tex) glDeleteTextures(1, &tex); }
unsigned int VisionEngine::GetFullScreenTexture() const { return m_fullTex; }
int VisionEngine::GetFullScreenWidth() const { return m_fullW; }
int VisionEngine::GetFullScreenHeight() const { return m_fullH; }
const std::vector<uint8_t>& VisionEngine::GetFullScreenPixels() const { return m_fullPixels; }
} // namespace hm
