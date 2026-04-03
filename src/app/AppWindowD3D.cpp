#include "app/AppWindow.h"
#include "app/AppWidgets.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <string>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

using namespace radiary;

namespace {

constexpr int kAppSettingsVersion = 1;
constexpr float kUiDesignDpiScale = 1.5f;

std::filesystem::path UserSettingsPath() {
    wchar_t localAppData[MAX_PATH] = L"";
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(localAppData) / "Radiary" / "user-settings.ini";
    }
    return std::filesystem::current_path() / "user-settings.ini";
}

std::string TrimAscii(std::string value) {
    const auto isSpace = [](const unsigned char character) {
        return std::isspace(character) != 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](const char character) {
        return !isSpace(static_cast<unsigned char>(character));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](const char character) {
        return !isSpace(static_cast<unsigned char>(character));
    }).base(), value.end());
    return value;
}

std::map<std::string, std::string> LoadKeyValueFile(const std::filesystem::path& path) {
    std::map<std::string, std::string> values;
    std::ifstream stream(path);
    if (!stream) {
        return values;
    }

    std::string line;
    while (std::getline(stream, line)) {
        const std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        values.emplace(
            TrimAscii(trimmed.substr(0, separator)),
            TrimAscii(trimmed.substr(separator + 1)));
    }
    return values;
}

bool TryParseIntValue(const std::map<std::string, std::string>& values, const char* key, int& output) {
    const auto iterator = values.find(key);
    if (iterator == values.end()) {
        return false;
    }
    try {
        output = std::stoi(iterator->second);
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseUintValue(const std::map<std::string, std::string>& values, const char* key, std::uint32_t& output) {
    const auto iterator = values.find(key);
    if (iterator == values.end()) {
        return false;
    }
    try {
        output = static_cast<std::uint32_t>(std::stoul(iterator->second));
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseFloatValue(const std::map<std::string, std::string>& values, const char* key, float& output) {
    const auto iterator = values.find(key);
    if (iterator == values.end()) {
        return false;
    }
    try {
        output = std::stof(iterator->second);
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseDoubleValue(const std::map<std::string, std::string>& values, const char* key, double& output) {
    const auto iterator = values.find(key);
    if (iterator == values.end()) {
        return false;
    }
    try {
        output = std::stod(iterator->second);
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseBoolValue(const std::map<std::string, std::string>& values, const char* key, bool& output) {
    const auto iterator = values.find(key);
    if (iterator == values.end()) {
        return false;
    }
    if (iterator->second == "1" || iterator->second == "true") {
        output = true;
        return true;
    }
    if (iterator->second == "0" || iterator->second == "false") {
        output = false;
        return true;
    }
    return false;
}

}  // namespace

namespace radiary {

std::filesystem::path AppWindow::UserLayoutPath() {
    const std::filesystem::path settingsPath = UserSettingsPath();
    return settingsPath.parent_path() / "imgui-layout.ini";
}

bool AppWindow::CreateDeviceD3D() {
    IDXGIFactory1* factory = nullptr;
    IDXGIAdapter* selectedAdapter = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        if (selectedAdapterIndex_ >= 0 && selectedAdapterIndex_ < static_cast<int>(adapterOptions_.size())) {
            factory->EnumAdapters(adapterOptions_[static_cast<std::size_t>(selectedAdapterIndex_)].ordinal, &selectedAdapter);
        }
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = window_;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel {};
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    usingWarpDevice_ = false;

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        selectedAdapter,
        selectedAdapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        levels,
        2,
        D3D11_SDK_VERSION,
        &swapChainDesc,
        swapChain_.GetAddressOf(),
        device_.GetAddressOf(),
        &featureLevel,
        deviceContext_.GetAddressOf());
    if (result == DXGI_ERROR_UNSUPPORTED) {
        usingWarpDevice_ = true;
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            levels,
            2,
            D3D11_SDK_VERSION,
            &swapChainDesc,
            swapChain_.GetAddressOf(),
            device_.GetAddressOf(),
            &featureLevel,
            deviceContext_.GetAddressOf());
    }
    if (FAILED(result)) {
        if (selectedAdapter) { selectedAdapter->Release(); }
        if (factory) { factory->Release(); }
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))
        && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC adapterDesc {};
        if (SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
            renderAdapterName_ = adapterDesc.Description;
            activeAdapterIndex_ = -1;
            for (std::size_t index = 0; index < adapterOptions_.size(); ++index) {
                const LUID& luid = adapterOptions_[index].luid;
                if (luid.HighPart == adapterDesc.AdapterLuid.HighPart && luid.LowPart == adapterDesc.AdapterLuid.LowPart) {
                    activeAdapterIndex_ = static_cast<int>(index);
                    selectedAdapterIndex_ = activeAdapterIndex_;
                    break;
                }
            }
        }
    }
    if (adapter) { adapter->Release(); }
    if (dxgiDevice) { dxgiDevice->Release(); }

    IDXGIFactory* swapChainFactory = nullptr;
    if (SUCCEEDED(swapChain_->GetParent(IID_PPV_ARGS(&swapChainFactory)))) {
        swapChainFactory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
        swapChainFactory->Release();
    }
    if (selectedAdapter) { selectedAdapter->Release(); }
    if (factory) { factory->Release(); }

    CreateRenderTarget();
    if (!renderBackend_) {
        renderBackend_.reset(CreateD3D11RenderBackend());
    }
    if (renderBackend_) {
        renderBackend_->Initialize(device_.Get(), deviceContext_.Get());
    }
    return true;
}

bool AppWindow::EnsureGpuFlameRendererInitialized() {
    if (gpuFlameRenderer_.IsReady()) {
        return true;
    }
    if (device_ == nullptr || deviceContext_ == nullptr) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(L"GPU flame", false, {});
        return false;
    }
    if (!gpuFlameRenderer_.Initialize(device_.Get(), deviceContext_.Get())) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(L"GPU flame", true, gpuFlameRenderer_.LastError());
        return false;
    }
    return true;
}

bool AppWindow::EnsureGpuPathRendererInitialized(GpuPathRenderer& renderer, const wchar_t* label) {
    if (renderer.IsReady()) {
        return true;
    }
    if (device_ == nullptr || deviceContext_ == nullptr) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(label, false, {});
        return false;
    }
    if (!renderer.Initialize(device_.Get(), deviceContext_.Get())) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(label, true, renderer.LastError());
        return false;
    }
    return true;
}

