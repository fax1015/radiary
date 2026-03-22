#include "app/AppWindow.h"
#include "app/AppWidgets.h"
#include "app/CameraUtils.h"
#include "app/ExportUtils.h"
#include "app/Resource.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <functional>
#include <future>
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
constexpr int kMinimumClientWidth = 1120;
constexpr int kMinimumClientHeight = 720;
constexpr UINT_PTR kLiveResizeTimerId = 1;
constexpr UINT_PTR kStartupAnimationTimerId = 2;
constexpr UINT kLiveResizeTimerIntervalMs = 15;
constexpr UINT kStartupAnimationTimerIntervalMs = 16;
constexpr float kOverlayPanelMarginX = 16.0f;
constexpr float kOverlayPanelMarginY = 12.0f;
constexpr int kAppSettingsVersion = 1;
// Replace this file to swap the app logo everywhere it is used.
constexpr wchar_t kAppLogoRelativePath[] = L"assets/ui/app-logo.png";
constexpr wchar_t kAnimatedAppLogoRelativePath[] = L"assets/ui/app-logo.svg";

std::filesystem::path GetExecutableDirectory() {
    wchar_t modulePath[MAX_PATH] = L"";
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(modulePath).parent_path();
}

std::vector<std::filesystem::path> AssetCandidates(const std::filesystem::path& relativePath) {
    const std::filesystem::path exeDir = GetExecutableDirectory();
    return {
        std::filesystem::current_path() / relativePath,
        exeDir / relativePath,
        exeDir.parent_path() / relativePath,
        exeDir.parent_path().parent_path() / relativePath
    };
}

bool LoadImagePixelsWithWic(
    const std::filesystem::path& path,
    std::vector<std::uint32_t>& pixels,
    int& width,
    int& height) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool success = false;
    UINT imageWidth = 0;
    UINT imageHeight = 0;

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        && SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))
        && SUCCEEDED(decoder->GetFrame(0, &frame))
        && SUCCEEDED(factory->CreateFormatConverter(&converter))
        && SUCCEEDED(frame->GetSize(&imageWidth, &imageHeight))
        && imageWidth > 0
        && imageHeight > 0
        && SUCCEEDED(converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom))) {
        width = static_cast<int>(imageWidth);
        height = static_cast<int>(imageHeight);
        const UINT stride = imageWidth * 4;
        const UINT bufferSize = stride * imageHeight;
        pixels.resize(static_cast<std::size_t>(imageWidth) * static_cast<std::size_t>(imageHeight));
        if (SUCCEEDED(converter->CopyPixels(nullptr, stride, bufferSize, reinterpret_cast<BYTE*>(pixels.data())))) {
            success = true;
        }
    }

    if (!success) {
        pixels.clear();
        width = 0;
        height = 0;
    }

    if (converter) { converter->Release(); }
    if (frame) { frame->Release(); }
    if (decoder) { decoder->Release(); }
    if (factory) { factory->Release(); }
    return success;
}

SIZE WindowSizeForClientSize(HWND window, const int clientWidth, const int clientHeight) {
    RECT rect {0, 0, clientWidth, clientHeight};
    const DWORD style = window != nullptr ? static_cast<DWORD>(GetWindowLongW(window, GWL_STYLE)) : WS_OVERLAPPEDWINDOW;
    const DWORD exStyle = window != nullptr ? static_cast<DWORD>(GetWindowLongW(window, GWL_EXSTYLE)) : 0;
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    return {
        rect.right - rect.left,
        rect.bottom - rect.top
    };
}

}  // namespace

namespace radiary {


bool AppWindow::Create(HINSTANCE instance, const int showCommand) {
    instance_ = instance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        gdiplusToken_ = 0;
    }
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW windowClass {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hIcon = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_RADIARY_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_RADIARY_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
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
        if (gdiplusToken_ != 0) {
            Gdiplus::GdiplusShutdown(gdiplusToken_);
            gdiplusToken_ = 0;
        }
        CoUninitialize();
        return false;
    }
    EnsureAppLogoLoaded();
    EnsureStartupLogoSvgLoaded();
    startupAnimationDashOffset_ = 0.0f;
    if (gdiplusToken_ != 0 && !startupLogoSvgStrokes_.empty()) {
        SetTimer(window_, kStartupAnimationTimerId, kStartupAnimationTimerIntervalMs, nullptr);
    }
    ApplyDarkTitleBar(window_);

    ShowWindow(window_, showCommand);
    UpdateWindow(window_);

