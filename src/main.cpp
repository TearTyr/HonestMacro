#include "windows.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"
#include <GL/gl.h>
#include "app.h"
#include "Theme.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool s_resizing = false;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
        case WM_ERASEBKGND:
            return TRUE; // Prevent background clear during resize
        case WM_ENTERSIZEMOVE:
            s_resizing = true;
            return 0;
        case WM_EXITSIZEMOVE:
            s_resizing = false;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDPIAware();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"HonestMacro";
    wc.hIcon         = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Honest Macro",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1150, 680,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // OpenGL Context Setup
    HDC hDc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    int fmt = ChoosePixelFormat(hDc, &pfd);
    SetPixelFormat(hDc, fmt, &pfd);
    HGLRC rc = wglCreateContext(hDc);
    wglMakeCurrent(hDc, rc);
    ReleaseDC(hwnd, hDc);

    // ImGui Setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL3_Init();
    Application::Get().Init();

    bool running = true;
    MSG msg;

    while (running) {
        bool hasMsg = false;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            hasMsg = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        auto state = hm::MacroEngine::Get().GetState();
        bool isVisible = IsWindowVisible(hwnd) && !IsIconic(hwnd);

        if (!hasMsg && state == hm::EngineState::Idle && !isVisible) {
            WaitMessage();
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Application::Get().RenderUI();
        ImGui::Render();

        ImVec4 clear = theme::BgClear();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear.x, clear.y, clear.z, clear.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        HDC dc = GetDC(hwnd);
        SwapBuffers(dc);
        ReleaseDC(hwnd, dc);

        if (s_resizing) continue;

        if (state == hm::EngineState::Idle && !hasMsg) {
            Sleep(isVisible ? 16 : 33);
        } else {
            Sleep(8);
        }
    }

    Application::Get().Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);

    return 0;
}
