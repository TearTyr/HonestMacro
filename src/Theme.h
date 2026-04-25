#pragma once
#include "imgui.h"

namespace theme {
inline void Apply() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    s.WindowRounding = 10.0f; s.ChildRounding = 8.0f; s.FrameRounding = 6.0f;
    s.GrabRounding = 6.0f; s.PopupRounding = 8.0f; s.ScrollbarRounding = 6.0f;
    s.TabRounding = 6.0f; s.WindowPadding = ImVec2(16, 16); s.ItemSpacing = ImVec2(12, 10);
    s.ItemInnerSpacing = ImVec2(8, 6); s.FramePadding = ImVec2(10, 6); s.IndentSpacing = 20.0f;
    s.ScrollbarSize = 12.0f; s.GrabMinSize = 12.0f; s.WindowBorderSize = 1.0f;
    s.ChildBorderSize = 1.0f; s.FrameBorderSize = 1.0f; s.TabBorderSize = 0.0f;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f); s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f); s.Alpha = 1.0f; s.DisabledAlpha = 0.45f;

    const ImVec4 kBgDeep = ImVec4(0.039f, 0.039f, 0.059f, 1.00f);
    const ImVec4 kBgWindow = ImVec4(0.051f, 0.051f, 0.075f, 1.00f);
    const ImVec4 kBgFrame = ImVec4(0.027f, 0.027f, 0.043f, 1.00f);
    const ImVec4 kBgPopup = ImVec4(0.055f, 0.055f, 0.082f, 0.98f);
    const ImVec4 kSurf0 = ImVec4(0.086f, 0.086f, 0.122f, 1.00f);
    const ImVec4 kSurf1 = ImVec4(0.118f, 0.118f, 0.161f, 1.00f);
    const ImVec4 kSurf2 = ImVec4(0.153f, 0.153f, 0.204f, 1.00f);
    const ImVec4 kBorder = ImVec4(0.165f, 0.165f, 0.220f, 0.65f);
    const ImVec4 kBorderHi = ImVec4(0.220f, 0.220f, 0.290f, 0.85f);
    const ImVec4 kText = ImVec4(0.820f, 0.830f, 0.860f, 1.00f);
    const ImVec4 kTextMuted = ImVec4(0.530f, 0.540f, 0.600f, 1.00f);
    const ImVec4 kTextDim = ImVec4(0.360f, 0.370f, 0.420f, 1.00f);
    const ImVec4 kAccentBlue = ImVec4(0.340f, 0.560f, 0.920f, 1.00f);
    const ImVec4 kAccentGreen = ImVec4(0.290f, 0.740f, 0.480f, 1.00f);
    const ImVec4 kAccentYell = ImVec4(0.850f, 0.710f, 0.240f, 1.00f);

    c[ImGuiCol_Text] = kText; c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kBgWindow; c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = kBgPopup; c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = kBgFrame; c[ImGuiCol_FrameBgHovered] = kSurf0;
    c[ImGuiCol_FrameBgActive] = kSurf1; c[ImGuiCol_TitleBg] = kBgDeep;
    c[ImGuiCol_TitleBgActive] = kBgWindow; c[ImGuiCol_TitleBgCollapsed] = kBgDeep;
    c[ImGuiCol_MenuBarBg] = kBgDeep; c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = kSurf1; c[ImGuiCol_ScrollbarGrabHovered] = kSurf2;
    c[ImGuiCol_ScrollbarGrabActive] = kTextMuted; c[ImGuiCol_CheckMark] = kAccentGreen;
    c[ImGuiCol_SliderGrab] = kAccentBlue; c[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.65f, 1.00f, 1.00f);
    c[ImGuiCol_Button] = kSurf0; c[ImGuiCol_ButtonHovered] = kSurf1; c[ImGuiCol_ButtonActive] = kSurf2;
    c[ImGuiCol_Header] = kSurf0; c[ImGuiCol_HeaderHovered] = kSurf1; c[ImGuiCol_HeaderActive] = kSurf2;
    c[ImGuiCol_Separator] = kBorder; c[ImGuiCol_SeparatorHovered] = kBorderHi;
    c[ImGuiCol_SeparatorActive] = kTextMuted; c[ImGuiCol_ResizeGrip] = kSurf1;
    c[ImGuiCol_ResizeGripHovered] = kSurf2; c[ImGuiCol_ResizeGripActive] = kTextMuted;
    c[ImGuiCol_Tab] = kSurf0; c[ImGuiCol_TabHovered] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.35f);
    c[ImGuiCol_TabActive] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.22f);
    c[ImGuiCol_TabUnfocused] = kBgDeep; c[ImGuiCol_TabUnfocusedActive] = kBgWindow;
    c[ImGuiCol_PlotLines] = kAccentBlue; c[ImGuiCol_PlotLinesHovered] = kAccentGreen;
    c[ImGuiCol_PlotHistogram] = kAccentBlue; c[ImGuiCol_PlotHistogramHovered] = kAccentGreen;
    c[ImGuiCol_TableHeaderBg] = kBgDeep; c[ImGuiCol_TableBorderStrong] = kBorderHi;
    c[ImGuiCol_TableBorderLight] = kBorder; c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(kSurf0.x, kSurf0.y, kSurf0.z, 0.30f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(kAccentBlue.x, kAccentBlue.y, kAccentBlue.z, 0.35f);
    c[ImGuiCol_DragDropTarget] = kAccentYell; c[ImGuiCol_NavHighlight] = kAccentBlue;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.75f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(kBgDeep.x, kBgDeep.y, kBgDeep.z, 0.78f);
}

inline ImVec4 AccentBlue() { return ImVec4(0.340f, 0.560f, 0.920f, 1.00f); }
inline ImVec4 AccentGreen() { return ImVec4(0.290f, 0.740f, 0.480f, 1.00f); }
inline ImVec4 AccentRed() { return ImVec4(0.880f, 0.280f, 0.320f, 1.00f); }
inline ImVec4 AccentYell() { return ImVec4(0.850f, 0.710f, 0.240f, 1.00f); }
inline ImVec4 TextMain() { return ImVec4(0.820f, 0.830f, 0.860f, 1.00f); }
inline ImVec4 TextDim() { return ImVec4(0.360f, 0.370f, 0.420f, 1.00f); }
inline ImVec4 Surface0() { return ImVec4(0.086f, 0.086f, 0.122f, 1.00f); }
inline ImVec4 Surface1() { return ImVec4(0.118f, 0.118f, 0.161f, 1.00f); }
inline ImVec4 Surface2() { return ImVec4(0.153f, 0.153f, 0.204f, 1.00f); }
inline ImVec4 BgClear() { return ImVec4(0.039f, 0.039f, 0.059f, 1.00f); }
} // namespace theme