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
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/Scene.h"
#include "app/StartupLogoSvg.h"
#include "io/PresetLibrary.h"
#include "io/SceneSerializer.h"
#include "renderer/GpuFlameRenderer.h"
#include "renderer/GpuDofRenderer.h"
#include "renderer/GpuDenoiser.h"
#include "renderer/GpuPostProcess.h"
#include "renderer/GpuPathRenderer.h"
#include "renderer/SoftwareRenderer.h"

struct ImGuiContext;
struct ImFont;
struct ImDrawList;
struct ImVec2;

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
        GpuPath,
        GpuHybrid,
        GpuDof,
        GpuDenoised,
        GpuPostProcessed
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
        std::uint32_t iterations = 480000;
        bool transparentBackground = false;
        bool hideGrid = true;
        bool useGpu = false;
        ExportFormat format = ExportFormat::Png;
        int startFrame = 0;
        int endFrame = 0;
    };

    struct ViewportRenderRequest {
        Scene scene;
        int width = 0;
        int height = 0;
        std::uint32_t previewIterations = 0;
        bool interactive = false;
        bool useGpuViewportPreview = false;
        bool useGpuDofPreview = false;
        bool useGpuDenoiserPreview = false;
        bool useGpuPostProcessPreview = false;
        PreviewBackend cpuPreviewBackend = PreviewBackend::CpuHybrid;
    };

    struct ViewportRenderSetup {
        std::chrono::steady_clock::time_point now {};
        bool sizeChanged = false;
        bool gpuAccumulationIncomplete = false;
        bool uiBusy = false;
        bool resizeInteractionActive = false;
        bool allowGpuResolvePasses = false;
        bool throttleGpuAccumulation = false;
        bool throttleGpuResize = false;
        bool needsGpuDofResolve = false;
        bool needsGpuDenoiserResolve = false;
        bool needsGpuPostProcessResolve = false;
    };

    struct GpuPreviewPipeline {
        GpuFrameInputs baseInputs {};
        ID3D11ShaderResourceView* directSourceColor = nullptr;
        PreviewBackend directBackend = PreviewBackend::GpuHybrid;
        PreviewBackend composedBackend = PreviewBackend::GpuHybrid;
        PreviewBackend compositeFailureBackend = PreviewBackend::GpuHybrid;
        std::uint32_t displayedIterations = 0;
        bool applyDenoiser = false;
        bool applyDof = false;
        bool useCompositeFinalize = false;
    };

    struct GpuPreviewBaseFrameResult {
        SceneMode mode = SceneMode::Flame;
        bool includeSeparateGridLayer = false;
        GpuFrameInputs baseInputs {};
        ID3D11ShaderResourceView* directSourceColor = nullptr;
        PreviewBackend directBackend = PreviewBackend::GpuHybrid;
        PreviewBackend composedBackend = PreviewBackend::GpuHybrid;
        PreviewBackend compositeFailureBackend = PreviewBackend::GpuHybrid;
        std::uint32_t displayedIterations = 0;
        bool useCompositeFinalize = false;
        bool requireCompletedAccumulation = false;
    };

    struct GpuFailureChecks {
        bool flame = false;
        bool grid = false;
        bool path = false;
    };

    struct GpuEffectChainState {
        GpuFrameInputs inputs {};
        GpuPassOutput resolvedFrame {};
        bool hasResolvedFrame = false;
    };

    struct GpuFlameRenderOptions {
        std::uint32_t iterations = 1;
        bool transparent = false;
        bool clearAccumulationForFrame = false;
        bool preserveTemporalState = false;
        bool resetTemporalState = false;
        bool pumpOverlay = false;
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
    GpuDenoiser gpuDenoiser_;
    GpuPostProcess gpuPostProcess_;
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
    bool presentingBlockingOverlay_ = false;
    bool showStatusOverlay_ = true;
    bool presentVsync_ = true;
    bool adaptiveInteractivePreview_ = true;
    bool exportHideGrid_ = true;
    bool exportTransparentBackground_ = false;
    bool exportUseGpu_ = false;
    bool exportStableFlameSampling_ = true;
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
    std::uint32_t exportIterations_ = 480000;
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
    bool bootstrapUiFramePending_ = true;
    bool loadingComplete_ = false;
    float loadingProgress_ = 0.0f;
    std::string loadingLabel_;
    RECT startupWindowRect_ {CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT};

    std::chrono::steady_clock::time_point lastFrame_ {};
    double fpsSmoothed_ = 60.0;
    std::chrono::steady_clock::time_point lastPreviewUpdate_ {};
    double previewFpsSmoothed_ = 60.0;
    bool viewportDirty_ = true;
    bool interactivePreview_ = false;
    bool asyncViewportRendering_ = true;
    std::uint32_t interactivePreviewIterations_ = 60000;
    ImFont* uiFont_ = nullptr;
    ImFont* brandFont_ = nullptr;
    std::filesystem::path appLogoPath_;
    std::vector<std::uint32_t> appLogoPixels_;
    int appLogoWidth_ = 0;
    int appLogoHeight_ = 0;
    bool appLogoLoadAttempted_ = false;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> appLogoSrv_;
    std::filesystem::path startupLogoSvgPath_;
    std::vector<StartupLogoStroke> startupLogoSvgStrokes_;
    bool startupLogoSvgLoadAttempted_ = false;
    COLORREF startupLogoSvgColor_ = RGB(115, 148, 235);
    float startupLogoSvgMinX_ = 0.0f;
    float startupLogoSvgMinY_ = 0.0f;
    float startupLogoSvgMaxX_ = 0.0f;
    float startupLogoSvgMaxY_ = 0.0f;
    ULONG_PTR gdiplusToken_ = 0;
    float startupAnimationDashOffset_ = 0.0f;
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
    std::chrono::steady_clock::time_point lastGpuPreviewDispatchAt_ {};
    int selectedTimelineKeyframe_ = -1;
    bool timelineDraggingPlayhead_ = false;
    bool timelineDraggingKeyframe_ = false;
    double newSceneFrameRateDefault_ = 24.0;
    int newSceneEndFrameDefault_ = 120;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateDeviceD3D();
    bool EnsureGpuFlameRendererInitialized();
    bool EnsureGpuPathRendererInitialized(GpuPathRenderer& renderer, const wchar_t* label);
    bool EnsureGpuDofRendererInitialized();
    bool EnsureGpuDenoiserInitialized();
    bool EnsureGpuPostProcessInitialized();
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
    bool EnsureAppLogoLoaded();
    bool EnsureStartupLogoSvgLoaded();
    void SetupImGui();
    void ShutdownImGui();
    void ApplyStyle() const;
    void ApplyPendingResize();
    void CreateAppLogoTexture();
    void CleanupAppLogoTexture();
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
    static PreviewBackend DetermineCpuPreviewBackend(SceneMode mode);
    Scene EvaluateSceneAtTimelineFrame(double frame) const;
    Scene BuildRenderableScene(const Scene& scene) const;
    Scene BuildExportRenderScene(const Scene& sourceScene, int width, int height, bool hideGrid) const;
    ViewportRenderRequest BuildViewportRenderRequest(int width, int height, bool interactive, bool useGpuViewportPreview) const;
    ViewportRenderSetup DetermineViewportRenderSetup(const ViewportRenderRequest& request) const;
    bool ShouldSkipViewportRender(const ViewportRenderSetup& setup) const;
    bool SetDirectGpuPreview(int width, int height, PreviewBackend backend, std::uint32_t displayedIterations, std::chrono::steady_clock::time_point dispatchTime);
    bool ResolveGpuPostProcessOutput(
        const Scene& scene,
        int width,
        int height,
        const GpuPassOutput& input,
        GpuPassOutput& output,
        bool* postProcessed = nullptr,
        std::optional<std::uint32_t> randomSeedOverride = std::nullopt);
    bool RenderGpuPostProcessPreview(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        ID3D11ShaderResourceView* sourceColor,
        PreviewBackend baseBackend,
        std::uint32_t displayedIterations);
    bool RenderGpuDofPreview(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        const GpuFrameInputs& inputs,
        std::uint32_t displayedIterations);
    bool RenderGpuPathScene(
        const Scene& scene,
        int width,
        int height,
        bool transparent,
        bool renderGrid,
        ID3D11ShaderResourceView* flameDepthSrv,
        GpuPathRenderer& renderer,
        const wchar_t* rendererLabel);
    bool RenderGpuFlameScene(
        const Scene& scene,
        int width,
        int height,
        const GpuFlameRenderOptions& options);
    bool RenderGpuFlameBaseLayers(
        const Scene& scene,
        int width,
        int height,
        bool renderBackground,
        bool backgroundTransparent,
        bool backgroundGrid,
        const GpuFlameRenderOptions& flameOptions,
        bool includePath,
        std::uint32_t* displayedIterations = nullptr);
    GpuFrameInputs CaptureCurrentGpuFrameInputs(bool includeGrid, bool includeFlame, bool includePath) const;
    GpuFrameInputs CaptureGpuFrameInputsForMode(SceneMode mode, bool includeSeparateGridLayer) const;
    GpuPreviewPipeline BuildGpuPreviewPipeline(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        const GpuFrameInputs& baseInputs,
        ID3D11ShaderResourceView* directSourceColor,
        PreviewBackend directBackend,
        PreviewBackend composedBackend,
        PreviewBackend compositeFailureBackend,
        std::uint32_t displayedIterations,
        bool useCompositeFinalize,
        bool requireCompletedAccumulation);
    static GpuFailureChecks BuildGpuFailureChecks(SceneMode mode, bool includeSeparateGridLayer);
    std::wstring BuildGpuFailureStatusMessage(
        const wchar_t* contextLabel,
        const GpuFailureChecks& checks,
        bool useGpuDenoiser,
        bool useGpuDof,
        bool useGpuPostProcess) const;
    void PopulateGpuPreviewBaseFrameResult(
        SceneMode mode,
        bool includeSeparateGridLayer,
        std::uint32_t displayedIterations,
        GpuPreviewBaseFrameResult& result) const;
    bool RenderGpuPreviewBaseLayers(
        const ViewportRenderRequest& request,
        std::uint32_t& displayedIterations,
        bool& includeSeparateGridLayer);
    bool RenderGpuPreviewFlameBaseLayers(
        const Scene& scene,
        int width,
        int height,
        std::uint32_t previewIterations,
        bool includePath,
        std::uint32_t& displayedIterations);
    void UpdateGpuPreviewFailureStatus(
        const GpuFailureChecks& checks,
        bool useGpuDenoiserPreview,
        bool useGpuDofPreview,
        bool useGpuPostProcessPreview);
    bool RunGpuDenoiserPass(const Scene& scene, int width, int height, const GpuFrameInputs& inputs, GpuPassOutput& output);
    bool RunGpuCompositePass(int width, int height, const GpuFrameInputs& inputs, GpuPassOutput& output);
    bool PrepareGpuEffectChainState(
        const Scene& scene,
        int width,
        int height,
        const GpuFrameInputs& baseInputs,
        bool applyDenoiser,
        bool resolveFrame,
        GpuEffectChainState& state);
    bool RunGpuDofPass(
        const Scene& scene,
        int width,
        int height,
        const GpuFrameInputs& inputs,
        GpuPassOutput& output);
    bool TryRenderGpuEffectChain(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        const GpuFrameInputs& baseInputs,
        bool applyDenoiser,
        bool applyDof,
        std::uint32_t displayedIterations,
        bool& handled);
    bool ExecuteGpuPreviewPipeline(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        const GpuPreviewPipeline& pipeline);
    bool ExecuteGpuPreviewPipelineWithFailureStatus(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        const GpuPreviewPipeline& pipeline,
        const GpuFailureChecks& checks);
    bool FinalizeGpuPreviewStage(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        ID3D11ShaderResourceView* directSourceColor,
        const GpuFrameInputs* compositeInputs,
        PreviewBackend directBackend,
        PreviewBackend composedBackend,
        PreviewBackend compositeFailureBackend,
        std::uint32_t displayedIterations);
    bool RenderGpuCompositePreview(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        const GpuFrameInputs& inputs,
        PreviewBackend composedBackend,
        std::uint32_t displayedIterations);
    bool BuildGpuPreviewBaseFrameResult(
        const ViewportRenderRequest& request,
        GpuPreviewBaseFrameResult& result);
    bool ExecuteGpuPreviewBaseFrameResult(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        const GpuPreviewBaseFrameResult& result);
    bool RenderGpuViewportPreview(const ViewportRenderRequest& request, const ViewportRenderSetup& setup);
    void ExecuteViewportRenderRequest(const ViewportRenderRequest& request, const ViewportRenderSetup& setup);
    bool QueueCpuViewportPreview(const ViewportRenderRequest& request);
    bool PresentViewportPixels(int width, int height, PreviewBackend backend, std::uint32_t displayedIterations);
    bool RenderCpuViewportPreview(const ViewportRenderRequest& request);
    void QueueViewportRender(int width, int height, bool interactive);
    void ConsumeCompletedRender();
    void RecordPreviewUpdate();
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
    bool ExportViewportImage(const std::filesystem::path& path, int width, int height, std::uint32_t iterations, bool transparentBackground, bool hideGrid, bool useGpu, ExportFormat format);
    bool ExportImageSequence(const std::filesystem::path& path, int width, int height, std::uint32_t iterations, bool transparentBackground, bool hideGrid, bool useGpu, ExportFormat format, int startFrame, int endFrame);
    bool ExportAviVideo(const std::filesystem::path& path, int width, int height, std::uint32_t iterations, bool hideGrid, bool useGpu, int startFrame, int endFrame);
    bool ExportFfmpegVideo(const std::filesystem::path& path, int width, int height, std::uint32_t iterations, bool hideGrid, bool useGpu, int startFrame, int endFrame, ExportFormat format);
    struct ExportRenderState {
        bool preserveTemporalFlameState = false;
        bool resetTemporalFlameState = false;
        SoftwareRenderer* cpuRenderer = nullptr;
    };
    struct GpuExportRenderPlan {
        Scene scene;
        int width = 0;
        int height = 0;
        bool transparentBackground = false;
        bool exportGrid = false;
        bool usesEffectPipeline = false;
        const ExportRenderState* renderState = nullptr;
    };
    struct GpuExportBaseFrameResult {
        GpuFrameInputs baseInputs {};
    };
    GpuFlameRenderOptions MakeExportGpuFlameRenderOptions(const Scene& scene, bool transparent, const ExportRenderState* renderState) const;
    bool RenderSceneToPixels(const Scene& sourceScene, int width, int height, std::uint32_t iterations, bool transparentBackground, bool hideGrid, bool useGpu, std::vector<std::uint32_t>& pixels, const ExportRenderState* renderState = nullptr);
    bool RenderSceneToPixelsCpu(const Scene& sourceScene, int width, int height, std::uint32_t iterations, bool transparentBackground, bool hideGrid, std::vector<std::uint32_t>& pixels, const ExportRenderState* renderState = nullptr) const;
    bool RenderSceneToPixelsGpu(const Scene& sourceScene, int width, int height, std::uint32_t iterations, bool transparentBackground, bool hideGrid, std::vector<std::uint32_t>& pixels, const ExportRenderState* renderState = nullptr);
    void InvalidateExportViewportPreview();
    bool PumpExportOverlay();
    GpuExportRenderPlan BuildGpuExportRenderPlan(const Scene& sourceScene, int width, int height, std::uint32_t iterations, bool transparentBackground, bool hideGrid, const ExportRenderState* renderState) const;
    static bool UsesSeparateGpuExportGridLayer(const GpuExportRenderPlan& plan);
    bool RenderGpuExportFlameBaseLayers(const Scene& exportScene, int width, int height, bool transparentBackground, bool exportGrid, const ExportRenderState* renderState);
    bool RenderGpuExportHybridBaseLayers(const Scene& exportScene, int width, int height, bool transparentBackground, bool exportGrid, const ExportRenderState* renderState);
    bool RenderGpuExportBaseLayers(const GpuExportRenderPlan& plan, bool& includeSeparateGridLayer);
    bool BuildGpuExportBaseFrameResult(const GpuExportRenderPlan& plan, GpuExportBaseFrameResult& result);
    bool ExecuteGpuExportBaseFrameResult(const GpuExportRenderPlan& plan, const GpuExportBaseFrameResult& result, std::vector<std::uint32_t>& pixels);
    bool ReadGpuExportBaseFramePixels(const GpuFrameInputs& inputs, int width, int height, bool transparentBackground, std::vector<std::uint32_t>& output) const;
    bool ApplyGpuExportPostProcessAndReadback(const Scene& exportScene, int width, int height, const GpuPassOutput& frame, std::vector<std::uint32_t>& output);
    bool ReadGpuExportEffectPixels(const Scene& exportScene, int width, int height, const GpuFrameInputs& baseInputs, std::vector<std::uint32_t>& output);
    void UpdateGpuExportFailureStatus(const GpuExportRenderPlan& plan);
    bool ExecuteGpuExportRenderPlan(const GpuExportRenderPlan& plan, std::vector<std::uint32_t>& pixels);
    bool RenderExportSceneWithGpuFallback(
        const Scene& renderScene,
        int width,
        int height,
        std::uint32_t iterations,
        bool transparentBackground,
        bool hideGrid,
        bool& useGpuRender,
        std::vector<std::uint32_t>& pixels,
        const ExportRenderState* renderState = nullptr);
    bool RenderAnimatedExportFrame(
        int frame,
        int frameIndex,
        int frameCount,
        int width,
        int height,
        std::uint32_t iterations,
        bool transparentBackground,
        bool hideGrid,
        bool& useGpuRender,
        SoftwareRenderer& cpuRenderer,
        std::vector<std::uint32_t>& pixels);
    bool ReadbackGpuTexture(ID3D11Texture2D* texture, std::vector<std::uint32_t>& pixels) const;
    bool ReadbackGpuDepthTexture(ID3D11Texture2D* texture, std::vector<float>& depthBuffer) const;
    void DrawLoadingLogo(HDC hdc, int clientWidth, int clientHeight, int barY) const;
    void DrawAnimatedLoadingLogo(HDC hdc, int clientWidth, int clientHeight, int barY) const;
    bool DrawAnimatedLoadingIcon(ImDrawList* drawList, const ImVec2& center, float maxExtent, std::uint32_t color) const;

    static std::wstring Utf8ToWide(const std::string& value);
    static std::string WideToUtf8(const std::wstring& value);
    static std::size_t GradientStopIndexForPaletteColor(int paletteIndex);
    static Color ToColor(const float color[3]);
};

}  // namespace radiary
