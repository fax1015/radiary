#include "app/AppWindow.h"
#include "app/CameraUtils.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "imgui_internal.h"

using namespace radiary;

namespace {

constexpr auto kInteractiveGpuPreviewCadence = std::chrono::milliseconds(16);
constexpr auto kUiBusyGpuPreviewCadence = std::chrono::milliseconds(48);
constexpr auto kSettledGpuPreviewCadence = std::chrono::milliseconds(16);
constexpr auto kResizeGpuPreviewCadence = std::chrono::milliseconds(80);

std::uint32_t ViewportPreviewIterations(
    const Scene& scene,
    const bool interactive,
    const bool useGpuViewportPreview,
    const bool adaptiveInteractivePreview,
    const std::uint32_t interactivePreviewIterations) {
    (void)useGpuViewportPreview;
    std::uint32_t previewIterations = scene.previewIterations;
    if (!adaptiveInteractivePreview) {
        return previewIterations;
    }

    if (interactive) {
        previewIterations = std::min(previewIterations, interactivePreviewIterations);
    }
    return previewIterations;
}

}  // namespace

namespace radiary {
void AppWindow::MarkViewportDirty(const PreviewResetReason reason) {
    viewportDirty_ = true;
    previewProgress_.dirty = true;
    previewProgress_.pendingResetReason = reason;
    previewProgress_.phase = PreviewProgressPhase::Dirty;
    previewProgress_.displayedIterations = 0;
    previewProgress_.temporalStateValid.reset();
}

radiary::PreviewResetReason AppWindow::DeterminePreviewResetReason(const Scene& before, const Scene& after) {
    return ::radiary::DeterminePreviewResetReason(before, after);
}

radiary::PreviewRenderContent AppWindow::PreviewContentForMode(const SceneMode mode) {
    return ::radiary::PreviewContentForMode(mode);
}

PreviewPresentationState AppWindow::MakePreviewPresentationState(
    const PreviewRenderDevice device,
    const PreviewRenderContent content,
    const PreviewRenderStage stage) {
    return PreviewPresentationState {
        .device = device,
        .content = content,
        .stage = stage,
    };
}

PreviewPresentationState AppWindow::WithPreviewPresentationStage(
    PreviewPresentationState state,
    const PreviewRenderStage stage) {
    state.stage = stage;
    return state;
}

bool AppWindow::IsGpuPreviewPresentationState(const PreviewPresentationState& state) {
    return state.device == PreviewRenderDevice::Gpu;
}

bool AppWindow::IsPreviewPresentationStageAtLeast(
    const PreviewPresentationState& state,
    const PreviewRenderStage stage) {
    if (!IsGpuPreviewPresentationState(state)) {
        return false;
    }
    return static_cast<int>(state.stage) >= static_cast<int>(stage);
}

const char* AppWindow::PreviewRenderDeviceLabel(const PreviewRenderDevice device) {
    switch (device) {
    case PreviewRenderDevice::Cpu:
        return "CPU";
    case PreviewRenderDevice::Gpu:
    default:
        return "GPU";
    }
}

const char* AppWindow::PreviewRenderContentLabel(const PreviewRenderContent content) {
    switch (content) {
    case PreviewRenderContent::Flame:
        return "Flame";
    case PreviewRenderContent::Path:
        return "Path";
    case PreviewRenderContent::Hybrid:
    default:
        return "Hybrid";
    }
}

const char* AppWindow::PreviewRenderStageLabel(const PreviewRenderStage stage) {
    switch (stage) {
    case PreviewRenderStage::Base:
        return "Base";
    case PreviewRenderStage::Composited:
        return "Composited";
    case PreviewRenderStage::Denoised:
        return "Denoised";
    case PreviewRenderStage::DepthOfField:
        return "DOF";
    case PreviewRenderStage::PostProcessed:
    default:
        return "Post";
    }
}

const char* AppWindow::PreviewProgressPhaseLabel(const PreviewProgressPhase phase) {
    switch (phase) {
    case PreviewProgressPhase::Dirty:
        return "Dirty";
    case PreviewProgressPhase::Queued:
        return "Queued";
    case PreviewProgressPhase::Accumulating:
        return "Accumulating";
    case PreviewProgressPhase::Complete:
    default:
        return "Complete";
    }
}

const char* AppWindow::PreviewResetReasonLabel(const PreviewResetReason reason) {
    switch (reason) {
    case PreviewResetReason::None:
        return "None";
    case PreviewResetReason::SceneChanged:
        return "Scene";
    case PreviewResetReason::CameraChanged:
        return "Camera";
    case PreviewResetReason::ModeChanged:
        return "Mode";
    case PreviewResetReason::IterationChanged:
        return "Iterations";
    case PreviewResetReason::ViewportResized:
        return "Resize";
    case PreviewResetReason::DeviceChanged:
    default:
        return "Device";
    }
}

std::wstring AppWindow::DescribePreviewPresentationState(const PreviewPresentationState& state) {
    std::wstring description = Utf8ToWide(PreviewRenderDeviceLabel(state.device));
    description += L" ";
    description += Utf8ToWide(PreviewRenderContentLabel(state.content));
    description += L" ";
    description += Utf8ToWide(PreviewRenderStageLabel(state.stage));
    return description;
}

radiary::PreviewRenderStage AppWindow::DetermineResolvedRenderStage(
    const bool useDenoiser,
    const bool useDof,
    const bool usePostProcess) {
    return ::radiary::DetermineResolvedRenderStage(useDenoiser, useDof, usePostProcess);
}

std::wstring AppWindow::BuildGpuRendererUnavailableStatusMessage(
    const wchar_t* label,
    const bool deviceReady,
    const std::string& lastError) {
    if (!deviceReady) {
        return std::wstring(label) + L" unavailable: D3D11 device/context is not ready.";
    }
    if (!lastError.empty()) {
        return std::wstring(label) + L" unavailable: " + Utf8ToWide(lastError);
    }
    return std::wstring(label) + L" unavailable.";
}

std::wstring AppWindow::BuildGpuFallbackStatusMessage(
    const PreviewPresentationState& failedState,
    const std::wstring& failureStatus) {
    if (!failureStatus.empty()) {
        return failureStatus + L" Falling back to CPU.";
    }
    return DescribePreviewPresentationState(failedState) + L" export unavailable, falling back to CPU.";
}

PreviewPresentationState AppWindow::DetermineCpuPreviewState(const SceneMode mode) {
    return MakePreviewPresentationState(PreviewRenderDevice::Cpu, PreviewContentForMode(mode));
}

Scene AppWindow::EvaluateSceneAtTimelineFrame(const double frame) const {
    Scene frameScene = EvaluateSceneAtFrame(scene_, frame);
    frameScene.timelineFrame = frame;
    frameScene.timelineSeconds = TimelineSecondsForFrame(frameScene, frameScene.timelineFrame);
    return frameScene;
}

Scene AppWindow::BuildRenderableScene(const Scene& scene) const {
    Scene renderScene = scene;
    renderScene.transforms.erase(
        std::remove_if(renderScene.transforms.begin(), renderScene.transforms.end(), [](const TransformLayer& layer) {
            return !layer.visible;
        }),
        renderScene.transforms.end());
    renderScene.paths.erase(
        std::remove_if(renderScene.paths.begin(), renderScene.paths.end(), [](const PathSettings& path) {
            return !path.visible;
        }),
        renderScene.paths.end());
    renderScene.selectedTransform = renderScene.transforms.empty()
        ? 0
        : std::clamp(renderScene.selectedTransform, 0, static_cast<int>(renderScene.transforms.size()) - 1);
    renderScene.selectedPath = renderScene.paths.empty()
        ? 0
        : std::clamp(renderScene.selectedPath, 0, static_cast<int>(renderScene.paths.size()) - 1);
    return renderScene;
}

Scene AppWindow::BuildExportRenderScene(const Scene& sourceScene, const int width, const int height, const bool hideGrid) const {
    const int previewWidth = std::max(0, UploadedViewportWidth());
    const int previewHeight = std::max(0, UploadedViewportHeight());
    return PrepareSceneForExport(BuildRenderableScene(sourceScene), previewWidth, previewHeight, width, height, hideGrid);
}

AppWindow::ViewportRenderRequest AppWindow::BuildViewportRenderRequest(
    const int width,
    const int height,
    const bool interactive,
    const bool useGpuViewportPreview) const {
    ViewportRenderRequest request;
    request.width = width;
    request.height = height;
    request.interactive = interactive;
    request.useGpuViewportPreview = useGpuViewportPreview;
    request.scene = BuildRenderableScene(EvaluateSceneAtTimelineFrame(scene_.timelineFrame));
    request.useGpuDofPreview = useGpuViewportPreview && request.scene.depthOfField.enabled;
    request.useGpuDenoiserPreview = useGpuViewportPreview && request.scene.denoiser.enabled;
    request.useGpuPostProcessPreview = useGpuViewportPreview && request.scene.postProcess.enabled;
    request.previewIterations = ViewportPreviewIterations(
        request.scene,
        interactive,
        useGpuViewportPreview,
        adaptiveInteractivePreview_,
        interactivePreviewIterations_);
    request.scene.previewIterations = request.previewIterations;
    request.cpuPreviewState = DetermineCpuPreviewState(request.scene.mode);
    return request;
}

AppWindow::ViewportRenderSetup AppWindow::DetermineViewportRenderSetup(const ViewportRenderRequest& request) const {
    ViewportRenderSetup setup;
    const GpuFlameRenderer::StatusSnapshot gpuFlameStatus = gpuFlameRenderer_.GetStatusSnapshot();
    setup.now = std::chrono::steady_clock::now();
    setup.sizeChanged = UploadedViewportWidth() != request.width || UploadedViewportHeight() != request.height;
    setup.gpuAccumulationIncomplete =
        request.useGpuViewportPreview
        && request.scene.mode != SceneMode::Path
        && gpuFlameStatus.accumulatedIterations < request.previewIterations;

    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext& imgui = *ImGui::GetCurrentContext();
    const bool uiWidgetActive = imgui.ActiveId != 0 && !viewportInteractionCaptured_;
    setup.uiBusy =
        io.WantTextInput
        || layersPanelActive_
        || keyframeListPanelActive_
        || inspectorPanelActive_
        || playbackPanelActive_
        || uiWidgetActive;
    setup.resizeInteractionActive = setup.sizeChanged && (inSizeMove_ || uiWidgetActive);
    setup.allowGpuResolvePasses = !request.interactive && !setup.resizeInteractionActive;

    const auto gpuPreviewCadence = setup.uiBusy
        ? kUiBusyGpuPreviewCadence
        : request.interactive ? kInteractiveGpuPreviewCadence : kSettledGpuPreviewCadence;
    const auto resizePreviewCadence = inSizeMove_ ? kResizeGpuPreviewCadence : gpuPreviewCadence;

    setup.throttleGpuAccumulation =
        request.useGpuViewportPreview
        && setup.gpuAccumulationIncomplete
        && !setup.sizeChanged
        && (lastGpuPreviewDispatchAt_ != std::chrono::steady_clock::time_point {})
        && (setup.now - lastGpuPreviewDispatchAt_) < gpuPreviewCadence;
    setup.throttleGpuResize =
        request.useGpuViewportPreview
        && setup.resizeInteractionActive
        && UploadedViewportWidth() > 0
        && UploadedViewportHeight() > 0
        && (lastGpuPreviewDispatchAt_ != std::chrono::steady_clock::time_point {})
        && (setup.now - lastGpuPreviewDispatchAt_) < resizePreviewCadence;
    setup.needsGpuDofResolve =
        request.useGpuDofPreview
        && setup.allowGpuResolvePasses
        && !setup.gpuAccumulationIncomplete
        && !IsPreviewPresentationStageAtLeast(previewProgress_.presentation, PreviewRenderStage::DepthOfField);
    setup.needsGpuDenoiserResolve =
        request.useGpuDenoiserPreview
        && setup.allowGpuResolvePasses
        && !setup.gpuAccumulationIncomplete
        && !IsPreviewPresentationStageAtLeast(previewProgress_.presentation, PreviewRenderStage::Denoised);
    setup.needsGpuPostProcessResolve =
        request.useGpuPostProcessPreview
        && setup.allowGpuResolvePasses
        && !setup.gpuAccumulationIncomplete
        && !IsPreviewPresentationStageAtLeast(previewProgress_.presentation, PreviewRenderStage::PostProcessed);
    return setup;
}

bool AppWindow::ShouldSkipViewportRender(const ViewportRenderSetup& setup) const {
    return ((!IsViewportDirty()
            && !setup.sizeChanged
            && !setup.gpuAccumulationIncomplete
            && !setup.needsGpuDofResolve
            && !setup.needsGpuDenoiserResolve
            && !setup.needsGpuPostProcessResolve)
        || setup.throttleGpuAccumulation
        || setup.throttleGpuResize);
}

bool AppWindow::SetDirectGpuPreview(
    const int width,
    const int height,
    const PreviewPresentationState state,
    const std::uint32_t displayedIterations,
    const std::chrono::steady_clock::time_point dispatchTime) {
    if (renderBackend_) {
        renderBackend_->SetPresentedPreviewSize(width, height);
    }
    lastGpuPreviewDispatchAt_ = dispatchTime;
    const auto snapshot = gpuFlameRenderer_.GetStatusSnapshot();
    SyncGpuPreviewProgress(state, previewProgress_.targetIterations, snapshot);
    ApplyPresentedPreviewState(state, displayedIterations, previewProgress_.pendingResetReason, snapshot.temporalStateValid);
    RecordPreviewUpdate();
    return true;
}

bool AppWindow::ResolveGpuPostProcessOutput(
    const Scene& scene,
    const int width,
    const int height,
    const GpuPassOutput& input,
    GpuPassOutput& output,
    bool* postProcessed,
    const std::optional<std::uint32_t> randomSeedOverride) {
    output = input;
    if (postProcessed != nullptr) {
        *postProcessed = false;
    }
    if (!scene.postProcess.enabled) {
        return output.colorSrv != nullptr || output.colorTexture != nullptr;
    }
    if (input.colorSrv == nullptr) {
        return false;
    }
    if (!EnsureGpuPostProcessInitialized()) {
        return input.colorTexture != nullptr || input.colorSrv != nullptr;
    }
    if (!gpuPostProcess_.Render(scene, width, height, MakeGpuPostProcessInputs(input.colorSrv, randomSeedOverride))) {
        return input.colorTexture != nullptr || input.colorSrv != nullptr;
    }
    output = gpuPostProcess_.Output();
    if (postProcessed != nullptr) {
        *postProcessed = true;
    }
    return true;
}

bool AppWindow::RenderGpuPostProcessPreview(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    ID3D11ShaderResourceView* sourceColor,
    const PreviewPresentationState baseState,
    const std::uint32_t displayedIterations) {
    if (!request.useGpuPostProcessPreview || !setup.allowGpuResolvePasses || sourceColor == nullptr) {
        return SetDirectGpuPreview(request.width, request.height, baseState, displayedIterations, setup.now);
    }
    GpuPassOutput resolvedFrame;
    bool postProcessed = false;
    if (!ResolveGpuPostProcessOutput(
            request.scene,
            request.width,
            request.height,
            GpuPassOutput {.colorSrv = sourceColor},
            resolvedFrame,
            &postProcessed)) {
        return false;
    }
    return SetDirectGpuPreview(
        request.width,
        request.height,
        postProcessed ? WithPreviewPresentationStage(baseState, PreviewRenderStage::PostProcessed) : baseState,
        displayedIterations,
        setup.now);
}

bool AppWindow::RenderGpuDofPreview(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    const GpuFrameInputs& inputs,
    const std::uint32_t displayedIterations) {
    GpuPassOutput dofOutput;
    if (!RunGpuDofPass(request.scene, request.width, request.height, inputs, dofOutput)) {
        return false;
    }
    return RenderGpuPostProcessPreview(
        request,
        setup,
        dofOutput.colorSrv,
        MakePreviewPresentationState(PreviewRenderDevice::Gpu, PreviewContentForMode(request.scene.mode), PreviewRenderStage::DepthOfField),
        displayedIterations);
}

bool AppWindow::RenderGpuPathScene(
    const Scene& scene,
    const int width,
    const int height,
    const bool transparent,
    const bool renderGrid,
    ID3D11ShaderResourceView* flameDepthSrv,
    GpuPathRenderer& renderer,
    const wchar_t* rendererLabel) {
    if (!EnsureGpuPathRendererInitialized(renderer, rendererLabel)) {
        return false;
    }
    return renderer.Render(scene, width, height, transparent, renderGrid, flameDepthSrv);
}

bool AppWindow::RenderGpuFlameScene(
    const Scene& scene,
    const int width,
    const int height,
    const GpuFlameRenderOptions& options) {
    if (!EnsureGpuFlameRendererInitialized()) {
        return false;
    }
    if (options.pumpOverlay) {
        if (exportCancelRequested_ || !PumpExportOverlay()) {
            return false;
        }
    }
    if (!gpuFlameRenderer_.Render(
            scene,
            width,
            height,
            options.iterations,
            options.transparent,
            options.clearAccumulationForFrame,
            options.preserveTemporalState,
            options.resetTemporalState)) {
        return false;
    }
    if (options.pumpOverlay && !PumpExportOverlay()) {
        return false;
    }
    return true;
}

bool AppWindow::RenderGpuFlameBaseLayers(
    const Scene& scene,
    const int width,
    const int height,
    const bool renderBackground,
    const bool backgroundTransparent,
    const bool backgroundGrid,
    const GpuFlameRenderOptions& flameOptions,
    const bool includePath,
    std::uint32_t* displayedIterations) {
    if (displayedIterations != nullptr) {
        *displayedIterations = 0;
    }

    if (renderBackground) {
        Scene backgroundScene = scene;
        backgroundScene.mode = SceneMode::Flame;
        if (!RenderGpuPathScene(
                backgroundScene,
                width,
                height,
                backgroundTransparent,
                backgroundGrid,
                nullptr,
                gpuGridRenderer_,
                L"GPU grid renderer")) {
            return false;
        }
    }

    if (!RenderGpuFlameScene(scene, width, height, flameOptions)) {
        return false;
    }

    if (includePath) {
        Scene pathScene = scene;
        pathScene.mode = SceneMode::Path;
        if (!RenderGpuPathScene(
                pathScene,
                width,
                height,
                true,
                false,
                gpuFlameRenderer_.DepthShaderResourceView(),
                gpuPathRenderer_,
                L"GPU path renderer")) {
            return false;
        }
    }

    if (displayedIterations != nullptr) {
        *displayedIterations = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                gpuFlameRenderer_.AccumulatedIterations(),
                static_cast<std::uint64_t>(flameOptions.iterations)));
    }
    return true;
}

