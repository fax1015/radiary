#include "renderer/GpuDenoiser.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace radiary {

namespace {

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

RWTexture2D<float4> OutputTexture : register(u0);

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
void MainCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    float4 centerColor = ComposeColor(pixel);
    float centerDepth = ComposeDepth(pixel);
    
    if (Strength <= 0.0)
    {
        OutputTexture[pixel] = centerColor;
        return;
    }

    int radius = clamp((int)ceil(SigmaSpatial * 2.0), 1, 8);
    float invSpatialVar = 1.0 / (2.0 * SigmaSpatial * SigmaSpatial);
    
    // Pass 1: Compute local mean and variance to identify isolated noise dots (fireflies)
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
            
            float4 sampleColor = ComposeColor(samplePixel);
            float spatialDistSq = (float)(x * x + y * y);
            float spatialWeight = exp(-spatialDistSq * invSpatialVar);
            
            // Only include actual flame pixels in the statistics to avoid biasing the mean towards the void
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
    
    // Outlier rejection: if the center pixel is much brighter than its neighbors, it's noise.
    // The Strength parameter controls how aggressively we clamp to the mean.
    float thresholdMult = lerp(4.0, 0.5, saturate(Strength)); 
    float4 minColor = meanColor - stdDev * thresholdMult;
    float4 maxColor = meanColor + stdDev * thresholdMult;
    
    float4 filteredColor = clamp(centerColor, minColor, maxColor);
    
    // Pass 2: Gentle smoothing to blend the clamped pixels, guided by depth
    float invDepthVar = 1.0 / (2.0 * SigmaDepth * SigmaDepth);
    float4 accumColor = float4(0.0, 0.0, 0.0, 0.0);
    float blurWeightSum = 0.0;
    
    // Smaller radius for the final smoothing pass
    int blurRadius = clamp((int)ceil(SigmaSpatial), 1, 4);
    
    for (int by = -blurRadius; by <= blurRadius; ++by)
    {
        for (int bx = -blurRadius; bx <= blurRadius; ++bx)
        {
            int2 samplePixel = pixel + int2(bx, by);
            samplePixel.x = clamp(samplePixel.x, 0, (int)Width - 1);
            samplePixel.y = clamp(samplePixel.y, 0, (int)Height - 1);
            
            float4 sampleColor = ComposeColor(samplePixel);
            // Pre-clamp the neighbor if it's also a firefly (using the same global threshold concept locally)
            sampleColor = clamp(sampleColor, minColor, maxColor);
            
            float sampleDepth = ComposeDepth(samplePixel);
            
            float spatialDistSq = (float)(bx * bx + by * by);
            float spatialWeight = exp(-spatialDistSq * invSpatialVar);
            
            float depthDiff = sampleDepth - centerDepth;
            float depthDistSq = depthDiff * depthDiff;
            float depthWeight = exp(-depthDistSq * invDepthVar);
            
            float weight = spatialWeight * depthWeight;
            
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

    if (!CreateShader()) {
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
    if (shader_) { shader_->Release(); shader_ = nullptr; }
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
    params.strength = static_cast<float>(scene.denoiser.strength);
    params.sigmaSpatial = 3.0f + static_cast<float>(scene.denoiser.strength) * 4.0f;
    params.sigmaColor = 0.15f + static_cast<float>(scene.denoiser.strength) * 0.25f;
    params.sigmaDepth = 0.05f + static_cast<float>(scene.denoiser.strength) * 0.1f;

    D3D11_MAPPED_SUBRESOURCE mapped {};
    HRESULT result = deviceContext_->Map(paramsBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result)) {
        SetError("Map(DenoiserParams)", result);
        return false;
    }
    std::memcpy(mapped.pData, &params, sizeof(params));
    deviceContext_->Unmap(paramsBuffer_, 0);

    ID3D11Buffer* constantBuffers[] = {paramsBuffer_};
    ID3D11ShaderResourceView* srvs[] = {gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv};
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

bool GpuDenoiser::CreateShader() {
    std::string compileError;
    ID3DBlob* shaderBlob = CompileShader(kGpuDenoiserShaderSource, "MainCS", "cs_5_0", compileError);
    if (shaderBlob == nullptr) {
        SetError(compileError.empty() ? "Failed to compile Denoiser compute shader." : compileError);
        return false;
    }

    const HRESULT result = device_->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &shader_);
    shaderBlob->Release();
    if (FAILED(result)) {
        SetError("CreateComputeShader(DenoiserCS)", result);
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

    outputWidth_ = width;
    outputHeight_ = height;
    return true;
}

void GpuDenoiser::ReleaseResources() {
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