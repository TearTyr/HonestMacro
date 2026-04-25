#include "MacroEngine.h"
#include "HookManager.h"
#include "VisionEngine.h"
#include "FileIO.h"
#include <cstdio>
#include <algorithm>
#include <mmsystem.h>
#include <thread>
#include <chrono>
#include <psapi.h>
#include <synchapi.h>

namespace hm {
MacroEngine& MacroEngine::Get() { static MacroEngine inst; return inst; }
MacroEngine::~MacroEngine() { Shutdown(); }

void MacroEngine::Init() {
    emergencyFlag.store(false);
    if (!m_stopEvent)
        m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    m_logFile = fopen("honestmacro.log", "a");
    if (m_logFile) {
        std::lock_guard<std::mutex> lock(m_logMutex);
        fseek(m_logFile, 0, SEEK_END);
        if (ftell(m_logFile) == 0)
            fprintf(m_logFile, "Time,Event,Details\n");
    }
    HookManager::Get().SetRecordCb([this](const HookEventData& e) { OnHookEvent(e); });
    HookManager::Get().SetKeybindCb([this](int id) {
        switch (id) {
            case 0: if (IsRecording()) StopRecording(); else StartRecording(); break;
            case 1: if (IsPlaying()) StopPlayback(); else StartPlayback(); break;
            case 2: StopPlayback(); StopRecording(); break;
            case 3: EmergencyStop(); break;
        }
    });
}

void MacroEngine::Shutdown() {
    EmergencyStop();
    StopRecording();
    if (m_playThread.joinable()) m_playThread.join();
    if (m_stopEvent) { CloseHandle(m_stopEvent); m_stopEvent = nullptr; }
    if (m_logFile) { fclose(m_logFile); m_logFile = nullptr; }
}

void MacroEngine::SetPowerState(bool active) {
    if (active) SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    else SetThreadExecutionState(ES_CONTINUOUS);
}

void MacroEngine::LogEvent(const char* evt, const char* detail) {
    if (!m_logFile) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d,%s,%s\n", st.wHour, st.wMinute, st.wSecond, evt, detail);
    std::lock_guard<std::mutex> lock(m_logMutex);
    fwrite(buf, 1, n, m_logFile);
    if (++m_logCount >= 50) { fflush(m_logFile); m_logCount = 0; }
}

std::string MacroEngine::GetExeNameFromHwnd(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return "";
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return "";
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return "";
    char path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameA(hProc, 0, path, &size)) {
        CloseHandle(hProc);
        const char* slash = strrchr(path, '\\');
        return slash ? (slash + 1) : path;
    }
    CloseHandle(hProc);
    return "";
}

bool MacroEngine::IsTargetFocusedAndValid(const PlaybackConfig& cfg) {
    if (!cfg.target) return true;
    HWND fg = GetForegroundWindow();
    if (fg != cfg.target) return false;
    if (cfg.targetExeName.empty()) return true;
    return GetExeNameFromHwnd(fg) == cfg.targetExeName;
}

std::vector<MacroAction> MacroEngine::GetActions() const {
    std::lock_guard<std::mutex> lock(m_actionMutex);
    return m_actions;
}

PlaybackConfig MacroEngine::SnapshotConfig() const {
    std::lock_guard<std::mutex> lock(m_configMutex);
    PlaybackConfig cfg;
    cfg.speed          = playbackSpeed;
    cfg.smartForce     = smartForcing;
    cfg.bgMode         = backgroundMode;
    cfg.muteUnfocused  = muteUnfocused;
    cfg.pauseUnfocused = pauseUnfocused;
    cfg.humanizeOn     = humanize;
    cfg.jitter         = jitterMs;
    cfg.relativeMouse  = relativeMouse;
    cfg.bezier         = bezierPaths;
    cfg.target         = targetHwnd;
    cfg.targetExeName  = targetExeName;
    cfg.recRect        = m_recWindowRect;
    cfg.altTabComp          = enableAltTabCompensation;
    cfg.altTabCompMs        = altTabCompensationMs;
    cfg.skipDelayOnRefocus  = skipDelayOnRefocus;
    return cfg;
}

