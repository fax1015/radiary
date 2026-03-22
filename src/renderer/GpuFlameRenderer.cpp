#include "renderer/GpuFlameRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Math.h"
#include "renderer/D3D11ResourceUtils.h"
#include "renderer/D3D11ShaderUtils.h"
#include "renderer/RenderMath.h"

namespace radiary {

namespace {

constexpr int kVariationCountGpu = static_cast<int>(VariationType::Count);
static_assert(kVariationCountGpu == 60, "Update GPU flame shader variation table to match VariationType::Count.");
constexpr std::uint32_t kMinProgressiveBatchIterations = 262144u;
constexpr std::uint32_t kMaxProgressiveBatchIterations = 2097152u;
constexpr std::uint32_t kGpuFlameBurnInIterations = 24u;
constexpr std::uint32_t kGpuFlameTargetOrbitIterations = 128u;
constexpr std::uint32_t kMinOrbitThreadCount = 256u;
constexpr std::uint32_t kMaxOrbitThreadCount = 65536u;
constexpr float kFlameWorldScale = 0.63f;

struct PaletteEntry {
    std::uint32_t r = 0;
    std::uint32_t g = 0;
    std::uint32_t b = 0;
    std::uint32_t a = 255;
};

constexpr wchar_t kGpuFlameShaderFile[] = L"GpuFlameRenderer.hlsl";

}  // namespace

GpuFlameRenderer::~GpuFlameRenderer() {
    Shutdown();
}

bool GpuFlameRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
    static_assert(sizeof(RenderParams) % 16 == 0, "RenderParams must be 16-byte aligned for D3D11 constant buffers.");
    Shutdown();
    lastError_.clear();
    if (device == nullptr || deviceContext == nullptr) {
        SetError("D3D11 device/context is not available.");
        return false;
    }
    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
        SetError("GPU flame preview requires Direct3D feature level 11_0 or newer.");
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
            static_cast<UINT>(sizeof(RenderParams)),
            "CreateBuffer(RenderParams)",
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

void GpuFlameRenderer::ResetAccumulation() {
    accumulationValid_ = false;
    accumulatedIterations_ = 0;
    sceneSignature_ = 0;
    temporalStateValid_ = false;
    lastRenderResetAccumulation_ = false;
    lastRenderResetTemporalState_ = false;
    temporalThreadCount_ = 0;
}

GpuFlameRenderer::StatusSnapshot GpuFlameRenderer::GetStatusSnapshot() const {
    return GpuFlameRenderer::StatusSnapshot {
        .accumulatedIterations = accumulatedIterations_,
        .accumulationValid = accumulationValid_,
        .temporalStateValid = temporalStateValid_,
        .lastRenderResetAccumulation = lastRenderResetAccumulation_,
        .lastRenderResetTemporalState = lastRenderResetTemporalState_,
    };
}

void GpuFlameRenderer::Shutdown() {
    ResetAccumulation();
    ReleaseTextures();
    ReleaseBuffers();
    if (toneMapShader_) { toneMapShader_->Release(); toneMapShader_ = nullptr; }
    if (accumulateShader_) { accumulateShader_->Release(); accumulateShader_ = nullptr; }
    if (paramsBuffer_) { paramsBuffer_->Release(); paramsBuffer_ = nullptr; }
    if (deviceContext_) { deviceContext_->Release(); deviceContext_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

bool GpuFlameRenderer::Render(
    const Scene& scene,
    const int width,
    const int height,
    const std::uint32_t previewIterations,
    const bool transparentBackground,
    const bool clearAccumulationForFrame,
    const bool preserveTemporalState,
    const bool resetTemporalState) {
    if (device_ == nullptr || deviceContext_ == nullptr || accumulateShader_ == nullptr || toneMapShader_ == nullptr) {
        if (lastError_.empty()) {
            SetError("GPU flame renderer is not initialized.");
        }
        return false;
    }
    if (width <= 0 || height <= 0) {
        SetError("Viewport size is invalid for GPU preview.");
        return false;
    }
    const std::uint32_t targetIterations = std::max<std::uint32_t>(previewIterations, 1u);
    const std::uint64_t remainingIterations = clearAccumulationForFrame
        ? static_cast<std::uint64_t>(targetIterations)
        : (accumulatedIterations_ >= targetIterations ? 0u : static_cast<std::uint64_t>(targetIterations) - accumulatedIterations_);
    const std::uint32_t progressiveBudget = std::clamp(targetIterations / 3u, kMinProgressiveBatchIterations, kMaxProgressiveBatchIterations);
    const std::uint32_t dispatchIterations = remainingIterations == 0u
        ? 0u
        : static_cast<std::uint32_t>(clearAccumulationForFrame ? remainingIterations : std::min<std::uint64_t>(remainingIterations, progressiveBudget));
    const std::uint32_t totalThreads = dispatchIterations == 0u
        ? 0u
        : std::clamp(
            (dispatchIterations + kGpuFlameTargetOrbitIterations - 1u) / kGpuFlameTargetOrbitIterations,
            kMinOrbitThreadCount,
            kMaxOrbitThreadCount);
    if (!EnsureResources(width, height, scene.transforms.size(), totalThreads)) {
        return false;
    }

    const std::uint64_t sceneSignature = ComputeSceneSignature(scene);
    const bool resetAccumulation = clearAccumulationForFrame || !accumulationValid_ || sceneSignature_ != sceneSignature;
    lastRenderResetAccumulation_ = resetAccumulation;
    lastRenderResetTemporalState_ = false;
    if (resetAccumulation) {
        const std::uint32_t clearAccum[4] = {0u, 0u, 0u, 0u};
        deviceContext_->ClearUnorderedAccessViewUint(accumulationUav_, clearAccum);
        accumulationValid_ = true;
        accumulatedIterations_ = 0;
        sceneSignature_ = sceneSignature;
    }
    if (clearAccumulationForFrame) {
        const bool resetOrbitStates =
            !preserveTemporalState
            || resetTemporalState
            || !temporalStateValid_
            || temporalThreadCount_ != totalThreads;
        lastRenderResetTemporalState_ = resetOrbitStates;
        if (resetOrbitStates && orbitStateUav_ != nullptr) {
            const std::uint32_t clearState[4] = {0u, 0u, 0u, 0u};
            deviceContext_->ClearUnorderedAccessViewUint(orbitStateUav_, clearState);
        }
        temporalStateValid_ = preserveTemporalState && totalThreads > 0u;
        temporalThreadCount_ = totalThreads;
    } else {
        lastRenderResetTemporalState_ = false;
    }

    std::vector<TransformGpu> transforms(scene.transforms.size());
    float cumulativeWeight = 0.0f;
    for (std::size_t index = 0; index < scene.transforms.size(); ++index) {
        const TransformLayer& layer = scene.transforms[index];
        TransformGpu gpuLayer;
        gpuLayer.weight = static_cast<float>(std::max(0.01, layer.weight));
        cumulativeWeight += gpuLayer.weight;
        gpuLayer.cumulativeWeight = cumulativeWeight;
        gpuLayer.rotationRadians = static_cast<float>(DegreesToRadians(layer.rotationDegrees));
        gpuLayer.scaleX = static_cast<float>(layer.scaleX);
        gpuLayer.scaleY = static_cast<float>(layer.scaleY);
        gpuLayer.translateX = static_cast<float>(layer.translateX);
        gpuLayer.translateY = static_cast<float>(layer.translateY);
        gpuLayer.shearX = static_cast<float>(layer.shearX);
        gpuLayer.shearY = static_cast<float>(layer.shearY);
        gpuLayer.colorIndex = static_cast<float>(layer.colorIndex);
        gpuLayer.useCustomColor = layer.useCustomColor ? 1.0f : 0.0f;
        gpuLayer.customColor[0] = static_cast<float>(layer.customColor.r) / 255.0f;
        gpuLayer.customColor[1] = static_cast<float>(layer.customColor.g) / 255.0f;
        gpuLayer.customColor[2] = static_cast<float>(layer.customColor.b) / 255.0f;
        gpuLayer.customColor[3] = static_cast<float>(layer.customColor.a) / 255.0f;
        for (std::size_t variation = 0; variation < kVariationCount; ++variation) {
            gpuLayer.variations[variation] = static_cast<float>(layer.variations[variation]);
        }
        transforms[index] = gpuLayer;
    }

    D3D11_MAPPED_SUBRESOURCE mapped {};
    HRESULT result = S_OK;
    if (!transforms.empty()) {
        result = deviceContext_->Map(transformBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result)) {
            SetError("Map(transformBuffer)", result);
            return false;
        }
        std::memcpy(mapped.pData, transforms.data(), transforms.size() * sizeof(TransformGpu));
        deviceContext_->Unmap(transformBuffer_, 0);
    }

    const std::vector<Color> palette = BuildGradientPalette(scene.gradientStops, 256);
    std::array<PaletteEntry, 256> paletteEntries {};
    for (std::size_t index = 0; index < paletteEntries.size(); ++index) {
        const Color& color = palette[index];
        paletteEntries[index] = {color.r, color.g, color.b, color.a};
    }
    result = deviceContext_->Map(paletteBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result)) {
        SetError("Map(paletteBuffer)", result);
        return false;
    }
    std::memcpy(mapped.pData, paletteEntries.data(), sizeof(paletteEntries));
    deviceContext_->Unmap(paletteBuffer_, 0);

    RenderParams params;
    params.width = static_cast<std::uint32_t>(width);
    params.height = static_cast<std::uint32_t>(height);
    params.previewIterations = std::max<std::uint32_t>(dispatchIterations, 1u);
    params.transformCount = static_cast<std::uint32_t>(transforms.size());
    params.yaw = static_cast<float>(scene.camera.yaw);
    params.pitch = static_cast<float>(scene.camera.pitch);
    params.distance = static_cast<float>(scene.camera.distance);
    params.panX = static_cast<float>(scene.camera.panX);
    params.panY = static_cast<float>(scene.camera.panY);
    params.zoom2D = static_cast<float>(scene.camera.zoom2D);
    params.flameRotateX = static_cast<float>(DegreesToRadians(scene.flameRender.rotationXDegrees));
    params.flameRotateY = static_cast<float>(DegreesToRadians(scene.flameRender.rotationYDegrees));
    params.flameRotateZ = static_cast<float>(DegreesToRadians(scene.flameRender.rotationZDegrees));
    params.flameDepthAmount = static_cast<float>(scene.flameRender.depthAmount);
    params.flameCurveExposure = static_cast<float>(scene.flameRender.curveExposure);
    params.flameCurveContrast = static_cast<float>(scene.flameRender.curveContrast);
    params.flameCurveHighlights = static_cast<float>(scene.flameRender.curveHighlights);
    params.flameCurveGamma = static_cast<float>(scene.flameRender.curveGamma);
    params.backgroundR = static_cast<float>(scene.backgroundColor.r) / 255.0f;
    params.backgroundG = static_cast<float>(scene.backgroundColor.g) / 255.0f;
    params.backgroundB = static_cast<float>(scene.backgroundColor.b) / 255.0f;
    params.backgroundA = static_cast<float>(scene.backgroundColor.a) / 255.0f;
    params.gridVisible = 0u;
    params.totalThreadCount = totalThreads;
    params.worldScale = kFlameWorldScale;
    params.totalWeight = std::max(cumulativeWeight, 0.01f);
    params.transparentBackground = transparentBackground ? 1u : 0u;
    params.randomSeedOffset = static_cast<std::uint32_t>(accumulatedIterations_ & 0xFFFFFFFFu);
    params.preserveOrbitState = clearAccumulationForFrame && preserveTemporalState ? 1u : 0u;
    params.farDepth = static_cast<float>(render_math::ComputeFarDepth(scene.camera.distance));

    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!WriteD3D11BufferData(
            deviceContext_,
            paramsBuffer_,
            &params,
            sizeof(params),
            "Map(paramsBuffer)",
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }

    ID3D11Buffer* constantBuffers[] = {paramsBuffer_};
    ID3D11ShaderResourceView* accumulateSrvs[] = {transforms.empty() ? nullptr : transformSrv_, paletteSrv_};
    if (dispatchIterations > 0u) {
        ID3D11UnorderedAccessView* accumulateUavs[] = {accumulationUav_, orbitStateUav_};
        deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);
        deviceContext_->CSSetShaderResources(0, 2, accumulateSrvs);
        deviceContext_->CSSetUnorderedAccessViews(0, 2, accumulateUavs, nullptr);
        deviceContext_->CSSetShader(accumulateShader_, nullptr, 0);
        deviceContext_->Dispatch((totalThreads + 127u) / 128u, 1u, 1u);
        accumulatedIterations_ += dispatchIterations;
    }

