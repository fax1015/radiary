#include "app/AppWindow.h"
#include "app/CameraUtils.h"
#include "app/ExportUtils.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace radiary;

namespace {

constexpr std::uint32_t kExportStablePostProcessSeed = 0x5EED1234u;

std::wstring Utf8ToWideFilename(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (wideLength <= 1) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), wideLength);
    wide.pop_back();
    return wide;
}

std::wstring BuildDefaultSceneFilename(const radiary::Scene& scene) {
    std::wstring filename = Utf8ToWideFilename(scene.name);
    for (wchar_t& ch : filename) {
        switch (ch) {
        case L'<':
        case L'>':
        case L':':
        case L'"':
        case L'/':
        case L'\\':
        case L'|':
        case L'?':
        case L'*':
            ch = L'_';
            break;
        default:
            if (ch < 32) {
                ch = L'_';
            }
            break;
        }
    }

    const auto isTrimmedChar = [](const wchar_t ch) {
        return ch == L' ' || ch == L'.';
    };
    while (!filename.empty() && isTrimmedChar(filename.front())) {
        filename.erase(filename.begin());
    }
    while (!filename.empty() && isTrimmedChar(filename.back())) {
        filename.pop_back();
    }
    if (filename.empty()) {
        filename = L"scene";
    }
    return filename + L".radiary";
}

void WriteFourCc(std::ostream& stream, const char fourCc[4]) {
    stream.write(fourCc, 4);
}

void WriteU16(std::ostream& stream, const std::uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu)
    };
    stream.write(bytes, 2);
}

void WriteU32(std::ostream& stream, const std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu),
        static_cast<char>((value >> 16u) & 0xFFu),
        static_cast<char>((value >> 24u) & 0xFFu)
    };
    stream.write(bytes, 4);
}

void WriteS16(std::ostream& stream, const std::int16_t value) {
    WriteU16(stream, static_cast<std::uint16_t>(value));
}

void WriteS32(std::ostream& stream, const std::int32_t value) {
    WriteU32(stream, static_cast<std::uint32_t>(value));
}

}  // namespace

