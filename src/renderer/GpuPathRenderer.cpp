#include "renderer/GpuPathRenderer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "renderer/SoftwareRenderer.h"

namespace radiary {

namespace {

constexpr float kFlameDepthNear = 0.15f;
constexpr float kFlameDepthRangePadding = 24.0f;
constexpr float kHybridFlameDepthBias = 0.005f;
constexpr float kHybridFlameDepthSoftness = 0.03f;

constexpr char kGpuPathShaderSource[] = R"(
cbuffer HybridParams : register(b0)
{
    float FlameDepthBias;
    float FlameDepthSoftness;
    uint UseFlameOcclusion;
    float Padding;
};

Texture2D<float> FlameDepthTexture : register(t0);

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
};

PSInput MainVS(VSInput input)
{
    PSInput output;
    output.Position = float4(input.Position, 1.0);
    output.Color = input.Color;
    return output;
}

float4 MainPS(PSInput input) : SV_Target0
{
    float alpha = input.Color.a;
    if (UseFlameOcclusion != 0u)
    {
        int2 pixel = int2(input.Position.xy);
        float flameDepth = FlameDepthTexture.Load(int3(pixel, 0));
        if (flameDepth < 0.999)
        {
            float depthDelta = input.Position.z - (flameDepth + FlameDepthBias);
            float occlusion = saturate(depthDelta / max(FlameDepthSoftness, 1.0e-5));
            alpha *= lerp(1.0, 0.14, occlusion);
        }
    }
    return float4(input.Color.rgb, alpha);
}
)";

float ToClipX(const float x, const int width) {
    return (x / std::max(1.0f, static_cast<float>(width))) * 2.0f - 1.0f;
}

float ToClipY(const float y, const int height) {
    return 1.0f - (y / std::max(1.0f, static_cast<float>(height))) * 2.0f;
}

float NormalizeDepth(const double depth, const double farDepth) {
    const double normalized = (depth - static_cast<double>(kFlameDepthNear)) / std::max(1.0e-6, static_cast<double>(farDepth - kFlameDepthNear));
    return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
}

GpuPathRenderer::Vertex MakeVertex(const float x, const float y, const float z, const Color& color, const double alpha, const int width, const int height) {
    GpuPathRenderer::Vertex vertex {};
    vertex.position[0] = ToClipX(x, width);
    vertex.position[1] = ToClipY(y, height);
    vertex.position[2] = std::clamp(z, 0.0f, 1.0f);
    const float alphaScale = static_cast<float>(std::clamp(alpha, 0.0, 1.0) * (static_cast<double>(color.a) / 255.0));
    vertex.color[0] = static_cast<float>(color.r) / 255.0f;
    vertex.color[1] = static_cast<float>(color.g) / 255.0f;
    vertex.color[2] = static_cast<float>(color.b) / 255.0f;
    vertex.color[3] = alphaScale;
    return vertex;
}

void EmitTriangle(
    std::vector<GpuPathRenderer::Vertex>& vertices,
    const SoftwareRenderer::ProjectedPoint& a,
    const SoftwareRenderer::ProjectedPoint& b,
    const SoftwareRenderer::ProjectedPoint& c,
    const Color& color,
    const double alpha,
    const int width,
    const int height,
    const double farDepth) {
    vertices.push_back(MakeVertex(static_cast<float>(a.x), static_cast<float>(a.y), NormalizeDepth(a.depth, farDepth), color, alpha, width, height));
    vertices.push_back(MakeVertex(static_cast<float>(b.x), static_cast<float>(b.y), NormalizeDepth(b.depth, farDepth), color, alpha, width, height));
    vertices.push_back(MakeVertex(static_cast<float>(c.x), static_cast<float>(c.y), NormalizeDepth(c.depth, farDepth), color, alpha, width, height));
}

