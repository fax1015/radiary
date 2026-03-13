#include "app/AppWindow.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

#include "imgui.h"
#include "imgui_internal.h"

using namespace radiary;

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
    std::uint32_t previewIterations = renderScene.previewIterations;
    PreviewBackend previewBackend = PreviewBackend::CpuHybrid;
    if (interactive && adaptiveInteractivePreview_) {
        previewIterations = std::min(previewIterations, interactivePreviewIterations_);
        renderScene.previewIterations = previewIterations;
    }
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
    std::vector<std::uint32_t> pixels;
    int width = 0;
    int height = 0;
    std::uint32_t previewIterations = 0;
    PreviewBackend previewBackend = PreviewBackend::CpuHybrid;

    {
        std::lock_guard<std::mutex> lock(renderMutex_);
        if (completedRenderGeneration_ == consumedRenderGeneration_ || completedRenderPixels_.empty()) {
            return;
        }

        pixels = completedRenderPixels_;
        width = completedRenderWidth_;
        height = completedRenderHeight_;
        previewIterations = completedRenderPreviewIterations_;
        previewBackend = completedRenderBackend_;
        consumedRenderGeneration_ = completedRenderGeneration_;
    }

    if (!EnsureViewportTexture(width, height)) {
        return;
    }

    viewportPixels_ = std::move(pixels);
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

            renderScene = pendingRenderScene_;
            width = pendingRenderWidth_;
            height = pendingRenderHeight_;
            previewIterations = pendingRenderPreviewIterations_;
            previewBackend = pendingRenderBackend_;
            generation = pendingRenderGeneration_;
            renderRequestPending_ = false;
            renderInProgress_ = true;
        }

        std::vector<std::uint32_t> pixels;
        workerRenderer.RenderViewport(renderScene, width, height, pixels);

        {
            std::lock_guard<std::mutex> lock(renderMutex_);
            renderInProgress_ = false;
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
    std::uint32_t previewIterations = scene_.previewIterations;
    if (interactivePreview_ && adaptiveInteractivePreview_) {
        previewIterations = std::min(previewIterations, interactivePreviewIterations_);
    }
    renderScene.previewIterations = previewIterations;

    const bool sizeChanged = uploadedViewportWidth_ != targetWidth || uploadedViewportHeight_ != targetHeight;
    const bool gpuAccumulationIncomplete =
        useGpuViewportPreview
        && renderScene.mode != SceneMode::Path
        && gpuFlameRenderer_.AccumulatedIterations() < previewIterations;
    const bool needsGpuDofResolve =
        useGpuDofPreview
        && !interactivePreview_
        && !gpuAccumulationIncomplete
        && displayedPreviewBackend_ != PreviewBackend::GpuDof;
    if (!viewportDirty_ && !sizeChanged && !gpuAccumulationIncomplete && !needsGpuDofResolve) {
        return;
    }

    if (useGpuViewportPreview) {
        const auto setDirectGpuPreview = [&](const PreviewBackend backend, const std::uint32_t displayedIterations) {
            uploadedViewportWidth_ = targetWidth;
            uploadedViewportHeight_ = targetHeight;
            displayedPreviewIterations_ = displayedIterations;
            displayedPreviewBackend_ = backend;
            viewportDirty_ = false;
            return true;
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
            return setDirectGpuPreview(PreviewBackend::GpuDof, displayedIterations);
        };

        if (renderScene.mode == SceneMode::Flame) {
            const bool compositeGridUnderFlame = renderScene.gridVisible;
            const bool flameOk = gpuFlameRenderer_.Render(renderScene, targetWidth, targetHeight, previewIterations, compositeGridUnderFlame);
            const bool gridOk = flameOk && (!renderScene.gridVisible || gpuGridRenderer_.Render(renderScene, targetWidth, targetHeight, true, true));
            if (flameOk && gridOk) {
                const std::uint32_t displayedIterations = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(gpuFlameRenderer_.AccumulatedIterations(), static_cast<std::uint64_t>(previewIterations)));
                if (useGpuDofPreview) {
                    const bool dofReady = !interactivePreview_ && displayedIterations >= previewIterations;
                    if (!dofReady) {
                        setDirectGpuPreview(renderScene.gridVisible ? PreviewBackend::GpuHybrid : PreviewBackend::GpuFlame, displayedIterations);
                        return;
                    }
                    if (renderGpuDofPreview(
                            renderScene.gridVisible ? gpuGridRenderer_.ShaderResourceView() : nullptr,
                            gpuFlameRenderer_.ShaderResourceView(),
                            gpuFlameRenderer_.DepthShaderResourceView(),
                            nullptr,
                            nullptr,
                            displayedIterations)) {
                        return;
                    }
                } else {
                    setDirectGpuPreview(renderScene.gridVisible ? PreviewBackend::GpuHybrid : PreviewBackend::GpuFlame, displayedIterations);
                    return;
                }
            }
            if (!gpuFlameRenderer_.LastError().empty()) {
                statusText_ = L"GPU flame preview failed: " + Utf8ToWide(gpuFlameRenderer_.LastError());
            } else if (!gridOk && !gpuGridRenderer_.LastError().empty()) {
                statusText_ = L"GPU grid preview failed: " + Utf8ToWide(gpuGridRenderer_.LastError());
            } else if (useGpuDofPreview && !gpuDofRenderer_.LastError().empty()) {
                statusText_ = L"GPU DOF preview failed: " + Utf8ToWide(gpuDofRenderer_.LastError());
            } else if (useGpuDofPreview) {
                statusText_ = L"GPU DOF preview failed; falling back to CPU preview.";
            }
        } else if (renderScene.mode == SceneMode::Path) {
            if (gpuPathRenderer_.Render(renderScene, targetWidth, targetHeight, false, true)) {
                if (useGpuDofPreview && !interactivePreview_) {
                    if (renderGpuDofPreview(
                            nullptr,
                            nullptr,
                            nullptr,
                            gpuPathRenderer_.ShaderResourceView(),
                            gpuPathRenderer_.DepthShaderResourceView(),
                            previewIterations)) {
                        return;
                    }
                } else {
                    setDirectGpuPreview(PreviewBackend::GpuPath, previewIterations);
                    return;
                }
            }
            if (!gpuPathRenderer_.LastError().empty()) {
                statusText_ = L"GPU path preview failed: " + Utf8ToWide(gpuPathRenderer_.LastError());
            } else if (useGpuDofPreview && !gpuDofRenderer_.LastError().empty()) {
                statusText_ = L"GPU DOF preview failed: " + Utf8ToWide(gpuDofRenderer_.LastError());
            }
        } else {
            const bool compositeGridUnderFlame = renderScene.gridVisible;
            const bool flameOk = gpuFlameRenderer_.Render(renderScene, targetWidth, targetHeight, previewIterations, compositeGridUnderFlame);
            Scene gridScene = renderScene;
            gridScene.mode = SceneMode::Flame;
            const bool gridOk = flameOk && (!renderScene.gridVisible || gpuGridRenderer_.Render(gridScene, targetWidth, targetHeight, true, true));
            Scene pathScene = renderScene;
            pathScene.mode = SceneMode::Path;
            const bool pathOk = flameOk && gpuPathRenderer_.Render(
                pathScene,
                targetWidth,
                targetHeight,
                true,
                false,
                gpuFlameRenderer_.DepthShaderResourceView());
            if (flameOk && gridOk && pathOk) {
                const std::uint32_t displayedIterations = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(gpuFlameRenderer_.AccumulatedIterations(), static_cast<std::uint64_t>(previewIterations)));
                if (useGpuDofPreview) {
                    const bool dofReady = !interactivePreview_ && displayedIterations >= previewIterations;
                    if (!dofReady) {
                        setDirectGpuPreview(PreviewBackend::GpuHybrid, displayedIterations);
                        return;
                    }
                    if (renderGpuDofPreview(
                            renderScene.gridVisible ? gpuGridRenderer_.ShaderResourceView() : nullptr,
                            gpuFlameRenderer_.ShaderResourceView(),
                            gpuFlameRenderer_.DepthShaderResourceView(),
                            gpuPathRenderer_.ShaderResourceView(),
                            gpuPathRenderer_.DepthShaderResourceView(),
                            displayedIterations)) {
                        return;
                    }
                } else {
                    setDirectGpuPreview(PreviewBackend::GpuHybrid, displayedIterations);
                    return;
                }
            }
            if (!flameOk && !gpuFlameRenderer_.LastError().empty()) {
                statusText_ = L"GPU flame preview failed: " + Utf8ToWide(gpuFlameRenderer_.LastError());
            } else if (!gridOk && !gpuGridRenderer_.LastError().empty()) {
                statusText_ = L"GPU grid preview failed: " + Utf8ToWide(gpuGridRenderer_.LastError());
            } else if (!pathOk && !gpuPathRenderer_.LastError().empty()) {
                statusText_ = L"GPU path preview failed: " + Utf8ToWide(gpuPathRenderer_.LastError());
            } else if (useGpuDofPreview && !gpuDofRenderer_.LastError().empty()) {
                statusText_ = L"GPU DOF preview failed: " + Utf8ToWide(gpuDofRenderer_.LastError());
            } else if (useGpuDofPreview) {
                statusText_ = L"GPU DOF preview failed; falling back to CPU preview.";
            }
        }
    }

    if (asyncViewportRendering_) {
        if (!viewportDirty_) {
            std::lock_guard<std::mutex> lock(renderMutex_);
            const bool sameRequestPending =
                (renderRequestPending_ || renderInProgress_)
                && pendingRenderWidth_ == targetWidth
                && pendingRenderHeight_ == targetHeight;
            if (sameRequestPending) {
                return;
            }
        }
        QueueViewportRender(targetWidth, targetHeight, interactivePreview_);
        viewportDirty_ = false;
        return;
    }

    renderer_.RenderViewport(renderScene, targetWidth, targetHeight, viewportPixels_);
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