namespace radiary {
bool AppWindow::SaveSceneToDialog(const bool saveAs) {
    std::filesystem::path path = currentScenePath_;
    if (saveAs || path.empty()) {
        wchar_t fileBuffer[MAX_PATH] = L"";
        const std::wstring defaultFilename = BuildDefaultSceneFilename(scene_);
        wcsncpy_s(fileBuffer, defaultFilename.c_str(), _TRUNCATE);
        OPENFILENAMEW dialog {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"Radiary Scene (*.radiary)\0*.radiary\0\0";
        dialog.lpstrFile = fileBuffer;
        dialog.nMaxFile = MAX_PATH;
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        dialog.lpstrDefExt = L"radiary";
        if (!GetSaveFileNameW(&dialog)) {
            return false;
        }
        path = fileBuffer;
    }

    std::string error;
    if (!serializer_.Save(scene_, path, error)) {
        statusText_ = L"Save failed";
        return false;
    }

    currentScenePath_ = path;
    statusText_ = L"Saved " + path.filename().wstring();
    return true;
}

bool AppWindow::LoadSceneFromDialog() {
    wchar_t fileBuffer[MAX_PATH] = L"";
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Radiary Scene (*.radiary)\0*.radiary\0\0";
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = L"radiary";
    if (!GetOpenFileNameW(&dialog)) {
        return false;
    }

    std::string error;
    if (auto scene = serializer_.Load(fileBuffer, error)) {
        ResetScene(*scene);
        currentScenePath_ = fileBuffer;
        statusText_ = L"Opened " + currentScenePath_.filename().wstring();
        return true;
    }

    statusText_ = error.empty() ? L"Open failed" : L"Open failed: " + Utf8ToWide(error);
    return false;
}

void AppWindow::OpenExportPanel() {
    const std::vector<ExportResolutionPreset> presets = BuildExportResolutionPresets(scene_.camera);
    if (!presets.empty()) {
        const std::size_t defaultPresetIndex = presets.size() > 1 ? 1 : 0;
        exportWidth_ = presets[defaultPresetIndex].width;
        exportHeight_ = presets[defaultPresetIndex].height;
    } else {
        exportWidth_ = 1920;
        exportHeight_ = 1080;
        ConstrainExportResolutionToCamera(scene_.camera, exportWidth_, exportHeight_, true);
    }
    exportIterations_ = std::max<std::uint32_t>(scene_.previewIterations, 1u);
    exportFrameStart_ = scene_.timelineStartFrame;
    exportFrameEnd_ = scene_.timelineEndFrame;
    exportPanelOpen_ = true;
}

bool AppWindow::ExportViewportToDialog() {
    const bool exportPng = exportFormat_ == ExportFormat::Png || exportFormat_ == ExportFormat::PngSequence;
    const bool exportJpeg = exportFormat_ == ExportFormat::Jpeg || exportFormat_ == ExportFormat::JpegSequence;
    const bool exportAvi = exportFormat_ == ExportFormat::Avi;
    const bool exportMp4 = exportFormat_ == ExportFormat::Mp4;
    const bool exportMov = exportFormat_ == ExportFormat::Mov;
    wchar_t fileBuffer[MAX_PATH] = L"radiary-export.png";
    if (exportJpeg) {
        wcscpy_s(fileBuffer, L"radiary-export.jpg");
    } else if (exportAvi) {
        wcscpy_s(fileBuffer, L"radiary-export.avi");
    } else if (exportMp4) {
        wcscpy_s(fileBuffer, L"radiary-export.mp4");
    } else if (exportMov) {
        wcscpy_s(fileBuffer, L"radiary-export.mov");
    }
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = exportAvi
        ? L"AVI Video (*.avi)\0*.avi\0\0"
        : exportMp4
            ? L"MP4 Video (*.mp4)\0*.mp4\0\0"
            : exportMov
                ? L"MOV Video (*.mov)\0*.mov\0\0"
                : exportPng
            ? L"PNG Image (*.png)\0*.png\0\0"
            : L"JPEG Image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = exportAvi ? L"avi" : exportMp4 ? L"mp4" : exportMov ? L"mov" : exportPng ? L"png" : L"jpg";
    if (!GetSaveFileNameW(&dialog)) {
        return false;
    }
    pendingExportRequest_.path = fileBuffer;
    pendingExportRequest_.width = exportWidth_;
    pendingExportRequest_.height = exportHeight_;
    pendingExportRequest_.iterations = exportIterations_;
    pendingExportRequest_.transparentBackground = exportPng && exportTransparentBackground_;
    pendingExportRequest_.hideGrid = exportHideGrid_;
    pendingExportRequest_.useGpu = exportUseGpu_;
    pendingExportRequest_.format = exportFormat_;
    pendingExportRequest_.startFrame = exportFrameStart_;
    pendingExportRequest_.endFrame = exportFrameEnd_;
    exportRequestPending_ = true;
    statusText_ = L"Preparing export...";
    return true;
}

bool AppWindow::RenderSceneToPixels(
    const Scene& sourceScene,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    const bool useGpu,
    std::vector<std::uint32_t>& pixels,
    const ExportRenderState* renderState) {
    return useGpu
        ? RenderSceneToPixelsGpu(sourceScene, width, height, iterations, transparentBackground, hideGrid, pixels, renderState)
        : RenderSceneToPixelsCpu(sourceScene, width, height, iterations, transparentBackground, hideGrid, pixels, renderState);
}

namespace {

std::uint32_t ResolveGpuExportFlameIterations(const radiary::Scene& scene) {
    return std::max<std::uint32_t>(scene.previewIterations, 1u);
}

}  // namespace

bool AppWindow::RenderSceneToPixelsCpu(
    const Scene& sourceScene,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    std::vector<std::uint32_t>& pixels,
    const ExportRenderState* renderState) const {
    const int previewWidth = std::max(0, uploadedViewportWidth_);
    const int previewHeight = std::max(0, uploadedViewportHeight_);
    Scene exportScene = PrepareSceneForExport(BuildRenderableScene(sourceScene), previewWidth, previewHeight, width, height, hideGrid);
    exportScene.previewIterations = std::max<std::uint32_t>(iterations, 1u);
    SoftwareRenderer localRenderer;
    SoftwareRenderer& exportRenderer = renderState != nullptr && renderState->cpuRenderer != nullptr
        ? *renderState->cpuRenderer
        : localRenderer;
    if (renderState != nullptr && renderState->resetTemporalFlameState) {
        exportRenderer.InvalidateAccumulation();
    }
    SoftwareRenderer::RenderOptions renderOptions;
    renderOptions.renderFlame = true;
    renderOptions.renderPaths = true;
    renderOptions.renderGrid = !hideGrid;
    renderOptions.transparentBackground = transparentBackground;
    renderOptions.preserveFlameState = renderState != nullptr && renderState->preserveTemporalFlameState;
    exportRenderer.RenderViewport(exportScene, std::max(1, width), std::max(1, height), pixels, renderOptions);
    return !pixels.empty();
}

bool AppWindow::ReadbackGpuTexture(ID3D11Texture2D* texture, std::vector<std::uint32_t>& pixels) const {
    pixels.clear();
    if (!device_ || !deviceContext_ || texture == nullptr) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc {};
    texture->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0) {
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
    const HRESULT createResult = device_->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.GetAddressOf());
    if (FAILED(createResult) || !stagingTexture) {
        return false;
    }

    bool success = false;
    deviceContext_->CopyResource(stagingTexture.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped {};
    const HRESULT mapResult = deviceContext_->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(mapResult)) {
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
    }

    return success;
}

bool AppWindow::ReadbackGpuDepthTexture(ID3D11Texture2D* texture, std::vector<float>& depthBuffer) const {
    depthBuffer.clear();
    if (!device_ || !deviceContext_ || texture == nullptr) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc {};
    texture->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0 || desc.Format != DXGI_FORMAT_R32_FLOAT) {
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
    const HRESULT createResult = device_->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.GetAddressOf());
    if (FAILED(createResult) || !stagingTexture) {
        return false;
    }

