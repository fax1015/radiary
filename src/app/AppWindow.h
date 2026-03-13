#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Scene.h"
#include "io/PresetLibrary.h"
#include "io/SceneSerializer.h"
#include "renderer/GpuFlameRenderer.h"
#include "renderer/GpuDofRenderer.h"
#include "renderer/GpuPathRenderer.h"
#include "renderer/SoftwareRenderer.h"

struct ImGuiContext;
struct ImFont;

namespace radiary {

class AppWindow {
public:
    bool Create(HINSTANCE instance, int showCommand);
    int Run();

private:
    struct AdapterOption {
        UINT ordinal = 0;
        std::wstring name;
        std::uint64_t dedicatedVideoMemoryMb = 0;
        bool software = false;
        LUID luid {};
    };

    enum class InspectorTarget {
        FlameLayer,
        PathLayer
    };

    enum class RenameTarget {
        None,
        Transform,
        Path
    };

    enum class LayerClipboardType {
        None,
        Transform,
        Path
    };

    enum class PreviewBackend {
        CpuFlame,
        CpuPath,
        CpuHybrid,
        GpuFlame,
        GpuDof,
        GpuPath,
        GpuHybrid
    };

    enum class ExportFormat {
        Png,
        Jpeg,
        PngSequence,
        JpegSequence,
        Avi,
        Mp4,
        Mov
    };

    struct ExportRequest {
        std::filesystem::path path;
        int width = 1920;
        int height = 1080;
        bool transparentBackground = false;
        bool hideGrid = true;
        bool useGpu = false;
        ExportFormat format = ExportFormat::Png;
        int startFrame = 0;
        int endFrame = 0;
    };

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mainRenderTargetView_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> viewportTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> viewportSrv_;
    UINT resizeWidth_ = 0;
    UINT resizeHeight_ = 0;
    bool swapChainOccluded_ = false;
    bool running_ = true;
    bool defaultLayoutBuilt_ = false;
    bool inSizeMove_ = false;

    Scene scene_ = CreateDefaultScene();
    std::vector<Scene> undoStack_;
    std::vector<Scene> redoStack_;
    GpuFlameRenderer gpuFlameRenderer_;
    GpuDofRenderer gpuDofRenderer_;
    GpuPathRenderer gpuGridRenderer_;
    GpuPathRenderer gpuPathRenderer_;
    SoftwareRenderer renderer_;
    SceneSerializer serializer_;
    PresetLibrary presetLibrary_;
    std::filesystem::path currentScenePath_;
    std::vector<std::uint32_t> viewportPixels_;
    std::size_t presetIndex_ = 0;
    InspectorTarget inspectorTarget_ = InspectorTarget::FlameLayer;
    std::wstring statusText_ = L"Ready";
    std::wstring renderAdapterName_ = L"Unknown adapter";
    Scene pendingUndoScene_ = scene_;
    bool hasPendingUndoScene_ = false;
    bool viewportInteractionCaptured_ = false;
    bool settingsPanelOpen_ = false;
    bool exportPanelOpen_ = false;
    bool easingPanelOpen_ = false;
    bool exportRequestPending_ = false;
    bool exportInProgress_ = false;
    bool exportCancelRequested_ = false;
    bool showStatusOverlay_ = true;
    bool presentVsync_ = true;
    bool adaptiveInteractivePreview_ = true;
    bool exportHideGrid_ = true;
    bool exportTransparentBackground_ = false;
    bool exportUseGpu_ = false;
    bool usingWarpDevice_ = false;
    bool gpuFlamePreviewEnabled_ = true;
    float toolbarScrollStep_ = 48.0f;
    float settingsButtonAnchorX_ = 0.0f;
    float settingsButtonAnchorY_ = 0.0f;
    float exportButtonAnchorX_ = 0.0f;
    float exportButtonAnchorY_ = 0.0f;
    float easingButtonAnchorX_ = 0.0f;
    float easingButtonAnchorY_ = 0.0f;
    int exportWidth_ = 1920;
    int exportHeight_ = 1080;
    int exportFrameStart_ = 0;
    int exportFrameEnd_ = 120;
    ExportFormat exportFormat_ = ExportFormat::Png;
    float exportProgress_ = 0.0f;
    std::wstring exportProgressTitle_;
    std::wstring exportProgressDetail_;
    std::wstring exportProgressEta_;
    std::chrono::steady_clock::time_point exportProgressStartedAt_ {};
    ExportRequest pendingExportRequest_;
    std::vector<AdapterOption> adapterOptions_;
    int selectedAdapterIndex_ = -1;
    int activeAdapterIndex_ = -1;
    bool graphicsDeviceChangePending_ = false;
    RenameTarget renameTarget_ = RenameTarget::None;
    int renamingLayerIndex_ = -1;
    bool focusLayerRename_ = false;
    std::string layerRenameBuffer_;
    Scene layerRenameSnapshot_ = scene_;
    LayerClipboardType layerClipboardType_ = LayerClipboardType::None;
    TransformLayer copiedTransformLayer_ {};
    PathSettings copiedPathLayer_ {};
    std::vector<int> selectedTransformLayers_;
    std::vector<int> selectedPathLayers_;
    int transformSelectionAnchor_ = 0;
    int pathSelectionAnchor_ = 0;
    int layerSelectionAnchorGlobal_ = 0;
    bool layersPanelActive_ = false;
    bool inspectorPanelActive_ = false;
    bool playbackPanelActive_ = false;
    bool startupWindowPlacementLoaded_ = false;
    bool startupWindowMaximized_ = false;
    RECT startupWindowRect_ {CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT};

