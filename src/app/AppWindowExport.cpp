#include "app/AppWindow.h"
#include "app/CameraUtils.h"
#include "app/ExportUtils.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace radiary;

namespace {

constexpr std::uint32_t kExportStablePostProcessSeed = 0x5EED1234u;

std::wstring Utf8ToWideFilename(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (wideLength <= 1) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), wideLength);
    wide.pop_back();
    return wide;
}

std::wstring BuildDefaultSceneFilename(const radiary::Scene& scene) {
    std::wstring filename = Utf8ToWideFilename(scene.name);
    for (wchar_t& ch : filename) {
        switch (ch) {
        case L'<':
        case L'>':
        case L':':
        case L'"':
        case L'/':
        case L'\\':
        case L'|':
        case L'?':
        case L'*':
            ch = L'_';
            break;
        default:
            if (ch < 32) {
                ch = L'_';
            }
            break;
        }
    }

    const auto isTrimmedChar = [](const wchar_t ch) {
        return ch == L' ' || ch == L'.';
    };
    while (!filename.empty() && isTrimmedChar(filename.front())) {
        filename.erase(filename.begin());
    }
    while (!filename.empty() && isTrimmedChar(filename.back())) {
        filename.pop_back();
    }
    if (filename.empty()) {
        filename = L"scene";
    }
    return filename + L".radiary";
}

void WriteFourCc(std::ostream& stream, const char fourCc[4]) {
    stream.write(fourCc, 4);
}

void WriteU16(std::ostream& stream, const std::uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu)
    };
    stream.write(bytes, 2);
}

void WriteU32(std::ostream& stream, const std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu),
        static_cast<char>((value >> 16u) & 0xFFu),
        static_cast<char>((value >> 24u) & 0xFFu)
    };
    stream.write(bytes, 4);
}

void WriteS16(std::ostream& stream, const std::int16_t value) {
    WriteU16(stream, static_cast<std::uint16_t>(value));
}

void WriteS32(std::ostream& stream, const std::int32_t value) {
    WriteU32(stream, static_cast<std::uint32_t>(value));
}

}  // namespace

