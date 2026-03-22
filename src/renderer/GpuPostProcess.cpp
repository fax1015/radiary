#include "renderer/GpuPostProcess.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "renderer/D3D11ResourceUtils.h"
#include "renderer/D3D11ShaderUtils.h"

namespace radiary {

namespace {

constexpr wchar_t kGpuPostProcessShaderFile[] = L"GpuPostProcess.hlsl";

}  // namespace

GpuPostProcess::~GpuPostProcess() {
    Shutdown();
}

bool GpuPostProcess::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
    static_assert(sizeof(Params) % 16 == 0, "Params must be 16-byte aligned for D3D11 constant buffers.");
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
        Shutdown();
        return false;
    }

    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!CreateDynamicConstantBuffer(
            device_,
            static_cast<UINT>(sizeof(Params)),
            "CreateBuffer(PostProcessParams)",
            &paramsBuffer_,
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        Shutdown();
        return false;
    }

    lastError_.clear();
    return true;
}

void GpuPostProcess::Shutdown() {
    ReleaseResources();
    if (bloomUpShader_) { bloomUpShader_->Release(); bloomUpShader_ = nullptr; }
    if (bloomDownShader_) { bloomDownShader_->Release(); bloomDownShader_ = nullptr; }
    if (postProcessShader_) { postProcessShader_->Release(); postProcessShader_ = nullptr; }
    if (paramsBuffer_) { paramsBuffer_->Release(); paramsBuffer_ = nullptr; }
    if (deviceContext_) { deviceContext_->Release(); deviceContext_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

bool GpuPostProcess::Render(
    const Scene& scene,
    const int width,
    const int height,
    const GpuPostProcessInputs& inputs) {
    ID3D11ShaderResourceView* inputSrv = inputs.sourceColor;
    const std::optional<std::uint32_t>& randomSeedOverride = inputs.randomSeedOverride;

    if (!IsReady()) {
        if (lastError_.empty()) SetError("GPU post-process renderer is not initialized.");
        return false;
    }
    if (width <= 0 || height <= 0 || inputSrv == nullptr) {
        SetError("Invalid input for GPU post-processing.");
        return false;
    }
    if (!EnsureResources(width, height)) {
        return false;
    }

    if (!randomSeedOverride.has_value()) {
        ++frameCounter_;
    }
    const PostProcessSettings& pp = scene.postProcess;

    ID3D11Buffer* constantBuffers[] = {paramsBuffer_};
    ID3D11ShaderResourceView* nullSrvs[2] = {nullptr, nullptr};
    ID3D11UnorderedAccessView* nullUavs[1] = {nullptr};

    // Bloom downsample pass
    if (pp.bloomIntensity > 0.001) {
        int mipW = std::max(1, width / 2);
        int mipH = std::max(1, height / 2);

        // First downsample from input
        {
            Params params {};
            params.width = static_cast<std::uint32_t>(width);
            params.height = static_cast<std::uint32_t>(height);
            params.bloomThreshold = static_cast<float>(pp.bloomThreshold);
            params.mipWidth = static_cast<std::uint32_t>(mipW);
            params.mipHeight = static_cast<std::uint32_t>(mipH);
            const char* failedStage = nullptr;
            HRESULT failedResult = S_OK;
            if (!WriteD3D11BufferData(deviceContext_, paramsBuffer_, &params, sizeof(params), "Map(paramsBuffer bloom down)", failedStage, failedResult)) {
                SetError(failedStage, failedResult);
                return false;
            }

            ID3D11ShaderResourceView* srvs[] = {inputSrv, nullptr};
            ID3D11UnorderedAccessView* uavs[] = {bloomUavs_[0]};
            deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);
            deviceContext_->CSSetShaderResources(0, 2, srvs);
            deviceContext_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            deviceContext_->CSSetShader(bloomDownShader_, nullptr, 0);
            deviceContext_->Dispatch(
                (static_cast<std::uint32_t>(mipW) + 7u) / 8u,
                (static_cast<std::uint32_t>(mipH) + 7u) / 8u, 1u);
            deviceContext_->CSSetShaderResources(0, 2, nullSrvs);
            deviceContext_->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
        }

        // Additional downsamples
        for (int i = 1; i < kBloomMipCount; ++i) {
            int prevW = mipW;
            int prevH = mipH;
            mipW = std::max(1, mipW / 2);
            mipH = std::max(1, mipH / 2);

            Params params {};
            params.width = static_cast<std::uint32_t>(prevW);
            params.height = static_cast<std::uint32_t>(prevH);
            params.bloomThreshold = 0.0f;
            params.mipWidth = static_cast<std::uint32_t>(mipW);
            params.mipHeight = static_cast<std::uint32_t>(mipH);
            const char* failedStage = nullptr;
            HRESULT failedResult = S_OK;
            if (!WriteD3D11BufferData(deviceContext_, paramsBuffer_, &params, sizeof(params), "Map(paramsBuffer bloom down mip)", failedStage, failedResult)) {
                SetError(failedStage, failedResult);
                return false;
            }

            ID3D11ShaderResourceView* srvs[] = {bloomSrvs_[i - 1], nullptr};
            ID3D11UnorderedAccessView* uavs[] = {bloomUavs_[i]};
            deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);
            deviceContext_->CSSetShaderResources(0, 2, srvs);
            deviceContext_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            deviceContext_->CSSetShader(bloomDownShader_, nullptr, 0);
            deviceContext_->Dispatch(
                (static_cast<std::uint32_t>(mipW) + 7u) / 8u,
                (static_cast<std::uint32_t>(mipH) + 7u) / 8u, 1u);
            deviceContext_->CSSetShaderResources(0, 2, nullSrvs);
            deviceContext_->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
        }

        // Bloom upsample passes
        for (int i = kBloomMipCount - 2; i >= 0; --i) {
            int upMipW = width;
            int upMipH = height;
            for (int j = 0; j <= i; ++j) {
                upMipW = std::max(1, upMipW / 2);
                upMipH = std::max(1, upMipH / 2);
            }

            Params params {};
            params.mipWidth = static_cast<std::uint32_t>(upMipW);
            params.mipHeight = static_cast<std::uint32_t>(upMipH);
            const char* failedStage = nullptr;
            HRESULT failedResult = S_OK;
            if (!WriteD3D11BufferData(deviceContext_, paramsBuffer_, &params, sizeof(params), "Map(paramsBuffer bloom up)", failedStage, failedResult)) {
                SetError(failedStage, failedResult);
                return false;
            }

            ID3D11ShaderResourceView* srvs[] = {bloomSrvs_[i + 1], bloomSrvs_[i]};
            ID3D11UnorderedAccessView* uavs[] = {bloomUavs_[i]};
            deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);
            deviceContext_->CSSetShaderResources(0, 2, srvs);
            deviceContext_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            deviceContext_->CSSetShader(bloomUpShader_, nullptr, 0);
            deviceContext_->Dispatch(
                (static_cast<std::uint32_t>(upMipW) + 7u) / 8u,
                (static_cast<std::uint32_t>(upMipH) + 7u) / 8u, 1u);
            deviceContext_->CSSetShaderResources(0, 2, nullSrvs);
            deviceContext_->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
        }
    }

    // Final composite pass
    {
        Params params {};
        params.width = static_cast<std::uint32_t>(width);
        params.height = static_cast<std::uint32_t>(height);
        params.bloomIntensity = static_cast<float>(pp.bloomIntensity);
        params.bloomThreshold = static_cast<float>(pp.bloomThreshold);
        params.chromaticAberration = static_cast<float>(pp.chromaticAberration);
        params.vignetteIntensity = static_cast<float>(pp.vignetteIntensity);
        params.vignetteRoundness = static_cast<float>(pp.vignetteRoundness);
        params.acesToneMap = pp.acesToneMap ? 1u : 0u;
        params.filmGrain = static_cast<float>(pp.filmGrain);
        params.colorTemperature = static_cast<float>(pp.colorTemperature);
        params.saturationBoost = static_cast<float>(pp.saturationBoost);
        params.randomSeed = randomSeedOverride.value_or(frameCounter_ * 1664525u + 1013904223u);

        const char* failedStage = nullptr;
        HRESULT failedResult = S_OK;
        if (!WriteD3D11BufferData(deviceContext_, paramsBuffer_, &params, sizeof(params), "Map(paramsBuffer composite)", failedStage, failedResult)) {
            SetError(failedStage, failedResult);
            return false;
        }

        ID3D11ShaderResourceView* srvs[] = {inputSrv, (pp.bloomIntensity > 0.001) ? bloomSrvs_[0] : nullptr};
        ID3D11UnorderedAccessView* uavs[] = {outputUav_};
        deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);
        deviceContext_->CSSetShaderResources(0, 2, srvs);
        deviceContext_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        deviceContext_->CSSetShader(postProcessShader_, nullptr, 0);
        deviceContext_->Dispatch(
            (static_cast<std::uint32_t>(width) + 7u) / 8u,
            (static_cast<std::uint32_t>(height) + 7u) / 8u, 1u);
        deviceContext_->CSSetShader(nullptr, nullptr, 0);
        deviceContext_->CSSetShaderResources(0, 2, nullSrvs);
        deviceContext_->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
    }

    lastError_.clear();
    return true;
}

