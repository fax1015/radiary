#include "engine/flame/IFSEngine.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "engine/flame/Variation.h"

namespace radiary {

namespace {

constexpr double kFlameWorldScale = 0.63;
constexpr std::uint32_t kFlameBurnInIterations = 24u;
constexpr std::uint32_t kFlameRetryMultiplier = 4u;
constexpr double kFlameOrbitResetRadius = 1000000.0;
constexpr std::uint64_t kAbortCheckInterval = 256u;

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
    if (!std::isfinite(rotated.x) || !std::isfinite(rotated.y) || !std::isfinite(rotated.z)) {
        return {};
    }

    rotated.z += camera.distance;
    if (rotated.z <= 0.15) {
        return {};
    }

    const double perspective = 240.0 * camera.zoom2D / rotated.z;
    if (!std::isfinite(perspective)) {
        return {};
    }
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

bool IsFinitePoint(const Vec2& point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool IsFinitePoint(const Vec3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool IsOrbitOutOfRange(const Vec2& point) {
    return std::abs(point.x) > kFlameOrbitResetRadius || std::abs(point.y) > kFlameOrbitResetRadius;
}

bool IsOrbitOutOfRange(const Vec3& point) {
    return std::abs(point.x) > kFlameOrbitResetRadius || std::abs(point.y) > kFlameOrbitResetRadius || std::abs(point.z) > kFlameOrbitResetRadius;
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

bool IFSEngine::Render(const Scene& scene, const int width, const int height, std::vector<FlamePixel>& output, const std::function<bool()>& shouldAbort) {
    output.assign(static_cast<std::size_t>(width * height), {});
    if (scene.transforms.empty() || width <= 0 || height <= 0) {
        return true;
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
    const auto resetOrbit = [&]() {
        return Vec3 {start(generator), start(generator), start(generator) * 0.35};
    };
    Vec3 point = resetOrbit();
    double colorIndex = 0.5;
    // Flame points live in the same camera space as path geometry.
    // zoom2D belongs in projection, not in flame-local world scaling.
    const double flameWorldScale = kFlameWorldScale;
    const double farDepth = std::max(1.15, scene.camera.distance + 24.0);
    std::uint32_t burnInRemaining = kFlameBurnInIterations;
    std::uint32_t stableIterations = 0;
    const std::uint64_t maxAttempts = static_cast<std::uint64_t>(scene.previewIterations + kFlameBurnInIterations)
        * static_cast<std::uint64_t>(kFlameRetryMultiplier);

    for (std::uint64_t attempt = 0; attempt < maxAttempts && stableIterations < scene.previewIterations; ++attempt) {
        if (shouldAbort && (attempt % kAbortCheckInterval) == 0u && shouldAbort()) {
            return false;
        }
        const TransformLayer& layer = scene.transforms[chooseTransform(generator)];
        const Vec2 affine = ApplyAffine(layer, {point.x, point.y});
        if (!IsFinitePoint(affine)) {
            point = resetOrbit();
            colorIndex = 0.5;
            burnInRemaining = kFlameBurnInIterations;
            continue;
        }

        Vec2 varied {};
        double totalVariationWeight = 0.0;
        bool unstableOrbit = false;
        for (std::size_t index = 0; index < kVariationCount; ++index) {
            const double amount = layer.variations[index];
            if (amount == 0.0) {
                continue;
            }
            const Vec2 variation = ApplyVariation(static_cast<VariationType>(index), affine);
            if (!IsFinitePoint(variation)) {
                unstableOrbit = true;
                break;
            }
            totalVariationWeight += amount;
            varied = varied + variation * amount;
        }
        if (unstableOrbit) {
            point = resetOrbit();
            colorIndex = 0.5;
            burnInRemaining = kFlameBurnInIterations;
            continue;
        }
        if (totalVariationWeight <= 1.0e-9) {
            varied = affine;
        } else {
            varied = varied * (1.0 / totalVariationWeight);
        }
        if (!IsFinitePoint(varied)) {
            point = resetOrbit();
            colorIndex = 0.5;
            burnInRemaining = kFlameBurnInIterations;
            continue;
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
        if (!IsFinitePoint(point) || IsOrbitOutOfRange(point)) {
            point = resetOrbit();
            colorIndex = 0.5;
            burnInRemaining = kFlameBurnInIterations;
            continue;
        }
        colorIndex = Lerp(colorIndex, layer.colorIndex, 0.16);

        if (burnInRemaining > 0) {
            --burnInRemaining;
            continue;
        }
        ++stableIterations;

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

    return true;
}

}  // namespace radiary
