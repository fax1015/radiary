#include "app/AppWindow.h"
#include "app/AppWidgets.h"
#include "app/CameraUtils.h"
#include "app/ExportUtils.h"
#include "app/Resource.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <system_error>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "misc/cpp/imgui_stdlib.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using namespace radiary;


namespace {

constexpr int kInitialWindowWidth = 1560;
constexpr int kInitialWindowHeight = 980;
constexpr UINT_PTR kLiveResizeTimerId = 1;
constexpr UINT kLiveResizeTimerIntervalMs = 15;
constexpr float kOverlayPanelMarginX = 16.0f;
constexpr float kOverlayPanelMarginY = 12.0f;
constexpr float kDefaultToolbarSplit = 0.055f;
constexpr float kDefaultBottomPanelSplit = 0.22f;
constexpr float kDefaultLeftPanelSplit = 0.3f;
constexpr float kDefaultRightPanelSplit = 0.47f;
constexpr int kAppSettingsVersion = 1;


}  // namespace

namespace radiary {


bool AppWindow::Create(HINSTANCE instance, const int showCommand) {
    instance_ = instance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW windowClass {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(RGB(10, 10, 13));
    windowClass.lpszClassName = L"RadiaryImGuiWindow";
    RegisterClassExW(&windowClass);

    window_ = CreateWindowW(
        windowClass.lpszClassName,
        L"Radiary",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kInitialWindowWidth,
        kInitialWindowHeight,
        nullptr,
        nullptr,
        instance_,
        this);
    if (!window_) {
        CoUninitialize();
        return false;
    }
    ApplyDarkTitleBar(window_);

    ShowWindow(window_, showCommand);
    UpdateWindow(window_);

    auto updateLoading = [this](float progress, const char* label) {
        loadingProgress_ = progress;
        loadingLabel_ = label;
        InvalidateRect(window_, nullptr, TRUE);
        UpdateWindow(window_);
    };

    auto showLoadingFor = [this, &updateLoading](float progress, const char* label, int minMs) {
        const auto start = std::chrono::steady_clock::now();
        updateLoading(progress, label);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed < minMs) {
            Sleep(static_cast<DWORD>(minMs - elapsed));
        }
    };

    showLoadingFor(0.05f, "Initializing...", 50);
    EnumerateAdapters();

    showLoadingFor(0.15f, "Loading presets...", 40);
    LoadPresets();
    if (presetLibrary_.Count() > 0) {
        scene_ = presetLibrary_.SceneAt(0);
    }

    showLoadingFor(0.25f, "Loading settings...", 40);
    LoadUserSettings();
    if (presetLibrary_.Count() == 0) {
        ApplyUserSceneDefaults(scene_);
    }

    if (startupWindowPlacementLoaded_) {
        const int width = std::max(640, static_cast<int>(startupWindowRect_.right - startupWindowRect_.left));
        const int height = std::max(480, static_cast<int>(startupWindowRect_.bottom - startupWindowRect_.top));
        SetWindowPos(
            window_,
            nullptr,
            startupWindowRect_.left,
            startupWindowRect_.top,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (startupWindowMaximized_) {
            ShowWindow(window_, SW_MAXIMIZE);
        }
    }

    showLoadingFor(0.35f, "Creating D3D device...", 50);
    if (!CreateDeviceD3D()) {
        CleanupDeviceD3D();
        DestroyWindow(window_);
        window_ = nullptr;
        CoUninitialize();
        return false;
    }

    showLoadingFor(0.45f, "Initializing UI...", 50);
    SetupImGui();
    ApplyStyle();

    if (gpuFlamePreviewEnabled_) {
        updateLoading(0.50f, "Compiling flame shader...");
        EnsureGpuFlameRendererInitialized();

        updateLoading(0.65f, "Compiling path shader...");
        EnsureGpuPathRendererInitialized(gpuPathRenderer_, L"GPU path renderer");

        updateLoading(0.75f, "Compiling grid shader...");
        EnsureGpuPathRendererInitialized(gpuGridRenderer_, L"GPU grid renderer");

        updateLoading(0.85f, "Compiling DOF shader...");
        EnsureGpuDofRendererInitialized();
    }

    showLoadingFor(0.95f, "Starting render thread...", 40);
    scene_.animatePath = false;
    lastFrame_ = std::chrono::steady_clock::now();
    StartRenderThread();

    updateLoading(1.0f, "Done");
    loadingComplete_ = true;
    RenderFrame();
    bootstrapUiFramePending_ = false;
    viewportDirty_ = true;
    return true;
}

int AppWindow::Run() {
    MSG message {};
    while (running_) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) {
                running_ = false;
            }
        }

        if (!running_) {
            break;
        }

