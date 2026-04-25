#pragma once
#include <windows.h>
#include <vector>
#include <functional>
#include <atomic>
#include <string>
#include <array>

namespace hm {
struct Keybind {
    int         id = -1;
    std::string label;
    WORD        vkCode    = 0;
    bool        ctrl      = false;
    bool        shift     = false;
    bool        alt       = false;
    std::string displayName = "None";
};

enum class HookEvt : uint8_t { KeyDown, KeyUp, MouseMove, MouseDown, MouseUp };

struct HookEventData {
    HookEvt  evt;
    WORD     vkCode   = 0;
    int      x = 0, y = 0;
    int      button   = 0;
    DWORD    timeMs   = 0;
};

class HookManager {
public:
    static HookManager& Get();
    bool Init();
    void Shutdown();
    void Poll();

    int   RegisterKeybind(const std::string& label, WORD vk, bool ctrl, bool shift, bool alt);
    void  UpdateKeybind(int id, WORD vk, bool ctrl, bool shift, bool alt);
    const std::vector<Keybind>& GetKeybinds() const;

    using KeybindCb = std::function<void(int id)>;
    void  SetKeybindCb(KeybindCb cb);

    void StartRecording();
    void StopRecording();
    bool IsRecording() const;

    using RecordCb = std::function<void(const HookEventData&)>;
    void SetRecordCb(RecordCb cb);

    void LoadKeybinds(const std::string& filepath = "hm_keybinds.dat");
    void SaveKeybinds(const std::string& filepath = "hm_keybinds.dat") const;

    int GetDroppedEvents() const { return m_droppedEvents.load(std::memory_order_relaxed); }
    void ResetDroppedEvents() { m_droppedEvents.store(0, std::memory_order_relaxed); }

private:
    HookManager();
    ~HookManager();
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

    static LRESULT CALLBACK KbProc(int nCode, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK MsProc(int nCode, WPARAM wp, LPARAM lp);

    HHOOK  m_kbHook   = nullptr;
    HHOOK  m_msHook   = nullptr;
    std::vector<Keybind> m_kbs;
    int  m_nextId = 0;

    // FIX #2: Small queue instead of single atomic so concurrent
    // keybinds within one frame aren't silently dropped
    static constexpr int kMaxTriggers = 8;
    int  m_triggerQueue[kMaxTriggers] = {};
    std::atomic<int> m_triggerHead{0};
    std::atomic<int> m_triggerTail{0};

    KeybindCb         m_kbCb;

    std::atomic<bool> m_recording{false};
    RecordCb          m_recCb;
    int m_lastMouseX = -1;
    int m_lastMouseY = -1;

    // NOTE: Both producer (KbProc/MsProc via OS hook dispatch on main thread)
    // and consumer (Poll on main thread) run on the same thread, so the
    // atomics here are correct but heavier than strictly necessary.
    static constexpr int kMaxQueue = 8192;
    HookEventData      m_recQueue[kMaxQueue];
    std::atomic<int>   m_recQueueHead{0};
    std::atomic<int>   m_recQueueTail{0};
    std::atomic<int>   m_droppedEvents{0};

    // FIX #1: Edge-trigger tracking with bounds-safe access
    static constexpr int kVkMax = 256;
    std::array<bool, kVkMax> m_keyStates{};

    void EnqueueRecord(const HookEventData& d);
    void FlushRecordQueue();
    void EnqueueTrigger(int id);

    static HookManager* s_inst;
};
} // namespace hm
