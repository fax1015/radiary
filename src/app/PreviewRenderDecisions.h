#pragma once

#include "app/PreviewPresentation.h"
#include "core/Scene.h"

namespace radiary {

enum class PreviewResetReason {
    None,
    SceneChanged,
    CameraChanged,
    ModeChanged,
    IterationChanged,
    ViewportResized,
    DeviceChanged
};

PreviewResetReason DeterminePreviewResetReason(const Scene& before, const Scene& after);
PreviewRenderContent PreviewContentForMode(SceneMode mode);
PreviewRenderStage DetermineResolvedRenderStage(bool useDenoiser, bool useDof, bool usePostProcess);

}  // namespace radiary
