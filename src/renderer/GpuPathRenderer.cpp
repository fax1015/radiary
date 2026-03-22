#include "renderer/GpuPathRenderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "renderer/D3D11ResourceUtils.h"
#include "renderer/D3D11ShaderUtils.h"

namespace radiary {

namespace {

constexpr float kHybridFlameDepthBias = 0.005f;
constexpr float kHybridFlameDepthSoftness = 0.03f;
constexpr wchar_t kGpuPathShaderFile[] = L"GpuPathRenderer.hlsl";

}  // namespace

GpuPathRenderer::~GpuPathRenderer() {
    Shutdown();
}

bool GpuPathRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
    Shutdown();
    lastError_.clear();
    if (device == nullptr || deviceContext == nullptr) {
        SetError("D3D11 device/context is not available.");
        return false;
    }

    device_ = device;
    deviceContext_ = deviceContext;
    device_->AddRef();
    deviceContext_->AddRef();

    if (!CreatePipeline()) {
        Shutdown();
        return false;
    }

    return true;
}

void GpuPathRenderer::Shutdown() {
    ReleaseResources();
    ReleasePipeline();
    if (deviceContext_) { deviceContext_->Release(); deviceContext_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    lastError_.clear();
}

bool GpuPathRenderer::Render(
    const Scene& scene,
    const int width,
    const int height,
    const bool transparentBackground,
    const bool renderGrid,
    ID3D11ShaderResourceView* flameDepthSrv) {
    const PathDrawList drawList = PathDrawListBuilder::Build(scene, width, height, renderGrid);
    return RenderDrawList(scene.backgroundColor, width, height, transparentBackground, drawList, flameDepthSrv);
}

bool GpuPathRenderer::RenderDrawList(
    const Color& backgroundColor,
    const int width,
    const int height,
    const bool transparentBackground,
    const PathDrawList& drawList,
    ID3D11ShaderResourceView* flameDepthSrv) {
    if (!IsReady()) {
        if (lastError_.empty()) {
            SetError("GPU path renderer is not initialized.");
        }
        return false;
    }
    if (width <= 0 || height <= 0) {
        SetError("Viewport size is invalid for GPU path preview.");
        return false;
    }
    if (!EnsureResources(width, height)) {
        return false;
    }

    const float clearColor[4] = {
        transparentBackground ? 0.0f : (static_cast<float>(backgroundColor.r) / 255.0f),
        transparentBackground ? 0.0f : (static_cast<float>(backgroundColor.g) / 255.0f),
        transparentBackground ? 0.0f : (static_cast<float>(backgroundColor.b) / 255.0f),
        transparentBackground ? 0.0f : 1.0f
    };
    deviceContext_->OMSetRenderTargets(1, &outputRtv_, depthDsv_);
    deviceContext_->ClearRenderTargetView(outputRtv_, clearColor);
    deviceContext_->ClearDepthStencilView(depthDsv_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT viewport {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    deviceContext_->RSSetViewports(1, &viewport);
    deviceContext_->IASetInputLayout(inputLayout_);
    deviceContext_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    deviceContext_->VSSetShader(vertexShader_, nullptr, 0);
    deviceContext_->PSSetShader(pixelShader_, nullptr, 0);
    deviceContext_->RSSetState(rasterizerState_);
    struct HybridParams {
        float flameDepthBias = 0.0f;
        float flameDepthSoftness = 0.0f;
        std::uint32_t useFlameOcclusion = 0u;
        float padding = 0.0f;
    } hybridParams;
    hybridParams.flameDepthBias = kHybridFlameDepthBias;
    hybridParams.flameDepthSoftness = kHybridFlameDepthSoftness;
    hybridParams.useFlameOcclusion = flameDepthSrv != nullptr ? 1u : 0u;

    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!WriteD3D11BufferData(
            deviceContext_,
            hybridParamsBuffer_,
            &hybridParams,
            sizeof(hybridParams),
            "Map(HybridParams)",
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }
    deviceContext_->PSSetConstantBuffers(0, 1, &hybridParamsBuffer_);
    deviceContext_->PSSetShaderResources(0, 1, &flameDepthSrv);
    const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    deviceContext_->OMSetBlendState(blendState_, blendFactor, 0xFFFFFFFFu);

    if (!DrawDrawList(drawList)) {
        return false;
    }

    ID3D11RenderTargetView* nullRtv = nullptr;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    deviceContext_->OMSetRenderTargets(1, &nullRtv, nullptr);
    deviceContext_->PSSetShaderResources(0, 1, &nullSrv);
    lastError_.clear();
    return true;
}

bool GpuPathRenderer::DrawDrawList(const PathDrawList& drawList) {
    const std::size_t totalVertexCount = drawList.TotalVertexCount();
    if (totalVertexCount == 0) {
        return true;
    }
    if (!EnsureVertexBuffer(totalVertexCount)) {
        return false;
    }
    if (!UploadDrawListVertices(drawList)) {
        return false;
    }
    DrawUploadedDrawList(drawList);
    return true;
}

bool GpuPathRenderer::UploadDrawListVertices(const PathDrawList& drawList) {
    D3D11_MAPPED_SUBRESOURCE mapped {};
    const HRESULT mapResult = deviceContext_->Map(vertexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        SetError("Map(PathVertexBuffer)", mapResult);
        return false;
    }

    auto* dst = static_cast<Vertex*>(mapped.pData);
    std::size_t offset = 0;
    if (!drawList.gridVertices.empty()) {
        std::memcpy(dst + offset, drawList.gridVertices.data(), drawList.gridVertices.size() * sizeof(Vertex));
        offset += drawList.gridVertices.size();
    }
    if (!drawList.fillVertices.empty()) {
        std::memcpy(dst + offset, drawList.fillVertices.data(), drawList.fillVertices.size() * sizeof(Vertex));
        offset += drawList.fillVertices.size();
    }
    if (!drawList.overlayVertices.empty()) {
        std::memcpy(dst + offset, drawList.overlayVertices.data(), drawList.overlayVertices.size() * sizeof(Vertex));
        offset += drawList.overlayVertices.size();
    }
    if (!drawList.pointVertices.empty()) {
        std::memcpy(dst + offset, drawList.pointVertices.data(), drawList.pointVertices.size() * sizeof(Vertex));
    }
    deviceContext_->Unmap(vertexBuffer_, 0);

    const UINT stride = sizeof(Vertex);
    const UINT zero = 0;
    deviceContext_->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &zero);
    return true;
}

void GpuPathRenderer::DrawUploadedDrawList(const PathDrawList& drawList) {
    UINT drawOffset = 0;
    if (!drawList.gridVertices.empty()) {
        deviceContext_->OMSetDepthStencilState(depthDisabledState_, 0);
        deviceContext_->Draw(static_cast<UINT>(drawList.gridVertices.size()), drawOffset);
        drawOffset += static_cast<UINT>(drawList.gridVertices.size());
    }
    if (!drawList.fillVertices.empty()) {
        deviceContext_->OMSetDepthStencilState(depthWriteState_, 0);
        deviceContext_->Draw(static_cast<UINT>(drawList.fillVertices.size()), drawOffset);
        drawOffset += static_cast<UINT>(drawList.fillVertices.size());
    }
    if (!drawList.overlayVertices.empty()) {
        deviceContext_->OMSetDepthStencilState(drawList.fillVertices.empty() ? depthWriteState_ : depthReadState_, 0);
        deviceContext_->Draw(static_cast<UINT>(drawList.overlayVertices.size()), drawOffset);
        drawOffset += static_cast<UINT>(drawList.overlayVertices.size());
    }
    if (!drawList.pointVertices.empty()) {
        deviceContext_->OMSetDepthStencilState(drawList.fillVertices.empty() ? depthWriteState_ : depthReadState_, 0);
        deviceContext_->Draw(static_cast<UINT>(drawList.pointVertices.size()), drawOffset);
    }
}

bool GpuPathRenderer::CreatePipeline() {
    std::string error;
    ID3DBlob* vertexBlob = CompileD3D11ShaderFromFile(
        kGpuPathShaderFile,
        "MainVS",
        "vs_5_0",
        GetOptimizedD3D11ShaderCompileFlags(),
        error);
    if (vertexBlob == nullptr) {
        SetError(error.empty() ? "Failed to compile path vertex shader." : error);
        return false;
    }
    ID3DBlob* pixelBlob = CompileD3D11ShaderFromFile(
        kGpuPathShaderFile,
        "MainPS",
        "ps_5_0",
        GetOptimizedD3D11ShaderCompileFlags(),
        error);
    if (pixelBlob == nullptr) {
        vertexBlob->Release();
        SetError(error.empty() ? "Failed to compile path pixel shader." : error);
        return false;
    }

    HRESULT result = device_->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &vertexShader_);
    if (FAILED(result)) {
        vertexBlob->Release();
        pixelBlob->Release();
        SetError("CreateVertexShader(GpuPath)", result);
        return false;
    }
    result = device_->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &pixelShader_);
    if (FAILED(result)) {
        vertexBlob->Release();
        pixelBlob->Release();
        SetError("CreatePixelShader(GpuPath)", result);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    result = device_->CreateInputLayout(inputElements, 2, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), &inputLayout_);
    vertexBlob->Release();
    pixelBlob->Release();
    if (FAILED(result)) {
        SetError("CreateInputLayout(GpuPath)", result);
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    result = device_->CreateRasterizerState(&rasterizerDesc, &rasterizerState_);
    if (FAILED(result)) {
        SetError("CreateRasterizerState(GpuPath)", result);
        return false;
    }

    D3D11_BLEND_DESC blendDesc {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    result = device_->CreateBlendState(&blendDesc, &blendState_);
    if (FAILED(result)) {
        SetError("CreateBlendState(GpuPath)", result);
        return false;
    }

    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!CreateDynamicConstantBuffer(
            device_,
            16u,
            "CreateBuffer(HybridParams)",
            &hybridParamsBuffer_,
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthDesc {};
    depthDesc.DepthEnable = FALSE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    result = device_->CreateDepthStencilState(&depthDesc, &depthDisabledState_);
    if (FAILED(result)) {
        SetError("CreateDepthStencilState(Grid)", result);
        return false;
    }

    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    result = device_->CreateDepthStencilState(&depthDesc, &depthWriteState_);
    if (FAILED(result)) {
        SetError("CreateDepthStencilState(Write)", result);
        return false;
    }

    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    result = device_->CreateDepthStencilState(&depthDesc, &depthReadState_);
    if (FAILED(result)) {
        SetError("CreateDepthStencilState(Read)", result);
        return false;
    }

    return true;
}

bool GpuPathRenderer::EnsureResources(const int width, const int height) {
    if (outputTexture_ != nullptr && outputWidth_ == width && outputHeight_ == height) {
        return true;
    }

    ReleaseResources();

    D3D11_TEXTURE2D_DESC outputDesc {};
    outputDesc.Width = static_cast<UINT>(width);
    outputDesc.Height = static_cast<UINT>(height);
    outputDesc.MipLevels = 1;
    outputDesc.ArraySize = 1;
    outputDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    outputDesc.SampleDesc.Count = 1;
    outputDesc.Usage = D3D11_USAGE_DEFAULT;
    outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    const D3D11RenderTargetViewStages outputStages {
        "CreateTexture2D(PathOutput)",
        "CreateRenderTargetView(PathOutput)",
        "CreateShaderResourceView(PathOutput)",
    };
    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!CreateRenderTargetTexture2DWithViews(
            device_,
            outputDesc,
            outputStages,
            &outputTexture_,
            &outputRtv_,
            &outputSrv_,
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }

    D3D11_TEXTURE2D_DESC depthDesc {};
    depthDesc.Width = static_cast<UINT>(width);
    depthDesc.Height = static_cast<UINT>(height);
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC depthDsvDesc {};
    depthDsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    depthDsvDesc.Texture2D.MipSlice = 0;
    D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc {};
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MostDetailedMip = 0;
    depthSrvDesc.Texture2D.MipLevels = 1;
    const D3D11DepthStencilViewStages depthStages {
        "CreateTexture2D(PathDepth)",
        "CreateDepthStencilView(PathDepth)",
        "CreateShaderResourceView(PathDepth)",
    };
    if (!CreateDepthStencilTexture2DWithViews(
            device_,
            depthDesc,
            depthDsvDesc,
            depthSrvDesc,
            depthStages,
            &depthTexture_,
            &depthDsv_,
            &depthSrv_,
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }

    outputWidth_ = width;
    outputHeight_ = height;
    return true;
}

bool GpuPathRenderer::EnsureVertexBuffer(const std::size_t vertexCount) {
    if (vertexBuffer_ != nullptr && vertexCount <= vertexCapacity_) {
        return true;
    }

    if (vertexBuffer_) {
        vertexBuffer_->Release();
        vertexBuffer_ = nullptr;
        vertexCapacity_ = 0;
    }

    vertexCapacity_ = std::max<std::size_t>(vertexCount, 1024);
    D3D11_BUFFER_DESC bufferDesc {};
    bufferDesc.ByteWidth = static_cast<UINT>(vertexCapacity_ * sizeof(Vertex));
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT result = device_->CreateBuffer(&bufferDesc, nullptr, &vertexBuffer_);
    if (FAILED(result)) {
        SetError("CreateBuffer(PathVertexBuffer)", result);
        return false;
    }
    return true;
}

void GpuPathRenderer::ReleasePipeline() {
    if (vertexBuffer_) { vertexBuffer_->Release(); vertexBuffer_ = nullptr; }
    vertexCapacity_ = 0;
    if (hybridParamsBuffer_) { hybridParamsBuffer_->Release(); hybridParamsBuffer_ = nullptr; }
    if (depthReadState_) { depthReadState_->Release(); depthReadState_ = nullptr; }
    if (depthWriteState_) { depthWriteState_->Release(); depthWriteState_ = nullptr; }
    if (depthDisabledState_) { depthDisabledState_->Release(); depthDisabledState_ = nullptr; }
    if (blendState_) { blendState_->Release(); blendState_ = nullptr; }
    if (rasterizerState_) { rasterizerState_->Release(); rasterizerState_ = nullptr; }
    if (inputLayout_) { inputLayout_->Release(); inputLayout_ = nullptr; }
    if (pixelShader_) { pixelShader_->Release(); pixelShader_ = nullptr; }
    if (vertexShader_) { vertexShader_->Release(); vertexShader_ = nullptr; }
}

void GpuPathRenderer::ReleaseResources() {
    if (depthDsv_) { depthDsv_->Release(); depthDsv_ = nullptr; }
    if (depthSrv_) { depthSrv_->Release(); depthSrv_ = nullptr; }
    if (depthTexture_) { depthTexture_->Release(); depthTexture_ = nullptr; }
    if (outputSrv_) { outputSrv_->Release(); outputSrv_ = nullptr; }
    if (outputRtv_) { outputRtv_->Release(); outputRtv_ = nullptr; }
    if (outputTexture_) { outputTexture_->Release(); outputTexture_ = nullptr; }
    outputWidth_ = 0;
    outputHeight_ = 0;
}

void GpuPathRenderer::SetError(const char* stage, const HRESULT result) {
    lastError_ = FormatD3D11StageError(stage, result);
}

}  // namespace radiary