bool AppWindow::EnsureGpuDofRendererInitialized() {
    if (gpuDofRenderer_.IsReady()) {
        return true;
    }
    if (device_ == nullptr || deviceContext_ == nullptr) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(L"GPU DOF", false, {});
        return false;
    }
    if (!gpuDofRenderer_.Initialize(device_.Get(), deviceContext_.Get())) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(L"GPU DOF", true, gpuDofRenderer_.LastError());
        return false;
    }
    return true;
}

bool AppWindow::EnsureGpuDenoiserInitialized() {
    if (gpuDenoiser_.IsReady()) {
        return true;
    }
    if (device_ == nullptr || deviceContext_ == nullptr) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(L"GPU Denoiser", false, {});
        return false;
    }
    if (!gpuDenoiser_.Initialize(device_.Get(), deviceContext_.Get())) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(L"GPU Denoiser", true, gpuDenoiser_.LastError());
        return false;
    }
    return true;
}

bool AppWindow::EnsureGpuPostProcessInitialized() {
    if (gpuPostProcess_.IsReady()) {
        return true;
    }
    if (device_ == nullptr || deviceContext_ == nullptr) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(L"GPU Post-Process", false, {});
        return false;
    }
    if (!gpuPostProcess_.Initialize(device_.Get(), deviceContext_.Get())) {
        statusText_ = BuildGpuRendererUnavailableStatusMessage(L"GPU Post-Process", true, gpuPostProcess_.LastError());
        return false;
    }
    return true;
}

void AppWindow::EnumerateAdapters() {
    adapterOptions_.clear();

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return;
    }

    for (UINT ordinal = 0;; ++ordinal) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(ordinal, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (adapter == nullptr) {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc {};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            AdapterOption option;
            option.ordinal = ordinal;
            option.name = desc.Description;
            option.dedicatedVideoMemoryMb = static_cast<std::uint64_t>(desc.DedicatedVideoMemory / (1024ull * 1024ull));
            option.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            option.luid = desc.AdapterLuid;
            adapterOptions_.push_back(std::move(option));
        }
        adapter->Release();
    }

    factory->Release();
}