bool MacroEngine::SaveMacro(const std::string& path) {
    std::vector<MacroAction> snapshot; RECT rect;
    { std::lock_guard<std::mutex> lock(m_actionMutex); snapshot = m_actions; rect = m_recWindowRect; }
    return FileIO::SaveMacro(path, snapshot, rect);
}

bool MacroEngine::LoadMacro(const std::string& path) {
    std::vector<MacroAction> loadedActions; RECT rect;
    if (!FileIO::LoadMacro(path, loadedActions, rect)) {
        VisionEngine::Get().ClearTargets();
        { std::lock_guard<std::mutex> lock(m_actionMutex); m_actions.clear(); }
        return false;
    }
    { std::lock_guard<std::mutex> lock(m_actionMutex); m_actions = std::move(loadedActions); m_recWindowRect = rect; }
    return true;
}

void MacroEngine::StartRecording() {
    if (GetState() != EngineState::Idle) return;
    if (targetHwnd && IsWindow(targetHwnd)) GetWindowRect(targetHwnd, &m_recWindowRect);
    else SetRectEmpty(&m_recWindowRect);
    {
        std::lock_guard<std::mutex> lock(m_actionMutex);
        m_actions.clear();
    }
    m_heldKeys.clear();
    m_heldButtons.clear();
    m_recStartTs = GetTickCount();
    m_state.store(EngineState::Recording);
    HookManager::Get().StartRecording();
    LogEvent("RECORD_START", targetTitle.empty() ? "Global" : targetTitle.c_str());
    if (m_stateCb) m_stateCb(EngineState::Recording);
}

void MacroEngine::StopRecording() {
    if (GetState() != EngineState::Recording) return;
    HookManager::Get().StopRecording();
    m_heldKeys.clear();
    m_heldButtons.clear();
    {
        std::lock_guard<std::mutex> lock(m_actionMutex);
        while (!m_actions.empty()) {
            auto type = m_actions.back().type;
            if (type == ActionType::KeyDown || type == ActionType::KeyUp) {
                WORD vk = m_actions.back().vkCode;
                if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                    vk == VK_TAB  || vk == VK_LWIN  || vk == VK_RWIN  ||
                    vk == VK_ESCAPE ||
                    vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                    vk == VK_SHIFT   || vk == VK_LSHIFT   || vk == VK_RSHIFT) {
                    m_actions.pop_back(); continue;
                }
            }
            break;
        }
    }
    m_state.store(EngineState::Idle);
    LogEvent("RECORD_STOP", std::to_string(m_actions.size()).c_str());
    if (m_stateCb) m_stateCb(EngineState::Idle);
}

void MacroEngine::StartPlayback() {
    if (GetState() != EngineState::Idle || m_actions.empty()) return;
    emergencyFlag.store(false);
    m_watchdogTripped.store(false);
    m_heartbeat.store(0);
    if (m_stopEvent) ResetEvent(m_stopEvent);
    SetPowerState(true);
    m_state.store(EngineState::Playing);
    if (m_playThread.joinable()) m_playThread.join();
    m_playThread = std::thread(&MacroEngine::PlaybackThread, this);
    LogEvent("PLAYBACK_START", std::to_string(m_actions.size()).c_str());
    if (m_stateCb) m_stateCb(EngineState::Playing);
}

void MacroEngine::StopPlayback() {
    if (GetState() != EngineState::Playing) return;
    if (m_stopEvent) SetEvent(m_stopEvent);
    emergencyFlag.store(true);
    if (m_playThread.joinable()) m_playThread.join();
    SetPowerState(false);
    m_state.store(EngineState::Idle);
    LogEvent("PLAYBACK_STOP", m_watchdogTripped.load() ? "Watchdog" : "Manual");
    if (m_stateCb) m_stateCb(EngineState::Idle);
}

void MacroEngine::EmergencyStop() {
    if (m_stopEvent) SetEvent(m_stopEvent);
    emergencyFlag.store(true);
    StopPlayback();
    StopRecording();
    SetPowerState(false);
}