    deviceContext_->CopyResource(stagingTexture.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped {};
    const HRESULT mapResult = deviceContext_->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(mapResult)) {
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

bool AppWindow::RenderSceneToPixelsGpu(
    const Scene& sourceScene,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    std::vector<std::uint32_t>& pixels,
    const ExportRenderState* renderState) {
    pixels.clear();
    const auto invalidateViewportPreview = [&]() {
        viewportDirty_ = true;
        uploadedViewportWidth_ = 0;
        uploadedViewportHeight_ = 0;
    };
    const auto pumpExportOverlay = [&]() {
        if (!exportInProgress_) {
            return true;
        }
        if (!PresentBlockingOverlay()) {
            return false;
        }
        return !exportCancelRequested_;
    };

    if (device_ == nullptr || deviceContext_ == nullptr) {
        invalidateViewportPreview();
        return false;
    }

    const int previewWidth = std::max(0, uploadedViewportWidth_);
    const int previewHeight = std::max(0, uploadedViewportHeight_);
    Scene exportScene = PrepareSceneForExport(BuildRenderableScene(sourceScene), previewWidth, previewHeight, width, height, hideGrid);
    exportScene.previewIterations = std::max<std::uint32_t>(iterations, 1u);
    const int exportWidth = std::max(1, width);
    const int exportHeight = std::max(1, height);
    const bool exportGrid = !hideGrid && exportScene.gridVisible;

    auto renderPathSceneGpu = [&](const Scene& scene, const bool transparent, const bool renderGrid, ID3D11ShaderResourceView* flameDepthSrv, GpuPathRenderer& renderer) {
        const wchar_t* label = &renderer == &gpuGridRenderer_ ? L"GPU grid renderer" : L"GPU path renderer";
        if (!EnsureGpuPathRendererInitialized(renderer, label)) {
            return false;
        }
        return renderer.Render(scene, exportWidth, exportHeight, transparent, renderGrid, flameDepthSrv);
    };

    auto renderFlameSceneGpu = [&](const Scene& scene, const bool transparent) {
        if (!EnsureGpuFlameRendererInitialized()) {
            return false;
        }
        const std::uint32_t targetIterations = ResolveGpuExportFlameIterations(scene);
        const bool preserveTemporalFlameState =
            renderState != nullptr
            && renderState->preserveTemporalFlameState;
        const bool resetTemporalFlameState =
            renderState != nullptr
            && renderState->resetTemporalFlameState;
        if (exportCancelRequested_) {
            return false;
        }
        if (!pumpExportOverlay()) {
            return false;
        }
        if (!gpuFlameRenderer_.Render(
                scene,
                exportWidth,
                exportHeight,
                targetIterations,
                transparent,
                true,
                preserveTemporalFlameState,
                resetTemporalFlameState)) {
            return false;
        }
        if (!pumpExportOverlay()) {
            return false;
        }
        return true;
    };

    auto readPathScene = [&](const Scene& scene, const bool transparent, const bool renderGrid, ID3D11ShaderResourceView* flameDepthSrv, std::vector<std::uint32_t>& output) {
        if (!renderPathSceneGpu(scene, transparent, renderGrid, flameDepthSrv, gpuPathRenderer_)) {
            return false;
        }
        return ReadbackGpuTexture(gpuPathRenderer_.OutputTexture(), output);
    };

    auto readFlameScene = [&](const Scene& scene, const bool transparent, std::vector<std::uint32_t>& output) {
        if (!renderFlameSceneGpu(scene, transparent)) {
            return false;
        }
        return ReadbackGpuTexture(gpuFlameRenderer_.OutputTexture(), output);
    };

    auto renderCompositeInputs = [&](
        ID3D11ShaderResourceView*& gridSrv,
        ID3D11ShaderResourceView*& flameSrv,
        ID3D11ShaderResourceView*& flameDepthSrv,
        ID3D11ShaderResourceView*& pathSrv,
        ID3D11ShaderResourceView*& pathDepthSrv) {
        bool success = false;
        gridSrv = nullptr;
        flameSrv = nullptr;
        flameDepthSrv = nullptr;
        pathSrv = nullptr;
        pathDepthSrv = nullptr;

        if (exportScene.mode == SceneMode::Path) {
            success = renderPathSceneGpu(exportScene, transparentBackground, exportGrid, nullptr, gpuPathRenderer_);
            pathSrv = success ? gpuPathRenderer_.ShaderResourceView() : nullptr;
            pathDepthSrv = success ? gpuPathRenderer_.DepthShaderResourceView() : nullptr;
            return success;
        }

        if (exportScene.mode == SceneMode::Flame) {
            if (exportGrid) {
                Scene backgroundScene = exportScene;
                backgroundScene.mode = SceneMode::Flame;
                success = renderPathSceneGpu(backgroundScene, transparentBackground, true, nullptr, gpuGridRenderer_);
                if (!success) {
                    return false;
                }
                gridSrv = gpuGridRenderer_.ShaderResourceView();
            }
            success = renderFlameSceneGpu(exportScene, transparentBackground || exportGrid);
            flameSrv = success ? gpuFlameRenderer_.ShaderResourceView() : nullptr;
            flameDepthSrv = success ? gpuFlameRenderer_.DepthShaderResourceView() : nullptr;
            return success;
        }

        if (!transparentBackground || exportGrid) {
            Scene backgroundScene = exportScene;
            backgroundScene.mode = SceneMode::Flame;
            success = renderPathSceneGpu(backgroundScene, transparentBackground, exportGrid, nullptr, gpuGridRenderer_);
            if (!success) {
                return false;
            }
            gridSrv = gpuGridRenderer_.ShaderResourceView();
        }

        success = renderFlameSceneGpu(exportScene, true);
        if (!success) {
            return false;
        }
        flameSrv = gpuFlameRenderer_.ShaderResourceView();
        flameDepthSrv = gpuFlameRenderer_.DepthShaderResourceView();

        Scene pathScene = exportScene;
        pathScene.mode = SceneMode::Path;
        success = renderPathSceneGpu(pathScene, true, false, gpuFlameRenderer_.DepthShaderResourceView(), gpuPathRenderer_);
        pathSrv = success ? gpuPathRenderer_.ShaderResourceView() : nullptr;
        pathDepthSrv = success ? gpuPathRenderer_.DepthShaderResourceView() : nullptr;
        return success;
    };

    auto renderDenoisedComposite = [&](
        ID3D11ShaderResourceView* gridSrv,
        ID3D11ShaderResourceView*& flameSrv,
        ID3D11ShaderResourceView*& flameDepthSrv,
        ID3D11ShaderResourceView* pathSrv,
        ID3D11ShaderResourceView* pathDepthSrv) {
        if (!EnsureGpuDenoiserInitialized()) {
            return false;
        }
        if (!gpuDenoiser_.Render(exportScene, exportWidth, exportHeight, gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv)) {
            return false;
        }
        flameSrv = gpuDenoiser_.ShaderResourceView();
        flameDepthSrv = gpuDenoiser_.DepthShaderResourceView();
        return true;
    };

    auto applyPostProcessAndReadback = [&](ID3D11ShaderResourceView* srv, ID3D11Texture2D* fallbackTexture, std::vector<std::uint32_t>& output) -> bool {
        if (exportScene.postProcess.enabled && srv != nullptr && EnsureGpuPostProcessInitialized()) {
            if (gpuPostProcess_.Render(exportScene, exportWidth, exportHeight, srv, kExportStablePostProcessSeed)) {
                return ReadbackGpuTexture(gpuPostProcess_.OutputTexture(), output);
            }
        }
        return ReadbackGpuTexture(fallbackTexture, output);
    };

    if (exportScene.denoiser.enabled || exportScene.depthOfField.enabled) {
        ID3D11ShaderResourceView* gridSrv = nullptr;
        ID3D11ShaderResourceView* flameSrv = nullptr;
        ID3D11ShaderResourceView* flameDepthSrv = nullptr;
        ID3D11ShaderResourceView* pathSrv = nullptr;
        ID3D11ShaderResourceView* pathDepthSrv = nullptr;
        if (!renderCompositeInputs(gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv)) {
            invalidateViewportPreview();
            return false;
        }
        if (!pumpExportOverlay()) {
            invalidateViewportPreview();
            return false;
        }

        if (exportScene.denoiser.enabled) {
            if (!renderDenoisedComposite(gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv)) {
                invalidateViewportPreview();
                return false;
            }
            gridSrv = nullptr;
            pathSrv = nullptr;
            pathDepthSrv = nullptr;
            if (!pumpExportOverlay()) {
                invalidateViewportPreview();
                return false;
            }
        }

        bool success = false;
        if (exportScene.depthOfField.enabled) {
            success = EnsureGpuDofRendererInitialized()
                && gpuDofRenderer_.Render(exportScene, exportWidth, exportHeight, gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv)
                && applyPostProcessAndReadback(gpuDofRenderer_.ShaderResourceView(), gpuDofRenderer_.OutputTexture(), pixels);
        } else {
            success = applyPostProcessAndReadback(gpuDenoiser_.ShaderResourceView(), gpuDenoiser_.OutputTexture(), pixels);
        }
        invalidateViewportPreview();
        return success && !pixels.empty();
    }

    if (exportScene.postProcess.enabled) {
        ID3D11ShaderResourceView* gridSrv = nullptr;
        ID3D11ShaderResourceView* flameSrv = nullptr;
        ID3D11ShaderResourceView* flameDepthSrv = nullptr;
        ID3D11ShaderResourceView* pathSrv = nullptr;
        ID3D11ShaderResourceView* pathDepthSrv = nullptr;
        if (!renderCompositeInputs(gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv)) {
            invalidateViewportPreview();
            return false;
        }
        if (!EnsureGpuDenoiserInitialized()) {
            invalidateViewportPreview();
            return false;
        }
        if (!gpuDenoiser_.Render(exportScene, exportWidth, exportHeight, gridSrv, flameSrv, flameDepthSrv, pathSrv, pathDepthSrv)) {
            invalidateViewportPreview();
            return false;
        }
        bool success = applyPostProcessAndReadback(gpuDenoiser_.ShaderResourceView(), gpuDenoiser_.OutputTexture(), pixels);
        invalidateViewportPreview();
        return success && !pixels.empty();
    }

    bool success = false;
    if (exportScene.mode == SceneMode::Path) {
        success = readPathScene(exportScene, transparentBackground, exportGrid, nullptr, pixels);
    } else if (exportScene.mode == SceneMode::Flame) {
        if (exportGrid) {
            Scene backgroundScene = exportScene;
            backgroundScene.mode = SceneMode::Flame;
            success = readPathScene(backgroundScene, transparentBackground, true, nullptr, pixels);
            if (!success) {
                invalidateViewportPreview();
                return false;
            }
        }
        std::vector<std::uint32_t> flamePixels;
        success = readFlameScene(exportScene, transparentBackground || exportGrid, flamePixels);
        if (!success) {
            invalidateViewportPreview();
            return false;
        }
        if (pixels.empty()) {
            pixels = std::move(flamePixels);
        } else {
            CompositePixelsOver(pixels, flamePixels);
        }
    } else {
        if (!transparentBackground || exportGrid) {
            Scene backgroundScene = exportScene;
            backgroundScene.mode = SceneMode::Flame;
            success = readPathScene(backgroundScene, transparentBackground, exportGrid, nullptr, pixels);
            if (!success) {
                invalidateViewportPreview();
                return false;
            }
        } else {
            pixels.assign(static_cast<std::size_t>(exportWidth) * static_cast<std::size_t>(exportHeight), PackBgra(0u, 0u, 0u, 0u));
        }

        std::vector<std::uint32_t> flamePixels;
        success = readFlameScene(exportScene, true, flamePixels);
        if (!success) {
            invalidateViewportPreview();
            return false;
        }
        CompositePixelsOver(pixels, flamePixels);

        Scene pathScene = exportScene;
        pathScene.mode = SceneMode::Path;
        std::vector<std::uint32_t> pathPixels;
        success = readPathScene(pathScene, true, false, gpuFlameRenderer_.DepthShaderResourceView(), pathPixels);
        if (!success) {
            invalidateViewportPreview();
            return false;
        }
        CompositePixelsOver(pixels, pathPixels);
    }

    invalidateViewportPreview();
    return success && !pixels.empty();
}

bool AppWindow::ExportViewportImage(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    const bool useGpu,
    const ExportFormat format) {
    exportProgressTitle_ = L"Exporting image";
    if (!UpdateExportProgress(0.08f, L"Rendering " + path.filename().wstring())) {
        statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
        return false;
    }
    const Scene exportScene = EvaluateSceneAtFrame(scene_, scene_.timelineFrame);
    std::vector<std::uint32_t> exportPixels;
    bool useGpuRender = useGpu;
    if (useGpuRender && !RenderSceneToPixels(exportScene, width, height, iterations, transparentBackground, hideGrid, true, exportPixels)) {
        useGpuRender = false;
        statusText_ = L"GPU export unavailable, falling back to CPU";
    }
    if (!useGpuRender && !RenderSceneToPixels(exportScene, width, height, iterations, transparentBackground, hideGrid, false, exportPixels)) {
        statusText_ = L"Export failed";
        return false;
    }
    if (!UpdateExportProgress(0.78f, L"Writing " + path.filename().wstring())) {
        statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
        return false;
    }
    const int exportWidth = std::max(1, width);
    const int exportHeight = std::max(1, height);
    const bool success = SavePixelsToImageFile(path, exportPixels, exportWidth, exportHeight, format == ExportFormat::Jpeg || format == ExportFormat::JpegSequence);
    if (success) {
        UpdateExportProgress(1.0f, L"Finished " + path.filename().wstring());
    }
    statusText_ = success ? L"Exported " + path.filename().wstring() : L"Export failed";
    return success;
}

bool AppWindow::ExportImageSequence(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool transparentBackground,
    const bool hideGrid,
    const bool useGpu,
    const ExportFormat format,
    const int startFrame,
    const int endFrame) {
    const bool jpeg = format == ExportFormat::JpegSequence;
    const std::filesystem::path directory = path.parent_path().empty() ? std::filesystem::current_path() : path.parent_path();
    const std::wstring stem = path.stem().wstring();
    const int frameCount = std::max(1, endFrame - startFrame + 1);
    exportProgressTitle_ = L"Exporting image sequence";
    if (!UpdateExportProgress(0.0f, L"Preparing " + std::to_wstring(frameCount) + L" frames")) {
        statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
        return false;
    }
    bool useGpuRender = useGpu;
    SoftwareRenderer cpuSequenceRenderer;
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        const int frameIndex = frame - startFrame;
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex) / static_cast<float>(frameCount),
                L"Rendering frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
            return false;
        }
        Scene frameScene = EvaluateSceneAtFrame(scene_, static_cast<double>(frame));
        frameScene.timelineFrame = static_cast<double>(frame);
        frameScene.timelineSeconds = TimelineSecondsForFrame(frameScene, frameScene.timelineFrame);
        const ExportRenderState renderState {
            exportStableFlameSampling_ && frameCount > 1,
            exportStableFlameSampling_ && frameIndex == 0,
            &cpuSequenceRenderer
        };
        std::vector<std::uint32_t> pixels;
        if (useGpuRender && !RenderSceneToPixels(frameScene, width, height, iterations, transparentBackground, hideGrid, true, pixels, &renderState)) {
            useGpuRender = false;
            statusText_ = L"GPU export unavailable, falling back to CPU";
        }
        if (!useGpuRender && !RenderSceneToPixels(frameScene, width, height, iterations, transparentBackground, hideGrid, false, pixels, &renderState)) {
            statusText_ = L"Export failed";
            return false;
        }
        std::wstringstream filename;
        filename << stem << L"_" << std::setw(5) << std::setfill(L'0') << frame << (jpeg ? L".jpg" : L".png");
        if (!SavePixelsToImageFile(directory / filename.str(), pixels, std::max(1, width), std::max(1, height), jpeg)) {
            statusText_ = L"Export failed";
            return false;
        }
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex + 1) / static_cast<float>(frameCount),
                L"Saved frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
            return false;
        }
    }
    statusText_ = L"Exported image sequence";
    return true;
}

