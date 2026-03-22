#include "renderer/D3D11ResourceUtils.h"

#include <cstring>

namespace radiary {

bool CreateTexture2DWithViews(
    ID3D11Device* device,
    const D3D11_TEXTURE2D_DESC& desc,
    const D3D11TextureViewStages& stages,
    ID3D11Texture2D** texture,
    ID3D11UnorderedAccessView** uav,
    ID3D11ShaderResourceView** srv,
    const char*& failedStage,
    HRESULT& failedResult) {
    failedStage = nullptr;
    failedResult = S_OK;
    if (texture == nullptr || uav == nullptr || srv == nullptr) {
        failedStage = "CreateTexture2DWithViews";
        failedResult = E_INVALIDARG;
        return false;
    }

    *texture = nullptr;
    *uav = nullptr;
    *srv = nullptr;

    failedResult = device->CreateTexture2D(&desc, nullptr, texture);
    if (FAILED(failedResult)) {
        failedStage = stages.textureStage;
        return false;
    }

    failedResult = device->CreateUnorderedAccessView(*texture, nullptr, uav);
    if (FAILED(failedResult)) {
        failedStage = stages.uavStage;
        (*texture)->Release();
        *texture = nullptr;
        return false;
    }

    failedResult = device->CreateShaderResourceView(*texture, nullptr, srv);
    if (FAILED(failedResult)) {
        failedStage = stages.srvStage;
        (*uav)->Release();
        *uav = nullptr;
        (*texture)->Release();
        *texture = nullptr;
        return false;
    }

    return true;
}

bool CreateRenderTargetTexture2DWithViews(
    ID3D11Device* device,
    const D3D11_TEXTURE2D_DESC& desc,
    const D3D11RenderTargetViewStages& stages,
    ID3D11Texture2D** texture,
    ID3D11RenderTargetView** rtv,
    ID3D11ShaderResourceView** srv,
    const char*& failedStage,
    HRESULT& failedResult) {
    failedStage = nullptr;
    failedResult = S_OK;
    if (texture == nullptr || rtv == nullptr || srv == nullptr) {
        failedStage = "CreateRenderTargetTexture2DWithViews";
        failedResult = E_INVALIDARG;
        return false;
    }

    *texture = nullptr;
    *rtv = nullptr;
    *srv = nullptr;

    failedResult = device->CreateTexture2D(&desc, nullptr, texture);
    if (FAILED(failedResult)) {
        failedStage = stages.textureStage;
        return false;
    }

    failedResult = device->CreateRenderTargetView(*texture, nullptr, rtv);
    if (FAILED(failedResult)) {
        failedStage = stages.rtvStage;
        (*texture)->Release();
        *texture = nullptr;
        return false;
    }

    failedResult = device->CreateShaderResourceView(*texture, nullptr, srv);
    if (FAILED(failedResult)) {
        failedStage = stages.srvStage;
        (*rtv)->Release();
        *rtv = nullptr;
        (*texture)->Release();
        *texture = nullptr;
        return false;
    }

    return true;
}

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
    HRESULT& failedResult) {
    failedStage = nullptr;
    failedResult = S_OK;
    if (texture == nullptr || dsv == nullptr || srv == nullptr) {
        failedStage = "CreateDepthStencilTexture2DWithViews";
        failedResult = E_INVALIDARG;
        return false;
    }

    *texture = nullptr;
    *dsv = nullptr;
    *srv = nullptr;

    failedResult = device->CreateTexture2D(&textureDesc, nullptr, texture);
    if (FAILED(failedResult)) {
        failedStage = stages.textureStage;
        return false;
    }

    failedResult = device->CreateDepthStencilView(*texture, &dsvDesc, dsv);
    if (FAILED(failedResult)) {
        failedStage = stages.dsvStage;
        (*texture)->Release();
        *texture = nullptr;
        return false;
    }

    failedResult = device->CreateShaderResourceView(*texture, &srvDesc, srv);
    if (FAILED(failedResult)) {
        failedStage = stages.srvStage;
        (*dsv)->Release();
        *dsv = nullptr;
        (*texture)->Release();
        *texture = nullptr;
        return false;
    }

    return true;
}