    std::chrono::steady_clock::time_point lastFrame_ {};
    double fpsSmoothed_ = 60.0;
    bool viewportDirty_ = true;
    bool interactivePreview_ = false;
    bool asyncViewportRendering_ = true;
    std::uint32_t interactivePreviewIterations_ = 60000;
    ImFont* uiFont_ = nullptr;
    ImFont* brandFont_ = nullptr;
    int uploadedViewportWidth_ = 0;
    int uploadedViewportHeight_ = 0;
    std::thread renderThread_;
    std::mutex renderMutex_;
    std::condition_variable renderCv_;
    Scene pendingRenderScene_ = scene_;
    int pendingRenderWidth_ = 0;
    int pendingRenderHeight_ = 0;
    std::uint32_t pendingRenderPreviewIterations_ = 0;
    PreviewBackend pendingRenderBackend_ = PreviewBackend::CpuHybrid;
    std::uint64_t pendingRenderGeneration_ = 0;
    bool renderRequestPending_ = false;
    bool renderThreadExit_ = false;
    bool renderInProgress_ = false;
    std::vector<std::uint32_t> completedRenderPixels_;
    int completedRenderWidth_ = 0;
    int completedRenderHeight_ = 0;
    std::uint32_t completedRenderPreviewIterations_ = 0;
    PreviewBackend completedRenderBackend_ = PreviewBackend::CpuHybrid;
    std::uint64_t completedRenderGeneration_ = 0;
    std::uint64_t consumedRenderGeneration_ = 0;
    std::uint32_t displayedPreviewIterations_ = 0;
    PreviewBackend displayedPreviewBackend_ = PreviewBackend::CpuHybrid;
    int selectedTimelineKeyframe_ = -1;
    bool timelineDraggingPlayhead_ = false;
    bool timelineDraggingKeyframe_ = false;
    double newSceneFrameRateDefault_ = 24.0;
    int newSceneEndFrameDefault_ = 120;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateDeviceD3D();
    void EnumerateAdapters();
    bool ApplyPendingGraphicsDeviceChange();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void CleanupViewportTexture();
    bool EnsureViewportTexture(int width, int height);

    void LoadPresets();
    void LoadUserSettings();
    void SaveUserSettings() const;
    void ApplyUserSceneDefaults(Scene& scene) const;
    void SetupImGui();
    void ShutdownImGui();
    void ApplyStyle() const;
    void ApplyPendingResize();
    bool RenderTick();
    void RenderFrame();
    void PumpPendingMessages();
    bool PresentBlockingOverlay();
    void DrawBlockingOverlay() const;
    void DrawDockspace();
    void BuildDefaultLayout();
    void DrawToolbar();
    void DrawLayersPanel();
    void DrawInspectorPanel();
    void DrawTimelinePanel();
    void DrawCameraPanel();
    void DrawViewportPanel();
    void DrawStatusBar();
    void DrawSettingsPanel();
    void DrawExportPanel();
    void DrawEasingPanel();

