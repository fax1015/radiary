#include "renderer/GpuPostProcess.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace radiary {

namespace {

constexpr char kGpuPostProcessShaderSource[] = R"(
cbuffer PostProcessParams : register(b0)
{
    uint Width;
    uint Height;
    float BloomIntensity;
    float BloomThreshold;
    float ChromaticAberration;
    float VignetteIntensity;
    float VignetteRoundness;
    uint AcesToneMap;
    float FilmGrain;
    float ColorTemperature;
    float SaturationBoost;
    uint RandomSeed;
    uint MipWidth;
    uint MipHeight;
    float2 Padding;
};

Texture2D<float4> InputTexture : register(t0);
Texture2D<float4> BloomTexture : register(t1);
RWTexture2D<float4> OutputTexture : register(u0);

static const float kPi = 3.14159265358979323846;

uint PCGHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(uint seed)
{
    return float(PCGHash(seed) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ColorTemperatureToRGB(float kelvin)
{
    float temp = clamp(kelvin, 1000.0, 15000.0) / 100.0;
    float3 color;
    if (temp <= 66.0)
    {
        color.r = 1.0;
        color.g = saturate(0.39008157876901960784 * log(temp) - 0.63184144378862745098);
    }
    else
    {
        color.r = saturate(1.29293618606274509804 * pow(temp - 60.0, -0.1332047592));
        color.g = saturate(1.12989086089529411765 * pow(temp - 60.0, -0.0755148492));
    }
    if (temp >= 66.0)
        color.b = 1.0;
    else if (temp <= 19.0)
        color.b = 0.0;
    else
        color.b = saturate(0.54320678911019607843 * log(temp - 10.0) - 1.19625408914);
    return color;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void BloomDownCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= MipWidth || dispatchThreadId.y >= MipHeight)
        return;

    float2 texelSize = 1.0 / float2(Width, Height);
    float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(MipWidth, MipHeight);
    int2 srcCoord = int2(uv * float2(Width, Height));

    float3 color = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 sampleCoord = srcCoord + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(Width - 1, Height - 1));
            float3 sampleColor = InputTexture.Load(int3(sampleCoord, 0)).rgb;
            float lum = Luminance(sampleColor);
            float brightPass = max(0.0, lum - BloomThreshold) / max(lum, 0.001);
            float kernel = (dx == 0 && dy == 0) ? 4.0 : ((abs(dx) + abs(dy) == 1) ? 2.0 : 1.0);
            color += sampleColor * brightPass * kernel;
            totalWeight += kernel;
        }
    }

    color /= max(totalWeight, 1.0);
    OutputTexture[int2(dispatchThreadId.xy)] = float4(color, 1.0);
}

[numthreads(8, 8, 1)]
void BloomUpCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= MipWidth || dispatchThreadId.y >= MipHeight)
        return;

    float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(MipWidth, MipHeight);

    float3 color = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    uint srcWidth, srcHeight;
    InputTexture.GetDimensions(srcWidth, srcHeight);

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            float2 offset = float2(dx, dy) / float2(srcWidth, srcHeight);
            int2 sampleCoord = int2((uv + offset) * float2(srcWidth, srcHeight));
            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(srcWidth - 1, srcHeight - 1));
            float kernel = (dx == 0 && dy == 0) ? 4.0 : ((abs(dx) + abs(dy) == 1) ? 2.0 : 1.0);
            color += InputTexture.Load(int3(sampleCoord, 0)).rgb * kernel;
            totalWeight += kernel;
        }
    }

    color /= max(totalWeight, 1.0);

    float3 existing = BloomTexture.Load(int3(dispatchThreadId.xy, 0)).rgb;
    OutputTexture[int2(dispatchThreadId.xy)] = float4(existing + color, 1.0);
}