    auto updateLoading = [this](float progress, const char* label) {
        loadingProgress_ = progress;
        loadingLabel_ = label;
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
    };

    auto showLoadingFor = [this, &updateLoading](float progress, const char* label, int minMs) {
        const auto start = std::chrono::steady_clock::now();
        updateLoading(progress, label);
        while (true) {
            PumpPendingMessages();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= minMs || !running_) {
                break;
            }
            Sleep(static_cast<DWORD>(std::min<long long>(16, minMs - elapsed)));
        }
    };
    auto runAnimatedLoadingTask = [this, &updateLoading](float progress, const char* label, auto&& task) {
        updateLoading(progress, label);
        auto future = std::async(std::launch::async, [task = std::forward<decltype(task)>(task)]() mutable {
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool initializedCom = SUCCEEDED(comResult);
            bool result = false;
            try {
                result = task();
            } catch (...) {
                if (initializedCom) {
                    CoUninitialize();
                }
                throw;
            }
            if (initializedCom) {
                CoUninitialize();
            }
            return result;
        });

        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            PumpPendingMessages();
            if (!running_) {
                Sleep(1);
                continue;
            }
            Sleep(8);
        }
        return future.get();
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

    if (!runAnimatedLoadingTask(0.35f, "Creating D3D device...", [this]() {
            return CreateDeviceD3D();
        })) {
        CleanupDeviceD3D();
        KillTimer(window_, kStartupAnimationTimerId);
        DestroyWindow(window_);
        window_ = nullptr;
        if (gdiplusToken_ != 0) {
            Gdiplus::GdiplusShutdown(gdiplusToken_);
            gdiplusToken_ = 0;
        }
        CoUninitialize();
        return false;
    }

    showLoadingFor(0.45f, "Initializing UI...", 50);
    SetupImGui();
    ApplyStyle();

    if (gpuFlamePreviewEnabled_) {
        runAnimatedLoadingTask(0.50f, "Compiling flame shader...", [this]() {
            return EnsureGpuFlameRendererInitialized();
        });

        runAnimatedLoadingTask(0.65f, "Compiling path shader...", [this]() {
            return EnsureGpuPathRendererInitialized(gpuPathRenderer_, L"GPU path renderer");
        });

        runAnimatedLoadingTask(0.75f, "Compiling grid shader...", [this]() {
            return EnsureGpuPathRendererInitialized(gpuGridRenderer_, L"GPU grid renderer");
        });

        runAnimatedLoadingTask(0.85f, "Compiling DOF shader...", [this]() {
            return EnsureGpuDofRendererInitialized();
        });
    }

    showLoadingFor(0.95f, "Starting render thread...", 40);
    scene_.animatePath = false;
    lastFrame_ = std::chrono::steady_clock::now();
    StartRenderThread();

    RenderFrame();
    bootstrapUiFramePending_ = false;
    MarkViewportDirty();
    updateLoading(1.0f, "Done");
    loadingComplete_ = true;
    KillTimer(window_, kStartupAnimationTimerId);
    ValidateRect(window_, nullptr);
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
    if (gdiplusToken_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}

bool AppWindow::EnsureAppLogoLoaded() {
    if (appLogoLoadAttempted_) {
        return !appLogoPixels_.empty();
    }

    appLogoLoadAttempted_ = true;
    for (const auto& candidate : AssetCandidates(kAppLogoRelativePath)) {
        if (!std::filesystem::exists(candidate)) {
            continue;
        }
        if (LoadImagePixelsWithWic(candidate, appLogoPixels_, appLogoWidth_, appLogoHeight_)) {
            appLogoPath_ = candidate;
            return true;
        }
    }

    appLogoPixels_.clear();
    appLogoWidth_ = 0;
    appLogoHeight_ = 0;
    appLogoPath_.clear();
    return false;
}

bool AppWindow::EnsureStartupLogoSvgLoaded() {
    if (startupLogoSvgLoadAttempted_) {
        return !startupLogoSvgStrokes_.empty();
    }

    startupLogoSvgLoadAttempted_ = true;
    for (const auto& candidate : AssetCandidates(kAnimatedAppLogoRelativePath)) {
        if (!std::filesystem::exists(candidate)) {
            continue;
        }
        StartupLogoSvgData svgData;
        if (!LoadStartupLogoSvg(candidate, svgData)) {
            continue;
        }
        startupLogoSvgPath_ = candidate;
        startupLogoSvgStrokes_ = std::move(svgData.strokes);
        startupLogoSvgColor_ = svgData.color;
        startupLogoSvgMinX_ = svgData.minX;
        startupLogoSvgMinY_ = svgData.minY;
        startupLogoSvgMaxX_ = svgData.maxX;
        startupLogoSvgMaxY_ = svgData.maxY;
        return true;
    }

    startupLogoSvgPath_.clear();
    startupLogoSvgStrokes_.clear();
    startupLogoSvgMinX_ = 0.0f;
    startupLogoSvgMinY_ = 0.0f;
    startupLogoSvgMaxX_ = 0.0f;
    startupLogoSvgMaxY_ = 0.0f;
    return false;
}

