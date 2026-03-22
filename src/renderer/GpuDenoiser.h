#pragma once

#include <d3d11.h>

#include <string>

#include "core/Scene.h"
#include "renderer/GpuFrame.h"

namespace radiary {

class GpuDenoiser {
public:
    ~GpuDenoiser();

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
    void Shutdown();

    bool Compose(
        int width,
        int height,
        const GpuFrameInputs& inputs);

    bool Render(
        const Scene& scene,
        int width,
        int height,
        const GpuFrameInputs& inputs);

    ID3D11ShaderResourceView* ShaderResourceView() const { return outputSrv_; }
    ID3D11Texture2D* OutputTexture() const { return outputTexture_; }
    ID3D11ShaderResourceView* DepthShaderResourceView() const { return depthSrv_; }
    ID3D11Texture2D* DepthTexture() const { return depthTexture_; }
    GpuPassOutput Output() const { return {outputSrv_, outputTexture_, depthSrv_, depthTexture_}; }
    bool IsReady() const { return device_ != nullptr && composeShader_ != nullptr && filterShader_ != nullptr && paramsBuffer_ != nullptr; }
    const std::string& LastError() const { return lastError_; }

private:
    struct Params {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t useGrid = 0;
        std::uint32_t useFlame = 0;
        std::uint32_t usePath = 0;
        float strength = 0.5f;
        float sigmaSpatial = 1.0f;
        float sigmaColor = 1.0f;
        float sigmaDepth = 1.0f;
        float padding[3] {};
    };

    bool CreateShaders();
    bool EnsureResources(int width, int height);
    bool DispatchComposePass(const Params& params, const GpuFrameInputs& inputs);
    bool DispatchFilterPasses(const Params& params);
    void ReleaseResources();
    void SetError(const std::string& error) { lastError_ = error; }
    void SetError(const char* stage, HRESULT result);
    DXGI_FORMAT ChooseOutputFormat() const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    ID3D11ComputeShader* composeShader_ = nullptr;
    ID3D11ComputeShader* filterShader_ = nullptr;
    ID3D11Buffer* paramsBuffer_ = nullptr;
    ID3D11Texture2D* outputTexture_ = nullptr;
    ID3D11UnorderedAccessView* outputUav_ = nullptr;
    ID3D11ShaderResourceView* outputSrv_ = nullptr;
    ID3D11Texture2D* tempTexture_ = nullptr;
    ID3D11UnorderedAccessView* tempUav_ = nullptr;
    ID3D11ShaderResourceView* tempSrv_ = nullptr;
    ID3D11Texture2D* depthTexture_ = nullptr;
    ID3D11UnorderedAccessView* depthUav_ = nullptr;
    ID3D11ShaderResourceView* depthSrv_ = nullptr;
    DXGI_FORMAT outputFormat_ = DXGI_FORMAT_UNKNOWN;
    int outputWidth_ = 0;
    int outputHeight_ = 0;
    std::string lastError_;
};

}  // namespace radiary