GpuFrameInputs AppWindow::CaptureCurrentGpuFrameInputs(
    const bool includeGrid,
    const bool includeFlame,
    const bool includePath) const {
    return MakeGpuFrameInputs(
        includeGrid ? gpuGridRenderer_.ShaderResourceView() : nullptr,
        includeFlame ? gpuFlameRenderer_.ShaderResourceView() : nullptr,
        includeFlame ? gpuFlameRenderer_.DepthShaderResourceView() : nullptr,
        includePath ? gpuPathRenderer_.ShaderResourceView() : nullptr,
        includePath ? gpuPathRenderer_.DepthShaderResourceView() : nullptr);
}

GpuFrameInputs AppWindow::CaptureGpuFrameInputsForMode(
    const SceneMode mode,
    const bool includeSeparateGridLayer) const {
    return mode == SceneMode::Flame
        ? CaptureCurrentGpuFrameInputs(includeSeparateGridLayer, true, false)
        : mode == SceneMode::Path
            ? CaptureCurrentGpuFrameInputs(false, false, true)
            : CaptureCurrentGpuFrameInputs(includeSeparateGridLayer, true, true);
}

AppWindow::GpuPreviewPipeline AppWindow::BuildGpuPreviewPipeline(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    const GpuFrameInputs& baseInputs,
    ID3D11ShaderResourceView* directSourceColor,
    const PreviewPresentationState directState,
    const PreviewPresentationState composedState,
    const PreviewPresentationState compositeFailureState,
    const std::uint32_t displayedIterations,
    const bool useCompositeFinalize,
    const bool requireCompletedAccumulation) {
    const bool accumulationReady =
        !requireCompletedAccumulation
        || displayedIterations >= request.previewIterations;
    const bool denoiserRendererReady =
        !request.useGpuDenoiserPreview
        || EnsureGpuDenoiserInitialized();
    const bool dofRendererReady =
        !request.useGpuDofPreview
        || EnsureGpuDofRendererInitialized();

    GpuPreviewPipeline pipeline;
    pipeline.baseInputs = baseInputs;
    pipeline.directSourceColor = directSourceColor;
    pipeline.directState = directState;
    pipeline.composedState = composedState;
    pipeline.compositeFailureState = compositeFailureState;
    pipeline.displayedIterations = displayedIterations;
    pipeline.applyDenoiser =
        request.useGpuDenoiserPreview
        && denoiserRendererReady
        && setup.allowGpuResolvePasses
        && accumulationReady;
    pipeline.applyDof =
        request.useGpuDofPreview
        && dofRendererReady
        && setup.allowGpuResolvePasses
        && accumulationReady;
    pipeline.useCompositeFinalize = useCompositeFinalize;
    return pipeline;
}

