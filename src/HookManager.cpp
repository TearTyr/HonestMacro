#include "HookManager.h"
#include <cstdio>
#include <fstream>

namespace hm {
HookManager* HookManager::s_inst = nullptr;

static std::string KbDisplayName(WORD vk, bool ctrl, bool shift, bool alt) {
    std::string s;
    if (ctrl)  s += "Ctrl+";
    if (shift) s += "Shift+";
    if (alt)   s += "Alt+";
    char buf[64] = {};
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = (LONG)(sc << 16);
    if (vk >= VK_PRIOR && vk <= VK_HELP) lp |= (1L << 24);
    if (!GetKeyNameTextA(lp, buf, 64)) {
        if (vk >= VK_F1 && vk <= VK_F24) snprintf(buf, 64, "F%d", (int)(vk - VK_F1 + 1));
        else snprintf(buf, 64, "0x%02X", (int)vk);
    }
    return s + buf;
}

HookManager::HookManager()  { s_inst = this; m_keyStates.fill(false); }
HookManager::~HookManager() { Shutdown(); s_inst = nullptr; }
HookManager& HookManager::Get() { static HookManager inst; return inst; }

bool HookManager::Init() {
    if (m_kbHook) return true;
    m_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, GetModuleHandleW(nullptr), 0);
    return m_kbHook != nullptr;
}

void HookManager::Shutdown() {
    StopRecording();
    if (m_kbHook)  { UnhookWindowsHookEx(m_kbHook);  m_kbHook  = nullptr; }
}

int HookManager::RegisterKeybind(const std::string& label, WORD vk, bool ctrl, bool shift, bool alt) {
    Keybind kb; kb.id = m_nextId++; kb.label = label;
    kb.vkCode = vk; kb.ctrl = ctrl; kb.shift = shift; kb.alt = alt;
    kb.displayName = KbDisplayName(vk, ctrl, shift, alt);
    m_kbs.push_back(kb); return kb.id;
}

void HookManager::UpdateKeybind(int id, WORD vk, bool ctrl, bool shift, bool alt) {
    for (auto& k : m_kbs) {
        if (k.id == id) {
            k.vkCode = vk; k.ctrl = ctrl; k.shift = shift; k.alt = alt;
            k.displayName = KbDisplayName(vk, ctrl, shift, alt);
            break;
        }
    }
}

const std::vector<Keybind>& HookManager::GetKeybinds() const { return m_kbs; }
void  HookManager::SetKeybindCb(KeybindCb cb) { m_kbCb = cb; }

void HookManager::StartRecording() {
    m_recording.store(true); m_recQueueHead.store(0); m_recQueueTail.store(0);
    m_lastMouseX = -1; m_lastMouseY = -1; m_droppedEvents.store(0);
    if (!m_msHook) m_msHook = SetWindowsHookExW(WH_MOUSE_LL, MsProc, GetModuleHandleW(nullptr), 0);
}

void HookManager::StopRecording() {
    m_recording.store(false);
    if (m_msHook) { UnhookWindowsHookEx(m_msHook); m_msHook = nullptr; }
}

bool HookManager::IsRecording() const { return m_recording.load(); }
void HookManager::SetRecordCb(RecordCb cb) { m_recCb = cb; }

void HookManager::EnqueueRecord(const HookEventData& d) {
    int head = m_recQueueHead.load(std::memory_order_relaxed);
    int next = (head + 1) % kMaxQueue;
    if (next == m_recQueueTail.load(std::memory_order_relaxed)) {
        m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    m_recQueue[head] = d;
    m_recQueueHead.store(next, std::memory_order_relaxed);
}

void HookManager::FlushRecordQueue() {
    if (!m_recCb) return;
    while (m_recQueueTail.load(std::memory_order_relaxed) != m_recQueueHead.load(std::memory_order_relaxed)) {
        int tail = m_recQueueTail.load(std::memory_order_relaxed);
        m_recCb(m_recQueue[tail]);
        m_recQueueTail.store((tail + 1) % kMaxQueue, std::memory_order_relaxed);
    }
}

// FIX #2: Enqueue keybind trigger into a small ring buffer
void HookManager::EnqueueTrigger(int id) {
    int head = m_triggerHead.load(std::memory_order_relaxed);
    int next = (head + 1) % kMaxTriggers;
    if (next == m_triggerTail.load(std::memory_order_relaxed))
        return; // drop oldest would add complexity; just drop silently
    m_triggerQueue[head] = id;
    m_triggerHead.store(next, std::memory_order_relaxed);
}

void HookManager::Poll() {
    // FIX #2: Drain all queued triggers, not just one
    while (m_triggerTail.load(std::memory_order_relaxed) != m_triggerHead.load(std::memory_order_relaxed)) {
        int tail = m_triggerTail.load(std::memory_order_relaxed);
        int id = m_triggerQueue[tail];
        m_triggerTail.store((tail + 1) % kMaxTriggers, std::memory_order_relaxed);
        if (m_kbCb) m_kbCb(id);
    }
    FlushRecordQueue();
}

void HookManager::SaveKeybinds(const std::string& filepath) const {
    std::ofstream f(filepath, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write("HMKYB", 5);
    uint8_t ver = 1; f.write(reinterpret_cast<char*>(&ver), 1);
    uint32_t count = static_cast<uint32_t>(m_kbs.size());
    f.write(reinterpret_cast<char*>(&count), sizeof(count));
    for (const auto& kb : m_kbs) {
        uint16_t len = static_cast<uint16_t>(std::min<size_t>(0xFFFF, kb.label.size()));
        f.write(reinterpret_cast<char*>(&len), sizeof(len));
        f.write(kb.label.c_str(), len);
        f.write(reinterpret_cast<const char*>(&kb.vkCode), sizeof(kb.vkCode));
        f.write(reinterpret_cast<const char*>(&kb.ctrl), sizeof(kb.ctrl));
        f.write(reinterpret_cast<const char*>(&kb.shift), sizeof(kb.shift));
        f.write(reinterpret_cast<const char*>(&kb.alt), sizeof(kb.alt));
    }
}

void HookManager::LoadKeybinds(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f) return;
    char magic[5]; f.read(magic, 5);
    if (std::string(magic, 5) != "HMKYB") return;
    uint8_t ver; f.read(reinterpret_cast<char*>(&ver), 1);
    uint32_t count; f.read(reinterpret_cast<char*>(&count), sizeof(count));
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t len; f.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string label(len, '\0');
        if (len > 0) f.read(&label[0], len);
        WORD vk; bool ctrl, shift, alt;
        f.read(reinterpret_cast<char*>(&vk), sizeof(vk));
        f.read(reinterpret_cast<char*>(&ctrl), sizeof(ctrl));
        f.read(reinterpret_cast<char*>(&shift), sizeof(shift));
        f.read(reinterpret_cast<char*>(&alt), sizeof(alt));
        for (auto& kb : m_kbs) {
            if (kb.label == label) { UpdateKeybind(kb.id, vk, ctrl, shift, alt); break; }
        }
    }
}

