#include "app/AppWindow.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

#include "imgui.h"
#include "imgui_internal.h"

using namespace radiary;

namespace {

constexpr auto kInteractiveGpuPreviewCadence = std::chrono::milliseconds(8);
constexpr auto kUiBusyGpuPreviewCadence = std::chrono::milliseconds(32);
constexpr auto kSettledGpuPreviewCadence = std::chrono::milliseconds(12);

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
void AppWindow::MarkViewportDirty() {
    viewportDirty_ = true;
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

void AppWindow::QueueViewportRender(const int width, const int height, const bool interactive) {
    Scene renderScene = BuildRenderableScene(EvaluateSceneAtFrame(scene_, scene_.timelineFrame));
    std::uint32_t previewIterations = ViewportPreviewIterations(
        renderScene,
        interactive,
        false,
        adaptiveInteractivePreview_,
        interactivePreviewIterations_);
    PreviewBackend previewBackend = PreviewBackend::CpuHybrid;
    renderScene.previewIterations = previewIterations;
    if (renderScene.mode == SceneMode::Flame) {
        previewBackend = PreviewBackend::CpuFlame;
    } else if (renderScene.mode == SceneMode::Path) {
        previewBackend = PreviewBackend::CpuPath;
    }

    {
        std::lock_guard<std::mutex> lock(renderMutex_);
        pendingRenderScene_ = std::move(renderScene);
        pendingRenderWidth_ = width;
        pendingRenderHeight_ = height;
        pendingRenderPreviewIterations_ = previewIterations;
        pendingRenderBackend_ = previewBackend;
        ++pendingRenderGeneration_;
        renderRequestPending_ = true;
    }
    renderCv_.notify_one();
}

void AppWindow::ConsumeCompletedRender() {
    int width = 0;
    int height = 0;
    std::uint32_t previewIterations = 0;
    PreviewBackend previewBackend = PreviewBackend::CpuHybrid;

    {
        std::lock_guard<std::mutex> lock(renderMutex_);
        if (completedRenderGeneration_ == consumedRenderGeneration_ || completedRenderPixels_.empty()) {
            return;
        }

        width = completedRenderWidth_;
        height = completedRenderHeight_;
        previewIterations = completedRenderPreviewIterations_;
        previewBackend = completedRenderBackend_;
        consumedRenderGeneration_ = completedRenderGeneration_;
        viewportPixels_ = std::move(completedRenderPixels_);
    }

    if (!EnsureViewportTexture(width, height)) {
        return;
    }
    displayedPreviewIterations_ = previewIterations;
    displayedPreviewBackend_ = previewBackend;
    D3D11_BOX box {};
    box.left = 0;
    box.top = 0;
    box.front = 0;
    box.right = static_cast<UINT>(width);
    box.bottom = static_cast<UINT>(height);
    box.back = 1;
    deviceContext_->UpdateSubresource(viewportTexture_.Get(), 0, &box, viewportPixels_.data(), width * 4, 0);
}

void AppWindow::StartRenderThread() {
    renderThreadExit_ = false;
    renderThread_ = std::thread(&AppWindow::RenderThreadMain, this);
}

void AppWindow::StopRenderThread() {
    {
        std::lock_guard<std::mutex> lock(renderMutex_);
        renderThreadExit_ = true;
    }
    renderCv_.notify_one();
    if (renderThread_.joinable()) {
        renderThread_.join();
    }
}

void AppWindow::RenderThreadMain() {
    SoftwareRenderer workerRenderer;

    for (;;) {
        Scene renderScene;
        int width = 0;
        int height = 0;
        std::uint32_t previewIterations = 0;
        PreviewBackend previewBackend = PreviewBackend::CpuHybrid;
        std::uint64_t generation = 0;

        {
            std::unique_lock<std::mutex> lock(renderMutex_);
            renderCv_.wait(lock, [&]() { return renderThreadExit_ || renderRequestPending_; });
            if (renderThreadExit_) {
                return;
            }

            renderScene = std::move(pendingRenderScene_);
            width = pendingRenderWidth_;
            height = pendingRenderHeight_;
            previewIterations = pendingRenderPreviewIterations_;
            previewBackend = pendingRenderBackend_;
            generation = pendingRenderGeneration_;
            renderRequestPending_ = false;
            renderInProgress_ = true;
        }

        std::vector<std::uint32_t> pixels;
        SoftwareRenderer::RenderOptions options;
        options.interactive = interactivePreview_;
        options.shouldAbort = [this, generation]() {
            std::lock_guard<std::mutex> lock(renderMutex_);
            return renderThreadExit_ || pendingRenderGeneration_ > generation;
        };
        const bool renderCompleted = workerRenderer.RenderViewport(renderScene, width, height, pixels, options);

        {
            std::lock_guard<std::mutex> lock(renderMutex_);
            renderInProgress_ = false;
            if (!renderCompleted) {
                continue;
            }
            completedRenderPixels_ = std::move(pixels);
            completedRenderWidth_ = width;
            completedRenderHeight_ = height;
            completedRenderPreviewIterations_ = previewIterations;
            completedRenderBackend_ = previewBackend;
            completedRenderGeneration_ = generation;
        }
    }
}

void AppWindow::RenderViewportIfNeeded(const int width, const int height) {
    const int targetWidth = width;
    const int targetHeight = height;
    Scene renderScene = BuildRenderableScene(EvaluateSceneAtFrame(scene_, scene_.timelineFrame));
    const bool useGpuViewportPreview = gpuFlamePreviewEnabled_;
    const bool useGpuDofPreview = useGpuViewportPreview && renderScene.depthOfField.enabled;
    const bool useGpuDenoiserPreview = useGpuViewportPreview && renderScene.denoiser.enabled;
    const bool useGpuPostProcessPreview = useGpuViewportPreview && renderScene.postProcess.enabled;
    std::uint32_t previewIterations = ViewportPreviewIterations(
        renderScene,
        interactivePreview_,
        useGpuViewportPreview,
        adaptiveInteractivePreview_,
        interactivePreviewIterations_);
    renderScene.previewIterations = previewIterations;

    const bool sizeChanged = uploadedViewportWidth_ != targetWidth || uploadedViewportHeight_ != targetHeight;
    const bool gpuAccumulationIncomplete =
        useGpuViewportPreview
        && renderScene.mode != SceneMode::Path
        && gpuFlameRenderer_.AccumulatedIterations() < previewIterations;
    const auto now = std::chrono::steady_clock::now();
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext& imgui = *ImGui::GetCurrentContext();
    const bool uiBusy =
        io.WantTextInput
        || layersPanelActive_
        || inspectorPanelActive_
        || playbackPanelActive_
        || (imgui.ActiveId != 0 && !viewportInteractionCaptured_);
    const auto gpuPreviewCadence = uiBusy
        ? kUiBusyGpuPreviewCadence
        : interactivePreview_ ? kInteractiveGpuPreviewCadence : kSettledGpuPreviewCadence;
    const bool throttleGpuAccumulation =
        useGpuViewportPreview
        && gpuAccumulationIncomplete
        && !sizeChanged
        && (lastGpuPreviewDispatchAt_ != std::chrono::steady_clock::time_point {})
        && (now - lastGpuPreviewDispatchAt_) < gpuPreviewCadence;
    const bool needsGpuDofResolve =
        useGpuDofPreview
        && !interactivePreview_
        && !gpuAccumulationIncomplete
        && (displayedPreviewBackend_ != PreviewBackend::GpuDof && displayedPreviewBackend_ != PreviewBackend::GpuDenoised && displayedPreviewBackend_ != PreviewBackend::GpuPostProcessed);
    const bool needsGpuDenoiserResolve =
        useGpuDenoiserPreview
        && !interactivePreview_
        && !gpuAccumulationIncomplete
        && (displayedPreviewBackend_ != PreviewBackend::GpuDenoised && displayedPreviewBackend_ != PreviewBackend::GpuPostProcessed);
    const bool needsGpuPostProcessResolve =
        useGpuPostProcessPreview
        && !gpuAccumulationIncomplete
        && displayedPreviewBackend_ != PreviewBackend::GpuPostProcessed;
    if ((!viewportDirty_ && !sizeChanged && !gpuAccumulationIncomplete && !needsGpuDofResolve && !needsGpuDenoiserResolve && !needsGpuPostProcessResolve) || throttleGpuAccumulation) {
        return;
    }

    if (useGpuViewportPreview) {
        const auto setDirectGpuPreview = [&](const PreviewBackend backend, const std::uint32_t displayedIterations) {
            uploadedViewportWidth_ = targetWidth;
            uploadedViewportHeight_ = targetHeight;
            displayedPreviewIterations_ = displayedIterations;
            displayedPreviewBackend_ = backend;
            lastGpuPreviewDispatchAt_ = now;
            viewportDirty_ = false;
            return true;
        };

        const auto renderGpuPostProcessPreview = [&](ID3D11ShaderResourceView* srv, const PreviewBackend baseBackend, const std::uint32_t displayedIterations) {
            if (!useGpuPostProcessPreview || srv == nullptr) {
                return setDirectGpuPreview(baseBackend, displayedIterations);
            }
            if (!EnsureGpuPostProcessInitialized()) {
                return setDirectGpuPreview(baseBackend, displayedIterations);
            }
            if (!gpuPostProcess_.Render(renderScene, targetWidth, targetHeight, srv)) {
                return setDirectGpuPreview(baseBackend, displayedIterations);
            }
            return setDirectGpuPreview(PreviewBackend::GpuPostProcessed, displayedIterations);
        };

        const auto renderGpuDofPreview = [&](
                                              ID3D11ShaderResourceView* gridSrv,
                                              ID3D11ShaderResourceView* flameSrv,
                                              ID3D11ShaderResourceView* flameDepthSrv,
                                              ID3D11ShaderResourceView* pathSrv,
                                              ID3D11ShaderResourceView* pathDepthSrv,
                                              const std::uint32_t displayedIterations) {
            if (!gpuDofRenderer_.Render(renderScene, targetWidth, targetHeight, gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv)) {
                return false;
            }
            return renderGpuPostProcessPreview(gpuDofRenderer_.ShaderResourceView(), PreviewBackend::GpuDof, displayedIterations);
        };

        const auto renderGpuDenoiserPreview = [&](
            ID3D11ShaderResourceView* gridSrv,
            ID3D11ShaderResourceView* flameSrv,
            ID3D11ShaderResourceView* flameDepthSrv,
            ID3D11ShaderResourceView* pathSrv,
            ID3D11ShaderResourceView* pathDepthSrv,
            const std::uint32_t displayedIterations) {
            if (!gpuDenoiser_.Render(renderScene, targetWidth, targetHeight, gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv)) {
                return false;
            }
            return renderGpuPostProcessPreview(gpuDenoiser_.ShaderResourceView(), PreviewBackend::GpuDenoised, displayedIterations);
        };

        if (renderScene.mode == SceneMode::Flame) {
            const bool flameRendererReady = EnsureGpuFlameRendererInitialized();
            const bool gridRendererReady = !renderScene.gridVisible || EnsureGpuPathRendererInitialized(gpuGridRenderer_, L"GPU grid renderer");
            const bool dofRendererReady = !useGpuDofPreview || EnsureGpuDofRendererInitialized();
            const bool flameTransparentBackground = renderScene.gridVisible;
            const bool flameOk = flameRendererReady && gpuFlameRenderer_.Render(
                renderScene,
                targetWidth,
                targetHeight,
                previewIterations,
                flameTransparentBackground);
            const bool gridOk = flameOk && gridRendererReady && (!renderScene.gridVisible || gpuGridRenderer_.Render(renderScene, targetWidth, targetHeight, false, true));
            if (flameOk && gridOk) {
                const std::uint32_t displayedIterations = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(gpuFlameRenderer_.AccumulatedIterations(), static_cast<std::uint64_t>(previewIterations)));
                
                const bool denoiserRendererReady = !useGpuDenoiserPreview || EnsureGpuDenoiserInitialized();
                const bool applyDenoiser = useGpuDenoiserPreview && denoiserRendererReady && !interactivePreview_ && displayedIterations >= previewIterations;
                const bool applyDof = useGpuDofPreview && dofRendererReady && !interactivePreview_ && displayedIterations >= previewIterations;

                if (applyDenoiser || applyDof) {
                    ID3D11ShaderResourceView* flameSrv = gpuFlameRenderer_.ShaderResourceView();
                    ID3D11ShaderResourceView* flameDepthSrv = gpuFlameRenderer_.DepthShaderResourceView();
                    
                    if (applyDenoiser) {
                        if (!gpuDenoiser_.Render(
                                renderScene,
                                targetWidth,
                                targetHeight,
                                renderScene.gridVisible ? gpuGridRenderer_.ShaderResourceView() : nullptr,
                                flameSrv,
                                flameDepthSrv,
                                nullptr,
                                nullptr)) {
                            return;
                        }
                        flameSrv = gpuDenoiser_.ShaderResourceView();
                        flameDepthSrv = gpuDenoiser_.DepthShaderResourceView();
                    }

                    if (applyDof) {
                        if (renderGpuDofPreview(
                                applyDenoiser ? nullptr : (renderScene.gridVisible ? gpuGridRenderer_.ShaderResourceView() : nullptr),
                                flameSrv,
                                flameDepthSrv,
                                nullptr,
                                nullptr,
                                displayedIterations)) {
                            return;
                        }
                    } else {
                        setDirectGpuPreview(PreviewBackend::GpuDenoised, displayedIterations);
                        return;
                    }
                } else if (useGpuPostProcessPreview) {
                    if (renderScene.gridVisible) {
                        if (!EnsureGpuDenoiserInitialized() || !gpuDenoiser_.Render(
                            renderScene,
                            targetWidth,
                            targetHeight,
                            gpuGridRenderer_.ShaderResourceView(),
                            gpuFlameRenderer_.ShaderResourceView(),
                            gpuFlameRenderer_.DepthShaderResourceView(),
                            nullptr,
                            nullptr)) {
                            setDirectGpuPreview(PreviewBackend::GpuFlame, displayedIterations);
                            return;
                        }
                        renderGpuPostProcessPreview(gpuDenoiser_.ShaderResourceView(), PreviewBackend::GpuDenoised, displayedIterations);
                        return;
                    } else {
                        renderGpuPostProcessPreview(gpuFlameRenderer_.ShaderResourceView(), PreviewBackend::GpuFlame, displayedIterations);
                        return;
                    }
                } else {
                    setDirectGpuPreview(renderScene.gridVisible ? PreviewBackend::GpuHybrid : PreviewBackend::GpuFlame, displayedIterations);
                    return;
                }
            }
            if (!flameRendererReady && !gpuFlameRenderer_.LastError().empty()) {
                statusText_ = L"GPU flame preview failed: " + Utf8ToWide(gpuFlameRenderer_.LastError());
            } else if (!gpuFlameRenderer_.LastError().empty()) {
                statusText_ = L"GPU flame preview failed: " + Utf8ToWide(gpuFlameRenderer_.LastError());
            } else if (!gridOk && !gpuGridRenderer_.LastError().empty()) {
                statusText_ = L"GPU grid preview failed: " + Utf8ToWide(gpuGridRenderer_.LastError());
            } else if (useGpuDofPreview && !gpuDofRenderer_.LastError().empty()) {
                statusText_ = L"GPU DOF preview failed: " + Utf8ToWide(gpuDofRenderer_.LastError());
            } else if (useGpuDofPreview) {
                statusText_ = L"GPU DOF preview failed; falling back to CPU preview.";
            } else if (useGpuPostProcessPreview && !gpuPostProcess_.LastError().empty()) {
                statusText_ = L"GPU post-process preview failed: " + Utf8ToWide(gpuPostProcess_.LastError());
            } else if (useGpuPostProcessPreview && !gpuPostProcess_.LastError().empty()) {
                statusText_ = L"GPU post-process preview failed: " + Utf8ToWide(gpuPostProcess_.LastError());
            }
        } else if (renderScene.mode == SceneMode::Path) {
            const bool pathRendererReady = EnsureGpuPathRendererInitialized(gpuPathRenderer_, L"GPU path renderer");
            const bool denoiserRendererReady = !useGpuDenoiserPreview || EnsureGpuDenoiserInitialized();
            const bool dofRendererReady = !useGpuDofPreview || EnsureGpuDofRendererInitialized();
            if (pathRendererReady && gpuPathRenderer_.Render(renderScene, targetWidth, targetHeight, false, true)) {
                const bool applyDenoiser = useGpuDenoiserPreview && denoiserRendererReady && !interactivePreview_;
                const bool applyDof = useGpuDofPreview && dofRendererReady && !interactivePreview_;
                if (applyDenoiser || applyDof) {
                    ID3D11ShaderResourceView* flameSrv = nullptr;
                    ID3D11ShaderResourceView* flameDepthSrv = nullptr;
                    ID3D11ShaderResourceView* pathSrv = gpuPathRenderer_.ShaderResourceView();
                    ID3D11ShaderResourceView* pathDepthSrv = gpuPathRenderer_.DepthShaderResourceView();

                    if (applyDenoiser) {
                        if (!gpuDenoiser_.Render(
                                renderScene,
                                targetWidth,
                                targetHeight,
                                nullptr,
                                nullptr,
                                nullptr,
                                pathSrv,
                                pathDepthSrv)) {
                            return;
                        }
                        flameSrv = gpuDenoiser_.ShaderResourceView();
                        flameDepthSrv = gpuDenoiser_.DepthShaderResourceView();
                        pathSrv = nullptr;
                        pathDepthSrv = nullptr;
                    }

                    if (applyDof) {
                        if (renderGpuDofPreview(
                                nullptr,
                                flameSrv,
                                flameDepthSrv,
                                pathSrv,
                                pathDepthSrv,
                                previewIterations)) {
                            return;
                        }
                    } else {
                        setDirectGpuPreview(PreviewBackend::GpuDenoised, previewIterations);
                        return;
                    }
                } else if (useGpuPostProcessPreview) {
                    renderGpuPostProcessPreview(gpuPathRenderer_.ShaderResourceView(), PreviewBackend::GpuPath, previewIterations);
                    return;
                } else {
                    setDirectGpuPreview(PreviewBackend::GpuPath, previewIterations);
                    return;
                }
            }
            if (!pathRendererReady && !gpuPathRenderer_.LastError().empty()) {
                statusText_ = L"GPU path preview failed: " + Utf8ToWide(gpuPathRenderer_.LastError());
            } else if (!gpuPathRenderer_.LastError().empty()) {
                statusText_ = L"GPU path preview failed: " + Utf8ToWide(gpuPathRenderer_.LastError());
            } else if (useGpuDenoiserPreview && !gpuDenoiser_.LastError().empty()) {
                statusText_ = L"GPU denoiser preview failed: " + Utf8ToWide(gpuDenoiser_.LastError());
            } else if (useGpuDofPreview && !gpuDofRenderer_.LastError().empty()) {
                statusText_ = L"GPU DOF preview failed: " + Utf8ToWide(gpuDofRenderer_.LastError());
            } else if (useGpuPostProcessPreview && !gpuPostProcess_.LastError().empty()) {
                statusText_ = L"GPU post-process preview failed: " + Utf8ToWide(gpuPostProcess_.LastError());
            }
        } else {
            const bool flameRendererReady = EnsureGpuFlameRendererInitialized();
            const bool gridRendererReady = !renderScene.gridVisible || EnsureGpuPathRendererInitialized(gpuGridRenderer_, L"GPU grid renderer");
            const bool pathRendererReady = EnsureGpuPathRendererInitialized(gpuPathRenderer_, L"GPU path renderer");
            const bool dofRendererReady = !useGpuDofPreview || EnsureGpuDofRendererInitialized();
            const bool flameTransparentBackground = renderScene.gridVisible;
            const bool flameOk = flameRendererReady && gpuFlameRenderer_.Render(
                renderScene,
                targetWidth,
                targetHeight,
                previewIterations,
                flameTransparentBackground);
            Scene gridScene = renderScene;
            gridScene.mode = SceneMode::Flame;
            const bool gridOk = flameOk && gridRendererReady && (!renderScene.gridVisible || gpuGridRenderer_.Render(gridScene, targetWidth, targetHeight, false, true));
            Scene pathScene = renderScene;
            pathScene.mode = SceneMode::Path;
            const bool pathOk = flameOk && pathRendererReady && gpuPathRenderer_.Render(
                pathScene,
                targetWidth,
                targetHeight,
                true,
                false,
                gpuFlameRenderer_.DepthShaderResourceView());
            if (flameOk && gridOk && pathOk) {
                const std::uint32_t displayedIterations = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(gpuFlameRenderer_.AccumulatedIterations(), static_cast<std::uint64_t>(previewIterations)));
                
                const bool denoiserRendererReady = !useGpuDenoiserPreview || EnsureGpuDenoiserInitialized();
                const bool applyDenoiser = useGpuDenoiserPreview && denoiserRendererReady && !interactivePreview_ && displayedIterations >= previewIterations;
                const bool applyDof = useGpuDofPreview && dofRendererReady && !interactivePreview_ && displayedIterations >= previewIterations;

                if (applyDenoiser || applyDof) {
                    ID3D11ShaderResourceView* flameSrv = gpuFlameRenderer_.ShaderResourceView();
                    ID3D11ShaderResourceView* flameDepthSrv = gpuFlameRenderer_.DepthShaderResourceView();
                    
                    if (applyDenoiser) {
                        if (!gpuDenoiser_.Render(
                                renderScene,
                                targetWidth,
                                targetHeight,
                                renderScene.gridVisible ? gpuGridRenderer_.ShaderResourceView() : nullptr,
                                flameSrv,
                                flameDepthSrv,
                                gpuPathRenderer_.ShaderResourceView(),
                                gpuPathRenderer_.DepthShaderResourceView())) {
                            return;
                        }
                        flameSrv = gpuDenoiser_.ShaderResourceView();
                        flameDepthSrv = gpuDenoiser_.DepthShaderResourceView();
                    }

                    if (applyDof) {
                        if (renderGpuDofPreview(
                                applyDenoiser ? nullptr : (renderScene.gridVisible ? gpuGridRenderer_.ShaderResourceView() : nullptr),
                                flameSrv,
                                flameDepthSrv,
                                applyDenoiser ? nullptr : gpuPathRenderer_.ShaderResourceView(),
                                applyDenoiser ? nullptr : gpuPathRenderer_.DepthShaderResourceView(),
                                displayedIterations)) {
                            return;
                        }
                    } else {
                        setDirectGpuPreview(PreviewBackend::GpuDenoised, displayedIterations);
                        return;
                    }
                } else if (useGpuPostProcessPreview) {
                    if (!EnsureGpuDenoiserInitialized() || !gpuDenoiser_.Render(
                        renderScene,
                        targetWidth,
                        targetHeight,
                        renderScene.gridVisible ? gpuGridRenderer_.ShaderResourceView() : nullptr,
                        gpuFlameRenderer_.ShaderResourceView(),
                        gpuFlameRenderer_.DepthShaderResourceView(),
                        gpuPathRenderer_.ShaderResourceView(),
                        gpuPathRenderer_.DepthShaderResourceView())) {
                        setDirectGpuPreview(PreviewBackend::GpuHybrid, displayedIterations);
                        return;
                    }
                    renderGpuPostProcessPreview(gpuDenoiser_.ShaderResourceView(), PreviewBackend::GpuDenoised, displayedIterations);
                    return;
                } else {
                    setDirectGpuPreview(PreviewBackend::GpuHybrid, displayedIterations);
                    return;
                }
            }
            if ((!flameRendererReady || !flameOk) && !gpuFlameRenderer_.LastError().empty()) {
                statusText_ = L"GPU flame preview failed: " + Utf8ToWide(gpuFlameRenderer_.LastError());
            } else if (!gridOk && !gpuGridRenderer_.LastError().empty()) {
                statusText_ = L"GPU grid preview failed: " + Utf8ToWide(gpuGridRenderer_.LastError());
            } else if (!pathOk && !gpuPathRenderer_.LastError().empty()) {
                statusText_ = L"GPU path preview failed: " + Utf8ToWide(gpuPathRenderer_.LastError());
            } else if (useGpuDofPreview && !gpuDofRenderer_.LastError().empty()) {
                statusText_ = L"GPU DOF preview failed: " + Utf8ToWide(gpuDofRenderer_.LastError());
            } else if (useGpuDofPreview) {
                statusText_ = L"GPU DOF preview failed; falling back to CPU preview.";
            } else if (useGpuPostProcessPreview && !gpuPostProcess_.LastError().empty()) {
                statusText_ = L"GPU post-process preview failed: " + Utf8ToWide(gpuPostProcess_.LastError());
            }
        }
    }