void AppWindow::DrawAnimatedLoadingLogo(HDC hdc, const int clientWidth, const int clientHeight, const int barY) const {
    if (hdc == nullptr || gdiplusToken_ == 0 || startupLogoSvgStrokes_.empty()) {
        return;
    }

    const float boundsWidth = std::max(1.0f, startupLogoSvgMaxX_ - startupLogoSvgMinX_);
    const float boundsHeight = std::max(1.0f, startupLogoSvgMaxY_ - startupLogoSvgMinY_);
    const float maxExtent = static_cast<float>(std::clamp(std::min(clientWidth, clientHeight) / 4, 90, 156));
    const float scale = std::min(maxExtent / boundsWidth, maxExtent / boundsHeight) * 0.9f;
    const float drawWidth = boundsWidth * scale;
    const float drawHeight = boundsHeight * scale;
    const float drawX = std::floor((static_cast<float>(clientWidth) - drawWidth) * 0.5f);
    const float drawY = std::floor(std::max(36.0f, static_cast<float>(barY) - drawHeight - 54.0f));

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::GraphicsPath path;
    for (const StartupLogoStroke& stroke : startupLogoSvgStrokes_) {
        if (stroke.points.size() < 2) {
            continue;
        }
        std::vector<Gdiplus::PointF> transformedPoints;
        transformedPoints.reserve(stroke.points.size());
        for (const StartupLogoPoint& point : stroke.points) {
            transformedPoints.push_back(Gdiplus::PointF(
                drawX + (point.x - startupLogoSvgMinX_) * scale,
                drawY + (point.y - startupLogoSvgMinY_) * scale));
        }
        path.StartFigure();
        path.AddLines(transformedPoints.data(), static_cast<INT>(transformedPoints.size()));
        if (stroke.closed) {
            path.CloseFigure();
        }
    }

    const float strokeWidth = std::clamp(maxExtent / 24.0f, 3.0f, 5.8f);
    const Gdiplus::Color logoColor(255, GetRValue(startupLogoSvgColor_), GetGValue(startupLogoSvgColor_), GetBValue(startupLogoSvgColor_));
    Gdiplus::GraphicsState state = graphics.Save();
    graphics.SetClip(&path, Gdiplus::CombineModeIntersect);
    Gdiplus::Pen dashPen(logoColor, strokeWidth);
    dashPen.SetLineJoin(Gdiplus::LineJoinRound);
    dashPen.SetStartCap(Gdiplus::LineCapSquare);
    dashPen.SetEndCap(Gdiplus::LineCapSquare);
    dashPen.SetDashCap(Gdiplus::DashCapFlat);
    Gdiplus::REAL dashPattern[] = {1.7f, 2.0f};
    dashPen.SetDashPattern(dashPattern, 2);
    dashPen.SetDashOffset(static_cast<Gdiplus::REAL>(startupAnimationDashOffset_));
    graphics.DrawPath(&dashPen, &path);
    graphics.Restore(state);
}

