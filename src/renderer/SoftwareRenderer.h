#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/Math.h"
#include "core/Scene.h"
#include "engine/flame/IFSEngine.h"
#include "engine/path/SplinePath.h"

namespace radiary {

class SoftwareRenderer {
public:
    struct RenderOptions {
        bool renderFlame = true;
        bool renderPaths = true;
        bool renderGrid = true;
        bool transparentBackground = false;
        bool interactive = false;
        std::function<bool()> shouldAbort;
    };

    struct ProjectedPoint {
        double x = 0.0;
        double y = 0.0;
        double depth = 0.0;
        bool visible = false;
    };

    struct PathTriangle {
        std::array<ProjectedPoint, 3> points {};
        Color color {};
        double alpha = 1.0;
        double depth = 0.0;
    };

    struct PathLine {
        ProjectedPoint start {};
        ProjectedPoint end {};
        Color color {};
        double alpha = 1.0;
        double thickness = 1.0;
        double depth = 0.0;
    };

    struct PathPointSprite {
        ProjectedPoint point {};
        Color color {};
        double alpha = 1.0;
        double size = 1.0;
        double depth = 0.0;
    };

    bool RenderViewport(const Scene& scene, int width, int height, std::vector<std::uint32_t>& pixels, const RenderOptions& options = {});
    void InvalidateAccumulation();

    static void Clear(std::vector<std::uint32_t>& pixels, int width, int height, const Color& color);
    static void Plot(std::vector<std::uint32_t>& pixels, int width, int height, int x, int y, const Color& color, double alpha);
    static void DrawLine(std::vector<std::uint32_t>& pixels, int width, int height, int x0, int y0, int x1, int y1, const Color& color, double alpha);
    static void DrawLineAA(std::vector<std::uint32_t>& pixels, int width, int height, double x0, double y0, double x1, double y1, const Color& color, double alpha, double thickness);
    static void BuildPathPrimitives(
        const Scene& scene,
        int width,
        int height,
        std::vector<PathTriangle>& fillTriangles,
        std::vector<PathLine>& lines,
        std::vector<PathPointSprite>& points);
    static void BuildDepthMap(const Scene& scene, int width, int height, std::vector<float>& depthBuffer);
    static void ApplyDepthOfField(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>& depthBuffer);
    static void ApplyDenoising(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>& depthBuffer, const std::function<bool()>& shouldAbort);
    static ProjectedPoint Project(const Vec3& point, const CameraState& camera, int width, int height);
    static double NormalizeProjectedDepth(double depth, const CameraState& camera);
    static Color ToneMap(const FlamePixel& pixel, const FlameRenderSettings& flameRender);
    static Color SurfaceColor(double factor);

private:
    IFSEngine flameEngine_;
    SplinePath splinePath_;
};

}  // namespace radiary
