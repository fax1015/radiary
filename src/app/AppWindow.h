#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/CpuPreviewWorker.h"
#include "app/PreviewPresentation.h"
#include "app/PreviewRenderDecisions.h"
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
#include "renderer/backend/RenderBackend.h"

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

    enum class PreviewProgressPhase {
        Dirty,
        Queued,
        Accumulating,
        Complete
    };

    struct PreviewProgressState {
        std::uint32_t targetIterations = 0;
        std::uint32_t displayedIterations = 0;
        PreviewPresentationState presentation {};
        PreviewResetReason pendingResetReason = PreviewResetReason::SceneChanged;
        PreviewResetReason appliedResetReason = PreviewResetReason::SceneChanged;
        PreviewProgressPhase phase = PreviewProgressPhase::Dirty;
        bool dirty = true;
        std::optional<bool> temporalStateValid;

        bool IsInProgress() const {
            return phase == PreviewProgressPhase::Queued || phase == PreviewProgressPhase::Accumulating;
        }

        bool IsComplete() const {
            return phase == PreviewProgressPhase::Complete;
        }
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
        PreviewPresentationState cpuPreviewState {};
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
        PreviewPresentationState directState {};
        PreviewPresentationState composedState {};
        PreviewPresentationState compositeFailureState {};
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
        PreviewPresentationState directState {};
        PreviewPresentationState composedState {};
        PreviewPresentationState compositeFailureState {};
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
        PreviewRenderStage finalStage = PreviewRenderStage::Base;
    };

    struct GpuFlameRenderOptions {
        std::uint32_t iterations = 1;
        bool transparent = false;
        bool clearAccumulationForFrame = false;
        bool preserveTemporalState = false;
        bool resetTemporalState = false;
        bool pumpOverlay = false;
    };

    struct HistoryEntry {
        Scene snapshot {};
        std::string label = "Scene";
    };

    struct HistogramCache {
        std::array<float, 256> red {};
        std::array<float, 256> green {};
        std::array<float, 256> blue {};
        std::array<float, 256> luminance {};
        int width = 0;
        int height = 0;
        std::size_t sampleCount = 0;
        double meanLuminance = 0.0;
        double medianLuminance = 0.0;
        double p95Luminance = 0.0;
        double shadowClipPercent = 0.0;
        double midtonePercent = 0.0;
        double highlightClipPercent = 0.0;
        bool valid = false;
        PreviewPresentationState presentation {};
        std::chrono::steady_clock::time_point sourceTimestamp {};
        std::chrono::steady_clock::time_point lastRefreshAt {};
    };

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mainRenderTargetView_;
    std::unique_ptr<RenderBackend> renderBackend_;
    UINT resizeWidth_ = 0;
    UINT resizeHeight_ = 0;
    bool swapChainOccluded_ = false;
    bool running_ = true;
    bool defaultLayoutBuilt_ = false;
    bool inSizeMove_ = false;

    Scene scene_ = CreateDefaultScene();
    std::vector<HistoryEntry> undoStack_;
    std::vector<HistoryEntry> redoStack_;
    GpuFlameRenderer gpuFlameRenderer_;
    GpuDofRenderer gpuDofRenderer_;
    GpuDenoiser gpuDenoiser_;
    GpuPostProcess gpuPostProcess_;
    GpuPathRenderer gpuGridRenderer_;
    GpuPathRenderer gpuPathRenderer_;
    SoftwareRenderer renderer_;
    CpuPreviewWorker cpuPreviewWorker_;
    SceneSerializer serializer_;
    PresetLibrary presetLibrary_;
    std::filesystem::path currentScenePath_;
    std::vector<std::uint32_t> viewportPixels_;
    std::size_t presetIndex_ = 0;
    InspectorTarget inspectorTarget_ = InspectorTarget::FlameLayer;
    std::wstring statusText_ = L"Ready";
    std::wstring renderAdapterName_ = L"Unknown adapter";
    std::chrono::steady_clock::time_point lastAutoSave_ {};
    Scene pendingUndoScene_ = scene_;
    bool hasPendingUndoScene_ = false;
    std::string currentHistoryLabel_ = "New Scene";
    bool sceneDirty_ = false;
    bool sceneModifiedSinceAutoSave_ = false;
    bool viewportInteractionCaptured_ = false;
    bool layersPanelOpen_ = true;
    bool keyframeListPanelOpen_ = true;
    bool inspectorPanelOpen_ = true;
    bool playbackPanelOpen_ = true;
    bool previewPanelOpen_ = true;
    bool effectsPanelOpen_ = true;
    char effectsPopupSearchText_[128] = {};
    std::vector<int> selectedEffects_;
    int selectedEffect_ = -1;
    int effectSelectionAnchor_ = 0;
    int rightClickedEffectIndex_ = -1;
    bool histogramPanelOpen_ = false;
    bool historyPanelOpen_ = true;
    bool cameraPanelOpen_ = true;
    bool viewportPanelOpen_ = true;
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
    float exportIterationScale_ = 1.0f;
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
    bool keyframeListPanelActive_ = false;
    bool inspectorPanelActive_ = false;
    bool playbackPanelActive_ = false;
    bool effectsPanelActive_ = false;
    bool cameraAspectLocked_ = false;
    double cameraAspectLockedRatio_ = 16.0 / 9.0;
    bool autoSaveEnabled_ = true;
    int autoSaveIntervalSeconds_ = 60;
    int undoHistoryLimit_ = 50;
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
    HistogramCache histogramCache_ {};
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
    PreviewProgressState previewProgress_ {};
    std::chrono::steady_clock::time_point lastGpuPreviewDispatchAt_ {};
    int selectedTimelineKeyframe_ = -1;
    bool timelineDraggingPlayhead_ = false;
    bool timelineDraggingKeyframe_ = false;
    bool timelineDraggingView_ = false;
    float timelineViewDragLastMouseX_ = 0.0f;
    float timelineZoom_ = 1.0f;
    double timelineViewCenterFrame_ = 0.0;
    double newSceneFrameRateDefault_ = 24.0;
    int newSceneEndFrameDefault_ = 120;
    static constexpr int kMinAutoSaveIntervalSeconds = 15;
    static constexpr int kMaxAutoSaveIntervalSeconds = 600;
    static constexpr int kMinUndoHistoryLimit = 10;
    static constexpr int kMaxUndoHistoryLimit = 200;

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
    int UploadedViewportWidth() const;
    int UploadedViewportHeight() const;

    void LoadPresets();
    void LoadUserSettings();
    void SaveUserSettings() const;
    static std::filesystem::path UserLayoutPath();
    static std::filesystem::path AutoSavePath();
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
    void DrawKeyframeListPanel();
    void DrawInspectorPanel();
    void DrawTimelinePanel();
    void DrawPreviewPanel();
    void DrawEffectsPanel();
    void DrawHistogramPanel();
    void DrawHistoryPanel();
    void DrawCameraPanel();
    void DrawViewportPanel();
    void OpenAllDockPanels();
    void DrawDockPanelVisibilityMenuItems();
    void DrawDockPanelTabContextMenu(const char* panelName, bool& panelOpen);
    void DrawStatusBar(const ImVec2& viewportMin, const ImVec2& viewportMax);
    void DrawSettingsPanel();
    void DrawExportPanel();
    void DrawEasingPanel();

    void EnsureSelectionIsValid();
    void ResetScene(Scene scene);
    void UpdateWindowTitle();
    void LoadPreset(std::size_t index);
    void MarkViewportDirty(PreviewResetReason reason = PreviewResetReason::SceneChanged);
    static PreviewResetReason DeterminePreviewResetReason(const Scene& before, const Scene& after);
    static PreviewRenderContent PreviewContentForMode(SceneMode mode);
    static PreviewPresentationState MakePreviewPresentationState(
        PreviewRenderDevice device,
        PreviewRenderContent content,
        PreviewRenderStage stage = PreviewRenderStage::Base);
    static PreviewPresentationState WithPreviewPresentationStage(
        PreviewPresentationState state,
        PreviewRenderStage stage);
    static bool IsGpuPreviewPresentationState(const PreviewPresentationState& state);
    static bool IsPreviewPresentationStageAtLeast(
        const PreviewPresentationState& state,
        PreviewRenderStage stage);
    static const char* PreviewRenderDeviceLabel(PreviewRenderDevice device);
    static const char* PreviewRenderContentLabel(PreviewRenderContent content);
    static const char* PreviewRenderStageLabel(PreviewRenderStage stage);
    static const char* PreviewProgressPhaseLabel(PreviewProgressPhase phase);
    static const char* PreviewResetReasonLabel(PreviewResetReason reason);
    static std::wstring DescribePreviewPresentationState(const PreviewPresentationState& state);
    static PreviewPresentationState DetermineCpuPreviewState(SceneMode mode);
    static PreviewRenderStage DetermineResolvedRenderStage(
        bool useDenoiser,
        bool useDof,
        bool usePostProcess);
    static PreviewRenderStage DetermineResolvedRenderStage(const Scene& scene);
    static std::wstring BuildGpuRendererUnavailableStatusMessage(
        const wchar_t* label,
        bool deviceReady,
        const std::string& lastError);
    static std::wstring BuildGpuFallbackStatusMessage(
        const PreviewPresentationState& failedState,
        const std::wstring& failureStatus);
    Scene EvaluateSceneAtTimelineFrame(double frame) const;
    Scene BuildRenderableScene(const Scene& scene) const;
    Scene BuildExportRenderScene(const Scene& sourceScene, int width, int height, bool hideGrid) const;
    ViewportRenderRequest BuildViewportRenderRequest(int width, int height, bool interactive, bool useGpuViewportPreview) const;
    ViewportRenderSetup DetermineViewportRenderSetup(const ViewportRenderRequest& request) const;
    bool ShouldSkipViewportRender(const ViewportRenderSetup& setup) const;
    bool SetDirectGpuPreview(int width, int height, PreviewPresentationState state, std::uint32_t displayedIterations, std::chrono::steady_clock::time_point dispatchTime);
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
        PreviewPresentationState baseState,
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
        PreviewPresentationState directState,
        PreviewPresentationState composedState,
        PreviewPresentationState compositeFailureState,
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
    static Scene MakeIsolatedEffectScene(const Scene& scene, EffectStackStage stage);
    static PreviewRenderStage PreviewRenderStageForEffectStage(EffectStackStage stage);
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
        PreviewPresentationState directState,
        PreviewPresentationState composedState,
        PreviewPresentationState compositeFailureState,
        std::uint32_t displayedIterations);
    bool RenderGpuCompositePreview(
        const ViewportRenderRequest& request,
        const ViewportRenderSetup& setup,
        const GpuFrameInputs& inputs,
        PreviewPresentationState composedState,
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
    static CpuPreviewWorker::Request MakeCpuPreviewRequest(const ViewportRenderRequest& request);
    bool PresentViewportPixels(int width, int height, PreviewPresentationState state, std::uint32_t displayedIterations);
    BackendPreviewSurfaceSet CollectPreviewSurfaces() const;
    bool RenderCpuViewportPreview(const ViewportRenderRequest& request);
    void ConsumeCompletedRender();
    void RecordPreviewUpdate();
    bool IsViewportDirty() const;
    void UpdatePreviewTargetIterations(std::uint32_t targetIterations);
    void QueuePreviewPresentation(PreviewPresentationState state);
    void ApplyPresentedPreviewState(
        PreviewPresentationState state,
        std::uint32_t displayedIterations,
        std::optional<PreviewResetReason> appliedResetReason = std::nullopt,
        std::optional<bool> temporalStateValid = std::nullopt,
        std::optional<PreviewProgressPhase> phase = std::nullopt);
    void SyncGpuPreviewProgress(
        PreviewPresentationState state,
        std::uint32_t targetIterations,
        const GpuFlameRenderer::StatusSnapshot& snapshot);
    void StartRenderThread();
    void StopRenderThread();
    void RenderViewportIfNeeded(int width, int height);
    void HandleViewportInteraction(bool hovered, int width, int height);
    void HandleShortcuts();
    void CaptureWidgetUndo(const Scene& beforeChange, bool changed);
    void PushUndoState(const Scene& snapshot, std::string label = {});
    bool CanUndo() const;
    bool CanRedo() const;
    void Undo();
    void Redo();
    void ClearPendingUndoCapture();
    void EnforceUndoStackLimits();
    void SetCurrentHistoryLabel(std::string label);
    std::string DescribeSceneChange(const Scene& before, const Scene& after) const;
    bool CaptureCurrentPreviewPixels(std::vector<std::uint32_t>& pixels, int& width, int& height);
    void CopySelectedLayer();
    void PasteCopiedLayer();
    void DuplicateSelectedLayer();
    void NormalizeEffectSelections();
    void ClearEffectSelections();
    void SetEffectSelection(std::vector<int> indices, int primaryIndex, int anchorIndex);
    void SelectSingleEffect(int index);
    void ToggleEffectSelection(int index);
    void SelectEffectRange(int index);
    void SelectAllEffects();
    bool IsEffectSelected(int index) const;
    int SelectedEffectCount() const;
    bool HasMultipleEffectsSelected() const;
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
    bool ApplyLayerVisibilityToSelectionOrItem(InspectorTarget target, int index, bool visible);
    bool ToggleLayerVisibilityForSelectionOrItem(InspectorTarget target, int index);
    bool IsLayerSelected(InspectorTarget target, int index) const;
    int SelectedLayerCount() const;
    bool HasSelectedLayer() const;
    bool HasMultipleLayersSelected() const;
    bool CanRemoveSelectedLayers() const;
    bool RemoveSelectedLayers();
    bool CanRemoveSelectedEffect() const;
    bool RemoveSelectedEffect();
    bool RemoveEffectAtIndex(int effectIndex, const std::string& actionLabel);
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
    std::uint32_t CurrentPreviewSampleBaseline() const;
    std::uint32_t CurrentExportDensityMatchedBaseline() const;
    std::uint32_t CurrentExportIterationCount() const;
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
        PreviewRenderContent content = PreviewRenderContent::Hybrid;
        PreviewRenderStage outputStage = PreviewRenderStage::Base;
        const ExportRenderState* renderState = nullptr;
    };
    struct GpuExportBaseFrameResult {
        GpuFrameInputs baseInputs {};
    };
    GpuFlameRenderOptions MakeExportGpuFlameRenderOptions(const Scene& scene, bool transparent, const ExportRenderState* renderState) const;
    bool RenderSceneToPixels(const Scene& sourceScene, int width, int height, std::uint32_t iterations, bool transparentBackground, bool hideGrid, bool useGpu, std::vector<std::uint32_t>& pixels, const ExportRenderState* renderState = nullptr);
    bool RenderSceneToPixelsCpu(const Scene& sourceScene, int width, int height, std::uint32_t iterations, bool transparentBackground, bool hideGrid, std::vector<std::uint32_t>& pixels, const ExportRenderState* renderState = nullptr);
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
    std::wstring BuildExportInterruptedStatus() const;
    void SetExportInterruptedStatus();
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
    void DrawLoadingLogo(HDC hdc, int clientWidth, int clientHeight, int barY) const;
    void DrawAnimatedLoadingLogo(HDC hdc, int clientWidth, int clientHeight, int barY) const;
    bool DrawAnimatedLoadingIcon(ImDrawList* drawList, const ImVec2& center, float maxExtent, std::uint32_t color) const;

    static std::wstring Utf8ToWide(const std::string& value);
    static std::string WideToUtf8(const std::wstring& value);
    static std::size_t GradientStopIndexForPaletteColor(int paletteIndex);
    static Color ToColor(const float color[3]);
};

}  // namespace radiary