    ID3D11ShaderResourceView* nullSrvs[2] = {nullptr, nullptr};
    ID3D11UnorderedAccessView* nullUavs[4] = {nullptr, nullptr, nullptr, nullptr};
    deviceContext_->CSSetShaderResources(0, 2, nullSrvs);
    deviceContext_->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);

    ID3D11UnorderedAccessView* toneMapUavs[] = {accumulationUav_, nullptr, outputUav_, depthUav_};
    deviceContext_->CSSetConstantBuffers(0, 1, constantBuffers);
    deviceContext_->CSSetUnorderedAccessViews(0, 4, toneMapUavs, nullptr);
    deviceContext_->CSSetShader(toneMapShader_, nullptr, 0);
    deviceContext_->Dispatch((static_cast<std::uint32_t>(width) + 7u) / 8u, (static_cast<std::uint32_t>(height) + 7u) / 8u, 1u);

    deviceContext_->CSSetShader(nullptr, nullptr, 0);
    deviceContext_->CSSetUnorderedAccessViews(0, 4, nullUavs, nullptr);
    lastError_.clear();
    return true;
}

bool GpuFlameRenderer::CreateShaders() {
    std::string compileError;
    const UINT compileFlags = GetOptimizedD3D11ShaderCompileFlags(false, true);
    if (!CreateD3D11ComputeShaderFromFile(
            device_,
            kGpuFlameShaderFile,
            "AccumulateCS",
            compileFlags,
            "CreateComputeShader(AccumulateCS)",
            &accumulateShader_,
            compileError)) {
        SetError(compileError.empty() ? "Failed to compile AccumulateCS." : compileError);
        return false;
    }

    if (!CreateD3D11ComputeShaderFromFile(
            device_,
            kGpuFlameShaderFile,
            "ToneMapCS",
            compileFlags,
            "CreateComputeShader(ToneMapCS)",
            &toneMapShader_,
            compileError)) {
        SetError(compileError.empty() ? "Failed to compile ToneMapCS." : compileError);
        return false;
    }
    return true;
}