bool AppWindow::ApplyPendingGraphicsDeviceChange() {
    if (!graphicsDeviceChangePending_) {
        return true;
    }

    graphicsDeviceChangePending_ = false;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    CleanupDeviceD3D();
    if (!CreateDeviceD3D()) {
        statusText_ = L"GPU switch failed";
        return false;
    }
    ImGui_ImplWin32_Init(window_);
    ImGui_ImplDX11_Init(device_.Get(), deviceContext_.Get());
    CreateAppLogoTexture();
    MarkViewportDirty(PreviewResetReason::DeviceChanged);
    resizeWidth_ = 0;
    resizeHeight_ = 0;
    statusText_ = L"Switched GPU to " + renderAdapterName_;
    return true;
}

void AppWindow::CleanupDeviceD3D() {
    if (renderBackend_) {
        renderBackend_->Shutdown();
    }
    CleanupAppLogoTexture();
    gpuPostProcess_.Shutdown();
    gpuDenoiser_.Shutdown();
    gpuPathRenderer_.Shutdown();
    gpuGridRenderer_.Shutdown();
    gpuDofRenderer_.Shutdown();
    gpuFlameRenderer_.Shutdown();
    CleanupRenderTarget();
    swapChain_.Reset();
    deviceContext_.Reset();
    device_.Reset();
}

void AppWindow::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
    device_->CreateRenderTargetView(backbuffer.Get(), nullptr, mainRenderTargetView_.GetAddressOf());
}

void AppWindow::CleanupRenderTarget() {
    mainRenderTargetView_.Reset();
}

int AppWindow::UploadedViewportWidth() const {
    return renderBackend_ ? renderBackend_->PresentedPreviewWidth() : 0;
}

int AppWindow::UploadedViewportHeight() const {
    return renderBackend_ ? renderBackend_->PresentedPreviewHeight() : 0;
}

