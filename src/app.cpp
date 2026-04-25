#include "app.h"
#include "Theme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <dwmapi.h>
#include <commdlg.h>

static bool ContainsIgnoreCase(const char* haystack, const char* needle) {
    if (!needle[0]) return true;
    for (const char* h = haystack; *h; ++h) {
        const char* n = needle;
        const char* p = h;
        while (*p && *n && std::tolower((unsigned char)*p) == std::tolower((unsigned char)*n)) {
            ++p; ++n;
        }
        if (!*n) return true;
    }
    return false;
}

std::string Application::WideToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

bool Application::IsActionVisible(const hm::MacroAction& a) const {
    if (m_hideMouseMoves && a.type == hm::ActionType::MouseMove) return false;
    if (!m_filterEnabled) return true;

    if (m_filterText[0] != '\0') {
        bool typeMatch = ContainsIgnoreCase(hm::ActionTypeStr(a.type), m_filterText);
        bool keyMatch  = ContainsIgnoreCase(a.keyName.c_str(), m_filterText);
        if (!typeMatch && !keyMatch) return false;
    }
    if (m_filterType == 1) {
        bool isMouse = (a.type == hm::ActionType::MouseMove || a.type == hm::ActionType::MouseDown || a.type == hm::ActionType::MouseUp);
        if (!isMouse) return false;
    }
    if (m_filterType == 2) {
        bool isKey = (a.type == hm::ActionType::KeyDown || a.type == hm::ActionType::KeyUp);
        if (!isKey) return false;
    }
    if (m_filterType == 3 && a.type != hm::ActionType::WaitVisionClick) return false;
    if (m_filterType == 4 && a.type != hm::ActionType::Delay) return false;
    return true;
}

void Application::UpdateFilterCache(const std::vector<hm::MacroAction>& actions) {
    bool textChanged = (strncmp(m_filterText, m_lastFilterText, sizeof(m_filterText)) != 0);
    if (textChanged || m_filterType != m_lastFilterType || m_hideMouseMoves != m_lastHideMouse || m_filterDirty) {
        m_filteredIndices.clear();
        m_filteredIndices.reserve(actions.size());
        for (int i = 0; i < (int)actions.size(); i++) {
            if (IsActionVisible(actions[i])) {
                m_filteredIndices.push_back(i);
            }
        }
        strncpy(m_lastFilterText, m_filterText, sizeof(m_filterText) - 1);
        m_lastFilterText[sizeof(m_filterText) - 1] = '\0';
        m_lastFilterType = m_filterType;
        m_lastHideMouse = m_hideMouseMoves;
        m_filterDirty = false;
    }
}

std::string Application::PromptSaveDialog() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = L"Honest Macro (*.hmac)\0*.hmac\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"hmac";
    return GetSaveFileNameW(&ofn) ? WideToUtf8(filename) : "";
}

std::string Application::PromptLoadDialog() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = L"Honest Macro (*.hmac)\0*.hmac\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? WideToUtf8(filename) : "";
}

void Application::Init() {
    ApplyTheme();
    hm::HookManager::Get().Init();
    auto& hk = hm::HookManager::Get();
    hk.RegisterKeybind("Toggle Recording", VK_F9, true, false, false);
    hk.RegisterKeybind("Toggle Playback", VK_F10, true, false, false);
    hk.RegisterKeybind("Stop All", VK_F11, true, false, false);
    hk.RegisterKeybind("Emergency Close", VK_F12, true, true, false);
    hk.LoadKeybinds();
    hm::MacroEngine::Get().Init();
}

void Application::Shutdown() {
    hm::HookManager::Get().SaveKeybinds();
    hm::MacroEngine::Get().Shutdown();
    hm::HookManager::Get().Shutdown();
}

void Application::ApplyTheme() { theme::Apply(); }