namespace radiary {
std::uint32_t AppWindow::CurrentPreviewSampleBaseline() const {
    std::uint32_t baseline = previewProgress_.displayedIterations;
    if (baseline == 0) {
        baseline = previewProgress_.targetIterations;
    }
    if (baseline == 0) {
        baseline = scene_.previewIterations;
    }
    return std::max<std::uint32_t>(baseline, 1u);
}

std::uint32_t AppWindow::CurrentExportDensityMatchedBaseline() const {
    const std::uint32_t previewBaseline = CurrentPreviewSampleBaseline();
    const int previewWidth = std::max(1, UploadedViewportWidth());
    const int previewHeight = std::max(1, UploadedViewportHeight());
    const int exportWidth = std::max(1, exportWidth_);
    const int exportHeight = std::max(1, exportHeight_);

    const ImVec2 previewFrameSize = FitCameraFrameToBounds(scene_.camera, static_cast<float>(previewWidth), static_cast<float>(previewHeight));
    const ImVec2 exportFrameSize = FitCameraFrameToBounds(scene_.camera, static_cast<float>(exportWidth), static_cast<float>(exportHeight));
    const double previewFrameArea = std::max(1.0, static_cast<double>(std::max(1.0f, previewFrameSize.x)) * static_cast<double>(std::max(1.0f, previewFrameSize.y)));
    const double exportFrameArea = std::max(1.0, static_cast<double>(std::max(1.0f, exportFrameSize.x)) * static_cast<double>(std::max(1.0f, exportFrameSize.y)));
    const double densityScale = exportFrameArea / previewFrameArea;
    const std::uint64_t scaled = static_cast<std::uint64_t>(std::llround(static_cast<double>(previewBaseline) * densityScale));
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(scaled, 1u, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
}

std::uint32_t AppWindow::CurrentExportIterationCount() const {
    constexpr std::uint32_t kMinExportIterations = 20000u;
    constexpr std::uint32_t kMaxExportIterations = std::numeric_limits<std::uint32_t>::max();
    const double scaled = static_cast<double>(CurrentExportDensityMatchedBaseline()) * static_cast<double>(exportIterationScale_);
    const auto rounded = static_cast<std::uint64_t>(std::llround(scaled));
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(rounded, kMinExportIterations, kMaxExportIterations));
}

bool AppWindow::SaveSceneToDialog(const bool saveAs) {
    std::filesystem::path path = currentScenePath_;
    if (saveAs || path.empty()) {
        wchar_t fileBuffer[MAX_PATH] = L"";
        const std::wstring defaultFilename = BuildDefaultSceneFilename(scene_);
        wcsncpy_s(fileBuffer, defaultFilename.c_str(), _TRUNCATE);
        OPENFILENAMEW dialog {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"Radiary Scene (*.radiary)\0*.radiary\0\0";
        dialog.lpstrFile = fileBuffer;
        dialog.nMaxFile = MAX_PATH;
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        dialog.lpstrDefExt = L"radiary";
        if (!GetSaveFileNameW(&dialog)) {
            return false;
        }
        path = fileBuffer;
    }

    std::string error;
    if (!serializer_.Save(scene_, path, error)) {
        statusText_ = L"Save failed";
        return false;
    }

    currentScenePath_ = path;
    sceneDirty_ = false;
    sceneModifiedSinceAutoSave_ = false;
    lastAutoSave_ = std::chrono::steady_clock::now();
    std::error_code removeError;
    std::filesystem::remove(AutoSavePath(), removeError);
    UpdateWindowTitle();
    statusText_ = L"Saved " + path.filename().wstring();
    return true;
}

bool AppWindow::LoadSceneFromDialog() {
    wchar_t fileBuffer[MAX_PATH] = L"";
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Radiary Scene (*.radiary)\0*.radiary\0\0";
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = L"radiary";
    if (!GetOpenFileNameW(&dialog)) {
        return false;
    }

    std::string error;
    if (auto scene = serializer_.Load(fileBuffer, error)) {
        ResetScene(*scene);
        currentScenePath_ = fileBuffer;
        sceneDirty_ = false;
        sceneModifiedSinceAutoSave_ = false;
        lastAutoSave_ = std::chrono::steady_clock::now();
        UpdateWindowTitle();
        statusText_ = L"Opened " + currentScenePath_.filename().wstring();
        return true;
    }

    statusText_ = error.empty() ? L"Open failed" : L"Open failed: " + Utf8ToWide(error);
    return false;
}

void AppWindow::OpenExportPanel() {
    const std::vector<ExportResolutionPreset> presets = BuildExportResolutionPresets(scene_.camera);
    if (!presets.empty()) {
        const std::size_t defaultPresetIndex = presets.size() > 1 ? 1 : 0;
        exportWidth_ = presets[defaultPresetIndex].width;
        exportHeight_ = presets[defaultPresetIndex].height;
    } else {
        exportWidth_ = 1920;
        exportHeight_ = 1080;
        ConstrainExportResolutionToCamera(scene_.camera, exportWidth_, exportHeight_, true);
    }
    exportIterationScale_ = 1.0f;
    exportIterations_ = CurrentExportIterationCount();
    exportFrameStart_ = scene_.timelineStartFrame;
    exportFrameEnd_ = scene_.timelineEndFrame;
    exportPanelOpen_ = true;
}

bool AppWindow::ExportViewportToDialog() {
    const bool exportPng = exportFormat_ == ExportFormat::Png || exportFormat_ == ExportFormat::PngSequence;
    const bool exportJpeg = exportFormat_ == ExportFormat::Jpeg || exportFormat_ == ExportFormat::JpegSequence;
    const bool exportAvi = exportFormat_ == ExportFormat::Avi;
    const bool exportMp4 = exportFormat_ == ExportFormat::Mp4;
    const bool exportMov = exportFormat_ == ExportFormat::Mov;
    wchar_t fileBuffer[MAX_PATH] = L"radiary-export.png";
    if (exportJpeg) {
        wcscpy_s(fileBuffer, L"radiary-export.jpg");
    } else if (exportAvi) {
        wcscpy_s(fileBuffer, L"radiary-export.avi");
    } else if (exportMp4) {
        wcscpy_s(fileBuffer, L"radiary-export.mp4");
    } else if (exportMov) {
        wcscpy_s(fileBuffer, L"radiary-export.mov");
    }
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = exportAvi
        ? L"AVI Video (*.avi)\0*.avi\0\0"
        : exportMp4
            ? L"MP4 Video (*.mp4)\0*.mp4\0\0"
            : exportMov
                ? L"MOV Video (*.mov)\0*.mov\0\0"
                : exportPng
            ? L"PNG Image (*.png)\0*.png\0\0"
            : L"JPEG Image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = exportAvi ? L"avi" : exportMp4 ? L"mp4" : exportMov ? L"mov" : exportPng ? L"png" : L"jpg";
    if (!GetSaveFileNameW(&dialog)) {
        return false;
    }
    pendingExportRequest_.path = fileBuffer;
    pendingExportRequest_.width = exportWidth_;
    pendingExportRequest_.height = exportHeight_;
    exportIterations_ = CurrentExportIterationCount();
    pendingExportRequest_.iterations = exportIterations_;
    pendingExportRequest_.transparentBackground = exportPng && exportTransparentBackground_;
    pendingExportRequest_.hideGrid = exportHideGrid_;
    pendingExportRequest_.useGpu = exportUseGpu_;
    pendingExportRequest_.format = exportFormat_;
    pendingExportRequest_.startFrame = exportFrameStart_;
    pendingExportRequest_.endFrame = exportFrameEnd_;
    exportRequestPending_ = true;
    statusText_ = L"Preparing export...";
    return true;
}

bool AppWindow::RenderSceneToPixels(
    const Scene& sourceScene,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    const bool useGpu,
    std::vector<std::uint32_t>& pixels,
    const ExportRenderState* renderState) {
    return useGpu
        ? RenderSceneToPixelsGpu(sourceScene, width, height, iterations, transparentBackground, hideGrid, pixels, renderState)
        : RenderSceneToPixelsCpu(sourceScene, width, height, iterations, transparentBackground, hideGrid, pixels, renderState);
}

namespace {

std::uint32_t ResolveGpuExportFlameIterations(const radiary::Scene& scene) {
    return std::max<std::uint32_t>(scene.previewIterations, 1u);
}

}  // namespace

bool AppWindow::RenderSceneToPixelsCpu(
    const Scene& sourceScene,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    std::vector<std::uint32_t>& pixels,
    const ExportRenderState* renderState) {
    Scene exportScene = BuildExportRenderScene(sourceScene, width, height, hideGrid);
    exportScene.previewIterations = std::max<std::uint32_t>(iterations, 1u);
    SoftwareRenderer localRenderer;
    SoftwareRenderer& exportRenderer = renderState != nullptr && renderState->cpuRenderer != nullptr
        ? *renderState->cpuRenderer
        : localRenderer;
    if (renderState != nullptr && renderState->resetTemporalFlameState) {
        exportRenderer.InvalidateAccumulation();
    }
    SoftwareRenderer::RenderOptions renderOptions;
    renderOptions.renderFlame = true;
    renderOptions.renderPaths = true;
    renderOptions.renderGrid = !hideGrid;
    renderOptions.transparentBackground = transparentBackground;
    renderOptions.preserveFlameState = renderState != nullptr && renderState->preserveTemporalFlameState;
    renderOptions.shouldAbort = [this]() {
        return exportCancelRequested_ || !PumpExportOverlay();
    };
    return exportRenderer.RenderViewport(exportScene, std::max(1, width), std::max(1, height), pixels, renderOptions)
        && !pixels.empty();
}

void AppWindow::InvalidateExportViewportPreview() {
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    if (renderBackend_) {
        renderBackend_->ResetCpuPreviewSurface();
    }
}

AppWindow::GpuExportRenderPlan AppWindow::BuildGpuExportRenderPlan(
    const Scene& sourceScene,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    const ExportRenderState* renderState) const {
    GpuExportRenderPlan plan;
    plan.scene = BuildExportRenderScene(sourceScene, width, height, hideGrid);
    plan.scene.previewIterations = std::max<std::uint32_t>(iterations, 1u);
    plan.width = std::max(1, width);
    plan.height = std::max(1, height);
    plan.transparentBackground = transparentBackground;
    plan.exportGrid = !hideGrid && plan.scene.gridVisible;
    plan.content = PreviewContentForMode(plan.scene.mode);
    plan.outputStage = DetermineResolvedRenderStage(plan.scene);
    plan.renderState = renderState;
    return plan;
}

bool AppWindow::UsesSeparateGpuExportGridLayer(const GpuExportRenderPlan& plan) {
    return plan.scene.mode == SceneMode::Flame
        ? plan.exportGrid
        : plan.scene.mode == SceneMode::Hybrid
            ? plan.exportGrid || !plan.transparentBackground
            : false;
}

bool AppWindow::PumpExportOverlay() {
    if (!exportInProgress_) {
        return true;
    }
    if (!PresentBlockingOverlay()) {
        return false;
    }
    return !exportCancelRequested_;
}

AppWindow::GpuFlameRenderOptions AppWindow::MakeExportGpuFlameRenderOptions(
    const Scene& scene,
    const bool transparent,
    const ExportRenderState* renderState) const {
    GpuFlameRenderOptions options;
    options.iterations = ResolveGpuExportFlameIterations(scene);
    options.transparent = transparent;
    options.clearAccumulationForFrame = true;
    options.preserveTemporalState =
        renderState != nullptr
        && renderState->preserveTemporalFlameState;
    options.resetTemporalState =
        renderState != nullptr
        && renderState->resetTemporalFlameState;
    options.pumpOverlay = true;
    return options;
}

bool AppWindow::RenderGpuExportFlameBaseLayers(
    const Scene& exportScene,
    const int width,
    const int height,
    const bool transparentBackground,
    const bool exportGrid,
    const ExportRenderState* renderState) {
    const GpuFlameRenderOptions flameOptions = MakeExportGpuFlameRenderOptions(
        exportScene,
        transparentBackground || exportGrid,
        renderState);
    return RenderGpuFlameBaseLayers(
        exportScene,
        width,
        height,
        exportGrid,
        transparentBackground,
        true,
        flameOptions,
        false);
}

bool AppWindow::RenderGpuExportHybridBaseLayers(
    const Scene& exportScene,
    const int width,
    const int height,
    const bool transparentBackground,
    const bool exportGrid,
    const ExportRenderState* renderState) {
    const GpuFlameRenderOptions flameOptions = MakeExportGpuFlameRenderOptions(exportScene, true, renderState);
    return RenderGpuFlameBaseLayers(
        exportScene,
        width,
        height,
        !transparentBackground || exportGrid,
        transparentBackground,
        exportGrid,
        flameOptions,
        true);
}

bool AppWindow::RenderGpuExportBaseLayers(
    const GpuExportRenderPlan& plan,
    bool& includeSeparateGridLayer) {
    includeSeparateGridLayer = false;
    const Scene& exportScene = plan.scene;

    if (exportScene.mode == SceneMode::Path) {
        return RenderGpuPathScene(
            exportScene,
            plan.width,
            plan.height,
            plan.transparentBackground,
            plan.exportGrid,
            nullptr,
            gpuPathRenderer_,
            L"GPU path renderer");
    }

    if (exportScene.mode == SceneMode::Flame) {
        includeSeparateGridLayer = plan.exportGrid;
        return RenderGpuExportFlameBaseLayers(
            exportScene,
            plan.width,
            plan.height,
            plan.transparentBackground,
            plan.exportGrid,
            plan.renderState);
    }

    includeSeparateGridLayer = plan.exportGrid || !plan.transparentBackground;
    return RenderGpuExportHybridBaseLayers(
        exportScene,
        plan.width,
        plan.height,
        plan.transparentBackground,
        plan.exportGrid,
        plan.renderState);
}

bool AppWindow::BuildGpuExportBaseFrameResult(
    const GpuExportRenderPlan& plan,
    GpuExportBaseFrameResult& result) {
    result = {};
    bool includeSeparateGridLayer = false;
    if (!RenderGpuExportBaseLayers(plan, includeSeparateGridLayer)) {
        return false;
    }
    result.baseInputs = CaptureGpuFrameInputsForMode(plan.scene.mode, includeSeparateGridLayer);
    return true;
}

bool AppWindow::ReadGpuExportBaseFramePixels(
    const GpuFrameInputs& inputs,
    const int width,
    const int height,
    const bool transparentBackground,
    std::vector<std::uint32_t>& output) const {
    output.clear();

    if (inputs.HasGrid()) {
        if (!renderBackend_ || !renderBackend_->ReadbackColorTexture(gpuGridRenderer_.OutputTexture(), output)) {
            return false;
        }
    } else if (transparentBackground && (inputs.HasFlame() || inputs.HasPath())) {
        output.assign(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
            PackBgra(0u, 0u, 0u, 0u));
    }

    auto compositeLayer = [&](ID3D11Texture2D* texture) {
        std::vector<std::uint32_t> layerPixels;
        if (!renderBackend_ || !renderBackend_->ReadbackColorTexture(texture, layerPixels)) {
            return false;
        }
        if (output.empty()) {
            output = std::move(layerPixels);
        } else {
            CompositePixelsOver(output, layerPixels);
        }
        return true;
    };

    if (inputs.HasFlame() && !compositeLayer(gpuFlameRenderer_.OutputTexture())) {
        return false;
    }
    if (inputs.HasPath() && !compositeLayer(gpuPathRenderer_.OutputTexture())) {
        return false;
    }
    return !output.empty();
}

bool AppWindow::ApplyGpuExportPostProcessAndReadback(
    const Scene& exportScene,
    const int width,
    const int height,
    const GpuPassOutput& frame,
    std::vector<std::uint32_t>& output) {
    GpuPassOutput resolvedFrame;
    if (!ResolveGpuPostProcessOutput(
            exportScene,
            width,
            height,
            frame,
            resolvedFrame,
            nullptr,
            kExportStablePostProcessSeed)) {
        return false;
    }
    return renderBackend_ != nullptr && renderBackend_->ReadbackColorTexture(resolvedFrame.colorTexture, output);
}

bool AppWindow::ReadGpuExportEffectPixels(
    const Scene& exportScene,
    const int width,
    const int height,
    const GpuFrameInputs& baseInputs,
    std::vector<std::uint32_t>& output) {
    const bool hasActiveEffects = std::any_of(exportScene.effectStack.begin(), exportScene.effectStack.end(), [&](const EffectStackStage stage) {
        return IsEffectStageEnabled(exportScene, stage);
    });
    GpuEffectChainState effectState;
    if (!PrepareGpuEffectChainState(
            exportScene,
            width,
            height,
            baseInputs,
            exportScene.denoiser.enabled,
            hasActiveEffects,
            effectState)) {
        return false;
    }

    if (!PumpExportOverlay()) {
        return false;
    }

    if (!effectState.hasResolvedFrame) {
        return false;
    }
    return renderBackend_ != nullptr && renderBackend_->ReadbackColorTexture(effectState.resolvedFrame.colorTexture, output);
}

void AppWindow::UpdateGpuExportFailureStatus(const GpuExportRenderPlan& plan) {
    const GpuFailureChecks checks = BuildGpuFailureChecks(
        plan.scene.mode,
        UsesSeparateGpuExportGridLayer(plan));
    const std::wstring status = BuildGpuFailureStatusMessage(
        L"export",
        checks,
        plan.scene.denoiser.enabled,
        plan.scene.depthOfField.enabled,
        HasActivePostProcess(plan.scene.postProcess));
    if (!status.empty()) {
        statusText_ = status;
    }
}

bool AppWindow::ExecuteGpuExportBaseFrameResult(
    const GpuExportRenderPlan& plan,
    const GpuExportBaseFrameResult& result,
    std::vector<std::uint32_t>& pixels) {
    if (plan.outputStage != PreviewRenderStage::Base) {
        return ReadGpuExportEffectPixels(
            plan.scene,
            plan.width,
            plan.height,
            result.baseInputs,
            pixels);
    }

    return ReadGpuExportBaseFramePixels(
        result.baseInputs,
        plan.width,
        plan.height,
        plan.transparentBackground,
        pixels);
}

bool AppWindow::ExecuteGpuExportRenderPlan(
    const GpuExportRenderPlan& plan,
    std::vector<std::uint32_t>& pixels) {
    GpuExportBaseFrameResult baseFrame;
    if (!BuildGpuExportBaseFrameResult(plan, baseFrame)) {
        return false;
    }
    return ExecuteGpuExportBaseFrameResult(plan, baseFrame, pixels);
}

bool AppWindow::RenderSceneToPixelsGpu(
    const Scene& sourceScene,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    std::vector<std::uint32_t>& pixels,
    const ExportRenderState* renderState) {
    pixels.clear();
    if (device_ == nullptr || deviceContext_ == nullptr) {
        InvalidateExportViewportPreview();
        return false;
    }
    const GpuExportRenderPlan plan = BuildGpuExportRenderPlan(
        sourceScene,
        width,
        height,
        iterations,
        transparentBackground,
        hideGrid,
        renderState);
    const bool success = ExecuteGpuExportRenderPlan(plan, pixels);
    if (!success) {
        UpdateGpuExportFailureStatus(plan);
    }

    InvalidateExportViewportPreview();
    return success && !pixels.empty();
}

bool AppWindow::RenderExportSceneWithGpuFallback(
    const Scene& renderScene,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    bool& useGpuRender,
    std::vector<std::uint32_t>& pixels,
    const ExportRenderState* renderState) {
    pixels.clear();
    if (useGpuRender && !RenderSceneToPixels(renderScene, width, height, iterations, transparentBackground, hideGrid, true, pixels, renderState)) {
        const std::wstring gpuFailureStatus = statusText_;
        useGpuRender = false;
        const PreviewPresentationState exportState = MakePreviewPresentationState(
            PreviewRenderDevice::Gpu,
            PreviewContentForMode(renderScene.mode),
            DetermineResolvedRenderStage(renderScene));
        statusText_ = BuildGpuFallbackStatusMessage(exportState, gpuFailureStatus);
    }
    if (!useGpuRender && !RenderSceneToPixels(renderScene, width, height, iterations, transparentBackground, hideGrid, false, pixels, renderState)) {
        statusText_ = L"Export failed";
        return false;
    }
    return true;
}

std::wstring AppWindow::BuildExportInterruptedStatus() const {
    return exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
}

void AppWindow::SetExportInterruptedStatus() {
    statusText_ = BuildExportInterruptedStatus();
}

bool AppWindow::RenderAnimatedExportFrame(
    const int frame,
    const int frameIndex,
    const int frameCount,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    bool& useGpuRender,
    SoftwareRenderer& cpuRenderer,
    std::vector<std::uint32_t>& pixels) {
    const Scene frameScene = EvaluateSceneAtTimelineFrame(static_cast<double>(frame));
    const ExportRenderState renderState {
        exportStableFlameSampling_ && frameCount > 1,
        exportStableFlameSampling_ && frameIndex == 0,
        &cpuRenderer
    };
    return RenderExportSceneWithGpuFallback(
        frameScene,
        width,
        height,
        iterations,
        transparentBackground,
        hideGrid,
        useGpuRender,
        pixels,
        &renderState);
}

bool AppWindow::ExportViewportImage(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    const bool useGpu,
    const ExportFormat format) {
    exportProgressTitle_ = L"Exporting image";
    if (!UpdateExportProgress(0.08f, L"Rendering " + path.filename().wstring())) {
        SetExportInterruptedStatus();
        return false;
    }
    const Scene exportScene = EvaluateSceneAtTimelineFrame(scene_.timelineFrame);
    std::vector<std::uint32_t> exportPixels;
    bool useGpuRender = useGpu;
    if (!RenderExportSceneWithGpuFallback(
            exportScene,
            width,
            height,
            iterations,
            transparentBackground,
            hideGrid,
            useGpuRender,
            exportPixels)) {
        return false;
    }
    if (!UpdateExportProgress(0.78f, L"Writing " + path.filename().wstring())) {
        SetExportInterruptedStatus();
        return false;
    }
    const int exportWidth = std::max(1, width);
    const int exportHeight = std::max(1, height);
    const bool success = SavePixelsToImageFile(path, exportPixels, exportWidth, exportHeight, format == ExportFormat::Jpeg || format == ExportFormat::JpegSequence);
    if (success) {
        UpdateExportProgress(1.0f, L"Finished " + path.filename().wstring());
    }
    statusText_ = success ? L"Exported " + path.filename().wstring() : L"Export failed";
    return success;
}

bool AppWindow::ExportImageSequence(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    const bool useGpu,
    const ExportFormat format,
    const int startFrame,
    const int endFrame) {
    const bool jpeg = format == ExportFormat::JpegSequence;
    const std::filesystem::path directory = path.parent_path().empty() ? std::filesystem::current_path() : path.parent_path();
    const std::wstring stem = path.stem().wstring();
    const int frameCount = std::max(1, endFrame - startFrame + 1);
    exportProgressTitle_ = L"Exporting image sequence";
    if (!UpdateExportProgress(0.0f, L"Preparing " + std::to_wstring(frameCount) + L" frames")) {
        SetExportInterruptedStatus();
        return false;
    }
    bool useGpuRender = useGpu;
    SoftwareRenderer cpuSequenceRenderer;
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        const int frameIndex = frame - startFrame;
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex) / static_cast<float>(frameCount),
                L"Rendering frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            SetExportInterruptedStatus();
            return false;
        }
        std::vector<std::uint32_t> pixels;
        if (!RenderAnimatedExportFrame(
                frame,
                frameIndex,
                frameCount,
                width,
                height,
                iterations,
                transparentBackground,
                hideGrid,
                useGpuRender,
                cpuSequenceRenderer,
                pixels)) {
            return false;
        }
        std::wstringstream filename;
        filename << stem << L"_" << std::setw(5) << std::setfill(L'0') << frame << (jpeg ? L".jpg" : L".png");
        if (!SavePixelsToImageFile(directory / filename.str(), pixels, std::max(1, width), std::max(1, height), jpeg)) {
            statusText_ = L"Export failed";
            return false;
        }
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex + 1) / static_cast<float>(frameCount),
                L"Saved frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            SetExportInterruptedStatus();
            return false;
        }
    }
    statusText_ = L"Exported image sequence";
    return true;
}

