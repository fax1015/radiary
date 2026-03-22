#pragma once

#include <d3d11.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "app/PreviewPresentation.h"
#include "core/Scene.h"

namespace radiary {

enum class BackendPreviewSurface {
    Cpu,
    Flame,
    Grid,
    Path,
    Denoised,
    DepthOfField,
    PostProcessed,
};

struct BackendPreviewSurfaceSet {
    ID3D11ShaderResourceView* cpu = nullptr;
    ID3D11ShaderResourceView* flame = nullptr;
    ID3D11ShaderResourceView* grid = nullptr;
    ID3D11ShaderResourceView* path = nullptr;
    ID3D11ShaderResourceView* denoised = nullptr;
    ID3D11ShaderResourceView* depthOfField = nullptr;
    ID3D11ShaderResourceView* postProcessed = nullptr;
};

struct BackendPreviewLookupRequest {
    PreviewPresentationState presentation {};
    SceneMode sceneMode = SceneMode::Flame;
    bool gridVisible = false;
    BackendPreviewSurfaceSet surfaces {};
};

struct BackendPreviewLayer {
    void* textureId = nullptr;
    BackendPreviewSurface surface = BackendPreviewSurface::Cpu;
};

struct BackendPreviewImage {
    std::array<BackendPreviewLayer, 3> layers {};
    std::size_t layerCount = 0;

    bool HasLayers() const {
        return layerCount > 0 && layers[0].textureId != nullptr;
    }
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) = 0;
    virtual void Shutdown() = 0;

    virtual bool UploadCpuPreview(int width, int height, const std::vector<std::uint32_t>& pixels) = 0;
    virtual void SetPresentedPreviewSize(int width, int height) = 0;
    virtual void ResetCpuPreviewSurface() = 0;
    virtual ID3D11ShaderResourceView* CpuPreviewShaderResourceView() const = 0;
    virtual int PresentedPreviewWidth() const = 0;
    virtual int PresentedPreviewHeight() const = 0;

    virtual bool ReadbackColorTexture(ID3D11Texture2D* texture, std::vector<std::uint32_t>& pixels) const = 0;
    virtual bool ReadbackDepthTexture(ID3D11Texture2D* texture, std::vector<float>& depthBuffer) const = 0;

    virtual BackendPreviewImage ResolvePreviewImage(const BackendPreviewLookupRequest& request) const = 0;
};

RenderBackend* CreateD3D11RenderBackend();

}  // namespace radiary