void AppWindow::LoadPresets() {
    wchar_t modulePath[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    const std::vector<std::filesystem::path> presetCandidates = {
        std::filesystem::current_path() / "assets" / "presets",
        exeDir / "assets" / "presets",
        exeDir.parent_path() / "assets" / "presets",
        exeDir.parent_path().parent_path() / "assets" / "presets"
    };

    for (const auto& candidate : presetCandidates) {
        if (std::filesystem::exists(candidate)) {
            presetLibrary_.LoadFromDirectory(candidate);
            break;
        }
    }
    if (presetLibrary_.Count() == 0) {
        presetLibrary_.LoadFromDirectory(std::filesystem::current_path() / "assets" / "presets");
    }
}

void AppWindow::LoadUserSettings() {
    defaultLayoutBuilt_ = std::filesystem::exists(UserLayoutPath());
    const std::map<std::string, std::string> values = LoadKeyValueFile(UserSettingsPath());
    if (values.empty()) {
        return;
    }

    int settingsVersion = 0;
    if (!TryParseIntValue(values, "version", settingsVersion) || settingsVersion != kAppSettingsVersion) {
        return;
    }

    TryParseBoolValue(values, "settings_panel_open", settingsPanelOpen_);
    TryParseBoolValue(values, "layers_panel_open", layersPanelOpen_);
    TryParseBoolValue(values, "keyframe_list_panel_open", keyframeListPanelOpen_);
    TryParseBoolValue(values, "inspector_panel_open", inspectorPanelOpen_);
    TryParseBoolValue(values, "playback_panel_open", playbackPanelOpen_);
    TryParseBoolValue(values, "preview_panel_open", previewPanelOpen_);
    TryParseBoolValue(values, "camera_panel_open", cameraPanelOpen_);
    TryParseBoolValue(values, "viewport_panel_open", viewportPanelOpen_);
    TryParseBoolValue(values, "export_panel_open", exportPanelOpen_);
    TryParseBoolValue(values, "easing_panel_open", easingPanelOpen_);
    TryParseBoolValue(values, "show_status_overlay", showStatusOverlay_);
    TryParseBoolValue(values, "gpu_viewport_preview", gpuFlamePreviewEnabled_);
    TryParseBoolValue(values, "export_hide_grid", exportHideGrid_);
    TryParseBoolValue(values, "export_transparent_background", exportTransparentBackground_);
    TryParseBoolValue(values, "export_use_gpu", exportUseGpu_);
    TryParseBoolValue(values, "export_stable_flame_sampling", exportStableFlameSampling_);
    TryParseBoolValue(values, "autosave_enabled", autoSaveEnabled_);
    TryParseBoolValue(values, "grid_visible", scene_.gridVisible);

    TryParseUintValue(values, "interactive_preview_iterations", interactivePreviewIterations_);
    TryParseUintValue(values, "scene_preview_iterations", scene_.previewIterations);
    TryParseIntValue(values, "autosave_interval_seconds", autoSaveIntervalSeconds_);
    TryParseIntValue(values, "undo_history_limit", undoHistoryLimit_);
    TryParseIntValue(values, "export_width", exportWidth_);
    TryParseIntValue(values, "export_height", exportHeight_);
    TryParseIntValue(values, "export_frame_start", exportFrameStart_);
    TryParseIntValue(values, "export_frame_end", exportFrameEnd_);
    TryParseDoubleValue(values, "new_scene_frame_rate", newSceneFrameRateDefault_);
    TryParseIntValue(values, "new_scene_end_frame", newSceneEndFrameDefault_);
    autoSaveIntervalSeconds_ = std::clamp(autoSaveIntervalSeconds_, kMinAutoSaveIntervalSeconds, kMaxAutoSaveIntervalSeconds);
    undoHistoryLimit_ = std::clamp(undoHistoryLimit_, kMinUndoHistoryLimit, kMaxUndoHistoryLimit);
    EnforceUndoStackLimits();

    int exportFormat = static_cast<int>(exportFormat_);
    if (TryParseIntValue(values, "export_format", exportFormat)
        && exportFormat >= static_cast<int>(ExportFormat::Png)
        && exportFormat <= static_cast<int>(ExportFormat::Mov)) {
        exportFormat_ = static_cast<ExportFormat>(exportFormat);
    }

    int modeIndex = static_cast<int>(scene_.mode);
    if (TryParseIntValue(values, "scene_mode", modeIndex)
        && modeIndex >= static_cast<int>(SceneMode::Flame)
        && modeIndex <= static_cast<int>(SceneMode::Hybrid)) {
        scene_.mode = static_cast<SceneMode>(modeIndex);
    }

    int windowLeft = 0;
    int windowTop = 0;
    int windowRight = 0;
    int windowBottom = 0;
    if (TryParseIntValue(values, "window_left", windowLeft)
        && TryParseIntValue(values, "window_top", windowTop)
        && TryParseIntValue(values, "window_right", windowRight)
        && TryParseIntValue(values, "window_bottom", windowBottom)
        && windowRight > windowLeft
        && windowBottom > windowTop) {
        startupWindowRect_ = RECT {windowLeft, windowTop, windowRight, windowBottom};
        startupWindowPlacementLoaded_ = true;
    }
    TryParseBoolValue(values, "window_maximized", startupWindowMaximized_);

    int adapterLuidHigh = 0;
    std::uint32_t adapterLuidLow = 0;
    if (TryParseIntValue(values, "selected_adapter_luid_high", adapterLuidHigh)
        && TryParseUintValue(values, "selected_adapter_luid_low", adapterLuidLow)) {
        for (std::size_t index = 0; index < adapterOptions_.size(); ++index) {
            const LUID& luid = adapterOptions_[index].luid;
            if (luid.HighPart == adapterLuidHigh && luid.LowPart == adapterLuidLow) {
                selectedAdapterIndex_ = static_cast<int>(index);
                break;
            }
        }
    }
}

void AppWindow::SaveUserSettings() const {
    const std::filesystem::path settingsPath = UserSettingsPath();
    const std::filesystem::path layoutPath = UserLayoutPath();
    std::error_code createError;
    std::filesystem::create_directories(settingsPath.parent_path(), createError);
    std::filesystem::create_directories(layoutPath.parent_path(), createError);

    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::SaveIniSettingsToDisk(layoutPath.string().c_str());
    }

    std::ofstream stream(settingsPath, std::ios::trunc);
    if (!stream) {
        return;
    }

    RECT windowRect {};
    if (window_ != nullptr) {
        GetWindowRect(window_, &windowRect);
        WINDOWPLACEMENT placement {};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(window_, &placement)) {
            windowRect = placement.rcNormalPosition;
        }
    }

    stream << "version=" << kAppSettingsVersion << "\n";
    stream << "settings_panel_open=" << (settingsPanelOpen_ ? 1 : 0) << "\n";
    stream << "layers_panel_open=" << (layersPanelOpen_ ? 1 : 0) << "\n";
    stream << "keyframe_list_panel_open=" << (keyframeListPanelOpen_ ? 1 : 0) << "\n";
    stream << "inspector_panel_open=" << (inspectorPanelOpen_ ? 1 : 0) << "\n";
    stream << "playback_panel_open=" << (playbackPanelOpen_ ? 1 : 0) << "\n";
    stream << "preview_panel_open=" << (previewPanelOpen_ ? 1 : 0) << "\n";
    stream << "camera_panel_open=" << (cameraPanelOpen_ ? 1 : 0) << "\n";
    stream << "viewport_panel_open=" << (viewportPanelOpen_ ? 1 : 0) << "\n";
    stream << "export_panel_open=" << (exportPanelOpen_ ? 1 : 0) << "\n";
    stream << "easing_panel_open=" << (easingPanelOpen_ ? 1 : 0) << "\n";
    stream << "show_status_overlay=" << (showStatusOverlay_ ? 1 : 0) << "\n";
    stream << "gpu_viewport_preview=" << (gpuFlamePreviewEnabled_ ? 1 : 0) << "\n";
    stream << "interactive_preview_iterations=" << interactivePreviewIterations_ << "\n";
    stream << "autosave_enabled=" << (autoSaveEnabled_ ? 1 : 0) << "\n";
    stream << "autosave_interval_seconds=" << autoSaveIntervalSeconds_ << "\n";
    stream << "undo_history_limit=" << undoHistoryLimit_ << "\n";
    stream << "scene_preview_iterations=" << scene_.previewIterations << "\n";
    stream << "scene_mode=" << static_cast<int>(scene_.mode) << "\n";
    stream << "grid_visible=" << (scene_.gridVisible ? 1 : 0) << "\n";
    stream << "new_scene_frame_rate=" << newSceneFrameRateDefault_ << "\n";
    stream << "new_scene_end_frame=" << newSceneEndFrameDefault_ << "\n";
    stream << "export_hide_grid=" << (exportHideGrid_ ? 1 : 0) << "\n";
    stream << "export_transparent_background=" << (exportTransparentBackground_ ? 1 : 0) << "\n";
    stream << "export_use_gpu=" << (exportUseGpu_ ? 1 : 0) << "\n";
    stream << "export_stable_flame_sampling=" << (exportStableFlameSampling_ ? 1 : 0) << "\n";
    stream << "export_format=" << static_cast<int>(exportFormat_) << "\n";
    stream << "export_width=" << exportWidth_ << "\n";
    stream << "export_height=" << exportHeight_ << "\n";
    stream << "export_frame_start=" << exportFrameStart_ << "\n";
    stream << "export_frame_end=" << exportFrameEnd_ << "\n";
    stream << "window_left=" << windowRect.left << "\n";
    stream << "window_top=" << windowRect.top << "\n";
    stream << "window_right=" << windowRect.right << "\n";
    stream << "window_bottom=" << windowRect.bottom << "\n";
    stream << "window_maximized=" << ((window_ != nullptr && IsZoomed(window_)) ? 1 : 0) << "\n";

    if (selectedAdapterIndex_ >= 0 && selectedAdapterIndex_ < static_cast<int>(adapterOptions_.size())) {
        const LUID& luid = adapterOptions_[static_cast<std::size_t>(selectedAdapterIndex_)].luid;
        stream << "selected_adapter_luid_high=" << luid.HighPart << "\n";
        stream << "selected_adapter_luid_low=" << static_cast<std::uint32_t>(luid.LowPart) << "\n";
    }
}