bool AppWindow::ExportAviVideo(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool hideGrid,
    const bool useGpu,
    const int startFrame,
    const int endFrame) {
    const int exportWidth = std::max(1, width);
    const int exportHeight = std::max(1, height);
    const int frameCount = std::max(1, endFrame - startFrame + 1);
    const std::uint32_t frameSize = static_cast<std::uint32_t>(exportWidth * exportHeight * 4);
    const std::uint32_t moviDataSize = static_cast<std::uint32_t>(frameCount) * (8u + frameSize);
    const std::uint32_t indexSize = static_cast<std::uint32_t>(frameCount) * 16u;
    constexpr std::uint32_t hdrlListSize = 192u;
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        statusText_ = L"Export failed";
        return false;
    }

    const std::uint32_t microsecondsPerFrame = static_cast<std::uint32_t>(std::round(1000000.0 / std::max(1.0, scene_.timelineFrameRate)));
    const std::uint32_t rate = static_cast<std::uint32_t>(std::round(scene_.timelineFrameRate * 1000.0));
    const std::uint32_t scale = 1000u;
    exportProgressTitle_ = L"Exporting video";
    if (!UpdateExportProgress(0.0f, L"Preparing AVI stream")) {
        SetExportInterruptedStatus();
        return false;
    }

    WriteFourCc(stream, "RIFF");
    WriteU32(stream, 4u + (8u + hdrlListSize) + (8u + 4u + moviDataSize) + (8u + indexSize));
    WriteFourCc(stream, "AVI ");
    WriteFourCc(stream, "LIST");
    WriteU32(stream, hdrlListSize);
    WriteFourCc(stream, "hdrl");
    WriteFourCc(stream, "avih");
    WriteU32(stream, 56u);
    WriteU32(stream, microsecondsPerFrame);
    WriteU32(stream, frameSize * static_cast<std::uint32_t>(std::max(1.0, scene_.timelineFrameRate)));
    WriteU32(stream, 0u);
    WriteU32(stream, 0x10u);
    WriteU32(stream, static_cast<std::uint32_t>(frameCount));
    WriteU32(stream, 0u);
    WriteU32(stream, 1u);
    WriteU32(stream, frameSize);
    WriteU32(stream, static_cast<std::uint32_t>(exportWidth));
    WriteU32(stream, static_cast<std::uint32_t>(exportHeight));
    for (int index = 0; index < 4; ++index) {
        WriteU32(stream, 0u);
    }
    WriteFourCc(stream, "LIST");
    WriteU32(stream, 116u);
    WriteFourCc(stream, "strl");
    WriteFourCc(stream, "strh");
    WriteU32(stream, 56u);
    WriteFourCc(stream, "vids");
    WriteFourCc(stream, "DIB ");
    WriteU32(stream, 0u);
    WriteU16(stream, 0u);
    WriteU16(stream, 0u);
    WriteU32(stream, 0u);
    WriteU32(stream, scale);
    WriteU32(stream, rate);
    WriteU32(stream, 0u);
    WriteU32(stream, static_cast<std::uint32_t>(frameCount));
    WriteU32(stream, frameSize);
    WriteU32(stream, 0xFFFFFFFFu);
    WriteU32(stream, 0u);
    WriteS16(stream, 0);
    WriteS16(stream, 0);
    WriteS16(stream, static_cast<std::int16_t>(std::clamp(exportWidth, 0, 32767)));
    WriteS16(stream, static_cast<std::int16_t>(std::clamp(exportHeight, 0, 32767)));
    WriteFourCc(stream, "strf");
    WriteU32(stream, 40u);
    WriteU32(stream, 40u);
    WriteS32(stream, exportWidth);
    WriteS32(stream, -exportHeight);
    WriteU16(stream, 1u);
    WriteU16(stream, 32u);
    WriteU32(stream, 0u);
    WriteU32(stream, frameSize);
    WriteS32(stream, 0);
    WriteS32(stream, 0);
    WriteU32(stream, 0u);
    WriteU32(stream, 0u);
    WriteFourCc(stream, "LIST");
    WriteU32(stream, 4u + moviDataSize);
    WriteFourCc(stream, "movi");
    const std::uint32_t moviListOffsetBase = static_cast<std::uint32_t>(stream.tellp()) - 4u;

    std::vector<std::uint32_t> chunkOffsets;
    chunkOffsets.reserve(static_cast<std::size_t>(frameCount));
    bool useGpuRender = useGpu;
    SoftwareRenderer cpuSequenceRenderer;
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        const int frameIndex = frame - startFrame;
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex) / static_cast<float>(frameCount),
                L"Rendering frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            SetExportInterruptedStatus();
            return false;
        }
        chunkOffsets.push_back(static_cast<std::uint32_t>(stream.tellp()) - moviListOffsetBase);
        std::vector<std::uint32_t> pixels;
        if (!RenderAnimatedExportFrame(
                frame,
                frameIndex,
                frameCount,
                exportWidth,
                exportHeight,
                iterations,
                false,
                hideGrid,
                useGpuRender,
                cpuSequenceRenderer,
                pixels)) {
            return false;
        }
        WriteFourCc(stream, "00db");
        WriteU32(stream, frameSize);
        stream.write(reinterpret_cast<const char*>(pixels.data()), frameSize);
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex + 1) / static_cast<float>(frameCount),
                L"Wrote frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            SetExportInterruptedStatus();
            return false;
        }
    }

    WriteFourCc(stream, "idx1");
    WriteU32(stream, indexSize);
    for (std::size_t index = 0; index < chunkOffsets.size(); ++index) {
        WriteFourCc(stream, "00db");
        WriteU32(stream, 0x10u);
        WriteU32(stream, chunkOffsets[index]);
        WriteU32(stream, frameSize);
    }

    UpdateExportProgress(1.0f, L"Finalized " + path.filename().wstring());
    statusText_ = L"Exported " + path.filename().wstring();
    return true;
}