AppWindow::GpuFailureChecks AppWindow::BuildGpuFailureChecks(
    const SceneMode mode,
    const bool includeSeparateGridLayer) {
    GpuFailureChecks checks;
    checks.flame = mode != SceneMode::Path;
    checks.grid = mode != SceneMode::Path && includeSeparateGridLayer;
    checks.path = mode != SceneMode::Flame;
    return checks;
}

std::wstring AppWindow::BuildGpuFailureStatusMessage(
    const wchar_t* contextLabel,
    const GpuFailureChecks& checks,
    const bool useGpuDenoiser,
    const bool useGpuDof,
    const bool useGpuPostProcess) const {
    if (checks.flame && !gpuFlameRenderer_.LastError().empty()) {
        return std::wstring(L"GPU flame ") + contextLabel + L" failed: " + Utf8ToWide(gpuFlameRenderer_.LastError());
    }
    if (checks.grid && !gpuGridRenderer_.LastError().empty()) {
        return std::wstring(L"GPU grid ") + contextLabel + L" failed: " + Utf8ToWide(gpuGridRenderer_.LastError());
    }
    if (checks.path && !gpuPathRenderer_.LastError().empty()) {
        return std::wstring(L"GPU path ") + contextLabel + L" failed: " + Utf8ToWide(gpuPathRenderer_.LastError());
    }
    if (useGpuDenoiser && !gpuDenoiser_.LastError().empty()) {
        return std::wstring(L"GPU denoiser ") + contextLabel + L" failed: " + Utf8ToWide(gpuDenoiser_.LastError());
    }
    if (useGpuDof && !gpuDofRenderer_.LastError().empty()) {
        return std::wstring(L"GPU DOF ") + contextLabel + L" failed: " + Utf8ToWide(gpuDofRenderer_.LastError());
    }
    if (useGpuDof) {
        return std::wstring(L"GPU DOF ") + contextLabel + L" failed.";
    }
    if (useGpuPostProcess && !gpuPostProcess_.LastError().empty()) {
        return std::wstring(L"GPU post-process ") + contextLabel + L" failed: " + Utf8ToWide(gpuPostProcess_.LastError());
    }
    if (useGpuPostProcess) {
        return std::wstring(L"GPU post-process ") + contextLabel + L" failed.";
    }
    if (useGpuDenoiser) {
        return std::wstring(L"GPU denoiser ") + contextLabel + L" failed.";
    }
    if (checks.path) {
        return std::wstring(L"GPU path ") + contextLabel + L" failed.";
    }
    if (checks.grid) {
        return std::wstring(L"GPU grid ") + contextLabel + L" failed.";
    }
    if (checks.flame) {
        return std::wstring(L"GPU flame ") + contextLabel + L" failed.";
    }
    return {};
}

