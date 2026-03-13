#pragma once

#include <d3d11.h>

#include <string>

#include "core/Scene.h"

namespace radiary {

class GpuDofRenderer {
public:
    ~GpuDofRenderer();

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
    void Shutdown();

    bool Render(
        const Scene& scene,
        int width,
        int height,
        ID3D11ShaderResourceView* gridSrv,
        ID3D11ShaderResourceView* flameSrv,
        ID3D11ShaderResourceView* flameDepthSrv,
        ID3D11ShaderResourceView* pathSrv,
        ID3D11ShaderResourceView* pathDepthSrv);

    ID3D11ShaderResourceView* ShaderResourceView() const { return outputSrv_; }
    ID3D11Texture2D* OutputTexture() const { return outputTexture_; }
    bool IsReady() const { return device_ != nullptr && shader_ != nullptr && paramsBuffer_ != nullptr; }
    const std::string& LastError() const { return lastError_; }

private:
    struct Params {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t useGrid = 0;
        std::uint32_t useFlame = 0;
        std::uint32_t usePath = 0;
        float focusDepth = 0.45f;
        float focusRange = 0.10f;
        float blurStrength = 0.45f;
        float padding[4] {};
    };

    bool CreateShader();
    bool EnsureResources(int width, int height);
    void ReleaseResources();
    void SetError(const std::string& error) { lastError_ = error; }
    void SetError(const char* stage, HRESULT result);
    DXGI_FORMAT ChooseOutputFormat() const;

    static ID3DBlob* CompileShader(const char* source, const char* entryPoint, const char* target, std::string& error);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    ID3D11ComputeShader* shader_ = nullptr;
    ID3D11Buffer* paramsBuffer_ = nullptr;
    ID3D11Texture2D* outputTexture_ = nullptr;
    ID3D11UnorderedAccessView* outputUav_ = nullptr;
    ID3D11ShaderResourceView* outputSrv_ = nullptr;
    DXGI_FORMAT outputFormat_ = DXGI_FORMAT_UNKNOWN;
    int outputWidth_ = 0;
    int outputHeight_ = 0;
    std::string lastError_;
};

}  // namespace radiary