bool AppWindow::ExportFfmpegVideo(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool hideGrid,
    const bool useGpu,
    const int startFrame,
    const int endFrame,
    const ExportFormat format) {
    const int exportWidth = std::max(1, width);
    const int exportHeight = std::max(1, height);
    const int frameCount = std::max(1, endFrame - startFrame + 1);
    const std::uint32_t frameSize = static_cast<std::uint32_t>(exportWidth * exportHeight * 4);
    const std::wstring fpsText = std::to_wstring(std::max(1.0, scene_.timelineFrameRate));
    const std::filesystem::path ffmpegPath = FindBundledFfmpegPath();
    const std::filesystem::path ffmpegLogPath = std::filesystem::temp_directory_path() / L"radiary-ffmpeg.log";
    const std::wstring codecArgs = format == ExportFormat::Mov
        ? L"-c:v libx264 -pix_fmt yuv420p"
        : L"-c:v libx264 -pix_fmt yuv420p";
    const std::wstring command =
        (ffmpegPath.empty() ? L"ffmpeg" : L"\"" + ffmpegPath.wstring() + L"\"")
        + L" -hide_banner -loglevel error"
        + L" -y -f rawvideo -pix_fmt bgra -s "
        + std::to_wstring(exportWidth) + L"x" + std::to_wstring(exportHeight)
        + L" -r " + fpsText
        + L" -i pipe:0 -an "
        + codecArgs
        + L" \"" + path.wstring() + L"\"";

    const auto ffmpegErrorDetail = [&]() {
        const std::wstring logText = ReadTextFile(ffmpegLogPath);
        std::error_code removeError;
        std::filesystem::remove(ffmpegLogPath, removeError);
        if (logText.empty()) {
            return std::wstring {};
        }

        std::wstring firstLine = logText;
        const std::size_t newline = firstLine.find_first_of(L"\r\n");
        if (newline != std::wstring::npos) {
            firstLine.resize(newline);
        }
        return firstLine;
    };
    SECURITY_ATTRIBUTES securityAttributes {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &securityAttributes, 0)) {
        statusText_ = L"Unable to create FFmpeg pipe";
        return false;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    HANDLE logHandle = CreateFileW(
        ffmpegLogPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &securityAttributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (logHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(stdinRead);
        CloseHandle(stdinWrite);
        statusText_ = L"Unable to create FFmpeg log";
        return false;
    }

    STARTUPINFOW startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinRead;
    startupInfo.hStdOutput = logHandle;
    startupInfo.hStdError = logHandle;

    PROCESS_INFORMATION processInfo {};
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    const BOOL created = CreateProcessW(
        nullptr,
        commandBuffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    CloseHandle(stdinRead);
    stdinRead = nullptr;
    if (!created) {
        CloseHandle(stdinWrite);
        CloseHandle(logHandle);
        statusText_ = ffmpegPath.empty() ? L"FFmpeg not found" : L"Bundled FFmpeg launch failed";
        ffmpegErrorDetail();
        return false;
    }

    const auto finishFfmpegProcess = [&](const bool terminate, DWORD* exitCode) {
        if (stdinWrite != nullptr) {
            CloseHandle(stdinWrite);
            stdinWrite = nullptr;
        }
        if (terminate) {
            TerminateProcess(processInfo.hProcess, 1u);
        }
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        if (exitCode != nullptr) {
            *exitCode = 1u;
            GetExitCodeProcess(processInfo.hProcess, exitCode);
        }
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        CloseHandle(logHandle);
    };

    exportProgressTitle_ = L"Exporting video";
    if (!UpdateExportProgress(0.0f, L"Streaming frames to FFmpeg")) {
        DWORD exitCode = 1u;
        finishFfmpegProcess(true, &exitCode);
        SetExportInterruptedStatus();
        std::error_code cleanupError;
        std::filesystem::remove(path, cleanupError);
        return false;
    }

    bool useGpuRender = useGpu;
    SoftwareRenderer cpuSequenceRenderer;
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        const int frameIndex = frame - startFrame;
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex) / static_cast<float>(frameCount),
                L"Rendering frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            DWORD exitCode = 1u;
            finishFfmpegProcess(true, &exitCode);
            SetExportInterruptedStatus();
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            ffmpegErrorDetail();
            return false;
        }
        std::vector<std::uint32_t> pixels;
        if (!RenderAnimatedExportFrame(
                frame,
                frameIndex,
                frameCount,
                exportWidth,
                exportHeight,
                iterations,
                false,
                hideGrid,
                useGpuRender,
                cpuSequenceRenderer,
                pixels)) {
            DWORD exitCode = 1u;
            finishFfmpegProcess(true, &exitCode);
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            ffmpegErrorDetail();
            return false;
        }
        DWORD bytesWritten = 0u;
        if (!WriteFile(stdinWrite, pixels.data(), frameSize, &bytesWritten, nullptr) || bytesWritten != frameSize) {
            DWORD exitCode = 1u;
            finishFfmpegProcess(false, &exitCode);
            const std::wstring errorDetail = ffmpegErrorDetail();
            statusText_ = errorDetail.empty()
                ? L"FFmpeg write failed"
                : L"FFmpeg error: " + errorDetail;
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            return false;
        }
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex + 1) / static_cast<float>(frameCount),
                L"Encoded frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            DWORD exitCode = 1u;
            finishFfmpegProcess(true, &exitCode);
            SetExportInterruptedStatus();
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            ffmpegErrorDetail();
            return false;
        }
    }

    DWORD ffmpegExitCode = 0u;
    finishFfmpegProcess(false, &ffmpegExitCode);
    if (ffmpegExitCode != 0u) {
        const std::wstring errorDetail = ffmpegErrorDetail();
        statusText_ = errorDetail.empty() ? L"FFmpeg export failed" : L"FFmpeg error: " + errorDetail;
        std::error_code cleanupError;
        std::filesystem::remove(path, cleanupError);
        return false;
    }

    ffmpegErrorDetail();
    UpdateExportProgress(1.0f, L"Finalized " + path.filename().wstring());
    statusText_ = L"Exported " + path.filename().wstring();
    return true;
}


}  // namespace radiary