void AppWindow::ApplyUserSceneDefaults(Scene& scene) const {
    scene.timelineFrameRate = std::max(1.0, newSceneFrameRateDefault_);
    scene.timelineStartFrame = 0;
    scene.timelineEndFrame = std::max(scene.timelineStartFrame, newSceneEndFrameDefault_);
    scene.timelineFrame = Clamp(scene.timelineFrame, static_cast<double>(scene.timelineStartFrame), static_cast<double>(scene.timelineEndFrame));
    scene.timelineSeconds = TimelineSecondsForFrame(scene, scene.timelineFrame);
}

void AppWindow::CreateAppLogoTexture() {
    CleanupAppLogoTexture();
    if (device_ == nullptr || !EnsureAppLogoLoaded() || appLogoPixels_.empty() || appLogoWidth_ <= 0 || appLogoHeight_ <= 0) {
        return;
    }

    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = static_cast<UINT>(appLogoWidth_);
    desc.Height = static_cast<UINT>(appLogoHeight_);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subresource {};
    subresource.pSysMem = appLogoPixels_.data();
    subresource.SysMemPitch = static_cast<UINT>(appLogoWidth_ * 4);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device_->CreateTexture2D(&desc, &subresource, texture.GetAddressOf()))) {
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(texture.Get(), &srvDesc, appLogoSrv_.GetAddressOf());
}

