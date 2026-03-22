#include "renderer/PathDrawListBuilder.h"

#include <algorithm>
#include <cmath>

#include "renderer/RenderMath.h"
#include "renderer/SoftwareRenderer.h"

namespace radiary {

namespace {

float ToClipX(const float x, const int width) {
    return (x / std::max(1.0f, static_cast<float>(width))) * 2.0f - 1.0f;
}

float ToClipY(const float y, const int height) {
    return 1.0f - (y / std::max(1.0f, static_cast<float>(height))) * 2.0f;
}

PathDrawVertex MakeVertex(const float x, const float y, const float z, const Color& color, const double alpha, const int width, const int height) {
    PathDrawVertex vertex {};
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
    std::vector<PathDrawVertex>& vertices,
    const SoftwareRenderer::ProjectedPoint& a,
    const SoftwareRenderer::ProjectedPoint& b,
    const SoftwareRenderer::ProjectedPoint& c,
    const Color& color,
    const double alpha,
    const int width,
    const int height,
    const double farDepth) {
    vertices.push_back(MakeVertex(static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(render_math::NormalizeDepth(a.depth, farDepth)), color, alpha, width, height));
    vertices.push_back(MakeVertex(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(render_math::NormalizeDepth(b.depth, farDepth)), color, alpha, width, height));
    vertices.push_back(MakeVertex(static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(render_math::NormalizeDepth(c.depth, farDepth)), color, alpha, width, height));
}

void EmitLineQuad(
    std::vector<PathDrawVertex>& vertices,
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
    const float z = static_cast<float>(render_math::NormalizeDepth(averageDepth, farDepth));

    const PathDrawVertex a = MakeVertex(x0 - nx, y0 - ny, z, color, alpha, width, height);
    const PathDrawVertex b = MakeVertex(x0 + nx, y0 + ny, z, color, alpha, width, height);
    const PathDrawVertex c = MakeVertex(x1 + nx, y1 + ny, z, color, alpha, width, height);
    const PathDrawVertex d = MakeVertex(x1 - nx, y1 - ny, z, color, alpha, width, height);
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
    vertices.push_back(a);
    vertices.push_back(c);
    vertices.push_back(d);
}

void EmitPointQuad(
    std::vector<PathDrawVertex>& vertices,
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
    const float z = static_cast<float>(render_math::NormalizeDepth(point.depth, farDepth));

    const PathDrawVertex a = MakeVertex(x - halfSize, y - halfSize, z, color, alpha, width, height);
    const PathDrawVertex b = MakeVertex(x + halfSize, y - halfSize, z, color, alpha, width, height);
    const PathDrawVertex c = MakeVertex(x + halfSize, y + halfSize, z, color, alpha, width, height);
    const PathDrawVertex d = MakeVertex(x - halfSize, y + halfSize, z, color, alpha, width, height);
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
    vertices.push_back(a);
    vertices.push_back(c);
    vertices.push_back(d);
}

void AppendGrid(
    std::vector<PathDrawVertex>& vertices,
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

std::size_t PathDrawList::TotalVertexCount() const {
    return gridVertices.size() + fillVertices.size() + overlayVertices.size() + pointVertices.size();
}

PathDrawList PathDrawListBuilder::Build(
    const Scene& scene,
    const int width,
    const int height,
    const bool renderGrid) {
    PathDrawList drawList;

    std::vector<SoftwareRenderer::PathTriangle> fillTriangles;
    std::vector<SoftwareRenderer::PathLine> lines;
    std::vector<SoftwareRenderer::PathPointSprite> points;
    SoftwareRenderer::BuildPathPrimitives(scene, width, height, fillTriangles, lines, points);

    const double farDepth = render_math::ComputeFarDepth(scene.camera.distance);

    if (renderGrid && scene.gridVisible) {
        AppendGrid(drawList.gridVertices, scene, width, height);
    }

    drawList.fillVertices.reserve(fillTriangles.size() * 3);
    for (const auto& triangle : fillTriangles) {
        EmitTriangle(
            drawList.fillVertices,
            triangle.points[0],
            triangle.points[1],
            triangle.points[2],
            triangle.color,
            triangle.alpha,
            width,
            height,
            farDepth);
    }

    drawList.overlayVertices.reserve(lines.size() * 6);
    for (const auto& line : lines) {
        EmitLineQuad(
            drawList.overlayVertices,
            line.start,
            line.end,
            line.color,
            line.alpha,
            line.thickness,
            width,
            height,
            farDepth);
    }

    drawList.pointVertices.reserve(points.size() * 6);
    for (const auto& point : points) {
        EmitPointQuad(
            drawList.pointVertices,
            point.point,
            point.color,
            point.alpha,
            point.size,
            width,
            height,
            farDepth);
    }

    return drawList;
}

}  // namespace radiary