void AppWindow::PopulateGpuPreviewBaseFrameResult(
    const SceneMode mode,
    const bool includeSeparateGridLayer,
    const std::uint32_t displayedIterations,
    GpuPreviewBaseFrameResult& result) const {
    result.mode = mode;
    result.includeSeparateGridLayer = includeSeparateGridLayer;
    result.baseInputs = CaptureGpuFrameInputsForMode(mode, includeSeparateGridLayer);
    result.displayedIterations = displayedIterations;

    if (mode == SceneMode::Path) {
        result.directSourceColor = gpuPathRenderer_.ShaderResourceView();
        result.directState = MakePreviewPresentationState(PreviewRenderDevice::Gpu, PreviewRenderContent::Path);
        result.composedState = result.directState;
        result.compositeFailureState = result.directState;
        return;
    }

    result.directSourceColor = gpuFlameRenderer_.ShaderResourceView();
    result.composedState = MakePreviewPresentationState(
        PreviewRenderDevice::Gpu,
        PreviewContentForMode(mode),
        PreviewRenderStage::Composited);
    result.requireCompletedAccumulation = true;

    if (mode == SceneMode::Flame) {
        result.directState = MakePreviewPresentationState(
            PreviewRenderDevice::Gpu,
            includeSeparateGridLayer ? PreviewRenderContent::Hybrid : PreviewRenderContent::Flame);
        result.compositeFailureState = MakePreviewPresentationState(PreviewRenderDevice::Gpu, PreviewRenderContent::Flame);
        result.useCompositeFinalize = includeSeparateGridLayer;
        return;
    }

    result.directState = MakePreviewPresentationState(PreviewRenderDevice::Gpu, PreviewRenderContent::Hybrid);
    result.compositeFailureState = result.directState;
    result.useCompositeFinalize = true;
}

