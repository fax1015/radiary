#include "renderer/GpuDenoiser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "renderer/D3D11ResourceUtils.h"
#include "renderer/D3D11ShaderUtils.h"
#include "renderer/GpuPassParams.h"

namespace radiary {

namespace {

constexpr DXGI_FORMAT kDenoiserDepthFormat = DXGI_FORMAT_R32_FLOAT;
constexpr wchar_t kGpuDenoiserShaderFile[] = L"GpuDenoiser.hlsl";

}  // namespace

GpuDenoiser::~GpuDenoiser() {
    Shutdown();
}

bool GpuDenoiser::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
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

    if (!CreateShaders()) {
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
            "CreateBuffer(DenoiserParams)",
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

void GpuDenoiser::Shutdown() {
    ReleaseResources();
    if (paramsBuffer_) { paramsBuffer_->Release(); paramsBuffer_ = nullptr; }
    if (filterShader_) { filterShader_->Release(); filterShader_ = nullptr; }
    if (composeShader_) { composeShader_->Release(); composeShader_ = nullptr; }
    if (deviceContext_) { deviceContext_->Release(); deviceContext_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    outputFormat_ = DXGI_FORMAT_UNKNOWN;
    lastError_.clear();
}

bool GpuDenoiser::Render(
    const Scene& scene,
    const int width,
    const int height,
    const GpuFrameInputs& inputs) {
    if (!IsReady()) {
        if (lastError_.empty()) {
            SetError("GPU Denoiser is not initialized.");
        }
        return false;
    }
    if (width <= 0 || height <= 0) {
        SetError("Viewport size is invalid for GPU Denoiser.");
        return false;
    }
    if (!EnsureResources(width, height)) {
        return false;
    }

    Params params {};
    const GpuViewportParams viewport = MakeGpuViewportParams(width, height);
    const GpuFrameLayerFlags layerFlags = MakeGpuFrameLayerFlags(inputs);
    params.width = viewport.width;
    params.height = viewport.height;
    params.useGrid = layerFlags.useGrid;
    params.useFlame = layerFlags.useFlame;
    params.usePath = layerFlags.usePath;
    params.strength = static_cast<float>(std::clamp(scene.denoiser.strength, 0.0, 1.0));
    params.sigmaSpatial = 3.0f + params.strength * 4.0f;
    params.sigmaColor = 0.15f + params.strength * 0.25f;
    params.sigmaDepth = 0.05f + params.strength * 0.1f;

    if (!DispatchComposePass(params, inputs)) {
        return false;
    }
    return DispatchFilterPasses(params);
}

bool GpuDenoiser::Compose(
    const int width,
    const int height,
    const GpuFrameInputs& inputs) {
    if (!IsReady()) {
        if (lastError_.empty()) {
            SetError("GPU Denoiser is not initialized.");
        }
        return false;
    }
    if (width <= 0 || height <= 0) {
        SetError("Viewport size is invalid for GPU composition.");
        return false;
    }
    if (!EnsureResources(width, height)) {
        return false;
    }

    Params params {};
    const GpuViewportParams viewport = MakeGpuViewportParams(width, height);
    const GpuFrameLayerFlags layerFlags = MakeGpuFrameLayerFlags(inputs);
    params.width = viewport.width;
    params.height = viewport.height;
    params.useGrid = layerFlags.useGrid;
    params.useFlame = layerFlags.useFlame;
    params.usePath = layerFlags.usePath;
    params.strength = 0.0f;
    params.sigmaSpatial = 1.0f;
    params.sigmaColor = 1.0f;
    params.sigmaDepth = 1.0f;

    return DispatchComposePass(params, inputs);
}

bool GpuDenoiser::DispatchComposePass(const Params& params, const GpuFrameInputs& inputs) {
    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!WriteD3D11BufferData(
            deviceContext_,
            paramsBuffer_,
            &params,
            sizeof(params),
            "Map(DenoiserParams)",
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }

    ID3D11Buffer* constantBuffers[] = {paramsBuffer_};
    deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);

    ID3D11ShaderResourceView* composeSrvs[] = {
        inputs.gridColor,
        inputs.flameColor,
        inputs.flameDepth,
        inputs.pathColor,
        inputs.pathDepth,
        nullptr,
        nullptr
    };
    ID3D11UnorderedAccessView* composeUavs[] = {outputUav_, depthUav_};
    deviceContext_->CSSetShaderResources(0, 7, composeSrvs);
    deviceContext_->CSSetUnorderedAccessViews(0, 2, composeUavs, nullptr);
    deviceContext_->CSSetShader(composeShader_, nullptr, 0);
    deviceContext_->Dispatch((params.width + 7u) / 8u, (params.height + 7u) / 8u, 1u);

    ID3D11ShaderResourceView* nullSrvs[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    ID3D11UnorderedAccessView* nullUavs[2] = {nullptr, nullptr};
    deviceContext_->CSSetShader(nullptr, nullptr, 0);
    deviceContext_->CSSetShaderResources(0, 7, nullSrvs);
    deviceContext_->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
    lastError_.clear();
    return true;
}

bool GpuDenoiser::DispatchFilterPasses(const Params& params) {
    const int passes = static_cast<int>(std::ceil(params.strength * 3.0f));
    if (passes <= 0) {
        lastError_.clear();
        return true;
    }

    ID3D11ShaderResourceView* nullSrvs[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    ID3D11UnorderedAccessView* nullUavs[2] = {nullptr, nullptr};
    for (int pass = 0; pass < passes; ++pass) {
        ID3D11ShaderResourceView* filterSrvs[] = {
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            (pass % 2 == 0) ? outputSrv_ : tempSrv_,
            depthSrv_
        };
        ID3D11UnorderedAccessView* filterUavs[] = {
            (pass % 2 == 0) ? tempUav_ : outputUav_,
            nullptr
        };

        deviceContext_->CSSetShaderResources(0, 7, filterSrvs);
        deviceContext_->CSSetUnorderedAccessViews(0, 2, filterUavs, nullptr);
        deviceContext_->CSSetShader(filterShader_, nullptr, 0);
        deviceContext_->Dispatch((params.width + 7u) / 8u, (params.height + 7u) / 8u, 1u);
        deviceContext_->CSSetShader(nullptr, nullptr, 0);
        deviceContext_->CSSetShaderResources(0, 7, nullSrvs);
        deviceContext_->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
    }

    if ((passes % 2) != 0) {
        deviceContext_->CopyResource(outputTexture_, tempTexture_);
    }

    lastError_.clear();
    return true;
}

bool GpuDenoiser::CreateShaders() {
    const UINT compileFlags = GetDevelopmentD3D11ShaderCompileFlags(true);
    std::string composeError;
    if (!CreateD3D11ComputeShaderFromFile(
            device_,
            kGpuDenoiserShaderFile,
            "ComposeCS",
            compileFlags,
            "CreateComputeShader(DenoiserComposeCS)",
            &composeShader_,
            composeError)) {
        SetError(composeError.empty() ? "Failed to compile Denoiser compose shader." : composeError);
        return false;
    }

    std::string filterError;
    if (!CreateD3D11ComputeShaderFromFile(
            device_,
            kGpuDenoiserShaderFile,
            "FilterCS",
            compileFlags,
            "CreateComputeShader(DenoiserFilterCS)",
            &filterShader_,
            filterError)) {
        SetError(filterError.empty() ? "Failed to compile Denoiser filter shader." : filterError);
        return false;
    }

    return true;
}

bool GpuDenoiser::EnsureResources(const int width, const int height) {
    if (outputTexture_ != nullptr && outputWidth_ == width && outputHeight_ == height) {
        return true;
    }

    ReleaseResources();
    outputFormat_ = ChooseOutputFormat();
    if (outputFormat_ == DXGI_FORMAT_UNKNOWN) {
        SetError("No supported UAV texture format was found for GPU Denoiser output.");
        return false;
    }

    constexpr UINT requiredDepthSupport =
        D3D11_FORMAT_SUPPORT_TEXTURE2D
        | D3D11_FORMAT_SUPPORT_SHADER_LOAD
        | D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
    if (!SupportsD3D11Format(device_, kDenoiserDepthFormat, requiredDepthSupport)) {
        SetError("No supported UAV texture format was found for GPU Denoiser depth output.");
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

    HRESULT result = S_OK;
    const char* failedStage = nullptr;
    const D3D11TextureViewStages outputStages {
        "CreateTexture2D(DenoiserOutput)",
        "CreateUnorderedAccessView(DenoiserOutput)",
        "CreateShaderResourceView(DenoiserOutput)",
    };
    if (!CreateTexture2DWithViews(
            device_,
            outputDesc,
            outputStages,
            &outputTexture_,
            &outputUav_,
            &outputSrv_,
            failedStage,
            result)) {
        SetError(failedStage, result);
        return false;
    }

    const D3D11TextureViewStages tempStages {
        "CreateTexture2D(DenoiserTemp)",
        "CreateUnorderedAccessView(DenoiserTemp)",
        "CreateShaderResourceView(DenoiserTemp)",
    };
    if (!CreateTexture2DWithViews(
            device_,
            outputDesc,
            tempStages,
            &tempTexture_,
            &tempUav_,
            &tempSrv_,
            failedStage,
            result)) {
        SetError(failedStage, result);
        return false;
    }

    D3D11_TEXTURE2D_DESC depthDesc = outputDesc;
    depthDesc.Format = kDenoiserDepthFormat;
    const D3D11TextureViewStages depthStages {
        "CreateTexture2D(DenoiserDepth)",
        "CreateUnorderedAccessView(DenoiserDepth)",
        "CreateShaderResourceView(DenoiserDepth)",
    };
    if (!CreateTexture2DWithViews(
            device_,
            depthDesc,
            depthStages,
            &depthTexture_,
            &depthUav_,
            &depthSrv_,
            failedStage,
            result)) {
        SetError(failedStage, result);
        return false;
    }

    outputWidth_ = width;
    outputHeight_ = height;
    return true;
}

void GpuDenoiser::ReleaseResources() {
    if (depthSrv_) { depthSrv_->Release(); depthSrv_ = nullptr; }
    if (depthUav_) { depthUav_->Release(); depthUav_ = nullptr; }
    if (depthTexture_) { depthTexture_->Release(); depthTexture_ = nullptr; }
    if (tempSrv_) { tempSrv_->Release(); tempSrv_ = nullptr; }
    if (tempUav_) { tempUav_->Release(); tempUav_ = nullptr; }
    if (tempTexture_) { tempTexture_->Release(); tempTexture_ = nullptr; }
    if (outputSrv_) { outputSrv_->Release(); outputSrv_ = nullptr; }
    if (outputUav_) { outputUav_->Release(); outputUav_ = nullptr; }
    if (outputTexture_) { outputTexture_->Release(); outputTexture_ = nullptr; }
    outputWidth_ = 0;
    outputHeight_ = 0;
}

void GpuDenoiser::SetError(const char* stage, const HRESULT result) {
    lastError_ = FormatD3D11StageError(stage, result);
}

DXGI_FORMAT GpuDenoiser::ChooseOutputFormat() const {
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
