#include "engine/flame/IFSEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <random>
#include <thread>
#include <utility>

#include "engine/flame/Variation.h"
#include "renderer/RenderMath.h"

namespace radiary {

namespace {

constexpr double kFlameWorldScale = 0.63;
constexpr std::uint32_t kFlameBurnInIterations = 24u;
constexpr std::uint32_t kFlameRetryMultiplier = 4u;
constexpr double kFlameOrbitResetRadius = 1000000.0;
constexpr std::uint64_t kAbortCheckInterval = 256u;
constexpr std::size_t kMaxThreadLocalFlameBufferBytes = 256u * 1024u * 1024u;
constexpr std::array<std::pair<int, int>, 9> kAdaptiveSplatOffsets = {{
    {-1, -1}, {0, -1}, {1, -1},
    {-1, 0}, {0, 0}, {1, 0},
    {-1, 1}, {0, 1}, {1, 1}
}};

struct FlameThreadState {
    std::mt19937 generator {};
    Vec3 point {};
    double colorIndex = 0.5;
    std::uint32_t burnInRemaining = kFlameBurnInIterations;
};

Vec2 ApplyAffineTransform(const TransformLayer& layer, const Vec2& point) {
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

double LocalDensityKernel(const int dx, const int dy) {
    if (dx == 0 && dy == 0) {
        return 1.0;
    }
    return (std::abs(dx) + std::abs(dy) == 1) ? 0.58 : 0.32;
}

double EdgeFavoringScale(const double localDensity) {
    return Clamp(1.32 / std::pow(1.0 + std::max(0.0, localDensity), 0.38), 0.18, 1.32);
}

double LowDensitySplatBlend(const double localDensity) {
    return Clamp((1.45 - std::log1p(std::max(0.0, localDensity))) / 1.45, 0.0, 1.0) * 0.58;
}

double WideSplatKernel(const double distance) {
    constexpr double kSupportRadius = 1.35;
    if (distance >= kSupportRadius) {
        return 0.0;
    }
    return std::pow(1.0 - distance / kSupportRadius, 1.75);
}

Vec3 ResetOrbit(std::mt19937& generator, std::uniform_real_distribution<double>& start) {
    return Vec3 {start(generator), start(generator), start(generator) * 0.35};
}

Vec3 RotateSymmetryPoint(const Vec3& point, const double radians) {
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {
        point.x * cosine - point.y * sine,
        point.x * sine + point.y * cosine,
        point.z
    };
}

template <typename Callback>
void ForEachSymmetryPoint(const Vec3& point, const FlameRenderSettings& settings, const Callback& callback) {
    callback(point);

    const SymmetryMode symmetry = settings.symmetry;
    if (symmetry == SymmetryMode::None) {
        return;
    }

    const int order = std::clamp(settings.symmetryOrder, 2, 12);
    const bool includeBilateral =
        symmetry == SymmetryMode::Bilateral || symmetry == SymmetryMode::BilateralRotational;
    const bool includeRotational =
        symmetry == SymmetryMode::Rotational || symmetry == SymmetryMode::BilateralRotational;

    std::array<Vec3, 12> rotationalCopies {};
    std::size_t rotationalCount = 0;
    if (includeRotational) {
        for (int index = 0; index < order; ++index) {
            rotationalCopies[rotationalCount++] = RotateSymmetryPoint(
                point,
                (kPi * 2.0 * static_cast<double>(index)) / static_cast<double>(order));
        }
    } else {
        rotationalCopies[rotationalCount++] = point;
    }

    for (std::size_t index = 0; index < rotationalCount; ++index) {
        const Vec3 copy = rotationalCopies[index];
        if (includeRotational && index == 0) {
            continue;
        }
        callback(copy);
    }

    if (!includeBilateral) {
        return;
    }

    for (std::size_t index = 0; index < rotationalCount; ++index) {
        const Vec3 mirrored {-rotationalCopies[index].x, rotationalCopies[index].y, rotationalCopies[index].z};
        if (std::abs(mirrored.x - point.x) <= 1.0e-9
            && std::abs(mirrored.y - point.y) <= 1.0e-9
            && std::abs(mirrored.z - point.z) <= 1.0e-9) {
            continue;
        }
        callback(mirrored);
    }
}

void AccumulateProjectedSample(
    const render_math::CameraProjectionPoint& projected,
    const Color& sampleColor,
    const double farDepth,
    std::vector<FlamePixel>& output,
    const int width,
    const int height) {
    const double sampleWeight = Clamp(std::pow(projected.perspective / 30.0, 2.0), 0.35, 6.0);
    const double normalizedDepth = render_math::NormalizeDepth(projected.depth, farDepth);
    const int baseX = static_cast<int>(std::floor(projected.x));
    const int baseY = static_cast<int>(std::floor(projected.y));
    const double fracX = projected.x - static_cast<double>(baseX);
    const double fracY = projected.y - static_cast<double>(baseY);
    const std::array<double, 4> splatWeights = {
        (1.0 - fracX) * (1.0 - fracY),
        fracX * (1.0 - fracY),
        (1.0 - fracX) * fracY,
        fracX * fracY
    };
    const std::array<std::pair<int, int>, 4> splatOffsets = {
        std::pair<int, int>{0, 0},
        std::pair<int, int>{1, 0},
        std::pair<int, int>{0, 1},
        std::pair<int, int>{1, 1}
    };

    std::array<double, 4> sharpenedSplatWeights = splatWeights;
    double sharpenedWeightSum = 0.0;
    for (double& weight : sharpenedSplatWeights) {
        weight = std::pow(std::max(0.0, weight), 2.2);
        sharpenedWeightSum += weight;
    }
    if (sharpenedWeightSum > 1.0e-9) {
        for (double& weight : sharpenedSplatWeights) {
            weight /= sharpenedWeightSum;
        }
    } else {
        sharpenedSplatWeights = {1.0, 0.0, 0.0, 0.0};
    }

    const int centerX = static_cast<int>(std::floor(projected.x + 0.5));
    const int centerY = static_cast<int>(std::floor(projected.y + 0.5));
    double localDensityEstimate = 0.0;
    double localDensityKernelSum = 0.0;
    for (const auto& offset : kAdaptiveSplatOffsets) {
        const int px = centerX + offset.first;
        const int py = centerY + offset.second;
        if (px < 0 || py < 0 || px >= width || py >= height) {
            continue;
        }

        const double kernel = LocalDensityKernel(offset.first, offset.second);
        localDensityEstimate += output[static_cast<std::size_t>(py * width + px)].density * kernel;
        localDensityKernelSum += kernel;
    }
    if (localDensityKernelSum > 1.0e-9) {
        localDensityEstimate /= localDensityKernelSum;
    }

    std::array<double, 9> wideSplatWeights {};
    double wideWeightSum = 0.0;
    for (std::size_t index = 0; index < kAdaptiveSplatOffsets.size(); ++index) {
        const int px = centerX + kAdaptiveSplatOffsets[index].first;
        const int py = centerY + kAdaptiveSplatOffsets[index].second;
        const double pixelCenterX = static_cast<double>(px) + 0.5;
        const double pixelCenterY = static_cast<double>(py) + 0.5;
        const double wx = WideSplatKernel(std::abs(projected.x - pixelCenterX));
        const double wy = WideSplatKernel(std::abs(projected.y - pixelCenterY));
        wideSplatWeights[index] = wx * wy;
        wideWeightSum += wideSplatWeights[index];
    }
    if (wideWeightSum > 1.0e-9) {
        for (double& weight : wideSplatWeights) {
            weight /= wideWeightSum;
        }
    } else {
        wideSplatWeights[4] = 1.0;
    }

    std::array<double, 9> narrowSplatWeights {};
    for (std::size_t splatIndex = 0; splatIndex < splatWeights.size(); ++splatIndex) {
        const int px = baseX + splatOffsets[splatIndex].first;
        const int py = baseY + splatOffsets[splatIndex].second;
        const int gridX = px - (centerX - 1);
        const int gridY = py - (centerY - 1);
        if (gridX < 0 || gridX >= 3 || gridY < 0 || gridY >= 3) {
            continue;
        }
        narrowSplatWeights[static_cast<std::size_t>(gridY * 3 + gridX)] += sharpenedSplatWeights[splatIndex];
    }

    const double wideBlend = LowDensitySplatBlend(localDensityEstimate);
    std::array<double, 9> finalSplatWeights {};
    double finalWeightSum = 0.0;
    for (std::size_t index = 0; index < finalSplatWeights.size(); ++index) {
        finalSplatWeights[index] = narrowSplatWeights[index] * (1.0 - wideBlend) + wideSplatWeights[index] * wideBlend;
        finalWeightSum += finalSplatWeights[index];
    }
    if (finalWeightSum > 1.0e-9) {
        for (double& weight : finalSplatWeights) {
            weight /= finalWeightSum;
        }
    } else {
        finalSplatWeights[4] = 1.0;
    }

    const double adjustedSampleWeight = sampleWeight * EdgeFavoringScale(localDensityEstimate);
    double visibleWeightSum = 0.0;
    for (std::size_t index = 0; index < finalSplatWeights.size(); ++index) {
        const int px = centerX + kAdaptiveSplatOffsets[index].first;
        const int py = centerY + kAdaptiveSplatOffsets[index].second;
        if (px < 0 || py < 0 || px >= width || py >= height) {
            continue;
        }
        visibleWeightSum += finalSplatWeights[index];
    }
    if (visibleWeightSum <= 1.0e-9) {
        return;
    }

    for (std::size_t index = 0; index < finalSplatWeights.size(); ++index) {
        const int px = centerX + kAdaptiveSplatOffsets[index].first;
        const int py = centerY + kAdaptiveSplatOffsets[index].second;
        if (px < 0 || py < 0 || px >= width || py >= height) {
            continue;
        }

        const double weight = adjustedSampleWeight * (finalSplatWeights[index] / visibleWeightSum);
        if (weight <= 1.0e-6) {
            continue;
        }

        FlamePixel& pixel = output[static_cast<std::size_t>(py * width + px)];
        pixel.density += static_cast<float>(weight);
        pixel.red += static_cast<float>(sampleColor.r * weight);
        pixel.green += static_cast<float>(sampleColor.g * weight);
        pixel.blue += static_cast<float>(sampleColor.b * weight);
        pixel.depth += static_cast<float>(normalizedDepth * weight);
    }
}

bool RenderFlameIterations(
    const Scene& scene,
    const int width,
    const int height,
    std::vector<FlamePixel>& output,
    const std::uint32_t requestedIterations,
    FlameThreadState& state,
    const std::vector<double>& weights,
    const std::vector<Color>& palette,
    const std::function<bool()>& shouldAbort,
    const bool checkAbortOnThisThread,
    std::atomic<bool>* abortRequested) {
    if (requestedIterations == 0u) {
        return true;
    }

    std::discrete_distribution<std::size_t> chooseTransform(weights.begin(), weights.end());
    std::uniform_real_distribution<double> start(-1.0, 1.0);
    const double farDepth = render_math::ComputeFarDepth(scene.camera.distance);
    const std::uint64_t maxAttempts = static_cast<std::uint64_t>(requestedIterations + kFlameBurnInIterations)
        * static_cast<std::uint64_t>(kFlameRetryMultiplier);
    std::uint32_t stableIterations = 0;

    for (std::uint64_t attempt = 0; attempt < maxAttempts && stableIterations < requestedIterations; ++attempt) {
        if (abortRequested != nullptr && abortRequested->load(std::memory_order_relaxed)) {
            return false;
        }
        if (checkAbortOnThisThread
            && shouldAbort
            && (attempt % kAbortCheckInterval) == 0u
            && shouldAbort()) {
            if (abortRequested != nullptr) {
                abortRequested->store(true, std::memory_order_relaxed);
            }
            return false;
        }

        const TransformLayer& layer = scene.transforms[chooseTransform(state.generator)];
        const Vec2 affine = ApplyAffineTransform(layer, {state.point.x, state.point.y});
        if (!IsFinitePoint(affine)) {
            state.point = ResetOrbit(state.generator, start);
            state.colorIndex = 0.5;
            state.burnInRemaining = kFlameBurnInIterations;
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
            state.point = ResetOrbit(state.generator, start);
            state.colorIndex = 0.5;
            state.burnInRemaining = kFlameBurnInIterations;
            continue;
        }
        if (totalVariationWeight <= 1.0e-9) {
            varied = affine;
        } else {
            varied = varied * (1.0 / totalVariationWeight);
        }
        if (!IsFinitePoint(varied)) {
            state.point = ResetOrbit(state.generator, start);
            state.colorIndex = 0.5;
            state.burnInRemaining = kFlameBurnInIterations;
            continue;
        }

        const double radius = std::sqrt(varied.x * varied.x + varied.y * varied.y);
        const double angle = std::atan2(varied.y, varied.x);
        const double rotation = DegreesToRadians(layer.rotationDegrees);
        const double depthDrive =
            std::sin(state.point.z * 1.35 + angle * (1.8 + std::abs(layer.shearX) * 0.35) + rotation * 0.7) * (0.24 + radius * 0.18)
            + std::cos((varied.x * 1.9 - varied.y * 1.6) * (1.0 + std::abs(layer.shearY) * 0.25) + state.point.z * 0.9) * (0.12 + radius * 0.08)
            + (layer.colorIndex - 0.5) * 0.28
            + (layer.translateX - layer.translateY) * 0.05;
        const double nextDepth = state.point.z * 0.74 + depthDrive;
        const double swirlAngle = nextDepth * (0.52 + radius * 0.18) + rotation * 0.22;
        const double swirlCos = std::cos(swirlAngle);
        const double swirlSin = std::sin(swirlAngle);
        state.point = {
            varied.x * swirlCos - varied.y * swirlSin,
            varied.x * swirlSin + varied.y * swirlCos,
            nextDepth
        };
        if (!IsFinitePoint(state.point) || IsOrbitOutOfRange(state.point)) {
            state.point = ResetOrbit(state.generator, start);
            state.colorIndex = 0.5;
            state.burnInRemaining = kFlameBurnInIterations;
            continue;
        }
        state.colorIndex = Lerp(state.colorIndex, layer.colorIndex, 0.16);

        if (state.burnInRemaining > 0) {
            --state.burnInRemaining;
            continue;
        }
        ++stableIterations;

        const Color sampleColor = layer.useCustomColor
            ? layer.customColor
            : palette[static_cast<std::size_t>(Clamp(state.colorIndex, 0.0, 1.0) * 255.0)];
        ForEachSymmetryPoint(state.point, scene.flameRender, [&](const Vec3& symmetryPoint) {
            const render_math::CameraProjectionPoint projected = render_math::ProjectCameraPoint(
                FlamePointToWorld(symmetryPoint, kFlameWorldScale, scene.flameRender),
                scene.camera,
                width,
                height);
            if (!projected.visible) {
                return;
            }
            AccumulateProjectedSample(projected, sampleColor, farDepth, output, width, height);
        });
    }

    return abortRequested == nullptr || !abortRequested->load(std::memory_order_relaxed);
}

}  // namespace

Vec2 IFSEngine::ApplyAffine(const TransformLayer& layer, const Vec2& point) {
    return ApplyAffineTransform(layer, point);
}

void IFSEngine::ResetTemporalState() {
    temporalGenerator_ = std::mt19937 {};
    temporalPoint_ = {};
    temporalColorIndex_ = 0.5;
    temporalBurnInRemaining_ = 0;
    temporalTransformCount_ = 0;
    temporalStateValid_ = false;
}

bool IFSEngine::Render(
    const Scene& scene,
    const int width,
    const int height,
    std::vector<FlamePixel>& output,
    const std::function<bool()>& shouldAbort,
    const bool preserveTemporalState) {
    output.assign(static_cast<std::size_t>(width * height), {});
    if (scene.transforms.empty() || width <= 0 || height <= 0) {
        return true;
    }

    std::vector<double> weights;
    weights.reserve(scene.transforms.size());
    for (const TransformLayer& layer : scene.transforms) {
        weights.push_back(std::max(0.01, layer.weight));
    }
    const std::vector<Color> palette = BuildGradientPalette(scene.gradientStops, 256);
    const std::uint32_t baseSeed = 0xC0FFEEu + static_cast<unsigned>(scene.transforms.size() * 17);
    if (preserveTemporalState) {
        FlameThreadState state;
        const bool reuseTemporalState =
            temporalStateValid_
            && temporalTransformCount_ == scene.transforms.size();
        if (reuseTemporalState) {
            state.generator = temporalGenerator_;
            state.point = temporalPoint_;
            state.colorIndex = temporalColorIndex_;
            state.burnInRemaining = temporalBurnInRemaining_;
        } else {
            state.generator.seed(baseSeed);
            std::uniform_real_distribution<double> start(-1.0, 1.0);
            state.point = ResetOrbit(state.generator, start);
            state.colorIndex = 0.5;
            state.burnInRemaining = kFlameBurnInIterations;
        }

        if (!RenderFlameIterations(
                scene,
                width,
                height,
                output,
                scene.previewIterations,
                state,
                weights,
                palette,
                shouldAbort,
                true,
                nullptr)) {
            return false;
        }

        temporalGenerator_ = state.generator;
        temporalPoint_ = state.point;
        temporalColorIndex_ = state.colorIndex;
        temporalBurnInRemaining_ = state.burnInRemaining;
        temporalTransformCount_ = scene.transforms.size();
        temporalStateValid_ = true;
        return true;
    }

    const std::size_t pixelCount = output.size();
    const std::size_t bytesPerBuffer = pixelCount * sizeof(FlamePixel);
    unsigned threadCount = std::max(1u, std::thread::hardware_concurrency());
    if (scene.previewIterations > 0u) {
        threadCount = std::min(threadCount, scene.previewIterations);
    }
    if (bytesPerBuffer > 0u) {
        const std::size_t maxThreadCountByMemory = std::max<std::size_t>(1u, kMaxThreadLocalFlameBufferBytes / bytesPerBuffer);
        threadCount = static_cast<unsigned>(std::min<std::size_t>(threadCount, maxThreadCountByMemory));
    }

    if (threadCount <= 1u) {
        FlameThreadState state;
        state.generator.seed(baseSeed);
        std::uniform_real_distribution<double> start(-1.0, 1.0);
        state.point = ResetOrbit(state.generator, start);
        return RenderFlameIterations(
            scene,
            width,
            height,
            output,
            scene.previewIterations,
            state,
            weights,
            palette,
            shouldAbort,
            true,
            nullptr);
    }

    std::atomic<bool> abortRequested = false;
    std::vector<std::vector<FlamePixel>> threadBuffers(
        threadCount,
        std::vector<FlamePixel>(pixelCount));
    std::vector<std::uint8_t> threadSucceeded(threadCount, 0u);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    const std::uint32_t iterationsPerThread = scene.previewIterations / threadCount;
    const std::uint32_t remainderIterations = scene.previewIterations % threadCount;
    for (unsigned threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
        const std::uint32_t threadIterations = iterationsPerThread + (threadIndex < remainderIterations ? 1u : 0u);
        threads.emplace_back([&, threadIndex, threadIterations]() {
            FlameThreadState state;
            state.generator.seed(baseSeed + threadIndex);
            std::uniform_real_distribution<double> start(-1.0, 1.0);
            state.point = ResetOrbit(state.generator, start);
            state.colorIndex = 0.5;
            state.burnInRemaining = kFlameBurnInIterations;
            threadSucceeded[threadIndex] = RenderFlameIterations(
                scene,
                width,
                height,
                threadBuffers[threadIndex],
                threadIterations,
                state,
                weights,
                palette,
                shouldAbort,
                threadIndex == 0u,
                &abortRequested);
        });
    }

    for (std::thread& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    if (abortRequested.load(std::memory_order_relaxed)
        || std::any_of(threadSucceeded.begin(), threadSucceeded.end(), [](const std::uint8_t succeeded) { return succeeded == 0u; })) {
        return false;
    }

    for (unsigned threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
        const std::vector<FlamePixel>& threadOutput = threadBuffers[threadIndex];
        for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
            output[pixelIndex].density += threadOutput[pixelIndex].density;
            output[pixelIndex].red += threadOutput[pixelIndex].red;
            output[pixelIndex].green += threadOutput[pixelIndex].green;
            output[pixelIndex].blue += threadOutput[pixelIndex].blue;
            output[pixelIndex].depth += threadOutput[pixelIndex].depth;
        }
    }

    return true;
}

}  // namespace radiary