bool MacroEngine::IsEmergencyStopped() const { return emergencyFlag.load(); }
EngineState MacroEngine::GetState() const { return m_state.load(); }
void MacroEngine::ClearActions() { std::lock_guard<std::mutex> lock(m_actionMutex); m_actions.clear(); }
void MacroEngine::RemoveAction(int idx) { std::lock_guard<std::mutex> lock(m_actionMutex); if (idx>=0 && idx<(int)m_actions.size()) m_actions.erase(m_actions.begin()+idx); }
void MacroEngine::MoveAction(int from, int to) {
    std::lock_guard<std::mutex> lock(m_actionMutex);
    if (from<0||from>=(int)m_actions.size()||to<0||to>=(int)m_actions.size()) return;
    auto a = m_actions[from]; m_actions.erase(m_actions.begin()+from); m_actions.insert(m_actions.begin()+to, a);
    RecalcTimestamps();
}
void MacroEngine::InsertDelay(int idx, int ms) { MacroAction a; a.type=ActionType::Delay; a.delayMs=ms; InsertAction(idx,a); }

void MacroEngine::InsertAction(int idx, const MacroAction& a) {
    std::lock_guard<std::mutex> lock(m_actionMutex);
    MacroAction newAction = a;
    if (m_actions.empty()) {
        newAction.tsMs = 0;
    } else {
        int clampedIdx = std::max(0, std::min(idx, (int)m_actions.size()));
        DWORD prevTs = (clampedIdx > 0) ? m_actions[clampedIdx-1].tsMs : 0;
        DWORD nextTs = (clampedIdx < (int)m_actions.size()) ? m_actions[clampedIdx].tsMs : (prevTs + 500);
        if (clampedIdx == (int)m_actions.size()) {
            newAction.tsMs = m_actions.back().tsMs + 100;
        } else if (clampedIdx == 0) {
            newAction.tsMs = 0;
        } else {
            newAction.tsMs = prevTs + (nextTs - prevTs) / 2;
        }
    }
    if (idx >= 0 && idx <= (int)m_actions.size())
        m_actions.insert(m_actions.begin() + idx, newAction);
    else
        m_actions.push_back(newAction);
    RecalcTimestamps();
}

void MacroEngine::UpdateAction(int idx, const MacroAction& a) {
    std::lock_guard<std::mutex> lock(m_actionMutex);
    if (idx>=0 && idx<(int)m_actions.size()) m_actions[idx]=a;
    RecalcTimestamps();
}

void MacroEngine::RecalcTimestamps() {
    if (m_actions.empty()) return;
    std::vector<DWORD> deltas(m_actions.size(), 0);
    for (size_t i = 1; i < m_actions.size(); i++)
        deltas[i] = (m_actions[i].tsMs >= m_actions[i-1].tsMs)
            ? (m_actions[i].tsMs - m_actions[i-1].tsMs) : 1;
    DWORD ts = m_actions[0].tsMs;
    for (size_t i = 1; i < m_actions.size(); i++) {
        ts += deltas[i];
        m_actions[i].tsMs = ts;
    }
}

void MacroEngine::SetStateChangedCb(StateChangedCb cb) { m_stateCb = cb; }

std::string MacroEngine::VkToName(WORD vk) {
    char buf[64]={}; UINT sc=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC); LONG lp=(LONG)(sc<<16);
    if(vk>=VK_PRIOR&&vk<=VK_HELP) lp|=(1L<<24);
    if(!GetKeyNameTextA(lp,buf,64)) { if(vk>=VK_F1&&vk<=VK_F24) snprintf(buf,64,"F%d",(int)(vk-VK_F1+1)); else snprintf(buf,64,"0x%02X",(int)vk); }
    return buf;
}

