#pragma once

#include <cstdint>

namespace radiary {

enum class PreviewRenderDevice {
    Cpu,
    Gpu
};

enum class PreviewRenderContent {
    Flame,
    Path,
    Hybrid
};

enum class PreviewRenderStage {
    Base,
    Composited,
    Denoised,
    DepthOfField,
    PostProcessed
};

struct PreviewPresentationState {
    PreviewRenderDevice device = PreviewRenderDevice::Cpu;
    PreviewRenderContent content = PreviewRenderContent::Hybrid;
    PreviewRenderStage stage = PreviewRenderStage::Base;

    bool operator==(const PreviewPresentationState&) const = default;
};

}  // namespace radiary