std::uint64_t GpuFlameRenderer::ComputeSceneSignature(const Scene& scene) const {
    auto mix = [](std::uint64_t seed, const std::uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
        return seed;
    };
    auto mixDouble = [&](std::uint64_t seed, const double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return mix(seed, bits);
    };

    std::uint64_t signature = 1469598103934665603ull;
    signature = mix(signature, static_cast<std::uint64_t>(scene.transforms.size()));
    signature = mixDouble(signature, scene.camera.yaw);
    signature = mixDouble(signature, scene.camera.pitch);
    signature = mixDouble(signature, scene.camera.distance);
    signature = mixDouble(signature, scene.camera.panX);
    signature = mixDouble(signature, scene.camera.panY);
    signature = mixDouble(signature, scene.camera.zoom2D);
    signature = mixDouble(signature, scene.flameRender.rotationXDegrees);
    signature = mixDouble(signature, scene.flameRender.rotationYDegrees);
    signature = mixDouble(signature, scene.flameRender.rotationZDegrees);
    signature = mixDouble(signature, scene.flameRender.depthAmount);
    signature = mix(signature, scene.backgroundColor.r);
    signature = mix(signature, scene.backgroundColor.g);
    signature = mix(signature, scene.backgroundColor.b);
    signature = mix(signature, scene.backgroundColor.a);
    for (const TransformLayer& layer : scene.transforms) {
        signature = mixDouble(signature, layer.weight);
        signature = mixDouble(signature, layer.rotationDegrees);
        signature = mixDouble(signature, layer.scaleX);
        signature = mixDouble(signature, layer.scaleY);
        signature = mixDouble(signature, layer.translateX);
        signature = mixDouble(signature, layer.translateY);
        signature = mixDouble(signature, layer.shearX);
        signature = mixDouble(signature, layer.shearY);
        signature = mixDouble(signature, layer.colorIndex);
        signature = mix(signature, layer.useCustomColor ? 1u : 0u);
        signature = mix(signature, layer.customColor.r);
        signature = mix(signature, layer.customColor.g);
        signature = mix(signature, layer.customColor.b);
        signature = mix(signature, layer.customColor.a);
        for (const double variation : layer.variations) {
            signature = mixDouble(signature, variation);
        }
    }

    for (const GradientStop& stop : scene.gradientStops) {
        signature = mixDouble(signature, stop.position);
        signature = mix(signature, stop.color.r);
        signature = mix(signature, stop.color.g);
        signature = mix(signature, stop.color.b);
        signature = mix(signature, stop.color.a);
    }

    return signature;
}