void MacroEngine::OnHookEvent(const HookEventData& evt) {
    if(GetState()!=EngineState::Recording) return;

    if(targetHwnd && GetForegroundWindow() != targetHwnd) {
        if (!m_heldKeys.empty() || !m_heldButtons.empty()) {
            DWORD now = evt.timeMs - m_recStartTs;
            std::lock_guard<std::mutex> lock(m_actionMutex);
            for (WORD vk : m_heldKeys) {
                MacroAction a;
                a.type = ActionType::KeyUp;
                a.vkCode = vk;
                a.keyName = VkToName(vk);
                a.tsMs = now;
                m_actions.push_back(a);
            }
            for (int btn : m_heldButtons) {
                MacroAction a;
                a.type = ActionType::MouseUp;
                a.button = btn;
                a.tsMs = now;
                m_actions.push_back(a);
            }
            m_heldKeys.clear();
            m_heldButtons.clear();
        }
        return;
    }

    MacroAction a; a.tsMs=evt.timeMs-m_recStartTs; a.x=evt.x; a.y=evt.y;
    switch(evt.evt) {
        case HookEvt::KeyDown:
            a.type=ActionType::KeyDown; a.vkCode=evt.vkCode; a.keyName=VkToName(evt.vkCode);
            if (std::find(m_heldKeys.begin(), m_heldKeys.end(), evt.vkCode) == m_heldKeys.end())
                m_heldKeys.push_back(evt.vkCode);
            break;
        case HookEvt::KeyUp:
            a.type=ActionType::KeyUp; a.vkCode=evt.vkCode; a.keyName=VkToName(evt.vkCode);
            {
                auto it = std::find(m_heldKeys.begin(), m_heldKeys.end(), evt.vkCode);
                if (it != m_heldKeys.end()) m_heldKeys.erase(it);
            }
            break;
        case HookEvt::MouseMove: a.type=ActionType::MouseMove; break;
        case HookEvt::MouseDown:
            a.type=ActionType::MouseDown; a.button=evt.button;
            if (std::find(m_heldButtons.begin(), m_heldButtons.end(), evt.button) == m_heldButtons.end())
                m_heldButtons.push_back(evt.button);
            break;
        case HookEvt::MouseUp:
            a.type=ActionType::MouseUp; a.button=evt.button;
            {
                auto it = std::find(m_heldButtons.begin(), m_heldButtons.end(), evt.button);
                if (it != m_heldButtons.end()) m_heldButtons.erase(it);
            }
            break;
    }
    std::lock_guard<std::mutex> lock(m_actionMutex); m_actions.push_back(a);
}

bool MacroEngine::ForceForeground(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    if (GetForegroundWindow() == hwnd) return true;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    DWORD fgTid = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD curTid = GetCurrentThreadId();
    if (fgTid != curTid) AttachThreadInput(fgTid, curTid, TRUE);
    bool ok = SetForegroundWindow(hwnd) != 0;
    if (fgTid != curTid) AttachThreadInput(fgTid, curTid, FALSE);
    return ok || GetForegroundWindow() == hwnd;
}

