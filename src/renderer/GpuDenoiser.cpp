#include "renderer/GpuDenoiser.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace radiary {

namespace {

constexpr DXGI_FORMAT kDenoiserDepthFormat = DXGI_FORMAT_R32_FLOAT;

constexpr char kGpuDenoiserShaderSource[] = R"(
cbuffer DenoiserParams : register(b0)
{
    uint Width;
    uint Height;
    uint UseGrid;
    uint UseFlame;
    uint UsePath;
    float Strength;
    float SigmaSpatial;
    float SigmaColor;
    float SigmaDepth;
    float3 Padding;
};

Texture2D<float4> GridTexture : register(t0);
Texture2D<float4> FlameTexture : register(t1);
Texture2D<float> FlameDepthTexture : register(t2);
Texture2D<float4> PathTexture : register(t3);
Texture2D<float> PathDepthTexture : register(t4);
Texture2D<float4> InputTexture : register(t5);
Texture2D<float> InputDepthTexture : register(t6);

RWTexture2D<float4> OutputTexture : register(u0);
RWTexture2D<float> OutputDepthTexture : register(u1);

float4 AlphaOver(float4 baseColor, float4 overlayColor)
{
    return float4(
        overlayColor.rgb + baseColor.rgb * (1.0 - overlayColor.a),
        overlayColor.a + baseColor.a * (1.0 - overlayColor.a));
}

float4 ComposeColor(int2 pixel)
{
    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    if (UseGrid != 0u)
    {
        color = GridTexture.Load(int3(pixel, 0));
    }
    if (UseFlame != 0u)
    {
        float4 flame = FlameTexture.Load(int3(pixel, 0));
        color = (UseGrid != 0u) ? AlphaOver(color, flame) : flame;
    }
    if (UsePath != 0u)
    {
        color = AlphaOver(color, PathTexture.Load(int3(pixel, 0)));
    }
    return color;
}

float ComposeDepth(int2 pixel)
{
    if (UsePath != 0u)
    {
        float4 path = PathTexture.Load(int3(pixel, 0));
        if (path.a > 0.001)
        {
            return PathDepthTexture.Load(int3(pixel, 0));
        }
    }

    if (UseFlame != 0u)
    {
        float flameDepth = FlameDepthTexture.Load(int3(pixel, 0));
        if (flameDepth < 0.999)
        {
            return flameDepth;
        }
    }

    return 1.0;
}

[numthreads(8, 8, 1)]
void ComposeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    OutputTexture[pixel] = ComposeColor(pixel);
    OutputDepthTexture[pixel] = ComposeDepth(pixel);
}

