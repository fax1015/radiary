#include "engine/flame/IFSEngine.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "engine/flame/Variation.h"

namespace radiary {

namespace {

constexpr double kFlameWorldScale = 0.63;

struct ProjectedFlamePoint {
    int x = 0;
    int y = 0;
    double perspective = 0.0;
    double depth = 0.0;
    bool visible = false;
};

ProjectedFlamePoint ProjectFlamePoint(const Vec3& point, const CameraState& camera, const int width, const int height) {
    const double yawCos = std::cos(camera.yaw);
    const double yawSin = std::sin(camera.yaw);
    const double pitchCos = std::cos(camera.pitch);
    const double pitchSin = std::sin(camera.pitch);

    Vec3 rotated {
        point.x * yawCos + point.z * yawSin,
        point.y,
        -point.x * yawSin + point.z * yawCos
    };

    rotated = {
        rotated.x,
        rotated.y * pitchCos - rotated.z * pitchSin,
        rotated.y * pitchSin + rotated.z * pitchCos
    };

    rotated.z += camera.distance;
    if (rotated.z <= 0.15) {
        return {};
    }

    const double perspective = 240.0 * camera.zoom2D / rotated.z;
    return {
        static_cast<int>(std::round(width * 0.5 + camera.panX + rotated.x * perspective)),
        static_cast<int>(std::round(height * 0.5 + camera.panY - rotated.y * perspective)),
        perspective,
        rotated.z,
        true
    };
}

Vec3 RotateFlameObjectPoint(const Vec3& point, const FlameRenderSettings& settings) {
    const double rx = DegreesToRadians(settings.rotationXDegrees);
    const double ry = DegreesToRadians(settings.rotationYDegrees);
    const double rz = DegreesToRadians(settings.rotationZDegrees);
    const double cosX = std::cos(rx);
    const double sinX = std::sin(rx);
    const double cosY = std::cos(ry);
    const double sinY = std::sin(ry);
    const double cosZ = std::cos(rz);
    const double sinZ = std::sin(rz);

    Vec3 rotated = point;
    rotated = {
        rotated.x,
        rotated.y * cosX - rotated.z * sinX,
        rotated.y * sinX + rotated.z * cosX
    };
    rotated = {
        rotated.x * cosY + rotated.z * sinY,
        rotated.y,
        -rotated.x * sinY + rotated.z * cosY
    };
    rotated = {
        rotated.x * cosZ - rotated.y * sinZ,
        rotated.x * sinZ + rotated.y * cosZ,
        rotated.z
    };
    return rotated;
}

Vec3 FlamePointToWorld(const Vec3& point, const double scale, const FlameRenderSettings& settings) {
    const double radius = std::sqrt(point.x * point.x + point.y * point.y);
    const double angle = std::atan2(point.y, point.x);
    const double depth = point.z * scale * 0.72 * std::max(0.0, settings.depthAmount);
    const double lateralOffset = depth * (0.18 + radius * 0.06);
    const Vec3 base {
        point.x * scale + std::cos(angle + point.z * 0.3) * lateralOffset,
        point.y * scale + std::sin(angle - point.z * 0.25) * lateralOffset * 0.82,
        depth
    };

    return RotateFlameObjectPoint(base, settings);
}

}  // namespace

Vec2 IFSEngine::ApplyAffine(const TransformLayer& layer, const Vec2& point) {
    const double radians = DegreesToRadians(layer.rotationDegrees);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);

    Vec2 scaled {point.x * layer.scaleX, point.y * layer.scaleY};
    Vec2 sheared {
        scaled.x + layer.shearX * scaled.y,
        scaled.y + layer.shearY * scaled.x
    };

    return {
        sheared.x * cosine - sheared.y * sine + layer.translateX,
        sheared.x * sine + sheared.y * cosine + layer.translateY
    };
}