void MacroEngine::PlaybackThread() {
    int totalLoops = infiniteLoop ? 999999 : loopCount;
    HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!hTimer) hTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    HANDLE waitHandles[2] = { hTimer, m_stopEvent };
    int numWaitHandles = (hTimer && m_stopEvent) ? 2 : (hTimer ? 1 : 0);

    long screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    long screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    long screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    long screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if(screenW==0) screenW=GetSystemMetrics(SM_CXSCREEN);
    if(screenH==0) screenH=GetSystemMetrics(SM_CYSCREEN);

    LARGE_INTEGER freq, start, current;
    QueryPerformanceFrequency(&freq);

    uint32_t rng_state = GetTickCount() ^ 0x5A5A5A5A;
    if (rng_state == 0) rng_state = 1;
    auto fast_rand = [&]() -> uint32_t {
        rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state;
    };
    auto fast_gaussian = [&](float scale) -> float {
        float u1=(fast_rand()%1000)/1000.0f, u2=(fast_rand()%1000)/1000.0f, u3=(fast_rand()%1000)/1000.0f;
        return ((u1+u2+u3)-1.5f)*(scale*0.66f);
    };

    int lastPlayX = 0, lastPlayY = 0;
    bool posInitialized = false;

    std::vector<WORD> keysDown; std::vector<int> buttonsDown;
    bool wasFocused = true;

    // Alt-tab compensation tracking
    bool compFocused = true;
    DWORD focusLostTick = 0;

    PlaybackConfig cfg;
    bool cfg_bgMode = false;
    HWND cfg_target = nullptr;

    auto ReleaseHeldInputs = [&]() {
        INPUT inp={};
        for(WORD vk:keysDown) {
            if(cfg_bgMode && cfg_target) { UINT sc=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC); LPARAM lp=1|(sc<<16)|(1<<30)|(1<<31); PostMessage(cfg_target,WM_KEYUP,vk,lp); }
            else { inp.type=INPUT_KEYBOARD; inp.ki.wVk=vk; inp.ki.wScan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC); inp.ki.dwFlags=KEYEVENTF_KEYUP; SendInput(1,&inp,sizeof(INPUT)); }
        }
        keysDown.clear();
        for(int btn:buttonsDown) {
            if(cfg_bgMode && cfg_target) { int msg=(btn==0)?WM_LBUTTONUP:((btn==1)?WM_MBUTTONUP:WM_RBUTTONUP); PostMessage(cfg_target,msg,0,0); }
            else { inp.type=INPUT_MOUSE; inp.mi.dx=0; inp.mi.dy=0; if(btn==0) inp.mi.dwFlags=MOUSEEVENTF_LEFTUP; else if(btn==1) inp.mi.dwFlags=MOUSEEVENTF_MIDDLEUP; else inp.mi.dwFlags=MOUSEEVENTF_RIGHTUP; SendInput(1,&inp,sizeof(INPUT)); }
        }
        buttonsDown.clear();
        if(!cfg_bgMode) {
            inp.type=INPUT_KEYBOARD; inp.ki.wVk=VK_MENU; inp.ki.dwFlags=KEYEVENTF_KEYUP; SendInput(1,&inp,sizeof(INPUT));
            inp.ki.wVk=VK_LMENU; SendInput(1,&inp,sizeof(INPUT)); inp.ki.wVk=VK_RMENU; SendInput(1,&inp,sizeof(INPUT));
            inp.ki.wVk=VK_SHIFT; SendInput(1,&inp,sizeof(INPUT)); inp.ki.wVk=VK_CONTROL; SendInput(1,&inp,sizeof(INPUT));
        }
    };

    auto UpdateTracking = [&](const MacroAction& act) {
        if(act.type==ActionType::MouseDown) { if(std::find(buttonsDown.begin(),buttonsDown.end(),act.button)==buttonsDown.end()) buttonsDown.push_back(act.button); }
        else if(act.type==ActionType::MouseUp) { auto it=std::find(buttonsDown.begin(),buttonsDown.end(),act.button); if(it!=buttonsDown.end()) buttonsDown.erase(it); }
        else if(act.type==ActionType::KeyDown) { if(std::find(keysDown.begin(),keysDown.end(),act.vkCode)==keysDown.end()) keysDown.push_back(act.vkCode); }
        else if(act.type==ActionType::KeyUp) { auto it=std::find(keysDown.begin(),keysDown.end(),act.vkCode); if(it!=keysDown.end()) keysDown.erase(it); }
    };

    bool stopped = false;
    for(int loop=0; loop<totalLoops && !stopped; loop++) {
        if(emergencyFlag.load()) break;

        cfg = SnapshotConfig();
        cfg_bgMode = cfg.bgMode;
        cfg_target = cfg.target;

        // Re-sync compensation focus tracking for this loop iteration
        if (cfg.altTabComp && cfg.target && !cfg.pauseUnfocused && !cfg.bgMode)
            compFocused = (GetForegroundWindow() == cfg.target);
        focusLostTick = 0;

        QueryPerformanceCounter(&start);
        std::vector<MacroAction> snapshot;
        { std::lock_guard<std::mutex> lock(m_actionMutex); snapshot = m_actions; }

        if (!posInitialized && !snapshot.empty()) {
            for (const auto& a : snapshot) {
                if (a.type == ActionType::MouseMove || a.type == ActionType::MouseDown || a.type == ActionType::MouseUp) {
                    lastPlayX = a.x; lastPlayY = a.y; posInitialized = true; break;
                }
            }
            if (!posInitialized) {
                POINT curC; GetCursorPos(&curC);
                lastPlayX = curC.x; lastPlayY = curC.y; posInitialized = true;
            }
        }

        for(size_t i=0; i<snapshot.size() && !stopped; i++) {
            if(emergencyFlag.load()) break;
            m_heartbeat.fetch_add(1, std::memory_order_relaxed);
            const auto& a = snapshot[i];

            if (cfg.pauseUnfocused && cfg.target && !IsTargetFocusedAndValid(cfg)) {
                ReleaseHeldInputs();
                DWORD pauseStart = GetTickCount();
                while (!emergencyFlag.load()) {
                    m_heartbeat.fetch_add(1, std::memory_order_relaxed);
                    Sleep(50);
                    if (IsTargetFocusedAndValid(cfg)) break;
                }
                if (emergencyFlag.load()) break;
                DWORD pauseEnd = GetTickCount();
                start.QuadPart += (LONGLONG)((pauseEnd - pauseStart) * freq.QuadPart / 1000.0);
            }

            double targetMs = (double)a.tsMs / cfg.speed;
            if(cfg.humanizeOn && cfg.jitter > 0.0f) {
                targetMs += fast_gaussian(cfg.jitter);
                if(targetMs < 0) targetMs = 0;
            }

            while(true) {
                if(emergencyFlag.load()) break;
                QueryPerformanceCounter(&current);
                double elapsedMs = (double)(current.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
                double remainingMs = targetMs - elapsedMs;
                if(remainingMs <= 0.0) break;

                // --- Alt-Tab Compensation (inside wait loop for instant effect) ---
                if (cfg.altTabComp && cfg.target && !cfg.pauseUnfocused && !cfg.bgMode) {
                    bool nowFoc = (GetForegroundWindow() == cfg.target);
                    if (compFocused && !nowFoc) {
                        focusLostTick = GetTickCount();
                    } else if (!compFocused && nowFoc && focusLostTick != 0) {
                        int awayMs = (int)(GetTickCount() - focusLostTick);
                        int compMs = std::min(cfg.altTabCompMs, awayMs);
                        if (compMs > 0) {
                            start.QuadPart += (LONGLONG)compMs * freq.QuadPart / 1000;
                        }
                        focusLostTick = 0;
                        // Skip remaining delay entirely on refocus
                        if (cfg.skipDelayOnRefocus && a.type == ActionType::Delay) {
                            break;
                        }
                    }
                    compFocused = nowFoc;
                }
                // --- End Alt-Tab Compensation ---

                if(!cfg.bgMode && cfg.smartForce && cfg.target) {
                    bool isInput = (a.type==ActionType::MouseDown || a.type==ActionType::KeyDown || a.type==ActionType::WaitVisionClick);
                    if(isInput && GetForegroundWindow() != cfg.target) {
                        ForceForeground(cfg.target);
                    }
                }

                if (remainingMs > 0.8) {
                    double chunk = std::min(remainingMs, 50.0);
                    if (hTimer && numWaitHandles >= 2) {
                        LARGE_INTEGER due;
                        due.QuadPart = -(LONGLONG)((chunk - 0.5) * 10000.0);
                        SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);
                        DWORD wr = WaitForMultipleObjects(numWaitHandles, waitHandles, FALSE, (DWORD)(chunk + 10));
                        if (wr == WAIT_OBJECT_0 + 1) { stopped = true; break; }
                    } else if (hTimer) {
                        LARGE_INTEGER due;
                        due.QuadPart = -(LONGLONG)((chunk - 0.5) * 10000.0);
                        SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);
                        WaitForSingleObject(hTimer, (DWORD)(chunk + 10));
                    } else {
                        Sleep((DWORD)std::min(chunk, 15.0));
                    }
                    m_heartbeat.fetch_add(1, std::memory_order_relaxed);
                } else {
                    YieldProcessor();
                }
            }
            if(emergencyFlag.load() || stopped) break;

            int clickX = a.x, clickY = a.y;
            if(cfg.target && !IsRectEmpty(&cfg.recRect)) {
                RECT curRect;
                if(GetWindowRect(cfg.target,&curRect)) {
                    clickX += (curRect.left - cfg.recRect.left);
                    clickY += (curRect.top - cfg.recRect.top);
                }
            }

            bool isTargetFocused = IsTargetFocusedAndValid(cfg);
            bool shouldExecute = !(cfg.muteUnfocused && !isTargetFocused);
            if(wasFocused && !isTargetFocused) ReleaseHeldInputs();
            wasFocused = isTargetFocused;
            if(!shouldExecute) continue;

            if(cfg.bgMode && cfg.target) {
                POINT pt={clickX,clickY}; ScreenToClient(cfg.target,&pt);
                if(a.type==ActionType::MouseMove) PostMessage(cfg.target,WM_MOUSEMOVE,0,MAKELPARAM(pt.x,pt.y));
                else if(a.type==ActionType::MouseDown) { int msg=(a.button==0)?WM_LBUTTONDOWN:((a.button==1)?WM_MBUTTONDOWN:WM_RBUTTONDOWN); int mk=(a.button==0)?MK_LBUTTON:((a.button==1)?MK_MBUTTON:MK_RBUTTON); PostMessage(cfg.target,msg,mk,MAKELPARAM(pt.x,pt.y)); }
                else if(a.type==ActionType::MouseUp) { int msg=(a.button==0)?WM_LBUTTONUP:((a.button==1)?WM_MBUTTONUP:WM_RBUTTONUP); PostMessage(cfg.target,msg,0,MAKELPARAM(pt.x,pt.y)); }
                else if(a.type==ActionType::KeyDown) { UINT sc=MapVirtualKeyW(a.vkCode,MAPVK_VK_TO_VSC); LPARAM lp=1|(sc<<16); PostMessage(cfg.target,WM_KEYDOWN,a.vkCode,lp); }
                else if(a.type==ActionType::KeyUp) { UINT sc=MapVirtualKeyW(a.vkCode,MAPVK_VK_TO_VSC); LPARAM lp=1|(sc<<16)|(1<<30)|(1<<31); PostMessage(cfg.target,WM_KEYUP,a.vkCode,lp); }
                else if(a.type==ActionType::WaitVisionClick) {
                    auto* t = VisionEngine::Get().GetTarget(a.visionTargetId);
                    if(t) {
                        auto res = VisionEngine::Get().WaitForTarget(a.visionTargetId, t->timeoutMs, cfg.target, true, &emergencyFlag);
                        if(res.found) {
                            int vx = res.x + t->clickOffsetX;
                            int vy = res.y + t->clickOffsetY;
                            PostMessage(cfg.target,WM_MOUSEMOVE,0,MAKELPARAM(vx,vy));
                            PostMessage(cfg.target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(vx,vy));
                            Sleep(20);
                            PostMessage(cfg.target,WM_LBUTTONUP,0,MAKELPARAM(vx,vy));
                            lastPlayX = vx; lastPlayY = vy;
                        } else LogEvent("VISION_FAIL", t->name.c_str());
                    }
                    LARGE_INTEGER aw; QueryPerformanceCounter(&aw); start.QuadPart += (aw.QuadPart - current.QuadPart);
                }
                UpdateTracking(a);
            } else {
                INPUT inp={};
                if(a.type==ActionType::WaitVisionClick) {
                    auto* t = VisionEngine::Get().GetTarget(a.visionTargetId);
                    if(t) {
                        auto res = VisionEngine::Get().WaitForTarget(a.visionTargetId, t->timeoutMs, nullptr, false, &emergencyFlag);
                        if(res.found) {
                            clickX=res.x+t->clickOffsetX; clickY=res.y+t->clickOffsetY;
                            inp.type=INPUT_MOUSE; inp.mi.dx=(long)(((clickX-screenX)*65535.0f)/(screenW-1)); inp.mi.dy=(long)(((clickY-screenY)*65535.0f)/(screenH-1));
                            inp.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|MOUSEEVENTF_LEFTDOWN; SendInput(1,&inp,sizeof(INPUT)); Sleep(20);
                            inp.mi.dwFlags=MOUSEEVENTF_LEFTUP; SendInput(1,&inp,sizeof(INPUT));
                            lastPlayX=clickX; lastPlayY=clickY;
                        } else LogEvent("VISION_FAIL", t->name.c_str());
                    }
                    LARGE_INTEGER aw; QueryPerformanceCounter(&aw); start.QuadPart += (aw.QuadPart - current.QuadPart);
                } else if(a.type==ActionType::MouseMove || a.type==ActionType::MouseDown || a.type==ActionType::MouseUp) {
                    inp.type = INPUT_MOUSE;
                    if(cfg.humanizeOn && cfg.bezier && a.type==ActionType::MouseMove) {
                        int steps = 8 + (fast_rand()%6);
                        float cx = lastPlayX + (fast_rand()%40 - 20), cy = lastPlayY + (fast_rand()%40 - 20);
                        for(int s=1; s<=steps; s++) {
                            float t = (float)s/steps, u = 1.0f-t;
                            float bx = u*u*lastPlayX + 2*u*t*cx + t*t*clickX;
                            float by = u*u*lastPlayY + 2*u*t*cy + t*t*clickY;
                            inp.mi.dx = (long)(((bx-screenX)*65535.0f)/(screenW-1));
                            inp.mi.dy = (long)(((by-screenY)*65535.0f)/(screenH-1));
                            inp.mi.dwFlags = MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
                            SendInput(1,&inp,sizeof(INPUT));
                            Sleep(2 + (fast_rand()%4));
                        }
                        lastPlayX=clickX; lastPlayY=clickY;
                    } else {
                        if(cfg.relativeMouse && a.type==ActionType::MouseMove) {
                            inp.mi.dx = clickX - lastPlayX; inp.mi.dy = clickY - lastPlayY; inp.mi.dwFlags = MOUSEEVENTF_MOVE;
                            lastPlayX=clickX; lastPlayY=clickY;
                        } else {
                            inp.mi.dx = (long)(((clickX-screenX)*65535.0f)/(screenW-1));
                            inp.mi.dy = (long)(((clickY-screenY)*65535.0f)/(screenH-1));
                            inp.mi.dwFlags = MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
                            if(a.type==ActionType::MouseDown) { if(a.button==0) inp.mi.dwFlags|=MOUSEEVENTF_LEFTDOWN; else if(a.button==1) inp.mi.dwFlags|=MOUSEEVENTF_MIDDLEDOWN; else inp.mi.dwFlags|=MOUSEEVENTF_RIGHTDOWN; }
                            else if(a.type==ActionType::MouseUp) { if(a.button==0) inp.mi.dwFlags|=MOUSEEVENTF_LEFTUP; else if(a.button==1) inp.mi.dwFlags|=MOUSEEVENTF_MIDDLEUP; else inp.mi.dwFlags|=MOUSEEVENTF_RIGHTUP; }
                            lastPlayX=clickX; lastPlayY=clickY;
                        }
                        SendInput(1,&inp,sizeof(INPUT));
                    }
                } else if(a.type==ActionType::KeyDown || a.type==ActionType::KeyUp) {
                    inp.type=INPUT_KEYBOARD; inp.ki.wVk=a.vkCode; inp.ki.wScan=MapVirtualKeyW(a.vkCode,MAPVK_VK_TO_VSC);
                    if(a.type==ActionType::KeyUp) inp.ki.dwFlags=KEYEVENTF_KEYUP;
                    SendInput(1,&inp,sizeof(INPUT));
                }
                UpdateTracking(a);
            }
        }
    }
    ReleaseHeldInputs();
    if (hTimer) CloseHandle(hTimer);
    SetPowerState(false);
    m_state.store(EngineState::Idle);
    if(m_stateCb) m_stateCb(EngineState::Idle);
}
} // namespace hm