void AppWindow::CleanupAppLogoTexture() {
    appLogoSrv_.Reset();
}

void AppWindow::SetupImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    const std::filesystem::path layoutPath = UserLayoutPath();
    if (std::filesystem::exists(layoutPath)) {
        ImGui::LoadIniSettingsFromDisk(layoutPath.string().c_str());
    }

    wchar_t modulePath[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    const std::vector<std::filesystem::path> fontCandidates = {
        std::filesystem::current_path() / "assets" / "fonts" / "Rubik-Regular.ttf",
        exeDir / "assets" / "fonts" / "Rubik-Regular.ttf",
        exeDir.parent_path() / "assets" / "fonts" / "Rubik-Regular.ttf",
        exeDir.parent_path().parent_path() / "assets" / "fonts" / "Rubik-Regular.ttf",
        std::filesystem::current_path() / "assets" / "fonts" / "Rubik-Medium.ttf",
        exeDir / "assets" / "fonts" / "Rubik-Medium.ttf",
        exeDir.parent_path() / "assets" / "fonts" / "Rubik-Medium.ttf",
        exeDir.parent_path().parent_path() / "assets" / "fonts" / "Rubik-Medium.ttf",
        std::filesystem::current_path() / "third_party" / "imgui" / "misc" / "fonts" / "Karla-Regular.ttf"
    };
    const std::vector<std::filesystem::path> brandFontCandidates = {
        std::filesystem::current_path() / "assets" / "fonts" / "Rubik-Bold.ttf",
        exeDir / "assets" / "fonts" / "Rubik-Bold.ttf",
        exeDir.parent_path() / "assets" / "fonts" / "Rubik-Bold.ttf",
        exeDir.parent_path().parent_path() / "assets" / "fonts" / "Rubik-Bold.ttf",
        std::filesystem::current_path() / "assets" / "fonts" / "Rubik-Regular.ttf",
        exeDir / "assets" / "fonts" / "Rubik-Regular.ttf",
        exeDir.parent_path() / "assets" / "fonts" / "Rubik-Regular.ttf",
        exeDir.parent_path().parent_path() / "assets" / "fonts" / "Rubik-Regular.ttf"
    };
    const std::vector<std::filesystem::path> iconFontCandidates = {
        std::filesystem::current_path() / "assets" / "fonts" / "MaterialSymbolsRounded.ttf",
        exeDir / "assets" / "fonts" / "MaterialSymbolsRounded.ttf",
        exeDir.parent_path() / "assets" / "fonts" / "MaterialSymbolsRounded.ttf",
        exeDir.parent_path().parent_path() / "assets" / "fonts" / "MaterialSymbolsRounded.ttf"
    };
    const std::vector<std::filesystem::path> monospaceFontCandidates = {
        std::filesystem::current_path() / "assets" / "fonts" / "JetBrainsMono-Regular.ttf",
        exeDir / "assets" / "fonts" / "JetBrainsMono-Regular.ttf",
        exeDir.parent_path() / "assets" / "fonts" / "JetBrainsMono-Regular.ttf",
        exeDir.parent_path().parent_path() / "assets" / "fonts" / "JetBrainsMono-Regular.ttf",
        std::filesystem::current_path() / "assets" / "fonts" / "JetBrainsMono-Medium.ttf",
        exeDir / "assets" / "fonts" / "JetBrainsMono-Medium.ttf",
        exeDir.parent_path() / "assets" / "fonts" / "JetBrainsMono-Medium.ttf",
        exeDir.parent_path().parent_path() / "assets" / "fonts" / "JetBrainsMono-Medium.ttf"
    };
    auto loadFont = [&](const std::vector<std::filesystem::path>& candidates, const float size, const ImFontConfig* config, const ImWchar* ranges = nullptr) -> ImFont* {
        for (const auto& candidate : candidates) {
            if (!std::filesystem::exists(candidate)) {
                continue;
            }
            if (ImFont* font = io.Fonts->AddFontFromFileTTF(candidate.string().c_str(), size, config, ranges)) {
                return font;
            }
        }
        return nullptr;
    };

    ImFontConfig uiConfig {};
    uiConfig.GlyphOffset.y = 0.3f;
    uiFont_ = loadFont(fontCandidates, 16.0f, &uiConfig);
    if (uiFont_ != nullptr) {
        io.FontDefault = uiFont_;
    }

    ImFontConfig brandConfig {};
    brandConfig.RasterizerMultiply = 1.05f;
    brandFont_ = loadFont(brandFontCandidates, 18.0f, &brandConfig);
    if (brandFont_ == nullptr) {
        brandFont_ = uiFont_;
    }
    ImFontConfig monospaceConfig {};
    monospaceConfig.GlyphExtraAdvanceX = -1.0f;
    monospaceConfig.GlyphOffset.y = -0.4f;
    ImFont* monospaceFont = loadFont(monospaceFontCandidates, 17.0f, &monospaceConfig);
    if (uiFont_ != nullptr && io.FontDefault == nullptr) {
        io.FontDefault = uiFont_;
    }
    if (io.FontDefault == nullptr) {
        io.FontDefault = io.Fonts->AddFontDefault();
    }
    SetMonospaceFont(monospaceFont != nullptr ? monospaceFont : io.FontDefault);

    static const ImWchar iconRanges[] = {
        0xE034, 0xE045,
        0xE145, 0xE166,
        0xE2C8, 0xE2C8,
        0xE412, 0xE412,
        0xE5C5, 0xE5C7,
        0xE89C, 0xE89C,
        0xE897, 0xE898,
        0xE8B8, 0xE8B8,
        0xE8D4, 0xE8D4,
        0xEB60, 0xEB60,
        0xF015, 0xF015,
        0xF09B, 0xF09B,
        0
    };
    ImFontConfig iconConfig {};
    iconConfig.PixelSnapH = true;
    iconConfig.RasterizerMultiply = 1.1f;
    SetActionIconFont(loadFont(iconFontCandidates, 24.0f, &iconConfig, iconRanges));

    ImGui_ImplWin32_Init(window_);
    ImGui_ImplDX11_Init(device_.Get(), deviceContext_.Get());
    CreateAppLogoTexture();
}