        if (!inSizeMove_ && !RenderTick()) {
            Sleep(1);
        }
    }

    SaveUserSettings();
    StopRenderThread();
    ShutdownImGui();
    CleanupViewportTexture();
    CleanupDeviceD3D();
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK AppWindow::WindowProc(HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<AppWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }

    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT AppWindow::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(window_, message, wParam, lParam)) {
        return 1;
    }

    switch (message) {
    case WM_PAINT:
        if (!loadingComplete_) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(window_, &ps);
            RECT clientRect;
            GetClientRect(window_, &clientRect);
            const int width = clientRect.right - clientRect.left;
            const int height = clientRect.bottom - clientRect.top;

            HBRUSH bgBrush = CreateSolidBrush(RGB(10, 10, 13));
            FillRect(hdc, &clientRect, bgBrush);
            DeleteObject(bgBrush);

            constexpr int barHeight = 4;
            constexpr int barMargin = 80;
            const int barWidth = width - barMargin * 2;
            const int barY = height / 2 + 20;

            RECT barBgRect = {barMargin, barY, barMargin + barWidth, barY + barHeight};
            HBRUSH barBgBrush = CreateSolidBrush(RGB(40, 40, 50));
            FillRect(hdc, &barBgRect, barBgBrush);
            DeleteObject(barBgBrush);

            const int progressWidth = static_cast<int>(barWidth * loadingProgress_);
            if (progressWidth > 0) {
                RECT barFillRect = {barMargin, barY, barMargin + progressWidth, barY + barHeight};
                HBRUSH barFillBrush = CreateSolidBrush(RGB(115, 148, 235));
                FillRect(hdc, &barFillRect, barFillBrush);
                DeleteObject(barFillBrush);
            }

            if (!loadingLabel_.empty()) {
                SetTextColor(hdc, RGB(200, 200, 210));
                SetBkMode(hdc, TRANSPARENT);
                HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
                std::wstring wideLabel = Utf8ToWide(loadingLabel_);
                TextOutW(hdc, barMargin, barY - 28, wideLabel.c_str(), static_cast<int>(wideLabel.length()));
                SelectObject(hdc, oldFont);
                DeleteObject(font);
            }

            EndPaint(window_, &ps);
            return 0;
        }
        break;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            return 0;
        }
        resizeWidth_ = static_cast<UINT>(LOWORD(lParam));
        resizeHeight_ = static_cast<UINT>(HIWORD(lParam));
        viewportDirty_ = true;
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_ENTERSIZEMOVE:
        inSizeMove_ = true;
        SetTimer(window_, kLiveResizeTimerId, kLiveResizeTimerIntervalMs, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        inSizeMove_ = false;
        KillTimer(window_, kLiveResizeTimerId);
        RenderTick();
        return 0;
    case WM_TIMER:
        if (wParam == kLiveResizeTimerId && inSizeMove_) {
            RenderTick();
            return 0;
        }
        break;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_DWMCOMPOSITIONCHANGED:
        ApplyDarkTitleBar(window_);
        return 0;
    case WM_DESTROY:
        KillTimer(window_, kLiveResizeTimerId);
        PostQuitMessage(0);
        running_ = false;
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window_, message, wParam, lParam);
}


bool AppWindow::RenderTick() {
    if (!ApplyPendingGraphicsDeviceChange()) {
        return false;
    }

    if (swapChainOccluded_ && swapChain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        return false;
    }
    swapChainOccluded_ = false;

    ApplyPendingResize();
    ProcessPendingExport();
    RenderFrame();
    return true;
}


void AppWindow::PumpPendingMessages() {
    MSG message {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (message.message == WM_QUIT) {
            running_ = false;
        }
    }
}