bool AppWindow::ExportAviVideo(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool hideGrid,
    const bool useGpu,
    const int startFrame,
    const int endFrame) {
    const int exportWidth = std::max(1, width);
    const int exportHeight = std::max(1, height);
    const int frameCount = std::max(1, endFrame - startFrame + 1);
    const std::uint32_t frameSize = static_cast<std::uint32_t>(exportWidth * exportHeight * 4);
    const std::uint32_t moviDataSize = static_cast<std::uint32_t>(frameCount) * (8u + frameSize);
    const std::uint32_t indexSize = static_cast<std::uint32_t>(frameCount) * 16u;
    constexpr std::uint32_t hdrlListSize = 192u;
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        statusText_ = L"Export failed";
        return false;
    }

    const std::uint32_t microsecondsPerFrame = static_cast<std::uint32_t>(std::round(1000000.0 / std::max(1.0, scene_.timelineFrameRate)));
    const std::uint32_t rate = static_cast<std::uint32_t>(std::round(scene_.timelineFrameRate * 1000.0));
    const std::uint32_t scale = 1000u;
    exportProgressTitle_ = L"Exporting video";
    if (!UpdateExportProgress(0.0f, L"Preparing AVI stream")) {
        statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
        return false;
    }

    WriteFourCc(stream, "RIFF");
    WriteU32(stream, 4u + (8u + hdrlListSize) + (8u + 4u + moviDataSize) + (8u + indexSize));
    WriteFourCc(stream, "AVI ");
    WriteFourCc(stream, "LIST");
    WriteU32(stream, hdrlListSize);
    WriteFourCc(stream, "hdrl");
    WriteFourCc(stream, "avih");
    WriteU32(stream, 56u);
    WriteU32(stream, microsecondsPerFrame);
    WriteU32(stream, frameSize * static_cast<std::uint32_t>(std::max(1.0, scene_.timelineFrameRate)));
    WriteU32(stream, 0u);
    WriteU32(stream, 0x10u);
    WriteU32(stream, static_cast<std::uint32_t>(frameCount));
    WriteU32(stream, 0u);
    WriteU32(stream, 1u);
    WriteU32(stream, frameSize);
    WriteU32(stream, static_cast<std::uint32_t>(exportWidth));
    WriteU32(stream, static_cast<std::uint32_t>(exportHeight));
    for (int index = 0; index < 4; ++index) {
        WriteU32(stream, 0u);
    }
    WriteFourCc(stream, "LIST");
    WriteU32(stream, 116u);
    WriteFourCc(stream, "strl");
    WriteFourCc(stream, "strh");
    WriteU32(stream, 56u);
    WriteFourCc(stream, "vids");
    WriteFourCc(stream, "DIB ");
    WriteU32(stream, 0u);
    WriteU16(stream, 0u);
    WriteU16(stream, 0u);
    WriteU32(stream, 0u);
    WriteU32(stream, scale);
    WriteU32(stream, rate);
    WriteU32(stream, 0u);
    WriteU32(stream, static_cast<std::uint32_t>(frameCount));
    WriteU32(stream, frameSize);
    WriteU32(stream, 0xFFFFFFFFu);
    WriteU32(stream, 0u);
    WriteS16(stream, 0);
    WriteS16(stream, 0);
    WriteS16(stream, static_cast<std::int16_t>(std::clamp(exportWidth, 0, 32767)));
    WriteS16(stream, static_cast<std::int16_t>(std::clamp(exportHeight, 0, 32767)));
    WriteFourCc(stream, "strf");
    WriteU32(stream, 40u);
    WriteU32(stream, 40u);
    WriteS32(stream, exportWidth);
    WriteS32(stream, -exportHeight);
    WriteU16(stream, 1u);
    WriteU16(stream, 32u);
    WriteU32(stream, 0u);
    WriteU32(stream, frameSize);
    WriteS32(stream, 0);
    WriteS32(stream, 0);
    WriteU32(stream, 0u);
    WriteU32(stream, 0u);
    WriteFourCc(stream, "LIST");
    WriteU32(stream, 4u + moviDataSize);
    WriteFourCc(stream, "movi");
    const std::uint32_t moviListOffsetBase = static_cast<std::uint32_t>(stream.tellp()) - 4u;

    std::vector<std::uint32_t> chunkOffsets;
    chunkOffsets.reserve(static_cast<std::size_t>(frameCount));
    bool useGpuRender = useGpu;
    SoftwareRenderer cpuSequenceRenderer;
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        const int frameIndex = frame - startFrame;
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex) / static_cast<float>(frameCount),
                L"Rendering frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
            return false;
        }
        chunkOffsets.push_back(static_cast<std::uint32_t>(stream.tellp()) - moviListOffsetBase);
        Scene frameScene = EvaluateSceneAtFrame(scene_, static_cast<double>(frame));
        frameScene.timelineFrame = static_cast<double>(frame);
        frameScene.timelineSeconds = TimelineSecondsForFrame(frameScene, frameScene.timelineFrame);
        const ExportRenderState renderState {
            exportStableFlameSampling_ && frameCount > 1,
            exportStableFlameSampling_ && frameIndex == 0,
            &cpuSequenceRenderer
        };
        std::vector<std::uint32_t> pixels;
        if (useGpuRender && !RenderSceneToPixels(frameScene, exportWidth, exportHeight, iterations, false, hideGrid, true, pixels, &renderState)) {
            useGpuRender = false;
            statusText_ = L"GPU export unavailable, falling back to CPU";
        }
        if (!useGpuRender && !RenderSceneToPixels(frameScene, exportWidth, exportHeight, iterations, false, hideGrid, false, pixels, &renderState)) {
            statusText_ = L"Export failed";
            return false;
        }
        WriteFourCc(stream, "00db");
        WriteU32(stream, frameSize);
        stream.write(reinterpret_cast<const char*>(pixels.data()), frameSize);
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex + 1) / static_cast<float>(frameCount),
                L"Wrote frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
            return false;
        }
    }

    WriteFourCc(stream, "idx1");
    WriteU32(stream, indexSize);
    for (std::size_t index = 0; index < chunkOffsets.size(); ++index) {
        WriteFourCc(stream, "00db");
        WriteU32(stream, 0x10u);
        WriteU32(stream, chunkOffsets[index]);
        WriteU32(stream, frameSize);
    }

    UpdateExportProgress(1.0f, L"Finalized " + path.filename().wstring());
    statusText_ = L"Exported " + path.filename().wstring();
    return true;
}

