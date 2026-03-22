#include "renderer/GpuDofRenderer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "renderer/D3D11ResourceUtils.h"
#include "renderer/D3D11ShaderUtils.h"

namespace radiary {

namespace {
constexpr wchar_t kGpuDofShaderFile[] = L"GpuDofRenderer.hlsl";

}  // namespace

GpuDofRenderer::~GpuDofRenderer() {
    Shutdown();
}

bool GpuDofRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
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

    if (!CreateShader()) {
        const std::string error = lastError_;
        Shutdown();
        lastError_ = error;
        return false;
    }

    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!CreateDynamicConstantBuffer(
            device_,
            static_cast<UINT>(sizeof(Params)),
            "CreateBuffer(DofParams)",
            &paramsBuffer_,
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        const std::string error = lastError_;
        Shutdown();
        lastError_ = error;
        return false;
    }

    return true;
}

void GpuDofRenderer::Shutdown() {
    ReleaseResources();
    if (paramsBuffer_) { paramsBuffer_->Release(); paramsBuffer_ = nullptr; }
    if (shader_) { shader_->Release(); shader_ = nullptr; }
    if (deviceContext_) { deviceContext_->Release(); deviceContext_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    outputFormat_ = DXGI_FORMAT_UNKNOWN;
    lastError_.clear();
}

bool GpuDofRenderer::Render(
    const Scene& scene,
    const int width,
    const int height,
    const GpuFrameInputs& inputs) {
    if (!IsReady()) {
        if (lastError_.empty()) {
            SetError("GPU DOF renderer is not initialized.");
        }
        return false;
    }
    if (width <= 0 || height <= 0) {
        SetError("Viewport size is invalid for GPU DOF.");
        return false;
    }
    if (!EnsureResources(width, height)) {
        return false;
    }

    Params params;
    params.width = static_cast<std::uint32_t>(width);
    params.height = static_cast<std::uint32_t>(height);
    params.useGrid = inputs.HasGrid() ? 1u : 0u;
    params.useFlame = inputs.HasFlame() ? 1u : 0u;
    params.usePath = inputs.HasPath() ? 1u : 0u;
    params.focusDepth = static_cast<float>(scene.depthOfField.focusDepth);
    params.focusRange = std::max(0.01f, static_cast<float>(scene.depthOfField.focusRange));
    params.blurStrength = static_cast<float>(std::clamp(scene.depthOfField.blurStrength, 0.0, 1.0));

    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!WriteD3D11BufferData(
            deviceContext_,
            paramsBuffer_,
            &params,
            sizeof(params),
            "Map(DofParams)",
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }

    ID3D11Buffer* constantBuffers[] = {paramsBuffer_};
    ID3D11ShaderResourceView* srvs[] = {
        inputs.gridColor,
        inputs.flameColor,
        inputs.flameDepth,
        inputs.pathColor,
        inputs.pathDepth
    };
    ID3D11UnorderedAccessView* uavs[] = {outputUav_};
    deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);
    deviceContext_->CSSetShaderResources(0, 5, srvs);
    deviceContext_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    deviceContext_->CSSetShader(shader_, nullptr, 0);
    deviceContext_->Dispatch((static_cast<std::uint32_t>(width) + 7u) / 8u, (static_cast<std::uint32_t>(height) + 7u) / 8u, 1u);

    ID3D11ShaderResourceView* nullSrvs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    ID3D11UnorderedAccessView* nullUavs[1] = {nullptr};
    deviceContext_->CSSetShader(nullptr, nullptr, 0);
    deviceContext_->CSSetShaderResources(0, 5, nullSrvs);
    deviceContext_->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
    lastError_.clear();
    return true;
}

bool GpuDofRenderer::CreateShader() {
    std::string compileError;
    const UINT compileFlags = GetDevelopmentD3D11ShaderCompileFlags(true);
    if (!CreateD3D11ComputeShaderFromFile(
            device_,
            kGpuDofShaderFile,
            "MainCS",
            compileFlags,
            "CreateComputeShader(DofCS)",
            &shader_,
            compileError)) {
        SetError(compileError.empty() ? "Failed to compile DOF compute shader." : compileError);
        return false;
    }
    return true;
}

bool GpuDofRenderer::EnsureResources(const int width, const int height) {
    if (outputTexture_ != nullptr && outputWidth_ == width && outputHeight_ == height) {
        return true;
    }

    ReleaseResources();
    outputFormat_ = ChooseOutputFormat();
    if (outputFormat_ == DXGI_FORMAT_UNKNOWN) {
        SetError("No supported UAV texture format was found for GPU DOF output.");
        return false;
    }

    D3D11_TEXTURE2D_DESC outputDesc {};
    outputDesc.Width = static_cast<UINT>(width);
    outputDesc.Height = static_cast<UINT>(height);
    outputDesc.MipLevels = 1;
    outputDesc.ArraySize = 1;
    outputDesc.Format = outputFormat_;
    outputDesc.SampleDesc.Count = 1;
    outputDesc.Usage = D3D11_USAGE_DEFAULT;
    outputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    const D3D11TextureViewStages stages {
        "CreateTexture2D(DofOutput)",
        "CreateUnorderedAccessView(DofOutput)",
        "CreateShaderResourceView(DofOutput)",
    };
    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!CreateTexture2DWithViews(
            device_,
            outputDesc,
            stages,
            &outputTexture_,
            &outputUav_,
            &outputSrv_,
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }

    outputWidth_ = width;
    outputHeight_ = height;
    return true;
}

void GpuDofRenderer::ReleaseResources() {
    if (outputSrv_) { outputSrv_->Release(); outputSrv_ = nullptr; }
    if (outputUav_) { outputUav_->Release(); outputUav_ = nullptr; }
    if (outputTexture_) { outputTexture_->Release(); outputTexture_ = nullptr; }
    outputWidth_ = 0;
    outputHeight_ = 0;
}

void GpuDofRenderer::SetError(const char* stage, const HRESULT result) {
    lastError_ = FormatD3D11StageError(stage, result);
}

DXGI_FORMAT GpuDofRenderer::ChooseOutputFormat() const {
    const std::array<DXGI_FORMAT, 2> formats = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT
    };
    constexpr UINT requiredSupport =
        D3D11_FORMAT_SUPPORT_TEXTURE2D
        | D3D11_FORMAT_SUPPORT_SHADER_LOAD
        | D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
    return ChooseSupportedD3D11Format(device_, formats.data(), formats.size(), requiredSupport);
}

}  // namespace radiary
