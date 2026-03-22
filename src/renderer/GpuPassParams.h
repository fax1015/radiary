#pragma once

#include <cstdint>
#include <optional>

#include "renderer/GpuFrame.h"

namespace radiary {

struct GpuViewportParams {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct GpuFrameLayerFlags {
    std::uint32_t useGrid = 0;
    std::uint32_t useFlame = 0;
    std::uint32_t usePath = 0;
};

inline GpuViewportParams MakeGpuViewportParams(const int width, const int height) {
    return {
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height)
    };
}

inline GpuFrameLayerFlags MakeGpuFrameLayerFlags(const GpuFrameInputs& inputs) {
    return {
        inputs.HasGrid() ? 1u : 0u,
        inputs.HasFlame() ? 1u : 0u,
        inputs.HasPath() ? 1u : 0u
    };
}

inline std::uint32_t MakePostProcessRandomSeed(
    const std::uint32_t frameCounter,
    const std::optional<std::uint32_t>& randomSeedOverride) {
    return randomSeedOverride.value_or(frameCounter * 1664525u + 1013904223u);
}

}  // namespace radiary
