#include "renderer/backend/RenderBackend.h"

#include <algorithm>
#include <memory>

#include <wrl/client.h>

#include "app/ExportUtils.h"

namespace radiary {
namespace {

class D3D11RenderBackend final : public RenderBackend {
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) override {
        device_ = device;
        deviceContext_ = deviceContext;
        ResetCpuPreviewSurface();
        return device_ != nullptr && deviceContext_ != nullptr;
    }

    void Shutdown() override {
        ResetCpuPreviewSurface();
        deviceContext_.Reset();
        device_.Reset();
    }

    bool UploadCpuPreview(const int width, const int height, const std::vector<std::uint32_t>& pixels) override {
        if (device_ == nullptr || deviceContext_ == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        const std::size_t expectedPixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (pixels.size() < expectedPixelCount) {
            return false;
        }
        if (!EnsureCpuPreviewTexture(width, height)) {
            return false;
        }

        D3D11_BOX box {};
        box.left = 0;
        box.top = 0;
        box.front = 0;
        box.right = static_cast<UINT>(width);
        box.bottom = static_cast<UINT>(height);
        box.back = 1;
        deviceContext_->UpdateSubresource(cpuPreviewTexture_.Get(), 0, &box, pixels.data(), width * 4, 0);
        SetPresentedPreviewSize(width, height);
        return true;
    }

    void SetPresentedPreviewSize(const int width, const int height) override {
        presentedPreviewWidth_ = std::max(0, width);
        presentedPreviewHeight_ = std::max(0, height);
    }

    void ResetCpuPreviewSurface() override {
        cpuPreviewSrv_.Reset();
        cpuPreviewTexture_.Reset();
        cpuPreviewWidth_ = 0;
        cpuPreviewHeight_ = 0;
        presentedPreviewWidth_ = 0;
        presentedPreviewHeight_ = 0;
    }

    ID3D11ShaderResourceView* CpuPreviewShaderResourceView() const override {
        return cpuPreviewSrv_.Get();
    }

    int PresentedPreviewWidth() const override {
        return presentedPreviewWidth_;
    }

    int PresentedPreviewHeight() const override {
        return presentedPreviewHeight_;
    }

    bool ReadbackColorTexture(ID3D11Texture2D* texture, std::vector<std::uint32_t>& pixels) const override {
        pixels.clear();
        if (device_ == nullptr || deviceContext_ == nullptr || texture == nullptr) {
            return false;
        }

        D3D11_TEXTURE2D_DESC desc {};
        texture->GetDesc(&desc);
        if (desc.Width == 0 || desc.Height == 0) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
        if (!CreateReadbackTexture(desc, stagingTexture)) {
            return false;
        }

        deviceContext_->CopyResource(stagingTexture.Get(), texture);

        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (FAILED(deviceContext_->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            return false;
        }

        bool success = false;
        pixels.resize(static_cast<std::size_t>(desc.Width) * static_cast<std::size_t>(desc.Height));
        for (UINT y = 0; y < desc.Height; ++y) {
            const BYTE* rowBytes = static_cast<const BYTE*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
            for (UINT x = 0; x < desc.Width; ++x) {
                const std::size_t pixelIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(desc.Width) + static_cast<std::size_t>(x);
                if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                    const BYTE* rgba = rowBytes + static_cast<std::size_t>(x) * 4u;
                    pixels[pixelIndex] = PackBgra(rgba[0], rgba[1], rgba[2], rgba[3]);
                } else if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                    const std::uint16_t* rgba = reinterpret_cast<const std::uint16_t*>(rowBytes + static_cast<std::size_t>(x) * sizeof(std::uint16_t) * 4u);
                    pixels[pixelIndex] = PackBgra(
                        FloatToByte(HalfToFloat(rgba[0])),
                        FloatToByte(HalfToFloat(rgba[1])),
                        FloatToByte(HalfToFloat(rgba[2])),
                        FloatToByte(HalfToFloat(rgba[3])));
                } else if (desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
                    const float* rgba = reinterpret_cast<const float*>(rowBytes + static_cast<std::size_t>(x) * sizeof(float) * 4u);
                    pixels[pixelIndex] = PackBgra(
                        FloatToByte(rgba[0]),
                        FloatToByte(rgba[1]),
                        FloatToByte(rgba[2]),
                        FloatToByte(rgba[3]));
                } else {
                    pixels.clear();
                    break;
                }
            }
            if (pixels.empty()) {
                break;
            }
        }
        deviceContext_->Unmap(stagingTexture.Get(), 0);
        success = !pixels.empty();
        return success;
    }