bool AppWindow::PresentBlockingOverlay() {
    PumpPendingMessages();
    if (!running_ || window_ == nullptr) {
        return false;
    }

    if (swapChainOccluded_ && swapChain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
        return running_;
    }
    swapChainOccluded_ = false;

    ApplyPendingResize();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawBlockingOverlay();
    ImGui::Render();

    constexpr float clearColor[4] = {0.03f, 0.03f, 0.04f, 1.0f};
    deviceContext_->OMSetRenderTargets(1, mainRenderTargetView_.GetAddressOf(), nullptr);
    deviceContext_->ClearRenderTargetView(mainRenderTargetView_.Get(), clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    const HRESULT result = swapChain_->Present(1, 0);
    swapChainOccluded_ = (result == DXGI_STATUS_OCCLUDED);
    return running_;
}


void AppWindow::ProcessPendingExport() {
    if (!exportRequestPending_ || exportInProgress_) {
        return;
    }

    exportInProgress_ = true;
    exportCancelRequested_ = false;
    exportProgress_ = 0.0f;
    exportProgressTitle_ = L"Exporting";
    exportProgressDetail_ = L"Preparing export...";
    exportProgressEta_.clear();
    exportProgressStartedAt_ = std::chrono::steady_clock::now();
    const ExportRequest request = pendingExportRequest_;
    exportRequestPending_ = false;

    PresentBlockingOverlay();
    const bool success = ExecuteExportRequest(request);
    if (success) {
        exportPanelOpen_ = false;
    }

    exportInProgress_ = false;
    exportCancelRequested_ = false;
    exportProgress_ = 0.0f;
    exportProgressTitle_.clear();
    exportProgressDetail_.clear();
    exportProgressEta_.clear();
    exportProgressStartedAt_ = {};
}

bool AppWindow::ExecuteExportRequest(const ExportRequest& request) {
    switch (request.format) {
    case ExportFormat::Png:
    case ExportFormat::Jpeg:
        return ExportViewportImage(
            request.path,
            request.width,
            request.height,
            request.transparentBackground,
            request.hideGrid,
            request.useGpu,
            request.format);
    case ExportFormat::PngSequence:
    case ExportFormat::JpegSequence:
        return ExportImageSequence(
            request.path,
            request.width,
            request.height,
            request.transparentBackground,
            request.hideGrid,
            request.useGpu,
            request.format,
            request.startFrame,
            request.endFrame);
    case ExportFormat::Avi:
        return ExportAviVideo(
            request.path,
            request.width,
            request.height,
            request.hideGrid,
            request.useGpu,
            request.startFrame,
            request.endFrame);
    case ExportFormat::Mp4:
    case ExportFormat::Mov:
        return ExportFfmpegVideo(
            request.path,
            request.width,
            request.height,
            request.hideGrid,
            request.useGpu,
            request.startFrame,
            request.endFrame,
            request.format);
    }
    return false;
}

bool AppWindow::UpdateExportProgress(const float progress, const std::wstring& detail) {
    exportProgress_ = std::clamp(progress, 0.0f, 1.0f);
    exportProgressDetail_ = detail;
    if (exportProgress_ > 0.001f && exportProgress_ < 0.999f && exportProgressStartedAt_ != std::chrono::steady_clock::time_point {}) {
        const auto elapsed = std::chrono::steady_clock::now() - exportProgressStartedAt_;
        const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
        const double remainingSeconds = elapsedSeconds * (1.0 - static_cast<double>(exportProgress_)) / static_cast<double>(exportProgress_);
        exportProgressEta_ = L"ETA " + FormatEtaDuration(std::chrono::seconds(static_cast<long long>(std::llround(std::max(0.0, remainingSeconds)))));
    } else {
        exportProgressEta_.clear();
    }
    if (!PresentBlockingOverlay()) {
        return false;
    }
    return !exportCancelRequested_;
}


void AppWindow::RenderFrame() {
    ConsumeCompletedRender();

    const auto now = std::chrono::steady_clock::now();
    const double deltaSeconds = std::chrono::duration<double>(now - lastFrame_).count();
    lastFrame_ = now;
    fpsSmoothed_ = fpsSmoothed_ * 0.92 + (deltaSeconds > 0.0 ? (1.0 / deltaSeconds) : fpsSmoothed_) * 0.08;

    if (scene_.animatePath) {
        const double nextFrame = scene_.timelineFrame + deltaSeconds * std::max(1.0, scene_.timelineFrameRate);
        const double endFrame = static_cast<double>(std::max(scene_.timelineStartFrame, scene_.timelineEndFrame));
        const double startFrame = static_cast<double>(scene_.timelineStartFrame);
        const double span = std::max(1.0, endFrame - startFrame + 1.0);
        double wrappedFrame = nextFrame;
        if (wrappedFrame > endFrame) {
            wrappedFrame = startFrame + std::fmod(wrappedFrame - startFrame, span);
        }
        SetTimelineFrame(wrappedFrame, false);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    HandleShortcuts();
    DrawDockspace();
    DrawToolbar();
    DrawLayersPanel();
    DrawInspectorPanel();
    DrawTimelinePanel();
    DrawCameraPanel();
    DrawViewportPanel();
    if (showStatusOverlay_) {
        DrawStatusBar();
    }
    DrawSettingsPanel();
    DrawExportPanel();
    DrawEasingPanel();

    ImGui::Render();
    constexpr float clearColor[4] = {0.04f, 0.04f, 0.05f, 1.0f};
    deviceContext_->OMSetRenderTargets(1, mainRenderTargetView_.GetAddressOf(), nullptr);
    deviceContext_->ClearRenderTargetView(mainRenderTargetView_.Get(), clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    const HRESULT result = swapChain_->Present(presentVsync_ ? 1 : 0, 0);
    swapChainOccluded_ = (result == DXGI_STATUS_OCCLUDED);
}


std::wstring AppWindow::Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string AppWindow::WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::size_t AppWindow::GradientStopIndexForPaletteColor(const int paletteIndex) {
    return static_cast<std::size_t>(paletteIndex + 1);
}

Color AppWindow::ToColor(const float color[3]) {
    return {
        static_cast<std::uint8_t>(std::clamp(color[0] * 255.0f, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(color[1] * 255.0f, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(color[2] * 255.0f, 0.0f, 255.0f)),
        255
    };
}

}  // namespace radiary

