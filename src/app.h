#pragma once
#include <string>
#include <vector>
#include <set>              // <-- ADD THIS
#include "imgui.h"
#include "HookManager.h"
#include "MacroEngine.h"
#include "VisionEngine.h"

enum class Tab { Editor, Config };

class Application {
public:
    static Application& Get() { static Application a; return a; }
    void Init();
    void RenderUI();
    void Shutdown();

private:
    void ApplyTheme();
    void RenderSidebar();
    void RenderHeader(const char* title, const char* subtitle);
    void RenderStatusBar();
    void RenderEditor();
    void RenderVisionIntegrated();
    void RenderConfig();
    void RenderCropOverlay();
    void RenderRebindPopup();
    void RenderManualBuilder();
    std::string PromptSaveDialog();
    std::string PromptLoadDialog();
    bool IsActionVisible(const hm::MacroAction& a) const;
    void UpdateFilterCache(const std::vector<hm::MacroAction>& actions);

    static std::string WideToUtf8(const wchar_t* wstr);

    Tab  currentTab       = Tab::Editor;
    int  selectedAction   = -1;
    int  selectedTarget   = -1;
    bool m_hideMouseMoves = true;
    bool m_filterEnabled  = true;
    char m_filterText[64] = "";
    int  m_filterType     = 0;
    std::vector<int> m_filteredIndices;
    bool m_filterDirty    = true;
    char m_lastFilterText[64] = "";
    int  m_lastFilterType = 0;
    bool m_lastHideMouse  = true;

    bool m_rebindActive   = false;
    int  m_rebindTargetId = -1;
    int  m_rebindCooldown = 0;

    bool m_cropActive     = false;
    ImVec2 m_cropStart    = ImVec2(0, 0);
    ImVec2 m_cropEnd      = ImVec2(0, 0);
    bool m_cropDragging   = false;
    char m_newTargetName[128] = "New Target";

    bool m_testPosActive  = false;
    ImVec2 m_testPos      = ImVec2(0, 0);
    DWORD m_testPosTime   = 0;

    bool m_showLoadError  = false;
    bool m_showSaveError  = false;

    // --- NEW: multi-select support ---
    std::set<int> m_selectedActions;
    int m_lastClickedAction = -1;
};