    bool ReadbackDepthTexture(ID3D11Texture2D* texture, std::vector<float>& depthBuffer) const override {
        depthBuffer.clear();
        if (device_ == nullptr || deviceContext_ == nullptr || texture == nullptr) {
            return false;
        }

        D3D11_TEXTURE2D_DESC desc {};
        texture->GetDesc(&desc);
        if (desc.Width == 0 || desc.Height == 0 || desc.Format != DXGI_FORMAT_R32_FLOAT) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
        if (!CreateReadbackTexture(desc, stagingTexture)) {
            return false;
        }

        deviceContext_->CopyResource(stagingTexture.Get(), texture);

        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (FAILED(deviceContext_->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            return false;
        }

        depthBuffer.resize(static_cast<std::size_t>(desc.Width) * static_cast<std::size_t>(desc.Height));
        for (UINT y = 0; y < desc.Height; ++y) {
            const float* row = reinterpret_cast<const float*>(static_cast<const BYTE*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch);
            std::copy_n(row, desc.Width, depthBuffer.begin() + static_cast<std::size_t>(y) * static_cast<std::size_t>(desc.Width));
        }

        deviceContext_->Unmap(stagingTexture.Get(), 0);
        return !depthBuffer.empty();
    }

    BackendPreviewImage ResolvePreviewImage(const BackendPreviewLookupRequest& request) const override {
        BackendPreviewImage image;
        const BackendPreviewSurfaceSet& surfaces = request.surfaces;

        const auto appendLayer = [&](ID3D11ShaderResourceView* srv, const BackendPreviewSurface surface) {
            if (srv == nullptr || image.layerCount >= image.layers.size()) {
                return;
            }
            image.layers[image.layerCount++] = BackendPreviewLayer {
                reinterpret_cast<void*>(srv),
                surface
            };
        };

        if (request.presentation.device != PreviewRenderDevice::Gpu) {
            appendLayer(surfaces.cpu, BackendPreviewSurface::Cpu);
            return image;
        }

        switch (request.presentation.stage) {
        case PreviewRenderStage::PostProcessed:
            appendLayer(surfaces.postProcessed, BackendPreviewSurface::PostProcessed);
            break;
        case PreviewRenderStage::Composited:
        case PreviewRenderStage::Denoised:
            appendLayer(surfaces.denoised, BackendPreviewSurface::Denoised);
            break;
        case PreviewRenderStage::DepthOfField:
            appendLayer(surfaces.depthOfField, BackendPreviewSurface::DepthOfField);
            break;
        case PreviewRenderStage::Base:
        default:
            if (request.presentation.content == PreviewRenderContent::Flame) {
                appendLayer(surfaces.flame, BackendPreviewSurface::Flame);
            } else if (request.presentation.content == PreviewRenderContent::Path) {
                appendLayer(surfaces.path, BackendPreviewSurface::Path);
            } else {
                if (request.gridVisible && surfaces.grid != nullptr) {
                    appendLayer(surfaces.grid, BackendPreviewSurface::Grid);
                } else {
                    appendLayer(surfaces.flame, BackendPreviewSurface::Flame);
                }
                if (request.gridVisible && surfaces.grid != nullptr) {
                    appendLayer(surfaces.flame, BackendPreviewSurface::Flame);
                }
                if (request.sceneMode == SceneMode::Hybrid) {
                    appendLayer(surfaces.path, BackendPreviewSurface::Path);
                }
            }
            break;
        }

        if (!image.HasLayers()) {
            appendLayer(surfaces.cpu, BackendPreviewSurface::Cpu);
        }
        return image;
    }

private:
    bool EnsureCpuPreviewTexture(const int width, const int height) {
        if (cpuPreviewTexture_ != nullptr && cpuPreviewWidth_ == width && cpuPreviewHeight_ == height) {
            return true;
        }

        ResetCpuPreviewSurface();

        D3D11_TEXTURE2D_DESC desc {};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device_->CreateTexture2D(&desc, nullptr, cpuPreviewTexture_.GetAddressOf()))) {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(device_->CreateShaderResourceView(cpuPreviewTexture_.Get(), &srvDesc, cpuPreviewSrv_.GetAddressOf()))) {
            ResetCpuPreviewSurface();
            return false;
        }

        cpuPreviewWidth_ = width;
        cpuPreviewHeight_ = height;
        return true;
    }

    bool CreateReadbackTexture(
        const D3D11_TEXTURE2D_DESC& sourceDesc,
        Microsoft::WRL::ComPtr<ID3D11Texture2D>& stagingTexture) const {
        D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
        stagingDesc.BindFlags = 0;
        stagingDesc.MiscFlags = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(device_->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.GetAddressOf()))
            && stagingTexture != nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> cpuPreviewTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cpuPreviewSrv_;
    int cpuPreviewWidth_ = 0;
    int cpuPreviewHeight_ = 0;
    int presentedPreviewWidth_ = 0;
    int presentedPreviewHeight_ = 0;
};

}  // namespace

RenderBackend* CreateD3D11RenderBackend() {
    return new D3D11RenderBackend();
}

}  // namespace radiary