bool GpuFlameRenderer::EnsureResources(const int width, const int height, const std::size_t transformCount, const std::size_t orbitThreadCount) {
    if (transformCount > transformCapacity_) {
        if (transformSrv_) { transformSrv_->Release(); transformSrv_ = nullptr; }
        if (transformBuffer_) { transformBuffer_->Release(); transformBuffer_ = nullptr; }

        D3D11_BUFFER_DESC bufferDesc {};
        bufferDesc.ByteWidth = static_cast<UINT>(sizeof(TransformGpu) * transformCount);
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(TransformGpu);
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(transformCount);
        const D3D11BufferViewStages transformStages {
            "CreateBuffer(transformBuffer)",
            "CreateShaderResourceView(transformBuffer)",
            nullptr,
        };
        const char* failedStage = nullptr;
        HRESULT failedResult = S_OK;
        if (!CreateBufferWithShaderResourceView(
                device_,
                bufferDesc,
                srvDesc,
                transformStages,
                &transformBuffer_,
                &transformSrv_,
                failedStage,
                failedResult)) {
            SetError(failedStage, failedResult);
            return false;
        }
        transformCapacity_ = transformCount;
    }

    if (paletteBuffer_ == nullptr) {
        D3D11_BUFFER_DESC bufferDesc {};
        bufferDesc.ByteWidth = static_cast<UINT>(sizeof(PaletteEntry) * 256);
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(PaletteEntry);
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = 256;
        const D3D11BufferViewStages paletteStages {
            "CreateBuffer(paletteBuffer)",
            "CreateShaderResourceView(paletteBuffer)",
            nullptr,
        };
        const char* failedStage = nullptr;
        HRESULT failedResult = S_OK;
        if (!CreateBufferWithShaderResourceView(
                device_,
                bufferDesc,
                srvDesc,
                paletteStages,
                &paletteBuffer_,
                &paletteSrv_,
                failedStage,
                failedResult)) {
            SetError(failedStage, failedResult);
            return false;
        }
    }

    if (orbitThreadCount > orbitStateCapacity_) {
        if (orbitStateUav_) { orbitStateUav_->Release(); orbitStateUav_ = nullptr; }
        if (orbitStateBuffer_) { orbitStateBuffer_->Release(); orbitStateBuffer_ = nullptr; }

        D3D11_BUFFER_DESC orbitDesc {};
        orbitDesc.ByteWidth = static_cast<UINT>(sizeof(OrbitStateGpu) * orbitThreadCount);
        orbitDesc.Usage = D3D11_USAGE_DEFAULT;
        orbitDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        orbitDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        orbitDesc.StructureByteStride = sizeof(OrbitStateGpu);
        D3D11_UNORDERED_ACCESS_VIEW_DESC orbitUavDesc {};
        orbitUavDesc.Format = DXGI_FORMAT_UNKNOWN;
        orbitUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        orbitUavDesc.Buffer.FirstElement = 0;
        orbitUavDesc.Buffer.NumElements = static_cast<UINT>(orbitThreadCount);
        const D3D11BufferViewStages orbitStages {
            "CreateBuffer(orbitStateBuffer)",
            nullptr,
            "CreateUnorderedAccessView(orbitStateBuffer)",
        };
        const char* failedStage = nullptr;
        HRESULT failedResult = S_OK;
        if (!CreateBufferWithUnorderedAccessView(
                device_,
                orbitDesc,
                orbitUavDesc,
                orbitStages,
                &orbitStateBuffer_,
                &orbitStateUav_,
                failedStage,
                failedResult)) {
            SetError(failedStage, failedResult);
            return false;
        }
        orbitStateCapacity_ = orbitThreadCount;
        temporalStateValid_ = false;
        temporalThreadCount_ = 0;
    }

    if (outputTexture_ != nullptr && outputWidth_ == width && outputHeight_ == height) {
        return true;
    }

    ReleaseTextures();

    D3D11_BUFFER_DESC accumulationDesc {};
    accumulationDesc.ByteWidth = static_cast<UINT>(width * height * 5 * sizeof(std::uint32_t));
    accumulationDesc.Usage = D3D11_USAGE_DEFAULT;
    accumulationDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    accumulationDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_UNORDERED_ACCESS_VIEW_DESC accumulationUavDesc {};
    accumulationUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    accumulationUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    accumulationUavDesc.Buffer.FirstElement = 0;
    accumulationUavDesc.Buffer.NumElements = static_cast<UINT>(width * height * 5);
    accumulationUavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    const D3D11BufferViewStages accumulationStages {
        "CreateBuffer(accumulationBuffer)",
        nullptr,
        "CreateUnorderedAccessView(accumulationBuffer)",
    };
    const char* failedStage = nullptr;
    HRESULT failedResult = S_OK;
    if (!CreateBufferWithUnorderedAccessView(
            device_,
            accumulationDesc,
            accumulationUavDesc,
            accumulationStages,
            &accumulationBuffer_,
            &accumulationUav_,
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }

    const DXGI_FORMAT outputFormat = ChooseOutputFormat();
    if (outputFormat == DXGI_FORMAT_UNKNOWN) {
        SetError("No supported UAV texture format was found for GPU flame output.");
        return false;
    }

    D3D11_TEXTURE2D_DESC outputDesc {};
    outputDesc.Width = static_cast<UINT>(width);
    outputDesc.Height = static_cast<UINT>(height);
    outputDesc.MipLevels = 1;
    outputDesc.ArraySize = 1;
    outputDesc.Format = outputFormat;
    outputDesc.SampleDesc.Count = 1;
    outputDesc.Usage = D3D11_USAGE_DEFAULT;
    outputDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    const D3D11TextureViewStages outputStages {
        "CreateTexture2D(outputTexture)",
        "CreateUnorderedAccessView(outputTexture)",
        "CreateShaderResourceView(outputTexture)",
    };
    if (!CreateTexture2DWithViews(
            device_,
            outputDesc,
            outputStages,
            &outputTexture_,
            &outputUav_,
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
    depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    const D3D11TextureViewStages depthStages {
        "CreateTexture2D(depthTexture)",
        "CreateUnorderedAccessView(depthTexture)",
        "CreateShaderResourceView(depthTexture)",
    };
    if (!CreateTexture2DWithViews(
            device_,
            depthDesc,
            depthStages,
            &depthTexture_,
            &depthUav_,
            &depthSrv_,
            failedStage,
            failedResult)) {
        SetError(failedStage, failedResult);
        return false;
    }
    outputWidth_ = width;
    outputHeight_ = height;
    ResetAccumulation();
    return true;
}

void GpuFlameRenderer::SetError(const char* stage, const HRESULT result) {
    lastError_ = FormatD3D11StageError(stage, result);
}

DXGI_FORMAT GpuFlameRenderer::ChooseOutputFormat() const {
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

void GpuFlameRenderer::ReleaseTextures() {
    if (depthSrv_) { depthSrv_->Release(); depthSrv_ = nullptr; }
    if (depthUav_) { depthUav_->Release(); depthUav_ = nullptr; }
    if (depthTexture_) { depthTexture_->Release(); depthTexture_ = nullptr; }
    if (outputSrv_) { outputSrv_->Release(); outputSrv_ = nullptr; }
    if (outputUav_) { outputUav_->Release(); outputUav_ = nullptr; }
    if (outputTexture_) { outputTexture_->Release(); outputTexture_ = nullptr; }
    if (accumulationUav_) { accumulationUav_->Release(); accumulationUav_ = nullptr; }
    if (accumulationBuffer_) { accumulationBuffer_->Release(); accumulationBuffer_ = nullptr; }
    outputWidth_ = 0;
    outputHeight_ = 0;
}

void GpuFlameRenderer::ReleaseBuffers() {
    if (paletteSrv_) { paletteSrv_->Release(); paletteSrv_ = nullptr; }
    if (paletteBuffer_) { paletteBuffer_->Release(); paletteBuffer_ = nullptr; }
    if (orbitStateUav_) { orbitStateUav_->Release(); orbitStateUav_ = nullptr; }
    if (orbitStateBuffer_) { orbitStateBuffer_->Release(); orbitStateBuffer_ = nullptr; }
    if (transformSrv_) { transformSrv_->Release(); transformSrv_ = nullptr; }
    if (transformBuffer_) { transformBuffer_->Release(); transformBuffer_ = nullptr; }
    transformCapacity_ = 0;
    orbitStateCapacity_ = 0;
}

}  // namespace radiary