bool AppWindow::RenderGpuPreviewBaseLayers(
    const ViewportRenderRequest& request,
    std::uint32_t& displayedIterations,
    bool& includeSeparateGridLayer) {
    includeSeparateGridLayer = false;
    displayedIterations = 0;
    const Scene& renderScene = request.scene;

    if (renderScene.mode == SceneMode::Path) {
        displayedIterations = request.previewIterations;
        return RenderGpuPathScene(
            renderScene,
            request.width,
            request.height,
            false,
            true,
            nullptr,
            gpuPathRenderer_,
            L"GPU path renderer");
    }

    includeSeparateGridLayer = renderScene.gridVisible;
    return RenderGpuPreviewFlameBaseLayers(
        renderScene,
        request.width,
        request.height,
        request.previewIterations,
        renderScene.mode == SceneMode::Hybrid,
        displayedIterations);
}

bool AppWindow::RenderGpuPreviewFlameBaseLayers(
    const Scene& scene,
    const int width,
    const int height,
    const std::uint32_t previewIterations,
    const bool includePath,
    std::uint32_t& displayedIterations) {
    GpuFlameRenderOptions flameOptions;
    flameOptions.iterations = previewIterations;
    flameOptions.transparent = scene.gridVisible;
    return RenderGpuFlameBaseLayers(
        scene,
        width,
        height,
        scene.gridVisible,
        false,
        true,
        flameOptions,
        includePath,
        &displayedIterations);
}

void AppWindow::UpdateGpuPreviewFailureStatus(
    const GpuFailureChecks& checks,
    const bool useGpuDenoiserPreview,
    const bool useGpuDofPreview,
    const bool useGpuPostProcessPreview) {
    const std::wstring status = BuildGpuFailureStatusMessage(
        L"preview",
        checks,
        useGpuDenoiserPreview,
        useGpuDofPreview,
        useGpuPostProcessPreview);
    if (!status.empty()) {
        statusText_ = status;
    }
}

bool AppWindow::RunGpuDenoiserPass(
    const Scene& scene,
    const int width,
    const int height,
    const GpuFrameInputs& inputs,
    GpuPassOutput& output) {
    if (!EnsureGpuDenoiserInitialized()) {
        return false;
    }
    if (!gpuDenoiser_.Render(scene, width, height, inputs)) {
        return false;
    }
    output = gpuDenoiser_.Output();
    return true;
}

bool AppWindow::RunGpuCompositePass(
    const int width,
    const int height,
    const GpuFrameInputs& inputs,
    GpuPassOutput& output) {
    if (!EnsureGpuDenoiserInitialized()) {
        return false;
    }
    if (!gpuDenoiser_.Compose(width, height, inputs)) {
        return false;
    }
    output = gpuDenoiser_.Output();
    return true;
}