void AppWindow::ShutdownImGui() {
    SetActionIconFont(nullptr);
    SetMonospaceFont(nullptr);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void AppWindow::RefreshUiScale() {
    if (window_ == nullptr) {
        uiScale_ = 1.0f;
        return;
    }

    const float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(window_);
    uiScale_ = std::max(0.5f, dpiScale / kUiDesignDpiScale);
}

float AppWindow::UiScale() const {
    return uiScale_;
}

float AppWindow::ScaleUi(const float value) const {
    return value * uiScale_;
}

void AppWindow::ApplyStyle() {
    RefreshUiScale();

    ImGuiStyle style {};
    ApplyRadiaryStyle(style);
    
    // Scale all UI values relative to 150% design baseline
    style.ScaleAllSizes(uiScale_);
    
    // Fix border radius scaling - ensure they don't disappear at lower DPI
    // ScaleAllSizes uses integer truncation which makes small radii vanish
    style.WindowRounding = std::max(2.0f, style.WindowRounding);
    style.ChildRounding = std::max(2.0f, style.ChildRounding);
    style.FrameRounding = std::max(2.0f, style.FrameRounding);
    style.PopupRounding = std::max(2.0f, style.PopupRounding);
    style.ScrollbarRounding = std::max(2.0f, style.ScrollbarRounding);
    style.GrabRounding = std::max(2.0f, style.GrabRounding);
    style.TabRounding = std::max(2.0f, style.TabRounding);
    
    // Fonts are already sized correctly for current DPI - don't scale them
    ImGui::GetIO().FontGlobalScale = 1.0f;
    
    ImGui::GetStyle() = style;
}

void AppWindow::ApplyPendingResize() {
    if (resizeWidth_ == 0 || resizeHeight_ == 0) {
        return;
    }

    CleanupRenderTarget();
    swapChain_->ResizeBuffers(0, resizeWidth_, resizeHeight_, DXGI_FORMAT_UNKNOWN, 0);
    resizeWidth_ = 0;
    resizeHeight_ = 0;
    CreateRenderTarget();
}

}  // namespace radiary