void IFSEngine::Render(const Scene& scene, const int width, const int height, std::vector<FlamePixel>& output) {
    output.assign(static_cast<std::size_t>(width * height), {});
    if (scene.transforms.empty() || width <= 0 || height <= 0) {
        return;
    }

    std::vector<double> weights;
    weights.reserve(scene.transforms.size());
    for (const TransformLayer& layer : scene.transforms) {
        weights.push_back(std::max(0.01, layer.weight));
    }

    std::mt19937 generator(0xC0FFEEu + static_cast<unsigned>(scene.transforms.size() * 17));
    std::discrete_distribution<std::size_t> chooseTransform(weights.begin(), weights.end());
    std::uniform_real_distribution<double> start(-1.0, 1.0);

    const std::vector<Color> palette = BuildGradientPalette(scene.gradientStops, 256);
    Vec3 point {start(generator), start(generator), start(generator) * 0.35};
    double colorIndex = 0.5;
    // Flame points live in the same camera space as path geometry.
    // zoom2D belongs in projection, not in flame-local world scaling.
    const double flameWorldScale = kFlameWorldScale;
    const double farDepth = std::max(1.15, scene.camera.distance + 24.0);

    for (std::uint32_t iteration = 0; iteration < scene.previewIterations + 32U; ++iteration) {
        const TransformLayer& layer = scene.transforms[chooseTransform(generator)];
        const Vec2 affine = ApplyAffine(layer, {point.x, point.y});

        Vec2 varied {};
        double totalVariationWeight = 0.0;
        for (std::size_t index = 0; index < kVariationCount; ++index) {
            const double amount = layer.variations[index];
            if (amount == 0.0) {
                continue;
            }
            totalVariationWeight += amount;
            varied = varied + ApplyVariation(static_cast<VariationType>(index), affine) * amount;
        }
        if (totalVariationWeight <= 1.0e-9) {
            varied = affine;
        } else {
            varied = varied * (1.0 / totalVariationWeight);
        }

        const double radius = std::sqrt(varied.x * varied.x + varied.y * varied.y);
        const double angle = std::atan2(varied.y, varied.x);
        const double rotation = DegreesToRadians(layer.rotationDegrees);
        const double depthDrive =
            std::sin(point.z * 1.35 + angle * (1.8 + std::abs(layer.shearX) * 0.35) + rotation * 0.7) * (0.24 + radius * 0.18)
            + std::cos((varied.x * 1.9 - varied.y * 1.6) * (1.0 + std::abs(layer.shearY) * 0.25) + point.z * 0.9) * (0.12 + radius * 0.08)
            + (layer.colorIndex - 0.5) * 0.28
            + (layer.translateX - layer.translateY) * 0.05;
        const double nextDepth = point.z * 0.74 + depthDrive;
        const double swirlAngle = nextDepth * (0.52 + radius * 0.18) + rotation * 0.22;
        const double swirlCos = std::cos(swirlAngle);
        const double swirlSin = std::sin(swirlAngle);
        point = {
            varied.x * swirlCos - varied.y * swirlSin,
            varied.x * swirlSin + varied.y * swirlCos,
            nextDepth
        };
        colorIndex = Lerp(colorIndex, layer.colorIndex, 0.16);

        if (iteration < 24U) {
            continue;
        }

        const ProjectedFlamePoint projected = ProjectFlamePoint(FlamePointToWorld(point, flameWorldScale, scene.flameRender), scene.camera, width, height);
        if (!projected.visible || projected.x < 0 || projected.y < 0 || projected.x >= width || projected.y >= height) {
            continue;
        }

        FlamePixel& pixel = output[static_cast<std::size_t>(projected.y * width + projected.x)];
        const Color sampleColor = layer.useCustomColor
            ? layer.customColor
            : palette[static_cast<std::size_t>(Clamp(colorIndex, 0.0, 1.0) * 255.0)];
        const double sampleWeight = Clamp(std::pow(projected.perspective / 30.0, 2.0), 0.35, 6.0);
        pixel.density += static_cast<float>(sampleWeight);
        pixel.red += static_cast<float>(sampleColor.r * sampleWeight);
        pixel.green += static_cast<float>(sampleColor.g * sampleWeight);
        pixel.blue += static_cast<float>(sampleColor.b * sampleWeight);
        pixel.depth += static_cast<float>(Clamp((projected.depth - 0.15) / std::max(1.0, farDepth - 0.15), 0.0, 1.0) * sampleWeight);
    }
}

}  // namespace radiary