[numthreads(8, 8, 1)]
void PostProcessCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= Width || dispatchThreadId.y >= Height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    float2 uv = (float2(pixel) + 0.5) / float2(Width, Height);
    float2 center = float2(0.5, 0.5);

    // Chromatic aberration
    float3 color;
    if (ChromaticAberration > 0.001)
    {
        float2 dir = uv - center;
        float dist = length(dir);
        float2 offset = dir * ChromaticAberration * 0.02 * dist;

        int2 coordR = clamp(int2((uv + offset) * float2(Width, Height)), int2(0, 0), int2(Width - 1, Height - 1));
        int2 coordG = pixel;
        int2 coordB = clamp(int2((uv - offset) * float2(Width, Height)), int2(0, 0), int2(Width - 1, Height - 1));

        color.r = InputTexture.Load(int3(coordR, 0)).r;
        color.g = InputTexture.Load(int3(coordG, 0)).g;
        color.b = InputTexture.Load(int3(coordB, 0)).b;
    }
    else
    {
        color = InputTexture.Load(int3(pixel, 0)).rgb;
    }

    // Add bloom
    if (BloomIntensity > 0.001)
    {
        uint bloomW, bloomH;
        BloomTexture.GetDimensions(bloomW, bloomH);
        int2 bloomCoord = clamp(int2(uv * float2(bloomW, bloomH)), int2(0, 0), int2(bloomW - 1, bloomH - 1));
        float3 bloom = BloomTexture.Load(int3(bloomCoord, 0)).rgb;
        color += bloom * BloomIntensity;
    }

    // Color temperature shift
    if (abs(ColorTemperature - 6500.0) > 10.0)
    {
        float3 tempColor = ColorTemperatureToRGB(ColorTemperature);
        float3 neutral = ColorTemperatureToRGB(6500.0);
        color *= tempColor / max(neutral, float3(0.001, 0.001, 0.001));
    }

    // Saturation boost
    if (abs(SaturationBoost) > 0.001)
    {
        float lum = Luminance(color);
        color = lerp(float3(lum, lum, lum), color, 1.0 + SaturationBoost);
        color = max(color, float3(0.0, 0.0, 0.0));
    }

    // ACES tone mapping
    if (AcesToneMap != 0u)
    {
        color = ACESFilm(color);
    }

    // Film grain
    if (FilmGrain > 0.001)
    {
        uint seed = (dispatchThreadId.y * Width + dispatchThreadId.x) ^ RandomSeed;
        float grain = (RandomFloat(seed) - 0.5) * FilmGrain * 0.15;
        float lum = Luminance(color);
        float grainScale = 1.0 - saturate(lum * 2.0);
        color += grain * grainScale;
    }

    // Vignette
    if (VignetteIntensity > 0.001)
    {
        float2 vignUv = uv - center;
        float aspectRatio = float(Width) / float(Height);
        vignUv.x *= lerp(1.0, aspectRatio, VignetteRoundness);
        float vignDist = length(vignUv) * 1.4142;
        float vignette = 1.0 - smoothstep(0.4, 1.2, vignDist) * VignetteIntensity;
        color *= vignette;
    }

    color = saturate(color);
    float alpha = InputTexture.Load(int3(pixel, 0)).a;
    OutputTexture[pixel] = float4(color, alpha);
}

)";

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

    D3D11_BUFFER_DESC paramsDesc {};
    paramsDesc.ByteWidth = sizeof(Params);
    paramsDesc.Usage = D3D11_USAGE_DYNAMIC;
    paramsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    paramsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT paramsResult = device_->CreateBuffer(&paramsDesc, nullptr, &paramsBuffer_);
    if (FAILED(paramsResult)) {
        SetError("CreateBuffer(PostProcessParams)", paramsResult);
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
    ID3D11ShaderResourceView* inputSrv) {

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

    ++frameCounter_;
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
            D3D11_MAPPED_SUBRESOURCE mapped {};
            HRESULT result = deviceContext_->Map(paramsBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(result)) { SetError("Map(paramsBuffer bloom down)", result); return false; }
            std::memcpy(mapped.pData, &params, sizeof(params));
            deviceContext_->Unmap(paramsBuffer_, 0);

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
            D3D11_MAPPED_SUBRESOURCE mapped {};
            HRESULT result = deviceContext_->Map(paramsBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(result)) { SetError("Map(paramsBuffer bloom down mip)", result); return false; }
            std::memcpy(mapped.pData, &params, sizeof(params));
            deviceContext_->Unmap(paramsBuffer_, 0);

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
            ID3D11Resource* resource = nullptr;
            bloomSrvs_[i]->GetResource(&resource);
            ID3D11Texture2D* tex = nullptr;
            resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
            D3D11_TEXTURE2D_DESC desc {};
            tex->GetDesc(&desc);
            tex->Release();
            resource->Release();

            Params params {};
            params.mipWidth = desc.Width;
            params.mipHeight = desc.Height;
            D3D11_MAPPED_SUBRESOURCE mapped {};
            HRESULT result = deviceContext_->Map(paramsBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(result)) { SetError("Map(paramsBuffer bloom up)", result); return false; }
            std::memcpy(mapped.pData, &params, sizeof(params));
            deviceContext_->Unmap(paramsBuffer_, 0);

            ID3D11ShaderResourceView* srvs[] = {bloomSrvs_[i + 1], bloomSrvs_[i]};
            ID3D11UnorderedAccessView* uavs[] = {bloomUavs_[i]};
            deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);
            deviceContext_->CSSetShaderResources(0, 2, srvs);
            deviceContext_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            deviceContext_->CSSetShader(bloomUpShader_, nullptr, 0);
            deviceContext_->Dispatch(
                (desc.Width + 7u) / 8u,
                (desc.Height + 7u) / 8u, 1u);
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
        params.randomSeed = frameCounter_ * 1664525u + 1013904223u;

        D3D11_MAPPED_SUBRESOURCE mapped {};
        HRESULT result = deviceContext_->Map(paramsBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result)) { SetError("Map(paramsBuffer composite)", result); return false; }
        std::memcpy(mapped.pData, &params, sizeof(params));
        deviceContext_->Unmap(paramsBuffer_, 0);

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

    ID3DBlob* postProcessBlob = CompileShader(kGpuPostProcessShaderSource, "PostProcessCS", "cs_5_0", compileError);
    if (!postProcessBlob) {
        SetError(compileError.empty() ? "Failed to compile PostProcessCS." : compileError);
        return false;
    }

    ID3DBlob* bloomDownBlob = CompileShader(kGpuPostProcessShaderSource, "BloomDownCS", "cs_5_0", compileError);
    if (!bloomDownBlob) {
        postProcessBlob->Release();
        SetError(compileError.empty() ? "Failed to compile BloomDownCS." : compileError);
        return false;
    }

    ID3DBlob* bloomUpBlob = CompileShader(kGpuPostProcessShaderSource, "BloomUpCS", "cs_5_0", compileError);
    if (!bloomUpBlob) {
        postProcessBlob->Release();
        bloomDownBlob->Release();
        SetError(compileError.empty() ? "Failed to compile BloomUpCS." : compileError);
        return false;
    }

    HRESULT result = device_->CreateComputeShader(postProcessBlob->GetBufferPointer(), postProcessBlob->GetBufferSize(), nullptr, &postProcessShader_);
    postProcessBlob->Release();
    if (FAILED(result)) { bloomDownBlob->Release(); bloomUpBlob->Release(); SetError("CreateComputeShader(PostProcessCS)", result); return false; }

    result = device_->CreateComputeShader(bloomDownBlob->GetBufferPointer(), bloomDownBlob->GetBufferSize(), nullptr, &bloomDownShader_);
    bloomDownBlob->Release();
    if (FAILED(result)) { bloomUpBlob->Release(); SetError("CreateComputeShader(BloomDownCS)", result); return false; }

    result = device_->CreateComputeShader(bloomUpBlob->GetBufferPointer(), bloomUpBlob->GetBufferSize(), nullptr, &bloomUpShader_);
    bloomUpBlob->Release();
    if (FAILED(result)) { SetError("CreateComputeShader(BloomUpCS)", result); return false; }

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
    HRESULT result = device_->CreateTexture2D(&outputDesc, nullptr, &outputTexture_);
    if (FAILED(result)) { SetError("CreateTexture2D(postprocess output)", result); return false; }
    result = device_->CreateUnorderedAccessView(outputTexture_, nullptr, &outputUav_);
    if (FAILED(result)) { SetError("CreateUAV(postprocess output)", result); return false; }
    result = device_->CreateShaderResourceView(outputTexture_, nullptr, &outputSrv_);
    if (FAILED(result)) { SetError("CreateSRV(postprocess output)", result); return false; }

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
        result = device_->CreateTexture2D(&bloomDesc, nullptr, &bloomTextures_[i]);
        if (FAILED(result)) { SetError("CreateTexture2D(bloom mip)", result); return false; }
        result = device_->CreateUnorderedAccessView(bloomTextures_[i], nullptr, &bloomUavs_[i]);
        if (FAILED(result)) { SetError("CreateUAV(bloom mip)", result); return false; }
        result = device_->CreateShaderResourceView(bloomTextures_[i], nullptr, &bloomSrvs_[i]);
        if (FAILED(result)) { SetError("CreateSRV(bloom mip)", result); return false; }
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
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08X)", stage, static_cast<unsigned int>(result));
    lastError_ = buffer;
}

DXGI_FORMAT GpuPostProcess::ChooseOutputFormat() const {
    constexpr DXGI_FORMAT candidates[] = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT
    };
    for (const DXGI_FORMAT format : candidates) {
        UINT support = 0;
        if (FAILED(device_->CheckFormatSupport(format, &support))) continue;
        const UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D
            | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE
            | D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
        if ((support & required) == required) return format;
    }
    return DXGI_FORMAT_UNKNOWN;
}

ID3DBlob* GpuPostProcess::CompileShader(const char* source, const char* entryPoint, const char* target, std::string& error) {
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    const UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PREFER_FLOW_CONTROL;
    const HRESULT result = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entryPoint, target, compileFlags, 0, &shaderBlob, &errorBlob);
    if (errorBlob != nullptr) {
        error.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
        errorBlob->Release();
    }
    if (FAILED(result)) {
        if (shaderBlob != nullptr) shaderBlob->Release();
        if (error.empty()) {
            char buffer[128];
            std::snprintf(buffer, sizeof(buffer), "D3DCompile(%s) failed (HRESULT 0x%08X)", entryPoint, static_cast<unsigned int>(result));
            error = buffer;
        }
        return nullptr;
    }
    error.clear();
    return shaderBlob;
}

}  // namespace radiary
