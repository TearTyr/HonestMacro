#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>

namespace hm {
enum class ActionType : uint8_t { MouseMove = 0, MouseDown, MouseUp, KeyDown, KeyUp, Delay, WaitVisionClick };

inline const char* ActionTypeStr(ActionType t) {
    switch (t) {
        case ActionType::MouseMove: return "Mouse Move";
        case ActionType::MouseDown: return "Mouse Down";
        case ActionType::MouseUp:   return "Mouse Up";
        case ActionType::KeyDown:   return "Key Down";
        case ActionType::KeyUp:     return "Key Up";
        case ActionType::Delay:     return "Delay";
        case ActionType::WaitVisionClick: return "Vision Click";
    } return "?";
}

struct MacroAction {
    ActionType  type     = ActionType::Delay;
    DWORD       tsMs     = 0;
    int         x = 0, y = 0;
    int         button   = 0;
    WORD        vkCode   = 0;
    int         delayMs  = 0;
    int         visionTargetId = -1;
    std::string keyName;
};

enum class EngineState : uint8_t { Idle, Recording, Playing };

struct PlaybackConfig {
    float       speed         = 1.0f;
    bool        smartForce    = true;
    bool        bgMode        = false;
    bool        muteUnfocused = true;
    bool        pauseUnfocused = true;
    bool        humanizeOn    = false;
    float       jitter        = 15.0f;
    bool        relativeMouse = false;
    bool        bezier        = false;
    HWND        target        = nullptr;
    std::string targetExeName;
    RECT        recRect       = {0,0,0,0};
    bool        altTabComp         = true;
    int         altTabCompMs       = 250;
    bool        skipDelayOnRefocus = false;
};


class MacroEngine {
public:
    static MacroEngine& Get();
    void Init(); void Shutdown();
    bool SaveMacro(const std::string& filepath);
    bool LoadMacro(const std::string& filepath);
    void StartRecording(); void StopRecording();
    void StartPlayback(); void StopPlayback();
    void EmergencyStop(); bool IsEmergencyStopped() const;
    EngineState GetState() const;
    bool IsRecording() const { return GetState() == EngineState::Recording; }
    bool IsPlaying()   const { return GetState() == EngineState::Playing; }

    std::vector<MacroAction> GetActions() const;

    void ClearActions();
    void RemoveAction(int index);
    void MoveAction(int from, int to);
    void InsertDelay(int index, int ms);
    void InsertAction(int index, const MacroAction& a);
    void UpdateAction(int index, const MacroAction& a);
    void RecalcTimestamps();
    static std::string VkToName(WORD vk);
    static std::string GetExeNameFromHwnd(HWND hwnd);
    bool IsTargetFocusedAndValid(const PlaybackConfig& cfg);

    mutable std::mutex m_configMutex;

    float               playbackSpeed   = 1.0f;
    int                 loopCount       = 1;
    bool                infiniteLoop    = true;
    bool                smartForcing    = true;
    bool                backgroundMode  = false;
    bool                muteUnfocused   = true;
    bool                pauseUnfocused  = true;
    bool                humanize        = false;
    float               jitterMs        = 15.0f;
    bool                relativeMouse   = false;
    bool                bezierPaths     = false;
    bool                enableAltTabCompensation = true;
    int                 altTabCompensationMs     = 250;
    bool                skipDelayOnRefocus       = false;

    HWND                targetHwnd      = nullptr;
    
    std::string         targetTitle;
    DWORD               targetPid       = 0;
    std::string         targetExeName;

    std::atomic<bool>     emergencyFlag{false};
    std::atomic<uint32_t> m_heartbeat{0};
    std::atomic<bool>     m_watchdogTripped{false};
    RECT                  m_recWindowRect = {0,0,0,0};

    using StateChangedCb = std::function<void(EngineState)>;
    void SetStateChangedCb(StateChangedCb cb);
    void OnHookEvent(const struct HookEventData& evt);
    static bool ForceForeground(HWND hwnd);
    void SetPowerState(bool active);
    void LogEvent(const char* evt, const char* detail);

private:
    MacroEngine() = default;
    ~MacroEngine();
    void PlaybackThread();
    PlaybackConfig SnapshotConfig() const;

    bool  m_recordingPaused = false;
    DWORD m_pauseStartTs    = 0;
    std::vector<MacroAction> m_actions;
    DWORD                    m_recStartTs = 0;
    std::vector<WORD>        m_heldKeys;
    std::vector<int>         m_heldButtons;
    std::atomic<EngineState> m_state{EngineState::Idle};
    std::thread              m_playThread;
    StateChangedCb           m_stateCb;
    mutable std::mutex       m_actionMutex;
    HANDLE                   m_stopEvent = nullptr;
    FILE*                    m_logFile = nullptr;
    std::mutex               m_logMutex;
    int                      m_logCount = 0;
};
} // namespace hm