    if (asyncViewportRendering_) {
        const PreviewBackend previewBackend = renderScene.mode == SceneMode::Flame ? PreviewBackend::CpuFlame
            : renderScene.mode == SceneMode::Path ? PreviewBackend::CpuPath
            : PreviewBackend::CpuHybrid;
        if (!viewportDirty_) {
            std::lock_guard<std::mutex> lock(renderMutex_);
            const bool sameRequestPending =
                renderRequestPending_
                && pendingRenderWidth_ == targetWidth
                && pendingRenderHeight_ == targetHeight
                && pendingRenderPreviewIterations_ == previewIterations
                && pendingRenderBackend_ == previewBackend;
            if (sameRequestPending) {
                return;
            }
        }
        QueueViewportRender(targetWidth, targetHeight, interactivePreview_);
        displayedPreviewIterations_ = previewIterations;
        displayedPreviewBackend_ = previewBackend;
        viewportDirty_ = false;
        return;
    }

    if (!renderer_.RenderViewport(renderScene, targetWidth, targetHeight, viewportPixels_)) {
        return;
    }
    displayedPreviewIterations_ = renderScene.previewIterations;
    displayedPreviewBackend_ = renderScene.mode == SceneMode::Flame ? PreviewBackend::CpuFlame
        : renderScene.mode == SceneMode::Path ? PreviewBackend::CpuPath
        : PreviewBackend::CpuHybrid;
    if (!EnsureViewportTexture(targetWidth, targetHeight)) {
        return;
    }