void AppWindow::DrawLoadingLogo(HDC hdc, const int clientWidth, const int clientHeight, const int barY) const {
    if (hdc == nullptr || appLogoPixels_.empty() || appLogoWidth_ <= 0 || appLogoHeight_ <= 0) {
        return;
    }

    const int maxExtent = std::clamp(std::min(clientWidth, clientHeight) / 4, 72, 124);
    int drawWidth = maxExtent;
    int drawHeight = std::max(1, static_cast<int>(std::llround(static_cast<double>(drawWidth) * static_cast<double>(appLogoHeight_) / static_cast<double>(appLogoWidth_))));
    if (drawHeight > maxExtent) {
        drawHeight = maxExtent;
        drawWidth = std::max(1, static_cast<int>(std::llround(static_cast<double>(drawHeight) * static_cast<double>(appLogoWidth_) / static_cast<double>(appLogoHeight_))));
    }

    const int drawX = (clientWidth - drawWidth) / 2;
    const int drawY = std::max(36, barY - drawHeight - 58);

    BITMAPINFO bitmapInfo {};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = appLogoWidth_;
    bitmapInfo.bmiHeader.biHeight = -appLogoHeight_;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    std::vector<std::uint32_t> compositedPixels(appLogoPixels_.size());
    constexpr std::uint8_t backgroundB = 10;
    constexpr std::uint8_t backgroundG = 10;
    constexpr std::uint8_t backgroundR = 13;
    for (std::size_t index = 0; index < appLogoPixels_.size(); ++index) {
        const std::uint32_t pixel = appLogoPixels_[index];
        const std::uint32_t alpha = (pixel >> 24U) & 0xFFU;
        if (alpha == 0U) {
            compositedPixels[index] = 0xFF000000U
                | static_cast<std::uint32_t>(backgroundB)
                | (static_cast<std::uint32_t>(backgroundG) << 8U)
                | (static_cast<std::uint32_t>(backgroundR) << 16U);
            continue;
        }
        if (alpha == 0xFFU) {
            compositedPixels[index] = pixel;
            continue;
        }

        const std::uint32_t inverseAlpha = 0xFFU - alpha;
        const std::uint32_t b = pixel & 0xFFU;
        const std::uint32_t g = (pixel >> 8U) & 0xFFU;
        const std::uint32_t r = (pixel >> 16U) & 0xFFU;
        const std::uint32_t outB = (b * alpha + backgroundB * inverseAlpha + 127U) / 255U;
        const std::uint32_t outG = (g * alpha + backgroundG * inverseAlpha + 127U) / 255U;
        const std::uint32_t outR = (r * alpha + backgroundR * inverseAlpha + 127U) / 255U;
        compositedPixels[index] = 0xFF000000U | outB | (outG << 8U) | (outR << 16U);
    }

    const int previousStretchMode = SetStretchBltMode(hdc, HALFTONE);
    POINT previousBrushOrigin {};
    SetBrushOrgEx(hdc, 0, 0, &previousBrushOrigin);
    StretchDIBits(
        hdc,
        drawX,
        drawY,
        drawWidth,
        drawHeight,
        0,
        0,
        appLogoWidth_,
        appLogoHeight_,
        compositedPixels.data(),
        &bitmapInfo,
        DIB_RGB_COLORS,
        SRCCOPY);
    SetBrushOrgEx(hdc, previousBrushOrigin.x, previousBrushOrigin.y, nullptr);
    SetStretchBltMode(hdc, previousStretchMode);
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
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (!loadingComplete_) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(window_, &ps);
            RECT clientRect;
            GetClientRect(window_, &clientRect);
            const int width = clientRect.right - clientRect.left;
            const int height = clientRect.bottom - clientRect.top;
            HDC bufferDc = CreateCompatibleDC(hdc);
            HBITMAP bufferBitmap = CreateCompatibleBitmap(hdc, std::max(1, width), std::max(1, height));
            HBITMAP oldBitmap = bufferBitmap != nullptr ? static_cast<HBITMAP>(SelectObject(bufferDc, bufferBitmap)) : nullptr;
            HDC paintDc = (bufferDc != nullptr && bufferBitmap != nullptr) ? bufferDc : hdc;

            HBRUSH bgBrush = CreateSolidBrush(RGB(10, 10, 13));
            FillRect(paintDc, &clientRect, bgBrush);
            DeleteObject(bgBrush);

            constexpr int barHeight = 4;
            constexpr int barMargin = 32;
            const int barWidth = std::max(280, std::min(width - barMargin * 2, 860));
            const int barX = (width - barWidth) / 2;
            const int barY = height / 2 + (appLogoPixels_.empty() ? 20 : 34);

            if (!startupLogoSvgStrokes_.empty()) {
                DrawAnimatedLoadingLogo(paintDc, width, height, barY);
            } else {
                DrawLoadingLogo(paintDc, width, height, barY);
            }

            RECT barBgRect = {barX, barY, barX + barWidth, barY + barHeight};
            HBRUSH barBgBrush = CreateSolidBrush(RGB(40, 40, 50));
            FillRect(paintDc, &barBgRect, barBgBrush);
            DeleteObject(barBgBrush);

            const int progressWidth = static_cast<int>(barWidth * loadingProgress_);
            if (progressWidth > 0) {
                RECT barFillRect = {barX, barY, barX + progressWidth, barY + barHeight};
                HBRUSH barFillBrush = CreateSolidBrush(RGB(115, 148, 235));
                FillRect(paintDc, &barFillRect, barFillBrush);
                DeleteObject(barFillBrush);
            }

            if (!loadingLabel_.empty()) {
                SetTextColor(paintDc, RGB(200, 200, 210));
                SetBkMode(paintDc, TRANSPARENT);
                HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HFONT oldFont = static_cast<HFONT>(SelectObject(paintDc, font));
                std::wstring wideLabel = Utf8ToWide(loadingLabel_);
                SIZE labelSize {};
                GetTextExtentPoint32W(paintDc, wideLabel.c_str(), static_cast<int>(wideLabel.length()), &labelSize);
                const int labelX = (width - labelSize.cx) / 2;
                TextOutW(paintDc, labelX, barY - 28, wideLabel.c_str(), static_cast<int>(wideLabel.length()));
                SelectObject(paintDc, oldFont);
                DeleteObject(font);
            }

            if (paintDc == bufferDc) {
                BitBlt(hdc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
            }
            if (oldBitmap != nullptr) {
                SelectObject(bufferDc, oldBitmap);
            }
            if (bufferBitmap != nullptr) {
                DeleteObject(bufferBitmap);
            }
            if (bufferDc != nullptr) {
                DeleteDC(bufferDc);
            }

            EndPaint(window_, &ps);
            return 0;
        }
        if (exportInProgress_) {
            PAINTSTRUCT ps;
            BeginPaint(window_, &ps);
            EndPaint(window_, &ps);
            PresentBlockingOverlay();
            return 0;
        }
        {
            // Once D3D is active, the swapchain owns the client area. Swallow
            // paint requests so Windows doesn't flash a background brush over it.
            PAINTSTRUCT ps;
            BeginPaint(window_, &ps);
            EndPaint(window_, &ps);
            return 0;
        }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            return 0;
        }
        resizeWidth_ = static_cast<UINT>(LOWORD(lParam));
        resizeHeight_ = static_cast<UINT>(HIWORD(lParam));
        MarkViewportDirty(PreviewResetReason::ViewportResized);
        if (!loadingComplete_) {
            InvalidateRect(window_, nullptr, FALSE);
            UpdateWindow(window_);
        } else if (exportInProgress_) {
            PresentBlockingOverlay();
        }
        return 0;
    case WM_GETMINMAXINFO:
        if (MINMAXINFO* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam)) {
            const SIZE minWindowSize = WindowSizeForClientSize(window_, kMinimumClientWidth, kMinimumClientHeight);
            minMaxInfo->ptMinTrackSize.x = minWindowSize.cx;
            minMaxInfo->ptMinTrackSize.y = minWindowSize.cy;
            return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_ENTERSIZEMOVE:
        inSizeMove_ = true;
        if (loadingComplete_) {
            SetTimer(window_, kLiveResizeTimerId, kLiveResizeTimerIntervalMs, nullptr);
        }
        return 0;
    case WM_EXITSIZEMOVE:
        inSizeMove_ = false;
        KillTimer(window_, kLiveResizeTimerId);
        if (exportInProgress_) {
            PresentBlockingOverlay();
        } else if (loadingComplete_) {
            RenderTick();
        } else {
            InvalidateRect(window_, nullptr, FALSE);
            UpdateWindow(window_);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kLiveResizeTimerId && inSizeMove_) {
            if (exportInProgress_) {
                PresentBlockingOverlay();
            } else if (loadingComplete_) {
                RenderTick();
            }
            return 0;
        }
        if (wParam == kStartupAnimationTimerId && !loadingComplete_) {
            startupAnimationDashOffset_ -= 0.16f;
            if (startupAnimationDashOffset_ < -200.0f) {
                startupAnimationDashOffset_ += 200.0f;
            }
            InvalidateRect(window_, nullptr, FALSE);
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
        KillTimer(window_, kStartupAnimationTimerId);
        PostQuitMessage(0);
        running_ = false;
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window_, message, wParam, lParam);
}


bool AppWindow::RenderTick() {
    if (!loadingComplete_) {
        return false;
    }

    if (exportInProgress_) {
        return PresentBlockingOverlay();
    }

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
    if (presentingBlockingOverlay_) {
        return running_;
    }

    presentingBlockingOverlay_ = true;
    struct OverlayPresentGuard {
        bool& flag;
        ~OverlayPresentGuard() {
            flag = false;
        }
    } guard {presentingBlockingOverlay_};

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
            request.iterations,
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
            request.iterations,
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
            request.iterations,
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
            request.iterations,
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