void EmitLineQuad(
    std::vector<GpuPathRenderer::Vertex>& vertices,
    const SoftwareRenderer::ProjectedPoint& start,
    const SoftwareRenderer::ProjectedPoint& end,
    const Color& color,
    const double alpha,
    const double thickness,
    const int width,
    const int height,
    const double farDepth) {
    const float x0 = static_cast<float>(start.x);
    const float y0 = static_cast<float>(start.y);
    const float x1 = static_cast<float>(end.x);
    const float y1 = static_cast<float>(end.y);
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.01f) {
        return;
    }

    const float nx = (-dy / length) * static_cast<float>(thickness * 0.5);
    const float ny = (dx / length) * static_cast<float>(thickness * 0.5);
    const double averageDepth = (start.depth + end.depth) * 0.5;
    const float z = NormalizeDepth(averageDepth, farDepth);

    const GpuPathRenderer::Vertex a = MakeVertex(x0 - nx, y0 - ny, z, color, alpha, width, height);
    const GpuPathRenderer::Vertex b = MakeVertex(x0 + nx, y0 + ny, z, color, alpha, width, height);
    const GpuPathRenderer::Vertex c = MakeVertex(x1 + nx, y1 + ny, z, color, alpha, width, height);
    const GpuPathRenderer::Vertex d = MakeVertex(x1 - nx, y1 - ny, z, color, alpha, width, height);
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
    vertices.push_back(a);
    vertices.push_back(c);
    vertices.push_back(d);
}

void EmitPointQuad(
    std::vector<GpuPathRenderer::Vertex>& vertices,
    const SoftwareRenderer::ProjectedPoint& point,
    const Color& color,
    const double alpha,
    const double size,
    const int width,
    const int height,
    const double farDepth) {
    const float halfSize = static_cast<float>(std::max(1.0, size) * 0.5);
    const float x = static_cast<float>(point.x);
    const float y = static_cast<float>(point.y);
    const float z = NormalizeDepth(point.depth, farDepth);

    const GpuPathRenderer::Vertex a = MakeVertex(x - halfSize, y - halfSize, z, color, alpha, width, height);
    const GpuPathRenderer::Vertex b = MakeVertex(x + halfSize, y - halfSize, z, color, alpha, width, height);
    const GpuPathRenderer::Vertex c = MakeVertex(x + halfSize, y + halfSize, z, color, alpha, width, height);
    const GpuPathRenderer::Vertex d = MakeVertex(x - halfSize, y + halfSize, z, color, alpha, width, height);
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
    vertices.push_back(a);
    vertices.push_back(c);
    vertices.push_back(d);
}

void AppendGrid(
    std::vector<GpuPathRenderer::Vertex>& vertices,
    const Scene& scene,
    const int width,
    const int height) {
    constexpr int kGridHalfCount = 18;
    constexpr int kGridSamples = 48;
    constexpr double kGridExtent = 18.0;
    constexpr double kGridY = -2.4;

    const auto appendGridLine = [&](const Vec3& startWorld, const Vec3& endWorld, const Color& color, const double alpha, const double thickness) {
        SoftwareRenderer::ProjectedPoint previous = SoftwareRenderer::Project(startWorld, scene.camera, width, height);
        for (int sampleIndex = 1; sampleIndex <= kGridSamples; ++sampleIndex) {
            const double t = static_cast<double>(sampleIndex) / static_cast<double>(kGridSamples);
            const Vec3 currentWorld = startWorld + (endWorld - startWorld) * t;
            const SoftwareRenderer::ProjectedPoint current = SoftwareRenderer::Project(currentWorld, scene.camera, width, height);
            if (previous.visible && current.visible) {
                const double averageDepth = (previous.depth + current.depth) * 0.5;
                const double depthFade = Clamp(1.25 - averageDepth / 20.0, 0.18, 1.0);
                const double effectiveAlpha = Clamp(alpha * depthFade * 1.15, 0.0, 1.0);
                const double effectiveThickness = std::max(1.05, thickness * (0.85 + depthFade * 0.75));
                EmitLineQuad(
                    vertices,
                    previous,
                    current,
                    color,
                    effectiveAlpha,
                    effectiveThickness,
                    width,
                    height,
                    1.0);
            }
            previous = current;
        }
    };

    for (int index = -kGridHalfCount; index <= kGridHalfCount; ++index) {
        const bool majorLine = (index % 4) == 0;
        const double coordinate = static_cast<double>(index);
        appendGridLine(
            {coordinate, kGridY, -kGridExtent},
            {coordinate, kGridY, kGridExtent},
            majorLine ? Color{56, 68, 88, 255} : Color{44, 52, 66, 255},
            majorLine ? 0.52 : 0.34,
            majorLine ? 1.35 : 1.10);
        appendGridLine(
            {-kGridExtent, kGridY, coordinate},
            {kGridExtent, kGridY, coordinate},
            majorLine ? Color{56, 68, 88, 255} : Color{44, 52, 66, 255},
            majorLine ? 0.52 : 0.34,
            majorLine ? 1.35 : 1.10);
    }

    appendGridLine({-kGridExtent, kGridY, 0.0}, {kGridExtent, kGridY, 0.0}, {84, 118, 178, 255}, 0.50, 1.55);
    appendGridLine({0.0, kGridY, -kGridExtent}, {0.0, kGridY, kGridExtent}, {104, 142, 204, 255}, 0.58, 1.70);
}

}  // namespace

