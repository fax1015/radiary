#pragma once

#include <d3d11.h>

#include <optional>
#include <string>

#include "core/Scene.h"
#include "renderer/GpuFrame.h"

namespace radiary {

class GpuPostProcess {
public:
    ~GpuPostProcess();

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
    void Shutdown();

    bool Render(
        const Scene& scene,
        int width,
        int height,
        const GpuPostProcessInputs& inputs);

    ID3D11ShaderResourceView* ShaderResourceView() const { return outputSrvs_[currentOutputIndex_]; }
    ID3D11Texture2D* OutputTexture() const { return outputTextures_[currentOutputIndex_]; }
    GpuPassOutput Output() const { return {outputSrvs_[currentOutputIndex_], outputTextures_[currentOutputIndex_], nullptr, nullptr}; }
    bool IsReady() const { return device_ != nullptr && postProcessShader_ != nullptr && bloomDownShader_ != nullptr && bloomUpShader_ != nullptr && paramsBuffer_ != nullptr; }
    const std::string& LastError() const { return lastError_; }

private:
    struct Params {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float bloomIntensity = 0.35f;
        float bloomThreshold = 0.6f;
        std::uint32_t curvesEnabled = 0;
        std::uint32_t sharpenEnabled = 0;
        std::uint32_t hueShiftEnabled = 0;
        std::uint32_t chromaticAberrationEnabled = 0;
        float curveBlackPoint = 0.0f;
        float curveWhitePoint = 1.0f;
        float curveGamma = 1.0f;
        std::uint32_t curveUseCustom = 0;
        float curvePointsX[8] = {};
        float curvePointsY[8] = {};
        std::uint32_t curvePointCount = 0;
        float sharpenAmount = 0.0f;
        float hueShiftDegrees = 0.0f;
        float hueShiftSaturation = 1.0f;
        float chromaticAberration = 0.0f;
        float vignetteIntensity = 0.0f;
        float vignetteRoundness = 0.5f;
        std::uint32_t vignetteEnabled = 0;
        std::uint32_t toneMappingEnabled = 0;
        std::uint32_t filmGrainEnabled = 0;
        std::uint32_t colorTemperatureEnabled = 0;
        float filmGrain = 0.0f;
        float filmGrainScale = 1.0f;
        float colorTemperature = 6500.0f;
        float saturationBoost = 0.0f;
        float saturationVibrance = 0.0f;
        std::uint32_t saturationEnabled = 0;
        std::uint32_t randomSeed = 0;
        std::uint32_t mipWidth = 0;
        std::uint32_t mipHeight = 0;
        float padding[4] {};
    };

    static constexpr int kBloomMipCount = 5;

    bool CreateShaders();
    bool EnsureResources(int width, int height);
    void ReleaseResources();
    void SetError(const std::string& error) { lastError_ = error; }
    void SetError(const char* stage, HRESULT result);
    DXGI_FORMAT ChooseOutputFormat() const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    ID3D11ComputeShader* postProcessShader_ = nullptr;
    ID3D11ComputeShader* bloomDownShader_ = nullptr;
    ID3D11ComputeShader* bloomUpShader_ = nullptr;
    ID3D11Buffer* paramsBuffer_ = nullptr;

    static constexpr int kOutputBufferCount = 2;

    ID3D11Texture2D* outputTextures_[kOutputBufferCount] {};
    ID3D11UnorderedAccessView* outputUavs_[kOutputBufferCount] {};
    ID3D11ShaderResourceView* outputSrvs_[kOutputBufferCount] {};

    ID3D11Texture2D* bloomTextures_[kBloomMipCount] {};
    ID3D11UnorderedAccessView* bloomUavs_[kBloomMipCount] {};
    ID3D11ShaderResourceView* bloomSrvs_[kBloomMipCount] {};

    DXGI_FORMAT outputFormat_ = DXGI_FORMAT_UNKNOWN;
    int outputWidth_ = 0;
    int outputHeight_ = 0;
    int currentOutputIndex_ = 0;
    std::uint32_t frameCounter_ = 0;
    std::string lastError_;
};

}  // namespace radiary