bool AppWindow::PrepareGpuEffectChainState(
    const Scene& scene,
    const int width,
    const int height,
    const GpuFrameInputs& baseInputs,
    const bool applyDenoiser,
    const bool resolveFrame,
    GpuEffectChainState& state) {
    state = {};
    state.inputs = baseInputs;
    if (applyDenoiser) {
        if (!RunGpuDenoiserPass(scene, width, height, state.inputs, state.resolvedFrame)) {
            return false;
        }
        state.inputs = {};
        state.inputs.flameColor = state.resolvedFrame.colorSrv;
        state.inputs.flameDepth = state.resolvedFrame.depthSrv;
        state.hasResolvedFrame = true;
        return true;
    }

    if (resolveFrame) {
        if (!RunGpuCompositePass(width, height, state.inputs, state.resolvedFrame)) {
            return false;
        }
        state.hasResolvedFrame = true;
    }
    return true;
}

bool AppWindow::RunGpuDofPass(
    const Scene& scene,
    const int width,
    const int height,
    const GpuFrameInputs& inputs,
    GpuPassOutput& output) {
    if (!EnsureGpuDofRendererInitialized()) {
        return false;
    }
    if (!gpuDofRenderer_.Render(scene, width, height, inputs)) {
        return false;
    }
    output = gpuDofRenderer_.Output();
    return true;
}

bool AppWindow::TryRenderGpuEffectChain(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    const GpuFrameInputs& baseInputs,
    const bool applyDenoiser,
    const bool applyDof,
    const std::uint32_t displayedIterations,
    bool& handled) {
    handled = applyDenoiser || applyDof;
    if (!handled) {
        return false;
    }

    GpuEffectChainState effectState;
    if (!PrepareGpuEffectChainState(
            request.scene,
            request.width,
            request.height,
            baseInputs,
            applyDenoiser,
            false,
            effectState)) {
        return false;
    }

    if (applyDof) {
        return RenderGpuDofPreview(request, setup, effectState.inputs, displayedIterations);
    }

    return SetDirectGpuPreview(
        request.width,
        request.height,
        MakePreviewPresentationState(
            PreviewRenderDevice::Gpu,
            PreviewContentForMode(request.scene.mode),
            PreviewRenderStage::Denoised),
        displayedIterations,
        setup.now);
}

bool AppWindow::ExecuteGpuPreviewPipeline(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    const GpuPreviewPipeline& pipeline) {
    bool effectsHandled = false;
    if (!TryRenderGpuEffectChain(
            request,
            setup,
            pipeline.baseInputs,
            pipeline.applyDenoiser,
            pipeline.applyDof,
            pipeline.displayedIterations,
            effectsHandled)) {
        if (effectsHandled) {
            return false;
        }
    }
    if (effectsHandled) {
        return true;
    }

    return FinalizeGpuPreviewStage(
        request,
        setup,
        pipeline.directSourceColor,
        pipeline.useCompositeFinalize ? &pipeline.baseInputs : nullptr,
        pipeline.directState,
        pipeline.composedState,
        pipeline.compositeFailureState,
        pipeline.displayedIterations);
}

bool AppWindow::ExecuteGpuPreviewPipelineWithFailureStatus(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    const GpuPreviewPipeline& pipeline,
    const GpuFailureChecks& checks) {
    if (ExecuteGpuPreviewPipeline(request, setup, pipeline)) {
        return true;
    }
    UpdateGpuPreviewFailureStatus(
        checks,
        request.useGpuDenoiserPreview,
        request.useGpuDofPreview,
        request.useGpuPostProcessPreview);
    return false;
}

bool AppWindow::FinalizeGpuPreviewStage(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    ID3D11ShaderResourceView* directSourceColor,
    const GpuFrameInputs* compositeInputs,
    const PreviewPresentationState directState,
    const PreviewPresentationState composedState,
    const PreviewPresentationState compositeFailureState,
    const std::uint32_t displayedIterations) {
    if (!request.useGpuPostProcessPreview || !setup.allowGpuResolvePasses) {
        return SetDirectGpuPreview(request.width, request.height, directState, displayedIterations, setup.now);
    }

    if (compositeInputs != nullptr) {
        if (!EnsureGpuDenoiserInitialized() || !RenderGpuCompositePreview(
                request,
                setup,
                *compositeInputs,
                composedState,
                displayedIterations)) {
            return SetDirectGpuPreview(request.width, request.height, compositeFailureState, displayedIterations, setup.now);
        }
        return true;
    }

    return RenderGpuPostProcessPreview(request, setup, directSourceColor, directState, displayedIterations);
}

bool AppWindow::RenderGpuCompositePreview(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    const GpuFrameInputs& inputs,
    const PreviewPresentationState composedState,
    const std::uint32_t displayedIterations) {
    GpuPassOutput composedOutput;
    if (!RunGpuCompositePass(request.width, request.height, inputs, composedOutput)) {
        return false;
    }
    return RenderGpuPostProcessPreview(
        request,
        setup,
        composedOutput.colorSrv,
        composedState,
        displayedIterations);
}

bool AppWindow::BuildGpuPreviewBaseFrameResult(
    const ViewportRenderRequest& request,
    GpuPreviewBaseFrameResult& result) {
    result = {};
    const Scene& renderScene = request.scene;
    const bool useGpuDenoiserPreview = request.useGpuDenoiserPreview;
    const bool useGpuDofPreview = request.useGpuDofPreview;
    const bool useGpuPostProcessPreview = request.useGpuPostProcessPreview;
    bool includeSeparateGridLayer = false;

    if (renderScene.mode == SceneMode::Flame || renderScene.mode == SceneMode::Path || renderScene.mode == SceneMode::Hybrid) {
        if (RenderGpuPreviewBaseLayers(request, result.displayedIterations, includeSeparateGridLayer)) {
            PopulateGpuPreviewBaseFrameResult(
                renderScene.mode,
                includeSeparateGridLayer,
                result.displayedIterations,
                result);
            return true;
        }
    }

    const GpuFailureChecks checks = BuildGpuFailureChecks(renderScene.mode, renderScene.gridVisible);
    UpdateGpuPreviewFailureStatus(
        checks,
        useGpuDenoiserPreview,
        useGpuDofPreview,
        useGpuPostProcessPreview);
    return false;
}

