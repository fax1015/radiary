#pragma once

#include <d3d11.h>

#include <string>

#include "core/Scene.h"

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
        ID3D11ShaderResourceView* inputSrv);

    ID3D11ShaderResourceView* ShaderResourceView() const { return outputSrv_; }
    ID3D11Texture2D* OutputTexture() const { return outputTexture_; }
    bool IsReady() const { return device_ != nullptr && postProcessShader_ != nullptr && bloomDownShader_ != nullptr && bloomUpShader_ != nullptr && paramsBuffer_ != nullptr; }
    const std::string& LastError() const { return lastError_; }

private:
    struct Params {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float bloomIntensity = 0.35f;
        float bloomThreshold = 0.6f;
        float chromaticAberration = 0.0f;
        float vignetteIntensity = 0.0f;
        float vignetteRoundness = 0.5f;
        std::uint32_t acesToneMap = 0;
        float filmGrain = 0.0f;
        float colorTemperature = 6500.0f;
        float saturationBoost = 0.0f;
        std::uint32_t randomSeed = 0;
        std::uint32_t mipWidth = 0;
        std::uint32_t mipHeight = 0;
        float padding[2] {};
    };

    static constexpr int kBloomMipCount = 5;

    bool CreateShaders();
    bool EnsureResources(int width, int height);
    void ReleaseResources();
    void SetError(const std::string& error) { lastError_ = error; }
    void SetError(const char* stage, HRESULT result);
    DXGI_FORMAT ChooseOutputFormat() const;

    static ID3DBlob* CompileShader(const char* source, const char* entryPoint, const char* target, std::string& error);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    ID3D11ComputeShader* postProcessShader_ = nullptr;
    ID3D11ComputeShader* bloomDownShader_ = nullptr;
    ID3D11ComputeShader* bloomUpShader_ = nullptr;
    ID3D11Buffer* paramsBuffer_ = nullptr;

    ID3D11Texture2D* outputTexture_ = nullptr;
    ID3D11UnorderedAccessView* outputUav_ = nullptr;
    ID3D11ShaderResourceView* outputSrv_ = nullptr;

    ID3D11Texture2D* bloomTextures_[kBloomMipCount] {};
    ID3D11UnorderedAccessView* bloomUavs_[kBloomMipCount] {};
    ID3D11ShaderResourceView* bloomSrvs_[kBloomMipCount] {};

    DXGI_FORMAT outputFormat_ = DXGI_FORMAT_UNKNOWN;
    int outputWidth_ = 0;
    int outputHeight_ = 0;
    std::uint32_t frameCounter_ = 0;
    std::string lastError_;
};

}  // namespace radiary
