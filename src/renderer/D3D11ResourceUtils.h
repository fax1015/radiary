#pragma once

#include <d3d11.h>

#include <cstddef>

namespace radiary {

struct D3D11TextureViewStages {
    const char* textureStage = nullptr;
    const char* uavStage = nullptr;
    const char* srvStage = nullptr;
};

struct D3D11RenderTargetViewStages {
    const char* textureStage = nullptr;
    const char* rtvStage = nullptr;
    const char* srvStage = nullptr;
};

struct D3D11DepthStencilViewStages {
    const char* textureStage = nullptr;
    const char* dsvStage = nullptr;
    const char* srvStage = nullptr;
};

struct D3D11BufferViewStages {
    const char* bufferStage = nullptr;
    const char* srvStage = nullptr;
    const char* uavStage = nullptr;
};

bool CreateTexture2DWithViews(
    ID3D11Device* device,
    const D3D11_TEXTURE2D_DESC& desc,
    const D3D11TextureViewStages& stages,
    ID3D11Texture2D** texture,
    ID3D11UnorderedAccessView** uav,
    ID3D11ShaderResourceView** srv,
    const char*& failedStage,
    HRESULT& failedResult);

bool CreateRenderTargetTexture2DWithViews(
    ID3D11Device* device,
    const D3D11_TEXTURE2D_DESC& desc,
    const D3D11RenderTargetViewStages& stages,
    ID3D11Texture2D** texture,
    ID3D11RenderTargetView** rtv,
    ID3D11ShaderResourceView** srv,
    const char*& failedStage,
    HRESULT& failedResult);

bool CreateDepthStencilTexture2DWithViews(
    ID3D11Device* device,
    const D3D11_TEXTURE2D_DESC& textureDesc,
    const D3D11_DEPTH_STENCIL_VIEW_DESC& dsvDesc,
    const D3D11_SHADER_RESOURCE_VIEW_DESC& srvDesc,
    const D3D11DepthStencilViewStages& stages,
    ID3D11Texture2D** texture,
    ID3D11DepthStencilView** dsv,
    ID3D11ShaderResourceView** srv,
    const char*& failedStage,
    HRESULT& failedResult);

bool CreateBufferWithShaderResourceView(
    ID3D11Device* device,
    const D3D11_BUFFER_DESC& bufferDesc,
    const D3D11_SHADER_RESOURCE_VIEW_DESC& srvDesc,
    const D3D11BufferViewStages& stages,
    ID3D11Buffer** buffer,
    ID3D11ShaderResourceView** srv,
    const char*& failedStage,
    HRESULT& failedResult);

bool CreateBufferWithUnorderedAccessView(
    ID3D11Device* device,
    const D3D11_BUFFER_DESC& bufferDesc,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC& uavDesc,
    const D3D11BufferViewStages& stages,
    ID3D11Buffer** buffer,
    ID3D11UnorderedAccessView** uav,
    const char*& failedStage,
    HRESULT& failedResult);

bool CreateDynamicConstantBuffer(
    ID3D11Device* device,
    UINT byteWidth,
    const char* failedStageName,
    ID3D11Buffer** buffer,
    const char*& failedStage,
    HRESULT& failedResult);

bool WriteD3D11BufferData(
    ID3D11DeviceContext* deviceContext,
    ID3D11Buffer* buffer,
    const void* data,
    std::size_t byteCount,
    const char* failedStageName,
    const char*& failedStage,
    HRESULT& failedResult);

bool SupportsD3D11Format(
    ID3D11Device* device,
    DXGI_FORMAT format,
    UINT requiredSupport);

DXGI_FORMAT ChooseSupportedD3D11Format(
    ID3D11Device* device,
    const DXGI_FORMAT* candidates,
    std::size_t candidateCount,
    UINT requiredSupport);

}  // namespace radiary