bool AppWindow::ExecuteGpuPreviewBaseFrameResult(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup,
    const GpuPreviewBaseFrameResult& result) {
    const GpuPreviewPipeline pipeline = BuildGpuPreviewPipeline(
        request,
        setup,
        result.baseInputs,
        result.directSourceColor,
        result.directState,
        result.composedState,
        result.compositeFailureState,
        result.displayedIterations,
        result.useCompositeFinalize,
        result.requireCompletedAccumulation);
    const GpuFailureChecks checks = BuildGpuFailureChecks(
        result.mode,
        result.includeSeparateGridLayer);
    return ExecuteGpuPreviewPipelineWithFailureStatus(
        request,
        setup,
        pipeline,
        checks);
}

bool AppWindow::RenderGpuViewportPreview(const ViewportRenderRequest& request, const ViewportRenderSetup& setup) {
    GpuPreviewBaseFrameResult result;
    if (!BuildGpuPreviewBaseFrameResult(request, result)) {
        return false;
    }
    return ExecuteGpuPreviewBaseFrameResult(request, setup, result);
}

void AppWindow::ExecuteViewportRenderRequest(
    const ViewportRenderRequest& request,
    const ViewportRenderSetup& setup) {
    if (request.useGpuViewportPreview && RenderGpuViewportPreview(request, setup)) {
        return;
    }

    if (asyncViewportRendering_) {
        QueueCpuViewportPreview(request);
        return;
    }

    RenderCpuViewportPreview(request);
}

bool AppWindow::QueueCpuViewportPreview(const ViewportRenderRequest& request) {
    cpuPreviewWorker_.Enqueue(MakeCpuPreviewRequest(request));
    UpdatePreviewTargetIterations(request.previewIterations);
    QueuePreviewPresentation(request.cpuPreviewState);
    return true;
}

CpuPreviewWorker::Request AppWindow::MakeCpuPreviewRequest(const ViewportRenderRequest& request) {
    CpuPreviewWorker::Request cpuRequest;
    cpuRequest.scene = request.scene;
    cpuRequest.width = request.width;
    cpuRequest.height = request.height;
    cpuRequest.previewIterations = request.previewIterations;
    cpuRequest.interactive = request.interactive;
    cpuRequest.presentation = request.cpuPreviewState;
    return cpuRequest;
}

bool AppWindow::PresentViewportPixels(
    const int width,
    const int height,
    const PreviewPresentationState state,
    const std::uint32_t displayedIterations) {
    if (!renderBackend_ || !renderBackend_->UploadCpuPreview(width, height, viewportPixels_)) {
        return false;
    }

    ApplyPresentedPreviewState(state, displayedIterations, previewProgress_.pendingResetReason);
    RecordPreviewUpdate();
    return true;
}

BackendPreviewSurfaceSet AppWindow::CollectPreviewSurfaces() const {
    BackendPreviewSurfaceSet surfaces;
    surfaces.cpu = renderBackend_ ? renderBackend_->CpuPreviewShaderResourceView() : nullptr;
    surfaces.flame = gpuFlameRenderer_.ShaderResourceView();
    surfaces.grid = gpuGridRenderer_.ShaderResourceView();
    surfaces.path = gpuPathRenderer_.ShaderResourceView();
    surfaces.denoised = gpuDenoiser_.ShaderResourceView();
    surfaces.depthOfField = gpuDofRenderer_.ShaderResourceView();
    surfaces.postProcessed = gpuPostProcess_.ShaderResourceView();
    return surfaces;
}

bool AppWindow::RenderCpuViewportPreview(const ViewportRenderRequest& request) {
    if (!renderer_.RenderViewport(request.scene, request.width, request.height, viewportPixels_)) {
        return false;
    }
    if (!PresentViewportPixels(
            request.width,
            request.height,
            request.cpuPreviewState,
            request.scene.previewIterations)) {
        return false;
    }
    return true;
}

void AppWindow::RecordPreviewUpdate() {
    const auto now = std::chrono::steady_clock::now();
    if (lastPreviewUpdate_ != std::chrono::steady_clock::time_point {}) {
        const double deltaSeconds = std::chrono::duration<double>(now - lastPreviewUpdate_).count();
        if (deltaSeconds > 0.0) {
            previewFpsSmoothed_ = previewFpsSmoothed_ * 0.92 + (1.0 / deltaSeconds) * 0.08;
        }
    }
    lastPreviewUpdate_ = now;
}

bool AppWindow::IsViewportDirty() const {
    return viewportDirty_ || previewProgress_.dirty;
}

void AppWindow::UpdatePreviewTargetIterations(const std::uint32_t targetIterations) {
    previewProgress_.targetIterations = targetIterations;
}

void AppWindow::QueuePreviewPresentation(const PreviewPresentationState state) {
    previewProgress_.presentation = state;
    previewProgress_.displayedIterations = 0;
    previewProgress_.dirty = false;
    previewProgress_.phase = PreviewProgressPhase::Queued;
    previewProgress_.temporalStateValid.reset();
    viewportDirty_ = false;
}

