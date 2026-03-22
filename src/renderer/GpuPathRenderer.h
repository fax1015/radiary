#pragma once

#include <d3d11.h>

#include <cstddef>
#include <string>

#include "core/Scene.h"
#include "renderer/PathDrawListBuilder.h"

namespace radiary {

class GpuPathRenderer {
public:
    using Vertex = PathDrawVertex;

    ~GpuPathRenderer();

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
    void Shutdown();

    bool Render(
        const Scene& scene,
        int width,
        int height,
        bool transparentBackground,
        bool renderGrid,
        ID3D11ShaderResourceView* flameDepthSrv = nullptr);
    bool RenderDrawList(
        const Color& backgroundColor,
        int width,
        int height,
        bool transparentBackground,
        const PathDrawList& drawList,
        ID3D11ShaderResourceView* flameDepthSrv = nullptr);

    ID3D11ShaderResourceView* ShaderResourceView() const { return outputSrv_; }
    ID3D11Texture2D* OutputTexture() const { return outputTexture_; }
    ID3D11ShaderResourceView* DepthShaderResourceView() const { return depthSrv_; }
    bool IsReady() const { return device_ != nullptr && vertexShader_ != nullptr && pixelShader_ != nullptr && inputLayout_ != nullptr; }
    const std::string& LastError() const { return lastError_; }

private:
    bool CreatePipeline();
    bool EnsureResources(int width, int height);
    bool EnsureVertexBuffer(std::size_t vertexCount);
    bool UploadDrawListVertices(const PathDrawList& drawList);
    void DrawUploadedDrawList(const PathDrawList& drawList);
    bool DrawDrawList(const PathDrawList& drawList);
    void ReleasePipeline();
    void ReleaseResources();
    void SetError(const std::string& error) { lastError_ = error; }
    void SetError(const char* stage, HRESULT result);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;

    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* pixelShader_ = nullptr;
    ID3D11InputLayout* inputLayout_ = nullptr;
    ID3D11RasterizerState* rasterizerState_ = nullptr;
    ID3D11BlendState* blendState_ = nullptr;
    ID3D11DepthStencilState* depthDisabledState_ = nullptr;
    ID3D11DepthStencilState* depthWriteState_ = nullptr;
    ID3D11DepthStencilState* depthReadState_ = nullptr;
    ID3D11Buffer* hybridParamsBuffer_ = nullptr;

    ID3D11Buffer* vertexBuffer_ = nullptr;
    std::size_t vertexCapacity_ = 0;

    ID3D11Texture2D* outputTexture_ = nullptr;
    ID3D11RenderTargetView* outputRtv_ = nullptr;
    ID3D11ShaderResourceView* outputSrv_ = nullptr;
    ID3D11Texture2D* depthTexture_ = nullptr;
    ID3D11DepthStencilView* depthDsv_ = nullptr;
    ID3D11ShaderResourceView* depthSrv_ = nullptr;

    int outputWidth_ = 0;
    int outputHeight_ = 0;
    std::string lastError_;
};

}  // namespace radiary
