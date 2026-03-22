#pragma once

#include <d3d11.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/Scene.h"

namespace radiary {

class GpuFlameRenderer {
public:
    ~GpuFlameRenderer();

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext);
    void Shutdown();

    bool Render(
        const Scene& scene,
        int width,
        int height,
        std::uint32_t previewIterations,
        bool transparentBackground = false,
        bool clearAccumulationForFrame = false,
        bool preserveTemporalState = false,
        bool resetTemporalState = false);
    void ResetAccumulation();

    ID3D11ShaderResourceView* ShaderResourceView() const { return outputSrv_; }
    ID3D11Texture2D* OutputTexture() const { return outputTexture_; }
    ID3D11Texture2D* DepthTexture() const { return depthTexture_; }
    ID3D11ShaderResourceView* DepthShaderResourceView() const { return depthSrv_; }
    bool IsReady() const { return device_ != nullptr && accumulateShader_ != nullptr && toneMapShader_ != nullptr && paramsBuffer_ != nullptr; }
    const std::string& LastError() const { return lastError_; }
    std::uint64_t AccumulatedIterations() const { return accumulatedIterations_; }

private:
    struct TransformGpu {
        float weight = 0.0f;
        float cumulativeWeight = 0.0f;
        float rotationRadians = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float translateX = 0.0f;
        float translateY = 0.0f;
        float shearX = 0.0f;
        float shearY = 0.0f;
        float colorIndex = 0.5f;
        float useCustomColor = 0.0f;
        float customColor[4] {};
        float variations[static_cast<std::size_t>(VariationType::Count)] {};
    };

    struct RenderParams {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t previewIterations = 0;
        std::uint32_t transformCount = 0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float distance = 0.0f;
        float panX = 0.0f;
        float panY = 0.0f;
        float zoom2D = 1.0f;
        float flameRotateX = 0.0f;
        float flameRotateY = 0.0f;
        float flameRotateZ = 0.0f;
        float flameDepthAmount = 1.0f;
        float flameCurveExposure = 1.0f;
        float flameCurveContrast = 1.0f;
        float flameCurveHighlights = 1.0f;
        float flameCurveGamma = 1.0f;
        float backgroundR = 10.0f / 255.0f;
        float backgroundG = 10.0f / 255.0f;
        float backgroundB = 13.0f / 255.0f;
        float backgroundA = 1.0f;
        std::uint32_t gridVisible = 0;
        std::uint32_t totalThreadCount = 0;
        float worldScale = 1.0f;
        float totalWeight = 1.0f;
        std::uint32_t transparentBackground = 0;
        std::uint32_t randomSeedOffset = 0;
        std::uint32_t preserveOrbitState = 0;
        float farDepth = 24.0f;
        float padding[2] {};
    };

    struct OrbitStateGpu {
        float samplePoint[3] {};
        float colorIndex = 0.5f;
        std::uint32_t rngState = 0;
        std::uint32_t burnInRemaining = 0;
        std::uint32_t initialized = 0;
        std::uint32_t padding = 0;
    };

    bool CreateShaders();
    bool EnsureResources(int width, int height, std::size_t transformCount, std::size_t orbitThreadCount);
    void ReleaseTextures();
    void ReleaseBuffers();
    std::uint64_t ComputeSceneSignature(const Scene& scene) const;
    void SetError(const std::string& error) { lastError_ = error; }
    void SetError(const char* stage, HRESULT result);
    DXGI_FORMAT ChooseOutputFormat() const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;

    ID3D11ComputeShader* accumulateShader_ = nullptr;
    ID3D11ComputeShader* toneMapShader_ = nullptr;
    ID3D11Buffer* paramsBuffer_ = nullptr;
    ID3D11Buffer* transformBuffer_ = nullptr;
    ID3D11ShaderResourceView* transformSrv_ = nullptr;
    ID3D11Buffer* paletteBuffer_ = nullptr;
    ID3D11ShaderResourceView* paletteSrv_ = nullptr;

    ID3D11Buffer* accumulationBuffer_ = nullptr;
    ID3D11UnorderedAccessView* accumulationUav_ = nullptr;
    ID3D11Buffer* orbitStateBuffer_ = nullptr;
    ID3D11UnorderedAccessView* orbitStateUav_ = nullptr;
    ID3D11Texture2D* outputTexture_ = nullptr;
    ID3D11UnorderedAccessView* outputUav_ = nullptr;
    ID3D11ShaderResourceView* outputSrv_ = nullptr;
    ID3D11Texture2D* depthTexture_ = nullptr;
    ID3D11UnorderedAccessView* depthUav_ = nullptr;
    ID3D11ShaderResourceView* depthSrv_ = nullptr;

    int outputWidth_ = 0;
    int outputHeight_ = 0;
    std::size_t transformCapacity_ = 0;
    std::size_t orbitStateCapacity_ = 0;
    std::uint64_t sceneSignature_ = 0;
    std::uint64_t accumulatedIterations_ = 0;
    bool accumulationValid_ = false;
    bool temporalStateValid_ = false;
    std::size_t temporalThreadCount_ = 0;
    std::string lastError_;
};

}  // namespace radiary