void Application::RenderSidebar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 18));
    ImGui::BeginChild("Sidebar", ImVec2(210, 0), true, ImGuiWindowFlags_NoScrollbar);

    ImGui::TextColored(theme::AccentBlue(), "HonestMacro");
    ImGui::TextColored(theme::TextDim(), "v0.0.2-alpha");
    ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    auto& engine = hm::MacroEngine::Get();
    auto state = engine.GetState();

    if (state == hm::EngineState::Recording) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::AccentRed());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::AccentRed());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::AccentRed());
        if (ImGui::Button("  Stop Recording", ImVec2(-1, 42))) engine.StopRecording();
        ImGui::PopStyleColor(3);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(theme::AccentGreen().x, theme::AccentGreen().y, theme::AccentGreen().z, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::AccentGreen());
        if (ImGui::Button("  Record Macro", ImVec2(-1, 42))) engine.StartRecording();
        ImGui::PopStyleColor(2);
    }

    if (state == hm::EngineState::Playing) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::AccentRed());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::AccentRed());
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::AccentRed());
        if (ImGui::Button("  Stop Playback", ImVec2(-1, 42))) engine.StopPlayback();
        ImGui::PopStyleColor(3);
    } else if (state == hm::EngineState::Idle && !engine.GetActions().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(theme::AccentBlue().x, theme::AccentBlue().y, theme::AccentBlue().z, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::AccentBlue());
        if (ImGui::Button("  Play Macro", ImVec2(-1, 42))) engine.StartPlayback();
        ImGui::PopStyleColor(2);
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("  Play Macro", ImVec2(-1, 42));
        ImGui::EndDisabled();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    auto NavBtn = [&](const char* label, Tab t) {
        bool active = (currentTab == t);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, theme::Surface1());
        if (ImGui::Button(label, ImVec2(-1, 36))) currentTab = t;
        if (active) ImGui::PopStyleColor();
    };
    NavBtn("Editor & Vision", Tab::Editor);
    NavBtn("Config", Tab::Config);

    float remain = ImGui::GetContentRegionAvail().y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + remain - 60);
    ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(theme::TextDim(), "? Documentation");

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void Application::RenderHeader(const char* title, const char* subtitle) {
    ImGui::BeginGroup();
    auto state = hm::MacroEngine::Get().GetState();

    ImVec4 dotColor = theme::TextDim();
    if (state == hm::EngineState::Recording) dotColor = theme::AccentRed();
    else if (state == hm::EngineState::Playing) dotColor = theme::AccentGreen();

    ImGui::PushStyleColor(ImGuiCol_Button, dotColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, dotColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, dotColor);
    ImGui::Button("##status", ImVec2(12, 12));
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::TextColored(theme::TextMain(), "%s", title);
    if (subtitle) {
        ImGui::SameLine();
        ImGui::TextColored(theme::TextDim(), "|  %s", subtitle);
    }
    ImGui::EndGroup();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 250);
    ImGui::SetNextItemWidth(250);
    auto& engine = hm::MacroEngine::Get();
    std::string preview = engine.targetTitle.empty() ? "Target: Global (None)" : engine.targetTitle;

    if (ImGui::BeginCombo("##TargetWindow", preview.c_str())) {
        std::vector<std::pair<HWND, std::string>> windows;
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            if (!IsWindowVisible(hwnd)) return TRUE;
            if (GetWindowTextLengthA(hwnd) == 0) return TRUE;
            DWORD cloaked = 0;
            DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(DWORD));
            if (cloaked) return TRUE;
            LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
            if (exStyle & WS_EX_TOOLWINDOW) return TRUE;
            if (GetParent(hwnd) != nullptr) return TRUE;
            char buf[256];
            GetWindowTextA(hwnd, buf, 256);
            std::string t(buf);
            if (t == "Honest Macro" || t == "Program Manager") return TRUE;
            if (t == "Default IME" || t == "MSCTFIME UI" || t == "CiceroUIWndFrame") return TRUE;
            if (t.empty()) return TRUE;
            reinterpret_cast<std::vector<std::pair<HWND, std::string>>*>(lp)->push_back({hwnd, t});
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));

        if (ImGui::Selectable("None (Global)", engine.targetHwnd == nullptr)) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.targetHwnd = nullptr;
            engine.targetTitle.clear();
            engine.targetPid = 0;
            engine.targetExeName.clear();
        }
        ImGui::Separator();
        for (size_t i = 0; i < windows.size(); i++) {
            ImGui::PushID((int)i);
            bool is_selected = (engine.targetHwnd == windows[i].first);
            if (ImGui::Selectable(windows[i].second.c_str(), is_selected)) {
                std::lock_guard<std::mutex> lock(engine.m_configMutex);
                engine.targetHwnd = windows[i].first;
                engine.targetTitle = windows[i].second;
                engine.targetExeName = hm::MacroEngine::GetExeNameFromHwnd(windows[i].first);
                GetWindowThreadProcessId(windows[i].first, &engine.targetPid);
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

void Application::RenderManualBuilder() {
    static int mouseTab = 0;
    static int mx = 0, my = 0, relX = 0, relY = 0, btn = 0;
    static int vkCode = VK_SPACE, keyDown = 1;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::TextColored(theme::TextDim(), "Manual Action Builder");
    ImGui::Spacing();

    if (ImGui::BeginTabBar("ManualTabs", ImGuiTabBarFlags_NoTooltip)) {
        if (ImGui::BeginTabItem("Mouse")) {
            ImGui::PushItemWidth(-1);
            ImGui::Combo("##MouseMode", &mouseTab, "Absolute Move\0Relative Move\0Click at Position\0");
            ImGui::PopItemWidth();
            ImGui::Spacing();

            if (mouseTab == 1) {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 4);
                ImGui::DragInt("Delta X", &relX, 1.0f, -5000, 5000);
                ImGui::SameLine();
                ImGui::DragInt("Delta Y", &relY, 1.0f, -5000, 5000);
                ImGui::PopItemWidth();
            } else {
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 4);
                ImGui::DragInt("Pos X", &mx, 1.0f, -10000, 10000);
                ImGui::SameLine();
                ImGui::DragInt("Pos Y", &my, 1.0f, -10000, 10000);
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("  Pick", ImVec2(65, 0))) {
                    m_testPosActive = true;
                    m_testPosTime = GetTickCount();
                    m_testPos = ImVec2((float)mx, (float)my);
                }
            }

            if (mouseTab == 2) {
                ImGui::PushItemWidth(-1);
                ImGui::Combo("Button", &btn, "Left\0Middle\0Right\0");
                ImGui::PopItemWidth();
            }

            ImGui::Spacing();
            if (ImGui::Button("Test Position", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4, 26))) {
                POINT old; GetCursorPos(&old);
                int tx = (mouseTab == 1) ? old.x + relX : mx;
                int ty = (mouseTab == 1) ? old.y + relY : my;
                SetCursorPos(tx, ty);
                m_testPos = ImVec2((float)tx, (float)ty);
                m_testPosActive = true;
                m_testPosTime = GetTickCount();
            }
            ImGui::SameLine();
            if (ImGui::Button("Insert", ImVec2(-1, 26))) {
                auto& engine = hm::MacroEngine::Get();
                int idx = selectedAction >= 0 ? selectedAction + 1 : (int)engine.GetActions().size();
                if (mouseTab == 0) {
                    hm::MacroAction a; a.type = hm::ActionType::MouseMove; a.x = mx; a.y = my;
                    engine.InsertAction(idx, a); selectedAction = idx; m_filterDirty = true;
                } else if (mouseTab == 1) {
                    hm::MacroAction a; a.type = hm::ActionType::MouseMove; a.x = relX; a.y = relY;
                    engine.InsertAction(idx, a); selectedAction = idx; m_filterDirty = true;
                } else {
                    hm::MacroAction a; a.type = hm::ActionType::MouseDown; a.x = mx; a.y = my; a.button = btn;
                    hm::MacroAction b; b.type = hm::ActionType::MouseUp; b.x = mx; b.y = my; b.button = btn;
                    engine.InsertAction(idx, a); engine.InsertAction(idx + 1, b); selectedAction = idx + 1; m_filterDirty = true;
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Keyboard")) {
            ImGui::PushItemWidth(-1);
            ImGui::InputInt("VK Code (Dec)", &vkCode, 1, 10);
            if (vkCode < 0) vkCode = 0;
            if (vkCode > 254) vkCode = 254;
            ImGui::TextColored(theme::AccentYell(), "Key: %s", hm::MacroEngine::VkToName((WORD)vkCode).c_str());
            ImGui::Combo("Action", &keyDown, "Key Down\0Key Up\0Down + Up\0");
            ImGui::PopItemWidth();
            ImGui::Spacing();

            if (ImGui::Button("Insert", ImVec2(-1, 26))) {
                auto& engine = hm::MacroEngine::Get();
                int idx = selectedAction >= 0 ? selectedAction + 1 : (int)engine.GetActions().size();
                if (keyDown == 0) {
                    hm::MacroAction a; a.type = hm::ActionType::KeyDown; a.vkCode = (WORD)vkCode; a.keyName = hm::MacroEngine::VkToName((WORD)vkCode);
                    engine.InsertAction(idx, a); selectedAction = idx; m_filterDirty = true;
                } else if (keyDown == 1) {
                    hm::MacroAction a; a.type = hm::ActionType::KeyUp; a.vkCode = (WORD)vkCode; a.keyName = hm::MacroEngine::VkToName((WORD)vkCode);
                    engine.InsertAction(idx, a); selectedAction = idx; m_filterDirty = true;
                } else {
                    hm::MacroAction a; a.type = hm::ActionType::KeyDown; a.vkCode = (WORD)vkCode; a.keyName = hm::MacroEngine::VkToName((WORD)vkCode);
                    hm::MacroAction b; b.type = hm::ActionType::KeyUp;
                    b.vkCode = (WORD)vkCode;
                    b.keyName = hm::MacroEngine::VkToName((WORD)vkCode);
                    engine.InsertAction(idx, a); engine.InsertAction(idx + 1, b); selectedAction = idx + 1; m_filterDirty = true;
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar(2);
}

void Application::RenderEditor() {
    auto& engine = hm::MacroEngine::Get();

    if (engine.IsRecording()) m_filterDirty = true;

    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Button, theme::Surface1());
    if (ImGui::Button("  New Macro  ", ImVec2(105, 30))) { engine.ClearActions(); selectedAction = -1; m_filterDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("  Load Macro  ", ImVec2(105, 30))) {
        std::string p = PromptLoadDialog();
        if (!p.empty()) { if (!engine.LoadMacro(p)) m_showLoadError = true; else m_filterDirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::Button("  Save Macro  ", ImVec2(105, 30))) {
        std::string p = PromptSaveDialog();
        if (!p.empty()) { if (!engine.SaveMacro(p)) m_showSaveError = true; }
    }
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Columns(2, "EditorSplit", true);
    float col0W = ImGui::GetWindowWidth() * 0.60f;
    if (col0W < 360) col0W = 360;
    ImGui::SetColumnWidth(0, col0W);

    auto actions = engine.GetActions();
    UpdateFilterCache(actions);

    ImGui::BeginChild("TableScrollRegion", ImVec2(0, 0));
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("ActionTable", 4, flags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##col_sel", ImGuiTableColumnFlags_WidthFixed, 26.0f);
        ImGui::TableSetupColumn("ACTION", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("DETAILS", ImGuiTableColumnFlags_WidthFixed, 135.0f);
        ImGui::TableSetupColumn("OFFSET", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)m_filteredIndices.size(), 30.0f);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                int idx = m_filteredIndices[i];
                if (idx < 0 || idx >= (int)actions.size()) continue;

                const auto& a = actions[idx];
                ImGui::TableNextRow(0, 30.0f);
                ImGui::TableSetColumnIndex(0);
                bool sel = (selectedAction == idx);
                ImGui::PushID(idx);
                if (ImGui::Selectable("##row", sel,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0, 30)))
                    selectedAction = idx;

                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("ACTION_ROW", &idx, sizeof(int));
                    ImVec4 col = theme::AccentBlue();
                    if (a.type == hm::ActionType::KeyDown || a.type == hm::ActionType::KeyUp)
                        col = theme::AccentYell();
                    else if (a.type == hm::ActionType::WaitVisionClick)
                        col = theme::AccentGreen();
                    else if (a.type == hm::ActionType::Delay)
                        col = theme::TextDim();
                    ImGui::TextColored(col, "  %s", hm::ActionTypeStr(a.type));
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("ACTION_ROW", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                        ImVec2 rowMin = ImGui::GetItemRectMin();
                        ImVec2 rowMax = ImGui::GetItemRectMax();
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        dl->AddLine(
                            ImVec2(rowMin.x, rowMin.y),
                            ImVec2(rowMax.x, rowMin.y),
                            IM_COL32(90, 141, 234, 200), 2.0f);
                        if (payload->IsDelivery()) {
                            int srcIdx = *(const int*)payload->Data;
                            if (srcIdx != idx) {
                                engine.MoveAction(srcIdx, idx);
                                selectedAction = idx;
                                m_filterDirty = true;
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("row_ctx")) {
                    selectedAction = idx;
                    if (ImGui::MenuItem("Duplicate Action")) {
                        engine.InsertAction(idx + 1, actions[idx]);
                        selectedAction = idx + 1;
                        m_filterDirty = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete Action")) {
                        engine.RemoveAction(idx);
                        selectedAction = -1;
                        m_filterDirty = true;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                ImVec4 actCol = theme::TextMain();
                if (a.type == hm::ActionType::MouseDown || a.type == hm::ActionType::MouseUp)
                    actCol = theme::AccentBlue();
                else if (a.type == hm::ActionType::KeyDown || a.type == hm::ActionType::KeyUp)
                    actCol = theme::AccentYell();
                else if (a.type == hm::ActionType::WaitVisionClick)
                    actCol = theme::AccentGreen();
                else if (a.type == hm::ActionType::Delay)
                    actCol = theme::TextDim();
                ImGui::TextColored(actCol, "%s", hm::ActionTypeStr(a.type));

                ImGui::TableSetColumnIndex(2);
                if (a.type == hm::ActionType::WaitVisionClick) {
                    auto& vision = hm::VisionEngine::Get();
                    {
                        std::lock_guard<std::recursive_mutex> vlock(vision.m_mutex);
                        auto* t = vision.GetTarget(a.visionTargetId);
                        if (t && t->glTexture) {
                            ImGui::Image((ImTextureID)(uintptr_t)t->glTexture, ImVec2(34, 22));
                            ImGui::SameLine();
                        }
                    }
                    ImGui::TextColored(theme::AccentGreen(), "[%s]", a.keyName.c_str());
                } else if (a.type == hm::ActionType::KeyDown || a.type == hm::ActionType::KeyUp) {
                    ImGui::Text("%s", a.keyName.c_str());
                } else if (a.type == hm::ActionType::Delay) {
                    ImGui::TextDisabled("+%.3fs", a.delayMs / 1000.0f);
                } else {
                    ImGui::Text("%d, %d", a.x, a.y);
                }

                ImGui::TableSetColumnIndex(3);
                {
                    DWORD prevTs = (idx > 0) ? actions[idx - 1].tsMs : 0;
                    DWORD delta = (a.tsMs >= prevTs) ? (a.tsMs - prevTs) : 0;
                    float sec = delta / 1000.0f;
                    if (idx == 0)
                        ImGui::TextDisabled("0.000s");
                    else if (sec < 10.0f)
                        ImGui::Text("+%.3fs", sec);
                    else
                        ImGui::Text("+%.2fs", sec);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Offset: +%u ms\nAbsolute: %u ms (%.3fs)",
                            (unsigned)delta, (unsigned)a.tsMs, a.tsMs / 1000.0f);
                }
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    if (m_filteredIndices.empty() && !actions.empty())
        ImGui::TextColored(theme::TextDim(), "No actions match current filters.");
    ImGui::EndChild();

    ImGui::NextColumn();
    ImGui::BeginChild("EditPanel", ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::TextColored(theme::TextDim(), "Edit Selected Action");
    ImGui::Spacing();
    if (selectedAction >= 0 && selectedAction < (int)actions.size()) {
        hm::MacroAction act = actions[selectedAction];
        bool changed = false;
        ImGui::TextColored(theme::AccentBlue(), "Action: %s", hm::ActionTypeStr(act.type));
        ImGui::Separator();
        ImGui::PushItemWidth(-1);

        {
            DWORD prevTs = (selectedAction > 0) ? actions[selectedAction - 1].tsMs : 0;
            int delta = (int)((act.tsMs >= prevTs) ? (act.tsMs - prevTs) : 0);
            if (selectedAction == 0) {
                ImGui::TextColored(theme::TextDim(), "Offset: 0.000s (first action)");
            } else {
                if (ImGui::DragInt("Offset (ms)", &delta, 10.0f, 0, 10000000)) {
                    if (delta < 0) delta = 0;
                    act.tsMs = prevTs + (DWORD)delta;
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextColored(theme::TextDim(), "(+%.3fs)", delta / 1000.0f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Gap from previous action.\nAbsolute: %u ms", (unsigned)act.tsMs);
            }
        }
        ImGui::Spacing();

        if (act.type == hm::ActionType::Delay) {
            int d = act.delayMs;
            if (ImGui::DragInt("Duration (ms)", &d, 10.0f, 1, 100000)) { act.delayMs = d; changed = true; }
            ImGui::SameLine();
            ImGui::TextColored(theme::TextDim(), "(%.3fs)", d / 1000.0f);
        } else if (act.type == hm::ActionType::MouseMove || act.type == hm::ActionType::MouseDown || act.type == hm::ActionType::MouseUp) {
            int pos[2] = { act.x, act.y };
            if (ImGui::DragInt2("Position X/Y", pos, 1.0f, -10000, 10000)) { act.x = pos[0]; act.y = pos[1]; changed = true; }
            if (act.type != hm::ActionType::MouseMove) {
                int btn = act.button;
                if (ImGui::Combo("Button", &btn, "Left\0Middle\0Right\0")) { act.button = btn; changed = true; }
            }
        } else if (act.type == hm::ActionType::KeyDown || act.type == hm::ActionType::KeyUp) {
            int vk = act.vkCode;
            if (ImGui::InputInt("VK Code (Dec)", &vk, 1, 10)) {
                if (vk >= 0 && vk <= 254) { act.vkCode = vk; act.keyName = hm::MacroEngine::VkToName(vk); changed = true; }
            }
            ImGui::TextColored(theme::AccentYell(), "Key: %s", act.keyName.c_str());
        } else if (act.type == hm::ActionType::WaitVisionClick) {
            ImGui::Text("Target: %s", act.keyName.c_str());
            ImGui::TextColored(theme::TextDim(), "Edit settings in Vision section below.");
        }
        ImGui::PopItemWidth();
        if (changed) engine.UpdateAction(selectedAction, act);
    } else {
        ImGui::TextColored(theme::TextDim(), "No action selected.");
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(theme::TextDim(), "Sequence Tools"); ImGui::Spacing();
    DWORD totalMs = actions.empty() ? 0 : actions.back().tsMs;
    float totalSec = totalMs / 1000.0f;
    ImGui::Text("Length: %d Actions  |  Duration: %.3fs", (int)actions.size(), totalSec);
    ImGui::Spacing();
    ImGui::BeginDisabled(selectedAction < 0 || selectedAction >= (int)actions.size());
    if (ImGui::Button("Move Up", ImVec2(75, 24))) { engine.MoveAction(selectedAction, selectedAction - 1); selectedAction--; m_filterDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Move Down", ImVec2(75, 24))) { engine.MoveAction(selectedAction, selectedAction + 1); selectedAction++; m_filterDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate", ImVec2(75, 24))) { engine.InsertAction(selectedAction + 1, actions[selectedAction]); selectedAction++; m_filterDirty = true; }
    ImGui::EndDisabled();
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    RenderManualBuilder();
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(theme::TextDim(), "Vision System"); ImGui::Spacing();
    auto& vision = hm::VisionEngine::Get();
    ImGui::PushItemWidth(-1);
    if (ImGui::Button("+ Capture New", ImVec2(-1, 26))) {
        HWND mainHwnd = FindWindowW(L"HonestMacro", nullptr);
        if (mainHwnd) { ShowWindow(mainHwnd, SW_HIDE); Sleep(250); }
        if (engine.targetHwnd && engine.backgroundMode)
            vision.CaptureFullScreen(engine.targetHwnd, true);
        else
            vision.CaptureFullScreen();
        if (mainHwnd) { ShowWindow(mainHwnd, SW_SHOW); SetForegroundWindow(mainHwnd); }
        m_cropActive = true; m_cropDragging = false;
    }
    ImGui::PopItemWidth();
    {
        std::lock_guard<std::recursive_mutex> vlock(vision.m_mutex);
        auto& targets = vision.GetTargets();
        if (!targets.empty()) {
            if (selectedTarget < 0) selectedTarget = targets[0].id;
            hm::ImageTarget* curTarget = vision.GetTarget(selectedTarget);
            if (!curTarget) { selectedTarget = targets[0].id; curTarget = &targets[0]; }
            ImGui::PushItemWidth(-1);
            if (ImGui::BeginCombo("##visSelect", curTarget ? curTarget->name.c_str() : "")) {
                for (auto& t : targets) { ImGui::PushID(t.id); if (ImGui::Selectable(t.name.c_str(), t.id == selectedTarget)) selectedTarget = t.id; ImGui::PopID(); }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            if (curTarget) {
                ImGui::Spacing();
                ImGui::BeginChild("vispreview", ImVec2(0, 110), true);
                if (curTarget->glTexture) {
                    float aspect = (float)curTarget->width / (float)curTarget->height;
                    float dispH = 95.0f, dispW = dispH * aspect;
                    if (dispW > ImGui::GetContentRegionAvail().x) { dispW = ImGui::GetContentRegionAvail().x; dispH = dispW / aspect; }
                    ImGui::Image((ImTextureID)(uintptr_t)curTarget->glTexture, ImVec2(dispW, dispH), ImVec2(0, 0), ImVec2(1, 1), theme::TextMain(), theme::AccentBlue());
                }
                ImGui::EndChild();
                ImGui::Spacing();
                ImGui::PushItemWidth(-1);
                static char nameBuf[128]; snprintf(nameBuf, sizeof(nameBuf), "%s", curTarget->name.c_str());
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) curTarget->name = nameBuf;
                ImGui::Text("Confidence: %.2f", curTarget->confidence);
                ImGui::SliderFloat("##conf", &curTarget->confidence, 0.50f, 1.00f, "");
                ImGui::Text("Click Offset X/Y");
                ImGui::PushItemWidth(70);
                ImGui::DragInt("##ox", &curTarget->clickOffsetX, 1.0f, -500, 500); ImGui::SameLine(); ImGui::DragInt("##oy", &curTarget->clickOffsetY, 1.0f, -500, 500);
                ImGui::PopItemWidth();
                ImGui::Text("Timeout (ms)"); ImGui::DragInt("##timeout", &curTarget->timeoutMs, 100.0f, 500, 30000);
                ImGui::PopItemWidth();
                ImGui::Spacing();
                static std::string matchResultText = ""; static DWORD matchResultTime = 0;
                if (ImGui::Button("Test Match", ImVec2(-1, 26))) {
                    int testId = curTarget->id;
                    HWND testHwnd = engine.targetHwnd;
                    bool testBg = engine.backgroundMode;
                    vlock.~lock_guard();
                    auto result = vision.WaitForTarget(testId, 1000, testHwnd, testBg);
                    if (result.found) { char buf[128]; snprintf(buf, sizeof(buf), "Found at X:%d Y:%d (Score: %.2f)", result.x, result.y, result.score); matchResultText = buf; }
                    else matchResultText = "Not found (Timed out)";
                    matchResultTime = GetTickCount();
                }
                if (GetTickCount() - matchResultTime < 3000 && !matchResultText.empty())
                    ImGui::TextColored(matchResultText.find("Found") != std::string::npos ? theme::AccentGreen() : theme::AccentRed(), "%s", matchResultText.c_str());
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(theme::AccentGreen().x, theme::AccentGreen().y, theme::AccentGreen().z, 0.45f));
                if (ImGui::Button("Insert to Macro", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4, 30))) {
                    hm::MacroAction act; act.type = hm::ActionType::WaitVisionClick; act.visionTargetId = curTarget->id; act.keyName = curTarget->name;
                    int idx = selectedAction >= 0 ? selectedAction : (int)engine.GetActions().size();
                    engine.InsertAction(idx, act); m_filterDirty = true;
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(theme::AccentRed().x, theme::AccentRed().y, theme::AccentRed().z, 0.45f));
                if (ImGui::Button("Delete Target", ImVec2(-1, 30))) { vision.RemoveTarget(selectedTarget); selectedTarget = -1; }
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::Spacing(); ImGui::TextColored(theme::TextDim(), "No Vision Targets saved.");
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(theme::TextDim(), "View & Filter Options"); ImGui::Spacing();
    if (ImGui::Checkbox("Hide Mouse Movements", &m_hideMouseMoves)) m_filterDirty = true;
    ImGui::Spacing();
    if (ImGui::Checkbox("Enable Filter", &m_filterEnabled)) m_filterDirty = true;
    if (m_filterEnabled) {
        ImGui::PushItemWidth(-1);
        if (ImGui::InputTextWithHint("##FilterText", "Search actions...", m_filterText, IM_ARRAYSIZE(m_filterText))) m_filterDirty = true;
        if (ImGui::Combo("##TypeFilter", &m_filterType, "All\0Mouse\0Keyboard\0Vision\0Delay\0")) m_filterDirty = true;
        ImGui::PopItemWidth();
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
    ImGui::Columns(1);

    if (m_cropActive) RenderCropOverlay();
    if (m_showLoadError) { ImGui::OpenPopup("Load Error"); m_showLoadError = false; }
    if (ImGui::BeginPopupModal("Load Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(theme::AccentRed(), "Failed to load macro file.");
        ImGui::Text("File may be corrupted or incompatible.");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (m_showSaveError) { ImGui::OpenPopup("Save Error"); m_showSaveError = false; }
    if (ImGui::BeginPopupModal("Save Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(theme::AccentRed(), "Failed to save macro file.");
        ImGui::Text("Check disk space or file permissions.");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Application::RenderConfig() {
    auto& engine = hm::MacroEngine::Get();
    auto& hk = hm::HookManager::Get();
    RenderHeader("Global Settings", "Configure keybinds and execution behavior");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float fullWidth = ImGui::GetContentRegionAvail().x;
    ImGui::Columns(2, "ConfigTop", false);
    ImGui::SetColumnWidth(0, fullWidth * 0.45f);

    ImGui::TextColored(theme::AccentBlue(), "Global Keybinds");
    ImGui::NextColumn();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
    ImGui::TextColored(theme::AccentBlue(), "Execution Dynamics");
    ImGui::NextColumn();

    ImGui::Separator();
    ImGui::Spacing();

    static ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("KeybindsTable", 3, tableFlags)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("Rebind", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        const auto& kbs = hk.GetKeybinds();
        for (const auto& kb : kbs) {
            ImGui::PushID(kb.id);
            ImGui::TableNextRow(0, 32.0f);

            ImGui::TableSetColumnIndex(0);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
            ImGui::TextUnformatted(kb.label.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Button, theme::Surface1());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button(kb.displayName.c_str(), ImVec2(80, 26))) {
                m_rebindTargetId = kb.id;
                m_rebindActive = true;
                m_rebindCooldown = 5;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Change", ImVec2(-1, 26))) {
                m_rebindTargetId = kb.id;
                m_rebindActive = true;
                m_rebindCooldown = 5;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::NextColumn();
    ImGui::Spacing();

    ImGui::TextColored(theme::TextDim(), "Playback");
    ImGui::Spacing();
    {
        float speed = engine.playbackSpeed;
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputFloat("Playback Speed", &speed, 0.1f, 1.0f, "%.2fx")) {
            speed = std::clamp(speed, 0.01f, 50.0f);
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.playbackSpeed = speed;
        }
        ImGui::Spacing();

        bool loop = engine.infiniteLoop;
        if (ImGui::Checkbox("Infinite Loop", &loop)) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.infiniteLoop = loop;
        }

        if (!loop) {
            int count = engine.loopCount;
            ImGui::Indent();
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt("Loop Count", &count)) {
                count = std::max(1, count);
                std::lock_guard<std::mutex> lock(engine.m_configMutex);
                engine.loopCount = count;
            }
            ImGui::Unindent();
        } else {
            ImGui::SameLine();
            ImGui::TextColored(theme::TextDim(), "(Until stopped manually)");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(theme::TextDim(), "Mouse");
    ImGui::Spacing();
    {
        ImGui::Checkbox("Humanize Mouse Movement", &engine.humanize);
        if (engine.humanize) {
            ImGui::Indent();
            ImGui::DragFloat("Jitter (ms)", &engine.jitterMs, 0.5f, 0.0f, 50.0f);
            ImGui::Checkbox("Bezier Paths", &engine.bezierPaths);
            ImGui::Unindent();
        }
        ImGui::Checkbox("Relative Mouse Mode", &engine.relativeMouse);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(theme::AccentYell(), "Alt-Tab Compensation");
    ImGui::Spacing();
    {
        bool altComp = engine.enableAltTabCompensation;
        if (ImGui::Checkbox("Compensate on Refocus", &altComp)) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.enableAltTabCompensation = altComp;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Subtracts time from the playback timeline\nwhen you alt-tab back into the target window.\nOnly active when 'Pause if Unfocused' is OFF.");

        if (engine.enableAltTabCompensation) {
            ImGui::Indent();
            int compMs = engine.altTabCompensationMs;
            if (ImGui::DragInt("Amount (ms)", &compMs, 5, 0, 2000)) {
                std::lock_guard<std::mutex> lock(engine.m_configMutex);
                engine.altTabCompensationMs = compMs;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Subtracted from the playback timeline\nwhen the target window regains focus.\nCapped at actual time spent unfocused.");
            ImGui::Unindent();
        }

        bool skipRef = engine.skipDelayOnRefocus;
        if (ImGui::Checkbox("Skip Delay on Refocus", &skipRef)) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.skipDelayOnRefocus = skipRef;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When you alt-tab back during a Delay action,\nimmediately skip to the next action.");
    }

    ImGui::Columns(1);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(theme::AccentBlue(), "Environment & Safety");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Columns(2, "EnvSplit", false);

    ImGui::TextColored(theme::TextDim(), "Execution Modifiers");
    ImGui::NextColumn();
    ImGui::TextColored(theme::TextDim(), "Target Window Status");
    ImGui::NextColumn();

    ImGui::Separator();
    ImGui::Spacing();

    {
        bool v = engine.smartForcing;
        if (ImGui::Checkbox("Smart Window Forcing", &v)) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.smartForcing = v;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Focuses target window before sending inputs.");

        bool bg = engine.backgroundMode;
        if (ImGui::Checkbox("Background Input Mode", &bg)) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.backgroundMode = bg;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Uses PostMessage / PrintWindow.\nAllows multi-tasking freely!");

        bool mute = engine.muteUnfocused;
        if (ImGui::Checkbox("Mute if Unfocused", &mute)) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.muteUnfocused = mute;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drops inputs if you Alt-Tab.\nKeeps timers running.");

        bool pause = engine.pauseUnfocused;
        if (ImGui::Checkbox("Pause if Unfocused", &pause)) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.pauseUnfocused = pause;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Freezes playback clock\nwhen you Alt-Tab away.");
    }

    ImGui::NextColumn();
    ImGui::Spacing();
    if (engine.targetHwnd) {
        ImGui::TextColored(theme::AccentGreen(), "LOCKED: %s", engine.targetTitle.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Reset Target", ImVec2(120, 28))) {
            std::lock_guard<std::mutex> lock(engine.m_configMutex);
            engine.targetHwnd = nullptr;
            engine.targetTitle.clear();
            engine.targetPid = 0;
            engine.targetExeName.clear();
        }
    } else {
        ImGui::TextColored(theme::TextDim(), "No target window selected.\nSelect from the dropdown in the header.");
    }

    ImGui::Columns(1);
    ImGui::Spacing();
}

void Application::RenderCropOverlay() {
    auto& vision = hm::VisionEngine::Get();
    ImGui::OpenPopup("Capture Region##overlay");
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);

    if (ImGui::BeginPopupModal("Capture Region##overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground)) {
        {
            std::lock_guard<std::recursive_mutex> vlock(vision.m_mutex);
            if (vision.GetFullScreenTexture())
                ImGui::Image((ImTextureID)(uintptr_t)vision.GetFullScreenTexture(), ImGui::GetIO().DisplaySize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 0.35f), ImVec4(0, 0, 0, 0));
        }

        ImGui::SetCursorPos(ImVec2(20, 20));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.75f));
        ImGui::BeginChild("hint", ImVec2(290, 55), true);
        ImGui::TextColored(theme::AccentBlue(), "Click & drag to select region");
        ImGui::TextColored(theme::TextDim(), "Press ESC to cancel");
        ImGui::EndChild();
        ImGui::PopStyleColor();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_cropDragging) {
            m_cropDragging = true; m_cropStart = ImGui::GetMousePos(); m_cropEnd = m_cropStart;
        }
        if (m_cropDragging) m_cropEnd = ImGui::GetMousePos();

        if (m_cropDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_cropDragging = false;
            int x1 = (int)m_cropStart.x, y1 = (int)m_cropStart.y, x2 = (int)m_cropEnd.x, y2 = (int)m_cropEnd.y;
            int rx = (x1 < x2) ? x1 : x2, ry = (y1 < y2) ? y1 : y2;
            int rw = std::abs(x2 - x1), rh = std::abs(y2 - y1);
            if (rw > 4 && rh > 4) {
                float scaleX = (float)vision.GetFullScreenWidth() / ImGui::GetIO().DisplaySize.x;
                float scaleY = (float)vision.GetFullScreenHeight() / ImGui::GetIO().DisplaySize.y;
                vision.CropAndAddTarget((int)(rx * scaleX), (int)(ry * scaleY), (int)(rw * scaleX), (int)(rh * scaleY), m_newTargetName);
                selectedTarget = (int)vision.GetTargets().back().id;
            }
            m_cropActive = false; ImGui::CloseCurrentPopup();
        }

        if (m_cropDragging) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImRect rect(m_cropStart, m_cropEnd);
            dl->AddRectFilled(rect.Min, rect.Max, IM_COL32(90, 141, 234, 50));
            dl->AddRect(rect.Min, rect.Max, IM_COL32(90, 141, 234, 200), 2.0f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_cropActive = false; m_cropDragging = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

void Application::RenderRebindPopup() {
    if (!m_rebindActive) return;
    if (m_rebindCooldown > 0) { m_rebindCooldown--; return; }

    ImGui::OpenPopup("Rebind Key##modal");
    ImGui::SetNextWindowSize(ImVec2(330, 170), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Rebind Key##modal", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextColored(theme::TextMain(), "Press your new key combination...");
        ImGui::TextColored(theme::TextDim(), "Hold Ctrl, Shift, or Alt + any key.");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        bool captured = false; WORD newVk = 0; bool newCtrl = false, newShift = false, newAlt = false;
        newCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        newShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        newAlt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        for (int vk = 8; vk <= 254; vk++) {
            bool isMod = (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN);
            if (isMod) continue;
            if (GetAsyncKeyState(vk) & 0x8000) { newVk = (WORD)vk; captured = true; break; }
        }

        if (captured) { hm::HookManager::Get().UpdateKeybind(m_rebindTargetId, newVk, newCtrl, newShift, newAlt); m_rebindActive = false; ImGui::CloseCurrentPopup(); }
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(-1, 34)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_rebindActive = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void Application::RenderStatusBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 6));
    ImGui::BeginChild("StatusBar", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);

    auto state = hm::MacroEngine::Get().GetState();
    const char* stateStr = "IDLE"; ImVec4 stateCol = theme::TextDim();
    if (state == hm::EngineState::Recording) { stateStr = "RECORDING"; stateCol = theme::AccentRed(); }
    else if (state == hm::EngineState::Playing) { stateStr = "PLAYING"; stateCol = theme::AccentGreen(); }

    ImGui::TextColored(stateCol, "[%s]", stateStr);
    ImGui::SameLine(130);
    auto& engine = hm::MacroEngine::Get();
    ImGui::TextColored(theme::TextDim(), "Actions: %d", (int)engine.GetActions().size());
    if (engine.targetHwnd) { ImGui::SameLine(260); ImGui::TextColored(theme::AccentBlue(), "Target: %s", engine.targetTitle.c_str()); }

    ImGui::SameLine(ImGui::GetWindowWidth() - 350);
    ImGui::TextColored(theme::TextDim(), "Speed: %.1fx", engine.playbackSpeed);
    ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    ImGui::Checkbox("Filter", &m_filterEnabled); ImGui::SameLine();
    ImGui::Checkbox("Hide Mouse", &m_hideMouseMoves);

    ImGui::EndChild(); ImGui::PopStyleVar();
}

void Application::RenderUI() {
    hm::HookManager::Get().Poll();
    static uint32_t lastHB = 0;
    static DWORD lastWDCheck = GetTickCount();

    if (GetTickCount() - lastWDCheck > 15000) {
        auto& eng = hm::MacroEngine::Get();
        uint32_t curHB = eng.m_heartbeat.load(std::memory_order_relaxed);
        if (curHB == lastHB && eng.GetState() == hm::EngineState::Playing) {
            eng.m_watchdogTripped.store(true);
            eng.EmergencyStop();
            eng.LogEvent("WATCHDOG_TRIP", "Playback frozen >15s");
        }
        lastHB = curHB;
        lastWDCheck = GetTickCount();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("MainWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    RenderSidebar();
    ImGui::SameLine();
    float statusBarHeight = 32.0f;
    ImGui::BeginChild("MainContent", ImVec2(0, -statusBarHeight), false);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 28));

    switch (currentTab) {
        case Tab::Editor: RenderEditor(); break;
        case Tab::Config: RenderConfig(); break;
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
    RenderStatusBar();
    ImGui::End();
    RenderRebindPopup();

    if (m_testPosActive && GetTickCount() - m_testPosTime < 1200) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 p = m_testPos;
        dl->AddLine(ImVec2(p.x - 15, p.y), ImVec2(p.x + 15, p.y), IM_COL32(255, 50, 50, 200), 2.0f);
        dl->AddLine(ImVec2(p.x, p.y - 15), ImVec2(p.x, p.y + 15), IM_COL32(255, 50, 50, 200), 2.0f);
        dl->AddCircle(p, 8.0f, IM_COL32(255, 50, 50, 150), 12, 2.0f);
    } else {
        m_testPosActive = false;
    }
}