static bool IsKeybindKey(const std::vector<Keybind>& kbs, WORD vk, bool ctrl, bool shift, bool alt) {
    for (const auto& k : kbs)
        if (k.vkCode == vk && k.ctrl == ctrl && k.shift == shift && k.alt == alt) return true;
    return false;
}

LRESULT CALLBACK HookManager::KbProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode >= 0 && s_inst) {
        KBDLLHOOKSTRUCT* ks = (KBDLLHOOKSTRUCT*)lp;
        bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

        // FIX #1: Bounds check before accessing m_keyStates
        WORD vk = (WORD)ks->vkCode;
        if (vk < kVkMax) {
            if (down && !s_inst->m_keyStates[vk]) {
                s_inst->m_keyStates[vk] = true;
                for (const auto& kb : s_inst->m_kbs) {
                    if (kb.vkCode == vk && kb.ctrl == ctrl && kb.shift == shift && kb.alt == alt) {
                        s_inst->EnqueueTrigger(kb.id);
                        break;
                    }
                }
            } else if (!down) {
                s_inst->m_keyStates[vk] = false;
            }
        }

        if (s_inst->m_recording.load() && !IsKeybindKey(s_inst->m_kbs, vk, ctrl, shift, alt)) {
            HookEventData d; d.evt = down ? HookEvt::KeyDown : HookEvt::KeyUp;
            d.vkCode = vk; d.timeMs = GetTickCount();
            POINT pt; GetCursorPos(&pt); d.x = pt.x; d.y = pt.y;
            s_inst->EnqueueRecord(d);
        }
    }
    return s_inst && s_inst->m_kbHook ? CallNextHookEx(s_inst->m_kbHook, nCode, wp, lp) : CallNextHookEx(nullptr, nCode, wp, lp);
}

LRESULT CALLBACK HookManager::MsProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode >= 0 && s_inst && s_inst->m_recording.load()) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
        if (wp == WM_MOUSEMOVE) {
            if (ms->pt.x == s_inst->m_lastMouseX && ms->pt.y == s_inst->m_lastMouseY)
                return CallNextHookEx(s_inst->m_msHook, nCode, wp, lp);
            s_inst->m_lastMouseX = (int)ms->pt.x; s_inst->m_lastMouseY = (int)ms->pt.y;
        }
        HookEventData d; d.timeMs = GetTickCount(); d.x = (int)ms->pt.x; d.y = (int)ms->pt.y;
        switch (wp) {
            case WM_MOUSEMOVE:   d.evt = HookEvt::MouseMove; break;
            case WM_LBUTTONDOWN: d.evt = HookEvt::MouseDown; d.button = 0; break;
            case WM_LBUTTONUP:   d.evt = HookEvt::MouseUp;   d.button = 0; break;
            case WM_MBUTTONDOWN: d.evt = HookEvt::MouseDown; d.button = 1; break;
            case WM_MBUTTONUP:   d.evt = HookEvt::MouseUp;   d.button = 1; break;
            case WM_RBUTTONDOWN: d.evt = HookEvt::MouseDown; d.button = 2; break;
            case WM_RBUTTONUP:   d.evt = HookEvt::MouseUp;   d.button = 2; break;
            default: return CallNextHookEx(s_inst->m_msHook, nCode, wp, lp);
        }
        s_inst->EnqueueRecord(d);
    }
    return s_inst && s_inst->m_msHook ? CallNextHookEx(s_inst->m_msHook, nCode, wp, lp) : CallNextHookEx(nullptr, nCode, wp, lp);
}
} // namespace hm