    D3D11_BOX box {};
    box.left = 0;
    box.top = 0;
    box.front = 0;
    box.right = static_cast<UINT>(targetWidth);
    box.bottom = static_cast<UINT>(targetHeight);
    box.back = 1;
    deviceContext_->UpdateSubresource(viewportTexture_.Get(), 0, &box, viewportPixels_.data(), targetWidth * 4, 0);
    viewportDirty_ = false;
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
            viewportDirty_ = true;
        }
        viewportInteractionCaptured_ = false;
        return;
    }
    const bool interacting = hovered && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || io.MouseWheel != 0.0f);
    if (interactivePreview_ != interacting) {
        interactivePreview_ = interacting;
        viewportDirty_ = true;
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
        viewportDirty_ = true;
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        captureViewportUndo();
        scene_.camera.panX += io.MouseDelta.x;
        scene_.camera.panY += io.MouseDelta.y;
        AutoKeyCurrentFrame();
        viewportDirty_ = true;
    }
    if (io.MouseWheel != 0.0f) {
        captureViewportUndo();
        constexpr double kMinCameraDistance = 0.05;
        constexpr double kMinCameraZoom = 0.2;
        const double zoomFactor = io.MouseWheel > 0.0f ? 1.08 : 0.92;
        scene_.camera.zoom2D = std::max(kMinCameraZoom, scene_.camera.zoom2D * zoomFactor);
        scene_.camera.distance = std::max(kMinCameraDistance, scene_.camera.distance * (io.MouseWheel > 0.0f ? 0.94 : 1.06));
        AutoKeyCurrentFrame();
        viewportDirty_ = true;
    }
    if (viewportDirty_) {
        SyncCurrentKeyframeFromScene();
    }
}

}  // namespace radiary