bool GpuPostProcess::CreateShaders() {
    std::string compileError;
    const UINT compileFlags = GetOptimizedD3D11ShaderCompileFlags(false, true);

    if (!CreateD3D11ComputeShaderFromFile(
            device_,
            kGpuPostProcessShaderFile,
            "PostProcessCS",
            compileFlags,
            "CreateComputeShader(PostProcessCS)",
            &postProcessShader_,
            compileError)) {
        SetError(compileError.empty() ? "Failed to compile PostProcessCS." : compileError);
        return false;
    }

    if (!CreateD3D11ComputeShaderFromFile(
            device_,
            kGpuPostProcessShaderFile,
            "BloomDownCS",
            compileFlags,
            "CreateComputeShader(BloomDownCS)",
            &bloomDownShader_,
            compileError)) {
        SetError(compileError.empty() ? "Failed to compile BloomDownCS." : compileError);
        return false;
    }

    if (!CreateD3D11ComputeShaderFromFile(
            device_,
            kGpuPostProcessShaderFile,
            "BloomUpCS",
            compileFlags,
            "CreateComputeShader(BloomUpCS)",
            &bloomUpShader_,
            compileError)) {
        SetError(compileError.empty() ? "Failed to compile BloomUpCS." : compileError);
        return false;
    }

    return true;
}