[numthreads(8, 8, 1)]
void FilterCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    float4 centerColor = InputTexture.Load(int3(pixel, 0));
    float centerDepth = InputDepthTexture.Load(int3(pixel, 0));

    if (Strength <= 0.0)
    {
        OutputTexture[pixel] = centerColor;
        return;
    }

    int radius = clamp((int)ceil(SigmaSpatial * 2.0), 1, 8);
    int blurRadius = clamp((int)ceil(SigmaSpatial), 1, 4);
    float invSpatialVar = 1.0 / (2.0 * SigmaSpatial * SigmaSpatial);
    float invColorVar = 1.0 / (2.0 * SigmaColor * SigmaColor);
    float invDepthVar = 1.0 / (2.0 * SigmaDepth * SigmaDepth);

    float4 meanColor = float4(0.0, 0.0, 0.0, 0.0);
    float4 m2Color = float4(0.0, 0.0, 0.0, 0.0);
    float weightSum = 0.0;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            int2 samplePixel = pixel + int2(x, y);
            samplePixel.x = clamp(samplePixel.x, 0, (int)Width - 1);
            samplePixel.y = clamp(samplePixel.y, 0, (int)Height - 1);

            float4 sampleColor = InputTexture.Load(int3(samplePixel, 0));
            float spatialDistSq = (float)(x * x + y * y);
            float spatialWeight = exp(-spatialDistSq * invSpatialVar);
            float weight = spatialWeight * (sampleColor.a > 0.01 ? 1.0 : 0.001);

            meanColor += sampleColor * weight;
            m2Color += sampleColor * sampleColor * weight;
            weightSum += weight;
        }
    }

    meanColor /= max(weightSum, 1.0e-6);
    m2Color /= max(weightSum, 1.0e-6);

    float4 variance = max(float4(0.0, 0.0, 0.0, 0.0), m2Color - meanColor * meanColor);
    float4 stdDev = sqrt(variance);
    float thresholdMult = lerp(4.0, 0.5, saturate(Strength));
    float4 minColor = meanColor - stdDev * thresholdMult;
    float4 maxColor = meanColor + stdDev * thresholdMult;
    float4 filteredColor = clamp(centerColor, minColor, maxColor);

    float4 accumColor = float4(0.0, 0.0, 0.0, 0.0);
    float blurWeightSum = 0.0;

    for (int by = -blurRadius; by <= blurRadius; ++by)
    {
        for (int bx = -blurRadius; bx <= blurRadius; ++bx)
        {
            int2 samplePixel = pixel + int2(bx, by);
            samplePixel.x = clamp(samplePixel.x, 0, (int)Width - 1);
            samplePixel.y = clamp(samplePixel.y, 0, (int)Height - 1);

            float4 sampleColor = InputTexture.Load(int3(samplePixel, 0));
            sampleColor = clamp(sampleColor, minColor, maxColor);
            float sampleDepth = InputDepthTexture.Load(int3(samplePixel, 0));

            float spatialDistSq = (float)(bx * bx + by * by);
            float spatialWeight = exp(-spatialDistSq * invSpatialVar);

            float depthDiff = sampleDepth - centerDepth;
            float depthWeight = exp(-(depthDiff * depthDiff) * invDepthVar);

            float3 colorDiff = sampleColor.rgb - filteredColor.rgb;
            float colorDistSq = dot(colorDiff, colorDiff);
            float colorWeight = exp(-colorDistSq * invColorVar);

            float weight = spatialWeight * depthWeight * colorWeight;
            accumColor += sampleColor * weight;
            blurWeightSum += weight;
        }
    }

    if (blurWeightSum > 1.0e-6)
    {
        filteredColor = lerp(filteredColor, accumColor / blurWeightSum, saturate(Strength * 1.5));
    }

    OutputTexture[pixel] = filteredColor;
}
)";

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

    D3D11_BUFFER_DESC paramsDesc {};
    paramsDesc.ByteWidth = (static_cast<UINT>(sizeof(Params)) + 15u) & ~15u;
    paramsDesc.Usage = D3D11_USAGE_DYNAMIC;
    paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    paramsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT result = device_->CreateBuffer(&paramsDesc, nullptr, &paramsBuffer_);
    if (FAILED(result)) {
        SetError("CreateBuffer(DenoiserParams)", result);
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
    ID3D11ShaderResourceView* gridSrv,
    ID3D11ShaderResourceView* flameSrv,
    ID3D11ShaderResourceView* flameDepthSrv,
    ID3D11ShaderResourceView* pathSrv,
    ID3D11ShaderResourceView* pathDepthSrv) {
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

    Params params;
    params.width = static_cast<std::uint32_t>(width);
    params.height = static_cast<std::uint32_t>(height);
    params.useGrid = gridSrv != nullptr ? 1u : 0u;
    params.useFlame = (flameSrv != nullptr && flameDepthSrv != nullptr) ? 1u : 0u;
    params.usePath = (pathSrv != nullptr && pathDepthSrv != nullptr) ? 1u : 0u;
    params.strength = static_cast<float>(std::clamp(scene.denoiser.strength, 0.0, 1.0));
    params.sigmaSpatial = 3.0f + params.strength * 4.0f;
    params.sigmaColor = 0.15f + params.strength * 0.25f;
    params.sigmaDepth = 0.05f + params.strength * 0.1f;

    D3D11_MAPPED_SUBRESOURCE mapped {};
    HRESULT result = deviceContext_->Map(paramsBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result)) {
        SetError("Map(DenoiserParams)", result);
        return false;
    }
    std::memcpy(mapped.pData, &params, sizeof(params));
    deviceContext_->Unmap(paramsBuffer_, 0);

    ID3D11Buffer* constantBuffers[] = {paramsBuffer_};
    deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);

    ID3D11ShaderResourceView* composeSrvs[] = {gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv, nullptr, nullptr};
    ID3D11UnorderedAccessView* composeUavs[] = {outputUav_, depthUav_};
    deviceContext_->CSSetShaderResources(0, 7, composeSrvs);
    deviceContext_->CSSetUnorderedAccessViews(0, 2, composeUavs, nullptr);
    deviceContext_->CSSetShader(composeShader_, nullptr, 0);
    deviceContext_->Dispatch((static_cast<std::uint32_t>(width) + 7u) / 8u, (static_cast<std::uint32_t>(height) + 7u) / 8u, 1u);

    ID3D11ShaderResourceView* nullSrvs[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    ID3D11UnorderedAccessView* nullUavs[2] = {nullptr, nullptr};
    deviceContext_->CSSetShader(nullptr, nullptr, 0);
    deviceContext_->CSSetShaderResources(0, 7, nullSrvs);
    deviceContext_->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);

    const int passes = static_cast<int>(std::ceil(params.strength * 3.0f));
    if (passes <= 0) {
        lastError_.clear();
        return true;
    }

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
        deviceContext_->Dispatch((static_cast<std::uint32_t>(width) + 7u) / 8u, (static_cast<std::uint32_t>(height) + 7u) / 8u, 1u);
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
    std::string composeError;
    ID3DBlob* composeBlob = CompileShader(kGpuDenoiserShaderSource, "ComposeCS", "cs_5_0", composeError);
    if (composeBlob == nullptr) {
        SetError(composeError.empty() ? "Failed to compile Denoiser compose shader." : composeError);
        return false;
    }

    HRESULT result = device_->CreateComputeShader(
        composeBlob->GetBufferPointer(),
        composeBlob->GetBufferSize(),
        nullptr,
        &composeShader_);
    composeBlob->Release();
    if (FAILED(result)) {
        SetError("CreateComputeShader(DenoiserComposeCS)", result);
        return false;
    }

    std::string filterError;
    ID3DBlob* filterBlob = CompileShader(kGpuDenoiserShaderSource, "FilterCS", "cs_5_0", filterError);
    if (filterBlob == nullptr) {
        SetError(filterError.empty() ? "Failed to compile Denoiser filter shader." : filterError);
        return false;
    }

    result = device_->CreateComputeShader(
        filterBlob->GetBufferPointer(),
        filterBlob->GetBufferSize(),
        nullptr,
        &filterShader_);
    filterBlob->Release();
    if (FAILED(result)) {
        SetError("CreateComputeShader(DenoiserFilterCS)", result);
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

    UINT depthSupport = 0;
    if (FAILED(device_->CheckFormatSupport(kDenoiserDepthFormat, &depthSupport))
        || (depthSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0
        || (depthSupport & D3D11_FORMAT_SUPPORT_SHADER_LOAD) == 0
        || (depthSupport & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) == 0) {
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

    HRESULT result = device_->CreateTexture2D(&outputDesc, nullptr, &outputTexture_);
    if (FAILED(result)) {
        SetError("CreateTexture2D(DenoiserOutput)", result);
        return false;
    }
    result = device_->CreateUnorderedAccessView(outputTexture_, nullptr, &outputUav_);
    if (FAILED(result)) {
        SetError("CreateUnorderedAccessView(DenoiserOutput)", result);
        return false;
    }
    result = device_->CreateShaderResourceView(outputTexture_, nullptr, &outputSrv_);
    if (FAILED(result)) {
        SetError("CreateShaderResourceView(DenoiserOutput)", result);
        return false;
    }

    result = device_->CreateTexture2D(&outputDesc, nullptr, &tempTexture_);
    if (FAILED(result)) {
        SetError("CreateTexture2D(DenoiserTemp)", result);
        return false;
    }
    result = device_->CreateUnorderedAccessView(tempTexture_, nullptr, &tempUav_);
    if (FAILED(result)) {
        SetError("CreateUnorderedAccessView(DenoiserTemp)", result);
        return false;
    }
    result = device_->CreateShaderResourceView(tempTexture_, nullptr, &tempSrv_);
    if (FAILED(result)) {
        SetError("CreateShaderResourceView(DenoiserTemp)", result);
        return false;
    }

    D3D11_TEXTURE2D_DESC depthDesc = outputDesc;
    depthDesc.Format = kDenoiserDepthFormat;
    result = device_->CreateTexture2D(&depthDesc, nullptr, &depthTexture_);
    if (FAILED(result)) {
        SetError("CreateTexture2D(DenoiserDepth)", result);
        return false;
    }
    result = device_->CreateUnorderedAccessView(depthTexture_, nullptr, &depthUav_);
    if (FAILED(result)) {
        SetError("CreateUnorderedAccessView(DenoiserDepth)", result);
        return false;
    }
    result = device_->CreateShaderResourceView(depthTexture_, nullptr, &depthSrv_);
    if (FAILED(result)) {
        SetError("CreateShaderResourceView(DenoiserDepth)", result);
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
    char buffer[96] {};
    std::snprintf(buffer, sizeof(buffer), "%s failed with HRESULT 0x%08X", stage, static_cast<unsigned>(result));
    lastError_ = buffer;
}

DXGI_FORMAT GpuDenoiser::ChooseOutputFormat() const {
    const std::array<DXGI_FORMAT, 2> formats = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT
    };

    for (const DXGI_FORMAT format : formats) {
        UINT support = 0;
        if (SUCCEEDED(device_->CheckFormatSupport(format, &support))
            && (support & D3D11_FORMAT_SUPPORT_TEXTURE2D)
            && (support & D3D11_FORMAT_SUPPORT_SHADER_LOAD)
            && (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)) {
            return format;
        }
    }

    return DXGI_FORMAT_UNKNOWN;
}

ID3DBlob* GpuDenoiser::CompileShader(const char* source, const char* entryPoint, const char* target, std::string& error) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
    flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    const HRESULT result = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entryPoint, target, flags, 0, &shaderBlob, &errorBlob);
    if (FAILED(result)) {
        if (errorBlob) {
            error.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
            errorBlob->Release();
        } else {
            char buffer[96] {};
            std::snprintf(buffer, sizeof(buffer), "D3DCompile failed with HRESULT 0x%08X", static_cast<unsigned>(result));
            error = buffer;
        }
        if (shaderBlob) {
            shaderBlob->Release();
        }
        return nullptr;
    }
    if (errorBlob) {
        errorBlob->Release();
    }
    return shaderBlob;
}

}  // namespace radiary