bool CreateBufferWithShaderResourceView(
    ID3D11Device* device,
    const D3D11_BUFFER_DESC& bufferDesc,
    const D3D11_SHADER_RESOURCE_VIEW_DESC& srvDesc,
    const D3D11BufferViewStages& stages,
    ID3D11Buffer** buffer,
    ID3D11ShaderResourceView** srv,
    const char*& failedStage,
    HRESULT& failedResult) {
    failedStage = nullptr;
    failedResult = S_OK;
    if (buffer == nullptr || srv == nullptr) {
        failedStage = "CreateBufferWithShaderResourceView";
        failedResult = E_INVALIDARG;
        return false;
    }

    *buffer = nullptr;
    *srv = nullptr;

    failedResult = device->CreateBuffer(&bufferDesc, nullptr, buffer);
    if (FAILED(failedResult)) {
        failedStage = stages.bufferStage;
        return false;
    }

    failedResult = device->CreateShaderResourceView(*buffer, &srvDesc, srv);
    if (FAILED(failedResult)) {
        failedStage = stages.srvStage;
        (*buffer)->Release();
        *buffer = nullptr;
        return false;
    }

    return true;
}

bool CreateBufferWithUnorderedAccessView(
    ID3D11Device* device,
    const D3D11_BUFFER_DESC& bufferDesc,
    const D3D11_UNORDERED_ACCESS_VIEW_DESC& uavDesc,
    const D3D11BufferViewStages& stages,
    ID3D11Buffer** buffer,
    ID3D11UnorderedAccessView** uav,
    const char*& failedStage,
    HRESULT& failedResult) {
    failedStage = nullptr;
    failedResult = S_OK;
    if (buffer == nullptr || uav == nullptr) {
        failedStage = "CreateBufferWithUnorderedAccessView";
        failedResult = E_INVALIDARG;
        return false;
    }

    *buffer = nullptr;
    *uav = nullptr;

    failedResult = device->CreateBuffer(&bufferDesc, nullptr, buffer);
    if (FAILED(failedResult)) {
        failedStage = stages.bufferStage;
        return false;
    }

    failedResult = device->CreateUnorderedAccessView(*buffer, &uavDesc, uav);
    if (FAILED(failedResult)) {
        failedStage = stages.uavStage;
        (*buffer)->Release();
        *buffer = nullptr;
        return false;
    }

    return true;
}

bool CreateDynamicConstantBuffer(
    ID3D11Device* device,
    const UINT byteWidth,
    const char* failedStageName,
    ID3D11Buffer** buffer,
    const char*& failedStage,
    HRESULT& failedResult) {
    failedStage = nullptr;
    failedResult = S_OK;
    if (buffer == nullptr) {
        failedStage = "CreateDynamicConstantBuffer";
        failedResult = E_INVALIDARG;
        return false;
    }

    *buffer = nullptr;

    D3D11_BUFFER_DESC desc {};
    desc.ByteWidth = (byteWidth + 15u) & ~15u;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    failedResult = device->CreateBuffer(&desc, nullptr, buffer);
    if (FAILED(failedResult)) {
        failedStage = failedStageName;
        return false;
    }

    return true;
}

bool WriteD3D11BufferData(
    ID3D11DeviceContext* deviceContext,
    ID3D11Buffer* buffer,
    const void* data,
    const std::size_t byteCount,
    const char* failedStageName,
    const char*& failedStage,
    HRESULT& failedResult) {
    failedStage = nullptr;
    failedResult = S_OK;
    if (deviceContext == nullptr || buffer == nullptr || data == nullptr) {
        failedStage = "WriteD3D11BufferData";
        failedResult = E_INVALIDARG;
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped {};
    failedResult = deviceContext->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(failedResult)) {
        failedStage = failedStageName;
        return false;
    }

    std::memcpy(mapped.pData, data, byteCount);
    deviceContext->Unmap(buffer, 0);
    return true;
}

bool SupportsD3D11Format(
    ID3D11Device* device,
    const DXGI_FORMAT format,
    const UINT requiredSupport) {
    UINT support = 0;
    return SUCCEEDED(device->CheckFormatSupport(format, &support))
        && (support & requiredSupport) == requiredSupport;
}

DXGI_FORMAT ChooseSupportedD3D11Format(
    ID3D11Device* device,
    const DXGI_FORMAT* candidates,
    const std::size_t candidateCount,
    const UINT requiredSupport) {
    for (std::size_t index = 0; index < candidateCount; ++index) {
        if (SupportsD3D11Format(device, candidates[index], requiredSupport)) {
            return candidates[index];
        }
    }
    return DXGI_FORMAT_UNKNOWN;
}

}  // namespace radiary
