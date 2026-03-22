#pragma once

#include <d3d11.h>

#include <cstdint>
#include <optional>

namespace radiary {

struct GpuFrameInputs {
    ID3D11ShaderResourceView* gridColor = nullptr;
    ID3D11ShaderResourceView* flameColor = nullptr;
    ID3D11ShaderResourceView* flameDepth = nullptr;
    ID3D11ShaderResourceView* pathColor = nullptr;
    ID3D11ShaderResourceView* pathDepth = nullptr;

    bool HasGrid() const { return gridColor != nullptr; }
    bool HasFlame() const { return flameColor != nullptr && flameDepth != nullptr; }
    bool HasPath() const { return pathColor != nullptr && pathDepth != nullptr; }
};

inline GpuFrameInputs MakeGpuFrameInputs(
    ID3D11ShaderResourceView* gridColor,
    ID3D11ShaderResourceView* flameColor,
    ID3D11ShaderResourceView* flameDepth,
    ID3D11ShaderResourceView* pathColor,
    ID3D11ShaderResourceView* pathDepth) {
    GpuFrameInputs inputs;
    inputs.gridColor = gridColor;
    inputs.flameColor = flameColor;
    inputs.flameDepth = flameDepth;
    inputs.pathColor = pathColor;
    inputs.pathDepth = pathDepth;
    return inputs;
}

struct GpuPassOutput {
    ID3D11ShaderResourceView* colorSrv = nullptr;
    ID3D11Texture2D* colorTexture = nullptr;
    ID3D11ShaderResourceView* depthSrv = nullptr;
    ID3D11Texture2D* depthTexture = nullptr;
};

struct GpuPostProcessInputs {
    ID3D11ShaderResourceView* sourceColor = nullptr;
    std::optional<std::uint32_t> randomSeedOverride = std::nullopt;
};

inline GpuPostProcessInputs MakeGpuPostProcessInputs(
    ID3D11ShaderResourceView* sourceColor,
    std::optional<std::uint32_t> randomSeedOverride = std::nullopt) {
    GpuPostProcessInputs inputs;
    inputs.sourceColor = sourceColor;
    inputs.randomSeedOverride = randomSeedOverride;
    return inputs;
}

}  // namespace radiary
