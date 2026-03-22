#include "app/PreviewRenderDecisions.h"

namespace radiary {

namespace {

bool CameraStateChanged(const CameraState& a, const CameraState& b) {
    return a.yaw != b.yaw
        || a.pitch != b.pitch
        || a.distance != b.distance
        || a.panX != b.panX
        || a.panY != b.panY
        || a.zoom2D != b.zoom2D
        || a.frameWidth != b.frameWidth
        || a.frameHeight != b.frameHeight;
}

}  // namespace

PreviewResetReason DeterminePreviewResetReason(const Scene& before, const Scene& after) {
    if (before.mode != after.mode) {
        return PreviewResetReason::ModeChanged;
    }
    if (before.previewIterations != after.previewIterations) {
        return PreviewResetReason::IterationChanged;
    }
    if (CameraStateChanged(before.camera, after.camera)) {
        return PreviewResetReason::CameraChanged;
    }
    return PreviewResetReason::SceneChanged;
}

PreviewRenderContent PreviewContentForMode(const SceneMode mode) {
    switch (mode) {
    case SceneMode::Flame:
        return PreviewRenderContent::Flame;
    case SceneMode::Path:
        return PreviewRenderContent::Path;
    case SceneMode::Hybrid:
    default:
        return PreviewRenderContent::Hybrid;
    }
}

PreviewRenderStage DetermineResolvedRenderStage(
    const bool useDenoiser,
    const bool useDof,
    const bool usePostProcess) {
    if (usePostProcess) {
        return PreviewRenderStage::PostProcessed;
    }
    if (useDof) {
        return PreviewRenderStage::DepthOfField;
    }
    if (useDenoiser) {
        return PreviewRenderStage::Denoised;
    }
    return PreviewRenderStage::Base;
}

}  // namespace radiary