    void EnsureSelectionIsValid();
    void ResetScene(Scene scene);
    void LoadPreset(std::size_t index);
    void MarkViewportDirty();
    Scene BuildRenderableScene(const Scene& scene) const;
    void QueueViewportRender(int width, int height, bool interactive);
    void ConsumeCompletedRender();
    void StartRenderThread();
    void StopRenderThread();
    void RenderThreadMain();
    void RenderViewportIfNeeded(int width, int height);
    void HandleViewportInteraction(bool hovered);
    void HandleShortcuts();
    void CaptureWidgetUndo(const Scene& beforeChange, bool changed);
    void PushUndoState(const Scene& snapshot);
    bool CanUndo() const;
    bool CanRedo() const;
    void Undo();
    void Redo();
    void ClearPendingUndoCapture();
    void CopySelectedLayer();
    void PasteCopiedLayer();
    void DuplicateSelectedLayer();
    void NormalizeLayerSelections();
    std::vector<int>& MutableLayerSelection(InspectorTarget target);
    const std::vector<int>& LayerSelection(InspectorTarget target) const;
    int& PrimaryLayerIndex(InspectorTarget target);
    int PrimaryLayerIndex(InspectorTarget target) const;
    int& LayerSelectionAnchor(InspectorTarget target);
    int LayerCount(InspectorTarget target) const;
    int GlobalLayerCount() const;
    int GlobalLayerIndex(InspectorTarget target, int index) const;
    bool ResolveGlobalLayerIndex(int globalIndex, InspectorTarget& target, int& index) const;
    void ClearLayerSelections();
    void SetLayerSelection(InspectorTarget target, std::vector<int> indices, int primaryIndex, int anchorIndex);
    void AssignCurrentLayerToKeyframe(SceneKeyframe& keyframe) const;
    void AdjustKeyframeOwnerIndicesForInsertedLayer(InspectorTarget target, int insertedIndex);
    void AdjustKeyframeOwnerIndicesForRemovedLayers(InspectorTarget target, const std::vector<int>& removedIndices);
    void SelectSingleLayer(InspectorTarget target, int index);
    void ToggleLayerSelection(InspectorTarget target, int index);
    void SelectLayerRange(InspectorTarget target, int index);
    void SelectAllLayers(InspectorTarget target);
    bool IsLayerSelected(InspectorTarget target, int index) const;
    int SelectedLayerCount() const;
    bool HasMultipleLayersSelected() const;
    bool CanRemoveSelectedLayers() const;
    bool RemoveSelectedLayers();
    bool CanRemoveSelectedOrCurrentKeyframe() const;
    bool RemoveSelectedOrCurrentKeyframe();
    void BeginLayerRename(RenameTarget target, int index);
    void FinishLayerRename(bool commit);
    void SetTimelineFrame(double frame, bool captureUndo);
    void RefreshTimelinePose();
    void AutoKeyCurrentFrame();
    void SyncCurrentKeyframeFromScene();

    bool SaveSceneToDialog(bool saveAs);
    bool LoadSceneFromDialog();
    void OpenExportPanel();
    void ProcessPendingExport();
    bool ExecuteExportRequest(const ExportRequest& request);
    bool UpdateExportProgress(float progress, const std::wstring& detail);
    bool ExportViewportToDialog();
    bool ExportViewportImage(const std::filesystem::path& path, int width, int height, bool transparentBackground, bool hideGrid, bool useGpu, ExportFormat format);
    bool ExportImageSequence(const std::filesystem::path& path, int width, int height, bool transparentBackground, bool hideGrid, bool useGpu, ExportFormat format, int startFrame, int endFrame);
    bool ExportAviVideo(const std::filesystem::path& path, int width, int height, bool hideGrid, bool useGpu, int startFrame, int endFrame);
    bool ExportFfmpegVideo(const std::filesystem::path& path, int width, int height, bool hideGrid, bool useGpu, int startFrame, int endFrame, ExportFormat format);
    bool RenderSceneToPixels(const Scene& sourceScene, int width, int height, bool transparentBackground, bool hideGrid, bool useGpu, std::vector<std::uint32_t>& pixels);
    bool RenderSceneToPixelsCpu(const Scene& sourceScene, int width, int height, bool transparentBackground, bool hideGrid, std::vector<std::uint32_t>& pixels) const;
    bool RenderSceneToPixelsGpu(const Scene& sourceScene, int width, int height, bool transparentBackground, bool hideGrid, std::vector<std::uint32_t>& pixels);
    bool ReadbackGpuTexture(ID3D11Texture2D* texture, std::vector<std::uint32_t>& pixels) const;
    bool ReadbackGpuDepthTexture(ID3D11Texture2D* texture, std::vector<float>& depthBuffer) const;

    static std::wstring Utf8ToWide(const std::string& value);
    static std::string WideToUtf8(const std::wstring& value);
    static std::size_t GradientStopIndexForPaletteColor(int paletteIndex);
    static Color ToColor(const float color[3]);
};

}  // namespace radiary