void AppWindow::ApplyPresentedPreviewState(
    const PreviewPresentationState state,
    const std::uint32_t displayedIterations,
    const std::optional<PreviewResetReason> appliedResetReason,
    const std::optional<bool> temporalStateValid,
    const std::optional<PreviewProgressPhase> phase) {
    previewProgress_.presentation = state;
    previewProgress_.displayedIterations = displayedIterations;
    previewProgress_.dirty = false;
    if (appliedResetReason.has_value() && *appliedResetReason != PreviewResetReason::None) {
        previewProgress_.appliedResetReason = *appliedResetReason;
        previewProgress_.pendingResetReason = PreviewResetReason::None;
    }
    if (temporalStateValid.has_value()) {
        previewProgress_.temporalStateValid = *temporalStateValid;
    } else if (!IsGpuPreviewPresentationState(state)) {
        previewProgress_.temporalStateValid.reset();
    }
    if (phase.has_value()) {
        previewProgress_.phase = *phase;
    } else if (previewProgress_.targetIterations > 0 && displayedIterations < previewProgress_.targetIterations) {
        previewProgress_.phase = PreviewProgressPhase::Accumulating;
    } else {
        previewProgress_.phase = PreviewProgressPhase::Complete;
    }
    viewportDirty_ = false;
}

void AppWindow::SyncGpuPreviewProgress(
    const PreviewPresentationState state,
    const std::uint32_t targetIterations,
    const GpuFlameRenderer::StatusSnapshot& snapshot) {
    previewProgress_.presentation = state;
    previewProgress_.targetIterations = targetIterations;
    previewProgress_.displayedIterations = static_cast<std::uint32_t>(std::min<std::uint64_t>(snapshot.accumulatedIterations, targetIterations));
    previewProgress_.temporalStateValid = snapshot.temporalStateValid;
    if (!snapshot.accumulationValid) {
        previewProgress_.phase = PreviewProgressPhase::Dirty;
    } else if (previewProgress_.displayedIterations < targetIterations) {
        previewProgress_.phase = PreviewProgressPhase::Accumulating;
    } else {
        previewProgress_.phase = PreviewProgressPhase::Complete;
    }
}

void AppWindow::ConsumeCompletedRender() {
    std::optional<CpuPreviewWorker::CompletedFrame> completed = cpuPreviewWorker_.ConsumeCompletedFrame();
    if (!completed.has_value()) {
        return;
    }

    viewportPixels_ = std::move(completed->pixels);
    if (PresentViewportPixels(completed->width, completed->height, completed->presentation, completed->previewIterations)) {
        viewportDirty_ = false;
    }
}

void AppWindow::StartRenderThread() {
    cpuPreviewWorker_.Start();
}

void AppWindow::StopRenderThread() {
    cpuPreviewWorker_.Stop();
}

void AppWindow::RenderViewportIfNeeded(const int width, const int height) {
    ViewportRenderRequest request = BuildViewportRenderRequest(width, height, interactivePreview_, gpuFlamePreviewEnabled_);
    UpdatePreviewTargetIterations(request.previewIterations);
    const ViewportRenderSetup setup = DetermineViewportRenderSetup(request);
    if (ShouldSkipViewportRender(setup)) {
        return;
    }
    ExecuteViewportRenderRequest(request, setup);
}

void AppWindow::HandleViewportInteraction(const bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* currentWindow = ImGui::GetCurrentWindow();
    const bool otherWindowHovered =
        g.HoveredWindow != nullptr
        && g.HoveredWindow != currentWindow;
    const bool widgetOwnsMouse =
        io.WantCaptureMouse
        && g.ActiveId != 0
        && g.ActiveIdWindow != nullptr
        && g.ActiveIdWindow != currentWindow;
    const bool uiCapturingMouse =
        widgetOwnsMouse
        || otherWindowHovered;
    if (uiCapturingMouse) {
        if (interactivePreview_) {
            interactivePreview_ = false;
            MarkViewportDirty(PreviewResetReason::IterationChanged);
        }
        viewportInteractionCaptured_ = false;
        return;
    }
    const bool interacting = hovered && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || io.MouseWheel != 0.0f);
    if (interactivePreview_ != interacting) {
        interactivePreview_ = interacting;
        MarkViewportDirty(PreviewResetReason::IterationChanged);
    }
    if (!interacting) {
        viewportInteractionCaptured_ = false;
    }

    if (!hovered) {
        return;
    }

    const auto captureViewportUndo = [&]() {
        if (!viewportInteractionCaptured_) {
            PushUndoState(scene_);
            viewportInteractionCaptured_ = true;
        }
    };

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        captureViewportUndo();
        scene_.camera.yaw += io.MouseDelta.x * 0.01;
        scene_.camera.pitch = std::clamp(scene_.camera.pitch + io.MouseDelta.y * 0.01, -1.45, 1.45);
        AutoKeyCurrentFrame();
        MarkViewportDirty(PreviewResetReason::CameraChanged);
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        captureViewportUndo();
        scene_.camera.panX += io.MouseDelta.x;
        scene_.camera.panY += io.MouseDelta.y;
        AutoKeyCurrentFrame();
        MarkViewportDirty(PreviewResetReason::CameraChanged);
    }
    if (io.MouseWheel != 0.0f) {
        captureViewportUndo();
        constexpr double kMinCameraDistance = 0.05;
        constexpr double kMinCameraZoom = 0.2;
        const double zoomFactor = io.MouseWheel > 0.0f ? 1.08 : 0.92;
        scene_.camera.zoom2D = std::max(kMinCameraZoom, scene_.camera.zoom2D * zoomFactor);
        scene_.camera.distance = std::max(kMinCameraDistance, scene_.camera.distance * (io.MouseWheel > 0.0f ? 0.94 : 1.06));
        AutoKeyCurrentFrame();
        MarkViewportDirty(PreviewResetReason::CameraChanged);
    }
    if (IsViewportDirty()) {
        SyncCurrentKeyframeFromScene();
    }
}

}  // namespace radiary