GpuPathRenderer::~GpuPathRenderer() {
    Shutdown();
}

bool GpuPathRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
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

    if (!CreatePipeline()) {
        Shutdown();
        return false;
    }

    return true;
}

void GpuPathRenderer::Shutdown() {
    ReleaseResources();
    ReleasePipeline();
    if (deviceContext_) { deviceContext_->Release(); deviceContext_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    lastError_.clear();
}

bool GpuPathRenderer::Render(
    const Scene& scene,
    const int width,
    const int height,
    const bool transparentBackground,
    const bool renderGrid,
    ID3D11ShaderResourceView* flameDepthSrv) {
    if (!IsReady()) {
        if (lastError_.empty()) {
            SetError("GPU path renderer is not initialized.");
        }
        return false;
    }
    if (width <= 0 || height <= 0) {
        SetError("Viewport size is invalid for GPU path preview.");
        return false;
    }
    if (!EnsureResources(width, height)) {
        return false;
    }

    std::vector<SoftwareRenderer::PathTriangle> fillTriangles;
    std::vector<SoftwareRenderer::PathLine> lines;
    std::vector<SoftwareRenderer::PathPointSprite> points;
    SoftwareRenderer::BuildPathPrimitives(scene, width, height, fillTriangles, lines, points);

    const double farDepth = std::max(
        static_cast<double>(kFlameDepthNear) + 1.0,
        scene.camera.distance + static_cast<double>(kFlameDepthRangePadding));

    std::vector<Vertex> gridVertices;
    std::vector<Vertex> fillVertices;
    std::vector<Vertex> overlayVertices;
    std::vector<Vertex> pointVertices;

    if (renderGrid && scene.gridVisible) {
        AppendGrid(gridVertices, scene, width, height);
    }
    fillVertices.reserve(fillTriangles.size() * 3);
    for (const auto& triangle : fillTriangles) {
        EmitTriangle(fillVertices, triangle.points[0], triangle.points[1], triangle.points[2], triangle.color, triangle.alpha, width, height, farDepth);
    }
    overlayVertices.reserve(lines.size() * 6);
    for (const auto& line : lines) {
        EmitLineQuad(overlayVertices, line.start, line.end, line.color, line.alpha, line.thickness, width, height, farDepth);
    }
    pointVertices.reserve(points.size() * 6);
    for (const auto& point : points) {
        EmitPointQuad(pointVertices, point.point, point.color, point.alpha, point.size, width, height, farDepth);
    }

    const float clearColor[4] = {
        transparentBackground ? 0.0f : (static_cast<float>(scene.backgroundColor.r) / 255.0f),
        transparentBackground ? 0.0f : (static_cast<float>(scene.backgroundColor.g) / 255.0f),
        transparentBackground ? 0.0f : (static_cast<float>(scene.backgroundColor.b) / 255.0f),
        transparentBackground ? 0.0f : 1.0f
    };
    deviceContext_->OMSetRenderTargets(1, &outputRtv_, depthDsv_);
    deviceContext_->ClearRenderTargetView(outputRtv_, clearColor);
    deviceContext_->ClearDepthStencilView(depthDsv_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT viewport {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    deviceContext_->RSSetViewports(1, &viewport);
    deviceContext_->IASetInputLayout(inputLayout_);
    deviceContext_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    deviceContext_->VSSetShader(vertexShader_, nullptr, 0);
    deviceContext_->PSSetShader(pixelShader_, nullptr, 0);
    deviceContext_->RSSetState(rasterizerState_);
    struct HybridParams {
        float flameDepthBias = 0.0f;
        float flameDepthSoftness = 0.0f;
        std::uint32_t useFlameOcclusion = 0u;
        float padding = 0.0f;
    } hybridParams;
    hybridParams.flameDepthBias = kHybridFlameDepthBias;
    hybridParams.flameDepthSoftness = kHybridFlameDepthSoftness;
    hybridParams.useFlameOcclusion = flameDepthSrv != nullptr ? 1u : 0u;

    D3D11_MAPPED_SUBRESOURCE mappedParams {};
    HRESULT result = deviceContext_->Map(hybridParamsBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedParams);
    if (FAILED(result)) {
        SetError("Map(HybridParams)", result);
        return false;
    }
    std::memcpy(mappedParams.pData, &hybridParams, sizeof(hybridParams));
    deviceContext_->Unmap(hybridParamsBuffer_, 0);
    deviceContext_->PSSetConstantBuffers(0, 1, &hybridParamsBuffer_);
    deviceContext_->PSSetShaderResources(0, 1, &flameDepthSrv);
    const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    deviceContext_->OMSetBlendState(blendState_, blendFactor, 0xFFFFFFFFu);

    const std::size_t totalVertexCount = gridVertices.size() + fillVertices.size() + overlayVertices.size() + pointVertices.size();
    if (totalVertexCount > 0) {
        if (!EnsureVertexBuffer(totalVertexCount)) {
            return false;
        }
        D3D11_MAPPED_SUBRESOURCE mapped {};
        HRESULT mapResult = deviceContext_->Map(vertexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(mapResult)) {
            SetError("Map(PathVertexBuffer)", mapResult);
            return false;
        }
        auto* dst = static_cast<Vertex*>(mapped.pData);
        std::size_t offset = 0;
        if (!gridVertices.empty()) {
            std::memcpy(dst + offset, gridVertices.data(), gridVertices.size() * sizeof(Vertex));
            offset += gridVertices.size();
        }
        if (!fillVertices.empty()) {
            std::memcpy(dst + offset, fillVertices.data(), fillVertices.size() * sizeof(Vertex));
            offset += fillVertices.size();
        }
        if (!overlayVertices.empty()) {
            std::memcpy(dst + offset, overlayVertices.data(), overlayVertices.size() * sizeof(Vertex));
            offset += overlayVertices.size();
        }
        if (!pointVertices.empty()) {
            std::memcpy(dst + offset, pointVertices.data(), pointVertices.size() * sizeof(Vertex));
        }
        deviceContext_->Unmap(vertexBuffer_, 0);

        const UINT stride = sizeof(Vertex);
        const UINT zero = 0;
        deviceContext_->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &zero);

        UINT drawOffset = 0;
        if (!gridVertices.empty()) {
            deviceContext_->OMSetDepthStencilState(depthDisabledState_, 0);
            deviceContext_->Draw(static_cast<UINT>(gridVertices.size()), drawOffset);
            drawOffset += static_cast<UINT>(gridVertices.size());
        }
        if (!fillVertices.empty()) {
            deviceContext_->OMSetDepthStencilState(depthWriteState_, 0);
            deviceContext_->Draw(static_cast<UINT>(fillVertices.size()), drawOffset);
            drawOffset += static_cast<UINT>(fillVertices.size());
        }
        if (!overlayVertices.empty()) {
            deviceContext_->OMSetDepthStencilState(fillVertices.empty() ? depthWriteState_ : depthReadState_, 0);
            deviceContext_->Draw(static_cast<UINT>(overlayVertices.size()), drawOffset);
            drawOffset += static_cast<UINT>(overlayVertices.size());
        }
        if (!pointVertices.empty()) {
            deviceContext_->OMSetDepthStencilState(fillVertices.empty() ? depthWriteState_ : depthReadState_, 0);
            deviceContext_->Draw(static_cast<UINT>(pointVertices.size()), drawOffset);
        }
    }

    ID3D11RenderTargetView* nullRtv = nullptr;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    deviceContext_->OMSetRenderTargets(1, &nullRtv, nullptr);
    deviceContext_->PSSetShaderResources(0, 1, &nullSrv);
    lastError_.clear();
    return true;
}

bool GpuPathRenderer::CreatePipeline() {
    std::string error;
    ID3DBlob* vertexBlob = CompileShader(kGpuPathShaderSource, "MainVS", "vs_5_0", error);
    if (vertexBlob == nullptr) {
        SetError(error.empty() ? "Failed to compile path vertex shader." : error);
        return false;
    }
    ID3DBlob* pixelBlob = CompileShader(kGpuPathShaderSource, "MainPS", "ps_5_0", error);
    if (pixelBlob == nullptr) {
        vertexBlob->Release();
        SetError(error.empty() ? "Failed to compile path pixel shader." : error);
        return false;
    }

    HRESULT result = device_->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &vertexShader_);
    if (FAILED(result)) {
        vertexBlob->Release();
        pixelBlob->Release();
        SetError("CreateVertexShader(GpuPath)", result);
        return false;
    }
    result = device_->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &pixelShader_);
    if (FAILED(result)) {
        vertexBlob->Release();
        pixelBlob->Release();
        SetError("CreatePixelShader(GpuPath)", result);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    result = device_->CreateInputLayout(inputElements, 2, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), &inputLayout_);
    vertexBlob->Release();
    pixelBlob->Release();
    if (FAILED(result)) {
        SetError("CreateInputLayout(GpuPath)", result);
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc {};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    result = device_->CreateRasterizerState(&rasterizerDesc, &rasterizerState_);
    if (FAILED(result)) {
        SetError("CreateRasterizerState(GpuPath)", result);
        return false;
    }

    D3D11_BLEND_DESC blendDesc {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    result = device_->CreateBlendState(&blendDesc, &blendState_);
    if (FAILED(result)) {
        SetError("CreateBlendState(GpuPath)", result);
        return false;
    }

    D3D11_BUFFER_DESC hybridParamsDesc {};
    hybridParamsDesc.ByteWidth = 16u;
    hybridParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
    hybridParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hybridParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = device_->CreateBuffer(&hybridParamsDesc, nullptr, &hybridParamsBuffer_);
    if (FAILED(result)) {
        SetError("CreateBuffer(HybridParams)", result);
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthDesc {};
    depthDesc.DepthEnable = FALSE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    result = device_->CreateDepthStencilState(&depthDesc, &depthDisabledState_);
    if (FAILED(result)) {
        SetError("CreateDepthStencilState(Grid)", result);
        return false;
    }

    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    result = device_->CreateDepthStencilState(&depthDesc, &depthWriteState_);
    if (FAILED(result)) {
        SetError("CreateDepthStencilState(Write)", result);
        return false;
    }

    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    result = device_->CreateDepthStencilState(&depthDesc, &depthReadState_);
    if (FAILED(result)) {
        SetError("CreateDepthStencilState(Read)", result);
        return false;
    }

    return true;
}

bool GpuPathRenderer::EnsureResources(const int width, const int height) {
    if (outputTexture_ != nullptr && outputWidth_ == width && outputHeight_ == height) {
        return true;
    }

    ReleaseResources();

    D3D11_TEXTURE2D_DESC outputDesc {};
    outputDesc.Width = static_cast<UINT>(width);
    outputDesc.Height = static_cast<UINT>(height);
    outputDesc.MipLevels = 1;
    outputDesc.ArraySize = 1;
    outputDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    outputDesc.SampleDesc.Count = 1;
    outputDesc.Usage = D3D11_USAGE_DEFAULT;
    outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT result = device_->CreateTexture2D(&outputDesc, nullptr, &outputTexture_);
    if (FAILED(result)) {
        SetError("CreateTexture2D(PathOutput)", result);
        return false;
    }
    result = device_->CreateRenderTargetView(outputTexture_, nullptr, &outputRtv_);
    if (FAILED(result)) {
        SetError("CreateRenderTargetView(PathOutput)", result);
        return false;
    }
    result = device_->CreateShaderResourceView(outputTexture_, nullptr, &outputSrv_);
    if (FAILED(result)) {
        SetError("CreateShaderResourceView(PathOutput)", result);
        return false;
    }

    D3D11_TEXTURE2D_DESC depthDesc {};
    depthDesc.Width = static_cast<UINT>(width);
    depthDesc.Height = static_cast<UINT>(height);
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    result = device_->CreateTexture2D(&depthDesc, nullptr, &depthTexture_);
    if (FAILED(result)) {
        SetError("CreateTexture2D(PathDepth)", result);
        return false;
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC depthDsvDesc {};
    depthDsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    depthDsvDesc.Texture2D.MipSlice = 0;
    result = device_->CreateDepthStencilView(depthTexture_, &depthDsvDesc, &depthDsv_);
    if (FAILED(result)) {
        SetError("CreateDepthStencilView(PathDepth)", result);
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc {};
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MostDetailedMip = 0;
    depthSrvDesc.Texture2D.MipLevels = 1;
    result = device_->CreateShaderResourceView(depthTexture_, &depthSrvDesc, &depthSrv_);
    if (FAILED(result)) {
        SetError("CreateShaderResourceView(PathDepth)", result);
        return false;
    }

    outputWidth_ = width;
    outputHeight_ = height;
    return true;
}

bool GpuPathRenderer::EnsureVertexBuffer(const std::size_t vertexCount) {
    if (vertexBuffer_ != nullptr && vertexCount <= vertexCapacity_) {
        return true;
    }

    if (vertexBuffer_) {
        vertexBuffer_->Release();
        vertexBuffer_ = nullptr;
        vertexCapacity_ = 0;
    }

    vertexCapacity_ = std::max<std::size_t>(vertexCount, 1024);
    D3D11_BUFFER_DESC bufferDesc {};
    bufferDesc.ByteWidth = static_cast<UINT>(vertexCapacity_ * sizeof(Vertex));
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const HRESULT result = device_->CreateBuffer(&bufferDesc, nullptr, &vertexBuffer_);
    if (FAILED(result)) {
        SetError("CreateBuffer(PathVertexBuffer)", result);
        return false;
    }
    return true;
}

bool GpuPathRenderer::DrawVertices(const Vertex* vertices, const std::size_t vertexCount, ID3D11DepthStencilState* depthState) {
    if (vertexCount == 0) {
        return true;
    }
    if (!EnsureVertexBuffer(vertexCount)) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped {};
    const HRESULT mapResult = deviceContext_->Map(vertexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        SetError("Map(PathVertexBuffer)", mapResult);
        return false;
    }
    std::memcpy(mapped.pData, vertices, vertexCount * sizeof(Vertex));
    deviceContext_->Unmap(vertexBuffer_, 0);

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    deviceContext_->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
    deviceContext_->OMSetDepthStencilState(depthState, 0);
    deviceContext_->Draw(static_cast<UINT>(vertexCount), 0);
    return true;
}

void GpuPathRenderer::ReleasePipeline() {
    if (vertexBuffer_) { vertexBuffer_->Release(); vertexBuffer_ = nullptr; }
    vertexCapacity_ = 0;
    if (hybridParamsBuffer_) { hybridParamsBuffer_->Release(); hybridParamsBuffer_ = nullptr; }
    if (depthReadState_) { depthReadState_->Release(); depthReadState_ = nullptr; }
    if (depthWriteState_) { depthWriteState_->Release(); depthWriteState_ = nullptr; }
    if (depthDisabledState_) { depthDisabledState_->Release(); depthDisabledState_ = nullptr; }
    if (blendState_) { blendState_->Release(); blendState_ = nullptr; }
    if (rasterizerState_) { rasterizerState_->Release(); rasterizerState_ = nullptr; }
    if (inputLayout_) { inputLayout_->Release(); inputLayout_ = nullptr; }
    if (pixelShader_) { pixelShader_->Release(); pixelShader_ = nullptr; }
    if (vertexShader_) { vertexShader_->Release(); vertexShader_ = nullptr; }
}

void GpuPathRenderer::ReleaseResources() {
    if (depthDsv_) { depthDsv_->Release(); depthDsv_ = nullptr; }
    if (depthSrv_) { depthSrv_->Release(); depthSrv_ = nullptr; }
    if (depthTexture_) { depthTexture_->Release(); depthTexture_ = nullptr; }
    if (outputSrv_) { outputSrv_->Release(); outputSrv_ = nullptr; }
    if (outputRtv_) { outputRtv_->Release(); outputRtv_ = nullptr; }
    if (outputTexture_) { outputTexture_->Release(); outputTexture_ = nullptr; }
    outputWidth_ = 0;
    outputHeight_ = 0;
}

void GpuPathRenderer::SetError(const char* stage, const HRESULT result) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08X)", stage, static_cast<unsigned int>(result));
    lastError_ = buffer;
}

ID3DBlob* GpuPathRenderer::CompileShader(const char* source, const char* entryPoint, const char* target, std::string& error) {
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    const UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT result = D3DCompile(
        source,
        std::strlen(source),
        nullptr,
        nullptr,
        nullptr,
        entryPoint,
        target,
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob);
    if (errorBlob != nullptr) {
        error.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
        errorBlob->Release();
    }
    if (FAILED(result)) {
        if (shaderBlob != nullptr) {
            shaderBlob->Release();
        }
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
