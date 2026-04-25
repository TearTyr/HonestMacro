#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>

namespace hm {

struct ImageTarget {
    int id = -1;
    std::string name;
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    int captureX = 0;
    int captureY = 0;
    float confidence = 0.80f;
    int clickOffsetX = 0;
    int clickOffsetY = 0;
    int timeoutMs = 2500;
    unsigned int glTexture = 0;
    bool textureDirty = true;
};

struct MatchResult {
    bool found = false;
    int x = 0;
    int y = 0;
    float score = 0.0f;
};

class VisionEngine {
public:
    static VisionEngine& Get();

    mutable std::recursive_mutex m_mutex;

    static bool CaptureScreen(
        std::vector<uint8_t>& outBgra,
        int& outW,
        int& outH,
        HWND hwnd = nullptr,
        bool background = false
    );

    int AddTarget(const std::string& name);
    void RemoveTarget(int id);
    ImageTarget* GetTarget(int id);
    std::vector<ImageTarget>& GetTargets();
    void ClearTargets();

    bool SaveVisionData(const std::string& path);
    bool LoadVisionData(const std::string& path);

    MatchResult MatchTemplate(
        const std::vector<uint8_t>& screen,
        int sw,
        int sh,
        const std::vector<uint8_t>& tpl,
        int tw,
        int th,
        float threshold
    );

    MatchResult WaitForTarget(
        int targetId,
        int timeoutMs,
        HWND hwnd = nullptr,
        bool background = false,
        const std::atomic<bool>* cancelFlag = nullptr
    );

    static unsigned int CreateGLTexture(const uint8_t* bgra, int w, int h);
    static void UpdateGLTexture(unsigned int tex, const uint8_t* bgra, int w, int h);
    static void DeleteGLTexture(unsigned int tex);

    bool CaptureFullScreen(HWND hwnd = nullptr, bool background = false);
    unsigned int GetFullScreenTexture() const;
    int GetFullScreenWidth() const;
    int GetFullScreenHeight() const;
    const std::vector<uint8_t>& GetFullScreenPixels() const;

    int CropAndAddTarget(int x, int y, int w, int h, const std::string& name);

private:
    VisionEngine() = default;
    ~VisionEngine();

    std::vector<ImageTarget> m_targets;
    int m_nextId = 0;

    std::vector<uint8_t> m_fullPixels;
    int m_fullW = 0;
    int m_fullH = 0;
    int m_fullTexW = 0;
    int m_fullTexH = 0;
    unsigned int m_fullTex = 0;
};

} // namespace hm