bool GpuPostProcess::EnsureResources(const int width, const int height) {
    if (outputTexture_ != nullptr && outputWidth_ == width && outputHeight_ == height) {
        return true;
    }

    ReleaseResources();

    const DXGI_FORMAT format = ChooseOutputFormat();
    if (format == DXGI_FORMAT_UNKNOWN) {
        SetError("No supported UAV texture format for post-processing.");
        return false;
    }
    outputFormat_ = format;

    D3D11_TEXTURE2D_DESC outputDesc {};
    outputDesc.Width = static_cast<UINT>(width);
    outputDesc.Height = static_cast<UINT>(height);
    outputDesc.MipLevels = 1;
    outputDesc.ArraySize = 1;
    outputDesc.Format = format;
    outputDesc.SampleDesc.Count = 1;
    outputDesc.Usage = D3D11_USAGE_DEFAULT;
    outputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    HRESULT result = S_OK;
    const char* failedStage = nullptr;
    const D3D11TextureViewStages outputStages {
        "CreateTexture2D(postprocess output)",
        "CreateUAV(postprocess output)",
        "CreateSRV(postprocess output)",
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

    int mipW = width;
    int mipH = height;
    for (int i = 0; i < kBloomMipCount; ++i) {
        mipW = std::max(1, mipW / 2);
        mipH = std::max(1, mipH / 2);
        D3D11_TEXTURE2D_DESC bloomDesc {};
        bloomDesc.Width = static_cast<UINT>(mipW);
        bloomDesc.Height = static_cast<UINT>(mipH);
        bloomDesc.MipLevels = 1;
        bloomDesc.ArraySize = 1;
        bloomDesc.Format = format;
        bloomDesc.SampleDesc.Count = 1;
        bloomDesc.Usage = D3D11_USAGE_DEFAULT;
        bloomDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        const D3D11TextureViewStages bloomStages {
            "CreateTexture2D(bloom mip)",
            "CreateUAV(bloom mip)",
            "CreateSRV(bloom mip)",
        };
        if (!CreateTexture2DWithViews(
                device_,
                bloomDesc,
                bloomStages,
                &bloomTextures_[i],
                &bloomUavs_[i],
                &bloomSrvs_[i],
                failedStage,
                result)) {
            SetError(failedStage, result);
            return false;
        }
    }

    outputWidth_ = width;
    outputHeight_ = height;
    return true;
}

void GpuPostProcess::ReleaseResources() {
    for (int i = 0; i < kBloomMipCount; ++i) {
        if (bloomSrvs_[i]) { bloomSrvs_[i]->Release(); bloomSrvs_[i] = nullptr; }
        if (bloomUavs_[i]) { bloomUavs_[i]->Release(); bloomUavs_[i] = nullptr; }
        if (bloomTextures_[i]) { bloomTextures_[i]->Release(); bloomTextures_[i] = nullptr; }
    }
    if (outputSrv_) { outputSrv_->Release(); outputSrv_ = nullptr; }
    if (outputUav_) { outputUav_->Release(); outputUav_ = nullptr; }
    if (outputTexture_) { outputTexture_->Release(); outputTexture_ = nullptr; }
    outputWidth_ = 0;
    outputHeight_ = 0;
}

void GpuPostProcess::SetError(const char* stage, const HRESULT result) {
    lastError_ = FormatD3D11StageError(stage, result);
}

DXGI_FORMAT GpuPostProcess::ChooseOutputFormat() const {
    constexpr DXGI_FORMAT candidates[] = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT
    };
    constexpr UINT requiredSupport =
        D3D11_FORMAT_SUPPORT_TEXTURE2D
        | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE
        | D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
    return ChooseSupportedD3D11Format(device_, candidates, std::size(candidates), requiredSupport);
}

}  // namespace radiary