bool AppWindow::ExportFfmpegVideo(
    const std::filesystem::path& path,
    const int width,
    const int height,
    const std::uint32_t iterations,
    const bool hideGrid,
    const bool useGpu,
    const int startFrame,
    const int endFrame,
    const ExportFormat format) {
    const int exportWidth = std::max(1, width);
    const int exportHeight = std::max(1, height);
    const int frameCount = std::max(1, endFrame - startFrame + 1);
    const std::uint32_t frameSize = static_cast<std::uint32_t>(exportWidth * exportHeight * 4);
    const std::wstring fpsText = std::to_wstring(std::max(1.0, scene_.timelineFrameRate));
    const std::filesystem::path ffmpegPath = FindBundledFfmpegPath();
    const std::filesystem::path ffmpegLogPath = std::filesystem::temp_directory_path() / L"radiary-ffmpeg.log";
    const std::wstring codecArgs = format == ExportFormat::Mov
        ? L"-c:v libx264 -pix_fmt yuv420p"
        : L"-c:v libx264 -pix_fmt yuv420p";
    const std::wstring command =
        (ffmpegPath.empty() ? L"ffmpeg" : L"\"" + ffmpegPath.wstring() + L"\"")
        + L" -hide_banner -loglevel error"
        + L" -y -f rawvideo -pix_fmt bgra -s "
        + std::to_wstring(exportWidth) + L"x" + std::to_wstring(exportHeight)
        + L" -r " + fpsText
        + L" -i pipe:0 -an "
        + codecArgs
        + L" \"" + path.wstring() + L"\"";

    const auto ffmpegErrorDetail = [&]() {
        const std::wstring logText = ReadTextFile(ffmpegLogPath);
        std::error_code removeError;
        std::filesystem::remove(ffmpegLogPath, removeError);
        if (logText.empty()) {
            return std::wstring {};
        }

        std::wstring firstLine = logText;
        const std::size_t newline = firstLine.find_first_of(L"\r\n");
        if (newline != std::wstring::npos) {
            firstLine.resize(newline);
        }
        return firstLine;
    };
    SECURITY_ATTRIBUTES securityAttributes {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &securityAttributes, 0)) {
        statusText_ = L"Unable to create FFmpeg pipe";
        return false;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    HANDLE logHandle = CreateFileW(
        ffmpegLogPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &securityAttributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (logHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(stdinRead);
        CloseHandle(stdinWrite);
        statusText_ = L"Unable to create FFmpeg log";
        return false;
    }

    STARTUPINFOW startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinRead;
    startupInfo.hStdOutput = logHandle;
    startupInfo.hStdError = logHandle;

    PROCESS_INFORMATION processInfo {};
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    const BOOL created = CreateProcessW(
        nullptr,
        commandBuffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    CloseHandle(stdinRead);
    stdinRead = nullptr;
    if (!created) {
        CloseHandle(stdinWrite);
        CloseHandle(logHandle);
        statusText_ = ffmpegPath.empty() ? L"FFmpeg not found" : L"Bundled FFmpeg launch failed";
        ffmpegErrorDetail();
        return false;
    }

    const auto finishFfmpegProcess = [&](const bool terminate, DWORD* exitCode) {
        if (stdinWrite != nullptr) {
            CloseHandle(stdinWrite);
            stdinWrite = nullptr;
        }
        if (terminate) {
            TerminateProcess(processInfo.hProcess, 1u);
        }
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        if (exitCode != nullptr) {
            *exitCode = 1u;
            GetExitCodeProcess(processInfo.hProcess, exitCode);
        }
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        CloseHandle(logHandle);
    };

    exportProgressTitle_ = L"Exporting video";
    if (!UpdateExportProgress(0.0f, L"Streaming frames to FFmpeg")) {
        DWORD exitCode = 1u;
        finishFfmpegProcess(true, &exitCode);
        statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
        std::error_code cleanupError;
        std::filesystem::remove(path, cleanupError);
        return false;
    }

    bool useGpuRender = useGpu;
    SoftwareRenderer cpuSequenceRenderer;
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        const int frameIndex = frame - startFrame;
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex) / static_cast<float>(frameCount),
                L"Rendering frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            DWORD exitCode = 1u;
            finishFfmpegProcess(true, &exitCode);
            statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            ffmpegErrorDetail();
            return false;
        }
        Scene frameScene = EvaluateSceneAtFrame(scene_, static_cast<double>(frame));
        frameScene.timelineFrame = static_cast<double>(frame);
        frameScene.timelineSeconds = TimelineSecondsForFrame(frameScene, frameScene.timelineFrame);
        const ExportRenderState renderState {
            exportStableFlameSampling_ && frameCount > 1,
            exportStableFlameSampling_ && frameIndex == 0,
            &cpuSequenceRenderer
        };
        std::vector<std::uint32_t> pixels;
        if (useGpuRender && !RenderSceneToPixels(frameScene, exportWidth, exportHeight, iterations, false, hideGrid, true, pixels, &renderState)) {
            useGpuRender = false;
            statusText_ = L"GPU export unavailable, falling back to CPU";
        }
        if (!useGpuRender && !RenderSceneToPixels(frameScene, exportWidth, exportHeight, iterations, false, hideGrid, false, pixels, &renderState)) {
            DWORD exitCode = 1u;
            finishFfmpegProcess(true, &exitCode);
            statusText_ = L"Export failed";
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            ffmpegErrorDetail();
            return false;
        }
        DWORD bytesWritten = 0u;
        if (!WriteFile(stdinWrite, pixels.data(), frameSize, &bytesWritten, nullptr) || bytesWritten != frameSize) {
            DWORD exitCode = 1u;
            finishFfmpegProcess(false, &exitCode);
            const std::wstring errorDetail = ffmpegErrorDetail();
            statusText_ = errorDetail.empty()
                ? L"FFmpeg write failed"
                : L"FFmpeg error: " + errorDetail;
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            return false;
        }
        if (!UpdateExportProgress(
                static_cast<float>(frameIndex + 1) / static_cast<float>(frameCount),
                L"Encoded frame " + std::to_wstring(frame) + L" of " + std::to_wstring(endFrame))) {
            DWORD exitCode = 1u;
            finishFfmpegProcess(true, &exitCode);
            statusText_ = exportCancelRequested_ ? L"Export cancelled" : L"Export failed";
            std::error_code cleanupError;
            std::filesystem::remove(path, cleanupError);
            ffmpegErrorDetail();
            return false;
        }
    }

    DWORD ffmpegExitCode = 0u;
    finishFfmpegProcess(false, &ffmpegExitCode);
    if (ffmpegExitCode != 0u) {
        const std::wstring errorDetail = ffmpegErrorDetail();
        statusText_ = errorDetail.empty() ? L"FFmpeg export failed" : L"FFmpeg error: " + errorDetail;
        std::error_code cleanupError;
        std::filesystem::remove(path, cleanupError);
        return false;
    }

    ffmpegErrorDetail();
    UpdateExportProgress(1.0f, L"Finalized " + path.filename().wstring());
    statusText_ = L"Exported " + path.filename().wstring();
    return true;
}


}  // namespace radiary
