#include "renderer/Effect.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <atomic>

#include "renderer/RenderMath.h"

namespace radiary {

using namespace radiary::render_math;

namespace {

// =============================================================================
// Pixel Conversion Helpers
// =============================================================================
// All effects work in linear float RGB [0,1] via Vec3{R, G, B}.
// The pixel buffer stores BGRA uint32 (B in low bits, A in high bits).

Vec3 PixelToRgb(const std::uint32_t pixel) {
    return {
        static_cast<double>((pixel >> 16U) & 0xFFU) / 255.0,
        static_cast<double>((pixel >> 8U) & 0xFFU) / 255.0,
        static_cast<double>(pixel & 0xFFU) / 255.0
    };
}

std::uint8_t PixelAlpha(const std::uint32_t pixel) {
    return static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU);
}

std::uint32_t RgbToPixel(const Vec3& color, const std::uint8_t alpha) {
    return static_cast<std::uint32_t>(Clamp(std::round(color.z * 255.0), 0.0, 255.0))
        | (static_cast<std::uint32_t>(Clamp(std::round(color.y * 255.0), 0.0, 255.0)) << 8U)
        | (static_cast<std::uint32_t>(Clamp(std::round(color.x * 255.0), 0.0, 255.0)) << 16U)
        | (static_cast<std::uint32_t>(alpha) << 24U);
}

// =============================================================================
// Color Math Helpers
// =============================================================================

double RgbLuminance(const Vec3& color) {
    return color.x * 0.2126 + color.y * 0.7152 + color.z * 0.0722;
}

Vec3 ClampRgb(const Vec3& color) {
    return {
        Clamp(color.x, 0.0, 1.0),
        Clamp(color.y, 0.0, 1.0),
        Clamp(color.z, 0.0, 1.0)
    };
}

Vec3 HsvToRgb(const double hue, const double saturation, const double value) {
    const double wrappedHue = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    const double c = value * saturation;
    const double x = c * (1.0 - std::abs(std::fmod(wrappedHue / 60.0, 2.0) - 1.0));
    const double m = value - c;
    if (wrappedHue < 60.0) {
        return {c + m, x + m, m};
    }
    if (wrappedHue < 120.0) {
        return {x + m, c + m, m};
    }
    if (wrappedHue < 180.0) {
        return {m, c + m, x + m};
    }
    if (wrappedHue < 240.0) {
        return {m, x + m, c + m};
    }
    if (wrappedHue < 300.0) {
        return {x + m, m, c + m};
    }
    return {c + m, m, x + m};
}

Vec3 RgbToHsv(const Vec3& color) {
    const double maxValue = std::max({color.x, color.y, color.z});
    const double minValue = std::min({color.x, color.y, color.z});
    const double delta = maxValue - minValue;
    double hue = 0.0;
    if (delta > 1.0e-6) {
        if (maxValue == color.x) {
            hue = 60.0 * std::fmod((color.y - color.z) / delta, 6.0);
        } else if (maxValue == color.y) {
            hue = 60.0 * (((color.z - color.x) / delta) + 2.0);
        } else {
            hue = 60.0 * (((color.x - color.y) / delta) + 4.0);
        }
    }
    if (hue < 0.0) {
        hue += 360.0;
    }
    const double saturation = maxValue <= 1.0e-6 ? 0.0 : delta / maxValue;
    return {hue, saturation, maxValue};
}

Vec3 ColorTemperatureToRgb(const double kelvin) {
    const double temp = Clamp(kelvin, 1000.0, 15000.0) / 100.0;
    double r = 1.0;
    double g = 0.0;
    double b = 0.0;
    if (temp <= 66.0) {
        g = Clamp(0.3900815787690196 * std::log(temp) - 0.6318414437886275, 0.0, 1.0);
    } else {
        r = Clamp(1.292936186062745 * std::pow(temp - 60.0, -0.1332047592), 0.0, 1.0);
        g = Clamp(1.1298908608952941 * std::pow(temp - 60.0, -0.0755148492), 0.0, 1.0);
    }
    if (temp >= 66.0) {
        b = 1.0;
    } else if (temp > 19.0) {
        b = Clamp(0.5432067891101961 * std::log(temp - 10.0) - 1.19625408914, 0.0, 1.0);
    }
    return {r, g, b};
}

double EvaluateCustomCurve(double t, const PostProcessSettings& settings) {
    const auto& points = settings.curveControlPoints;
    if (points.size() < 2) return t;
    
    for (size_t i = 0; i < points.size() - 1; ++i) {
        if (t >= points[i].x && t <= points[i + 1].x) {
            double localT = (t - points[i].x) / (points[i + 1].x - points[i].x);
            return points[i].y + localT * (points[i + 1].y - points[i].y);
        }
    }
    return t;
}

Vec3 ApplyLevelsCurve(const Vec3& color, const PostProcessSettings& settings) {
    if (settings.curveUseCustom && settings.curveControlPoints.size() >= 2) {
        return {
            Clamp(EvaluateCustomCurve(color.x, settings), 0.0, 1.0),
            Clamp(EvaluateCustomCurve(color.y, settings), 0.0, 1.0),
            Clamp(EvaluateCustomCurve(color.z, settings), 0.0, 1.0)
        };
    }
    const double blackPoint = std::min(settings.curveBlackPoint, settings.curveWhitePoint - 0.001);
    const double whitePoint = std::max(settings.curveWhitePoint, blackPoint + 0.001);
    const double gamma = std::max(settings.curveGamma, 0.05);
    return {
        std::pow(Clamp((color.x - blackPoint) / (whitePoint - blackPoint), 0.0, 1.0), 1.0 / gamma),
        std::pow(Clamp((color.y - blackPoint) / (whitePoint - blackPoint), 0.0, 1.0), 1.0 / gamma),
        std::pow(Clamp((color.z - blackPoint) / (whitePoint - blackPoint), 0.0, 1.0), 1.0 / gamma)
    };
}

Vec3 ACESFilm(const Vec3& color) {
    const auto mapChannel = [](const double x) {
        const double a = 2.51;
        const double b = 0.03;
        const double c = 2.43;
        const double d = 0.59;
        const double e = 0.14;
        return Clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    };
    return {mapChannel(color.x), mapChannel(color.y), mapChannel(color.z)};
}

unsigned ParallelWorkerCount(const int itemCount) {
    if (itemCount <= 1) {
        return 1u;
    }
    return std::max(1u, std::min<unsigned>(std::thread::hardware_concurrency(), static_cast<unsigned>(itemCount)));
}

// =============================================================================
// Effect Implementations
// =============================================================================

class DenoiserEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.denoiser.enabled && scene.denoiser.strength > 0.0;
    }

    bool NeedsDepthBuffer() const override { return true; }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>& depthBuffer, const std::function<bool()>& shouldAbort) const override {
        if (width <= 0 || height <= 0 || pixels.empty() || depthBuffer.size() != pixels.size()) {
            return;
        }

        const int passes = static_cast<int>(std::ceil(scene.denoiser.strength * 3.0));
        const double sigmaSpatial = 3.0 + scene.denoiser.strength * 4.0;
        const double sigmaColor = 0.15 + scene.denoiser.strength * 0.25;
        const double sigmaDepth = 0.05 + scene.denoiser.strength * 0.1;
        const int radius = std::clamp(static_cast<int>(std::ceil(sigmaSpatial * 2.0)), 1, 8);
        const int blurRadius = std::clamp(static_cast<int>(std::ceil(sigmaSpatial)), 1, 4);

        const double invSpatialVar = 1.0 / (2.0 * sigmaSpatial * sigmaSpatial);
        const double invColorVar = 1.0 / (2.0 * sigmaColor * sigmaColor);
        const double invDepthVar = 1.0 / (2.0 * sigmaDepth * sigmaDepth);
        const double thresholdMult = Lerp(4.0, 0.5, Clamp(scene.denoiser.strength, 0.0, 1.0));

        std::vector<std::uint32_t> source = pixels;
        std::vector<std::uint32_t> temp(pixels.size());

        std::vector<double> spatialWeightsLarge(static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const double distSq = static_cast<double>(dx * dx + dy * dy);
                spatialWeightsLarge[static_cast<std::size_t>((dy + radius) * (radius * 2 + 1) + (dx + radius))] = std::exp(-distSq * invSpatialVar);
            }
        }

        std::vector<double> spatialWeightsSmall(static_cast<std::size_t>((blurRadius * 2 + 1) * (blurRadius * 2 + 1)));
        for (int dy = -blurRadius; dy <= blurRadius; ++dy) {
            for (int dx = -blurRadius; dx <= blurRadius; ++dx) {
                const double distSq = static_cast<double>(dx * dx + dy * dy);
                spatialWeightsSmall[static_cast<std::size_t>((dy + blurRadius) * (blurRadius * 2 + 1) + (dx + blurRadius))] = std::exp(-distSq * invSpatialVar);
            }
        }

        for (int pass = 0; pass < passes; ++pass) {
            if (shouldAbort && shouldAbort()) return;

            const std::vector<std::uint32_t>& readBuffer = pass % 2 == 0 ? source : temp;
            std::vector<std::uint32_t>& writeBuffer = pass % 2 == 0 ? temp : source;
            std::atomic<bool> abortRequested = false;

            const auto processRows = [&](const int rowStart, const int rowEnd, const bool monitorAbort) {
                for (int y = rowStart; y < rowEnd; ++y) {
                    if (abortRequested.load(std::memory_order_relaxed)) return;
                    if (monitorAbort && shouldAbort && (y % 8) == 0 && shouldAbort()) {
                        abortRequested.store(true, std::memory_order_relaxed);
                        return;
                    }
                    for (int x = 0; x < width; ++x) {
                        const std::size_t centerIndex = static_cast<std::size_t>(y * width + x);
                        const Color centerColorRaw = UnpackBgra(readBuffer[centerIndex]);
                        const float centerDepth = depthBuffer[centerIndex];

                        const double cr = centerColorRaw.r / 255.0;
                        const double cg = centerColorRaw.g / 255.0;
                        const double cb = centerColorRaw.b / 255.0;
                        const double ca = centerColorRaw.a / 255.0;

                        double sumR = 0.0, sumG = 0.0, sumB = 0.0, sumA = 0.0;
                        double sumSqR = 0.0, sumSqG = 0.0, sumSqB = 0.0, sumSqA = 0.0;
                        double weightSum = 0.0;

                        const int minY = std::max(0, y - radius);
                        const int maxY = std::min(height - 1, y + radius);
                        const int minX = std::max(0, x - radius);
                        const int maxX = std::min(width - 1, x + radius);

                        for (int sy = minY; sy <= maxY; ++sy) {
                            for (int sx = minX; sx <= maxX; ++sx) {
                                const std::size_t sampleIndex = static_cast<std::size_t>(sy * width + sx);
                                const Color sampleColor = UnpackBgra(readBuffer[sampleIndex]);
                                const double sr = sampleColor.r / 255.0;
                                const double sg = sampleColor.g / 255.0;
                                const double sb = sampleColor.b / 255.0;
                                const double sa = sampleColor.a / 255.0;

                                const double spatialWeight = spatialWeightsLarge[static_cast<std::size_t>((sy - y + radius) * (radius * 2 + 1) + (sx - x + radius))];
                                const double weight = spatialWeight * (sa > 0.01 ? 1.0 : 0.001);

                                sumR += sr * weight; sumG += sg * weight; sumB += sb * weight; sumA += sa * weight;
                                sumSqR += sr * sr * weight; sumSqG += sg * sg * weight; sumSqB += sb * sb * weight; sumSqA += sa * sa * weight;
                                weightSum += weight;
                            }
                        }

                        const double meanR = sumR / weightSum; const double meanG = sumG / weightSum; const double meanB = sumB / weightSum; const double meanA = sumA / weightSum;
                        const double varR = std::max(0.0, (sumSqR / weightSum) - (meanR * meanR));
                        const double varG = std::max(0.0, (sumSqG / weightSum) - (meanG * meanG));
                        const double varB = std::max(0.0, (sumSqB / weightSum) - (meanB * meanB));
                        const double varA = std::max(0.0, (sumSqA / weightSum) - (meanA * meanA));

                        const double devR = std::sqrt(varR); const double devG = std::sqrt(varG); const double devB = std::sqrt(varB); const double devA = std::sqrt(varA);
                        const double minR = meanR - devR * thresholdMult; const double maxR = meanR + devR * thresholdMult;
                        const double minG = meanG - devG * thresholdMult; const double maxG = meanG + devG * thresholdMult;
                        const double minB = meanB - devB * thresholdMult; const double maxB = meanB + devB * thresholdMult;
                        const double minA = meanA - devA * thresholdMult; const double maxA = meanA + devA * thresholdMult;

                        const double clampedCr = Clamp(cr, minR, maxR); const double clampedCg = Clamp(cg, minG, maxG);
                        const double clampedCb = Clamp(cb, minB, maxB); const double clampedCa = Clamp(ca, minA, maxA);

                        double blurSumR = 0.0, blurSumG = 0.0, blurSumB = 0.0, blurSumA = 0.0;
                        double blurWeightSum = 0.0;

                        const int bMinY = std::max(0, y - blurRadius); const int bMaxY = std::min(height - 1, y + blurRadius);
                        const int bMinX = std::max(0, x - blurRadius); const int bMaxX = std::min(width - 1, x + blurRadius);

                        for (int sy = bMinY; sy <= bMaxY; ++sy) {
                            for (int sx = bMinX; sx <= bMaxX; ++sx) {
                                const std::size_t sampleIndex = static_cast<std::size_t>(sy * width + sx);
                                const Color sampleColorRaw = UnpackBgra(readBuffer[sampleIndex]);
                                const float sampleDepth = depthBuffer[sampleIndex];

                                const double sr = Clamp(sampleColorRaw.r / 255.0, minR, maxR);
                                const double sg = Clamp(sampleColorRaw.g / 255.0, minG, maxG);
                                const double sb = Clamp(sampleColorRaw.b / 255.0, minB, maxB);
                                const double sa = Clamp(sampleColorRaw.a / 255.0, minA, maxA);

                                const double spatialWeight = spatialWeightsSmall[static_cast<std::size_t>((sy - y + blurRadius) * (blurRadius * 2 + 1) + (sx - x + blurRadius))];
                                const double depthDist = static_cast<double>(sampleDepth - centerDepth);
                                const double depthWeight = std::exp(-(depthDist * depthDist) * invDepthVar);
                                const double colorDistSq = (sr - clampedCr) * (sr - clampedCr) + (sg - clampedCg) * (sg - clampedCg) + (sb - clampedCb) * (sb - clampedCb);
                                const double colorWeight = std::exp(-colorDistSq * invColorVar);

                                const double weight = spatialWeight * depthWeight * colorWeight;
                                blurSumR += sr * weight; blurSumG += sg * weight; blurSumB += sb * weight; blurSumA += sa * weight;
                                blurWeightSum += weight;
                            }
                        }

                        if (blurWeightSum > 1.0e-6) {
                            const double finalR = Lerp(clampedCr, blurSumR / blurWeightSum, Clamp(scene.denoiser.strength * 1.5, 0.0, 1.0));
                            const double finalG = Lerp(clampedCg, blurSumG / blurWeightSum, Clamp(scene.denoiser.strength * 1.5, 0.0, 1.0));
                            const double finalB = Lerp(clampedCb, blurSumB / blurWeightSum, Clamp(scene.denoiser.strength * 1.5, 0.0, 1.0));
                            const double finalA = Lerp(clampedCa, blurSumA / blurWeightSum, Clamp(scene.denoiser.strength * 1.5, 0.0, 1.0));

                            writeBuffer[centerIndex] = ToBgra({
                                static_cast<std::uint8_t>(std::clamp(std::round(finalR * 255.0), 0.0, 255.0)),
                                static_cast<std::uint8_t>(std::clamp(std::round(finalG * 255.0), 0.0, 255.0)),
                                static_cast<std::uint8_t>(std::clamp(std::round(finalB * 255.0), 0.0, 255.0)),
                                static_cast<std::uint8_t>(std::clamp(std::round(finalA * 255.0), 0.0, 255.0))
                            });
                        } else {
                            writeBuffer[centerIndex] = ToBgra({
                                static_cast<std::uint8_t>(std::clamp(std::round(clampedCr * 255.0), 0.0, 255.0)),
                                static_cast<std::uint8_t>(std::clamp(std::round(clampedCg * 255.0), 0.0, 255.0)),
                                static_cast<std::uint8_t>(std::clamp(std::round(clampedCb * 255.0), 0.0, 255.0)),
                                static_cast<std::uint8_t>(std::clamp(std::round(clampedCa * 255.0), 0.0, 255.0))
                            });
                        }
                    }
                }
            };

            const unsigned workerCount = ParallelWorkerCount(height);
            if (workerCount <= 1u) {
                processRows(0, height, true);
            } else {
                std::vector<std::thread> workers;
                const int rowsPerWorker = (height + workerCount - 1) / workerCount;
                for (unsigned i = 0; i < workerCount; ++i) {
                    int start = i * rowsPerWorker;
                    int end = std::min(height, start + rowsPerWorker);
                    if (start < end) workers.emplace_back(processRows, start, end, i == 0u);
                }
                for (auto& w : workers) if (w.joinable()) w.join();
            }

            if (abortRequested.load(std::memory_order_relaxed)) return;
        }

        pixels = passes % 2 == 1 ? temp : source;
    }
};

class DepthOfFieldEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.depthOfField.enabled;
    }

    bool NeedsDepthBuffer() const override { return true; }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>& depthBuffer, const std::function<bool()>& /*shouldAbort*/) const override {
        if (width <= 0 || height <= 0 || pixels.empty() || depthBuffer.size() != pixels.size()) return;

        const int maxRadius = std::clamp(static_cast<int>(std::round(scene.depthOfField.blurStrength * 12.0)), 1, 12);
        const float focusDepth = static_cast<float>(scene.depthOfField.focusDepth);
        const float focusRange = std::max(0.01f, static_cast<float>(scene.depthOfField.focusRange));
        const float blurStrength = static_cast<float>(std::clamp(scene.depthOfField.blurStrength, 0.0, 1.0));
        const std::vector<std::uint32_t> source = pixels;

        static const std::array<Vec2, 8> kBlurOffsets = {
            Vec2{1.0, 0.0}, Vec2{-1.0, 0.0}, Vec2{0.0, 1.0}, Vec2{0.0, -1.0},
            Vec2{0.707, 0.707}, Vec2{-0.707, 0.707}, Vec2{0.707, -0.707}, Vec2{-0.707, -0.707}
        };

        const auto processRows = [&](const int rowStart, const int rowEnd) {
            for (int y = rowStart; y < rowEnd; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y * width + x);
                    const float depth = depthBuffer[index];
                    const float blurAmount = std::clamp((std::abs(depth - focusDepth) - focusRange) / std::max(0.02f, 1.0f - focusRange), 0.0f, 1.0f) * blurStrength;
                    if (blurAmount <= 0.001f) continue;

                    const float radius = std::max(1.0f, blurAmount * static_cast<float>(maxRadius));
                    float r = 0, g = 0, b = 0, a = 0, weightSum = 0;

                    auto accumulate = [&](int sx, int sy, float weight) {
                        int cx = std::clamp(sx, 0, width - 1);
                        int cy = std::clamp(sy, 0, height - 1);
                        Color c = UnpackBgra(source[static_cast<std::size_t>(cy * width + cx)]);
                        r += c.r * weight; g += c.g * weight; b += c.b * weight; a += c.a * weight;
                        weightSum += weight;
                    };

                    accumulate(x, y, 2.0f);
                    for (const auto& off : kBlurOffsets) {
                        accumulate(static_cast<int>(std::round(x + off.x * radius)), static_cast<int>(std::round(y + off.y * radius)), 1.0f);
                    }

                    if (weightSum > 0) {
                        pixels[index] = ToBgra({
                            static_cast<std::uint8_t>(std::clamp<int>(std::lround(r / weightSum), 0, 255)),
                            static_cast<std::uint8_t>(std::clamp<int>(std::lround(g / weightSum), 0, 255)),
                            static_cast<std::uint8_t>(std::clamp<int>(std::lround(b / weightSum), 0, 255)),
                            static_cast<std::uint8_t>(std::clamp<int>(std::lround(a / weightSum), 0, 255))
                        });
                    }
                }
            }
        };

        unsigned workers = ParallelWorkerCount(height);
        if (workers <= 1) {
            processRows(0, height);
        } else {
            std::vector<std::thread> threadPool;
            int rpw = (height + workers - 1) / workers;
            for (unsigned i = 0; i < workers; ++i) {
                int start = i * rpw;
                int end = std::min(height, start + rpw);
                if (start < end) threadPool.emplace_back(processRows, start, end);
            }
            for (auto& t : threadPool) if (t.joinable()) t.join();
        }
    }
};

class ChromaticAberrationEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.chromaticAberrationEnabled
            && scene.postProcess.chromaticAberration > 0.001;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>&, const std::function<bool()>&) const override {
        const PostProcessSettings& pp = scene.postProcess;
        const std::vector<std::uint32_t> source = pixels;
        const auto sample = [&](int x, int y) {
            return PixelToRgb(source[static_cast<std::size_t>(std::clamp(y, 0, height-1) * width + std::clamp(x, 0, width-1))]);
        };

        const double strength = pp.chromaticAberration;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * width + x);
                const double u = (x + 0.5) / width, v = (y + 0.5) / height;
                const Vec2 dir = {u - 0.5, v - 0.5};
                const double dist = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                const Vec2 offset = dir * (strength * 0.02 * dist);
                
                const Vec3 rS = sample(static_cast<int>((u + offset.x) * width), static_cast<int>((v + offset.y) * height));
                const Vec3 gS = PixelToRgb(source[idx]);
                const Vec3 bS = sample(static_cast<int>((u - offset.x) * width), static_cast<int>((v - offset.y) * height));
                
                pixels[idx] = RgbToPixel({rS.x, gS.y, bS.z}, PixelAlpha(source[idx]));
            }
        }
    }
};

class SharpenEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.sharpenEnabled
            && scene.postProcess.sharpenAmount > 0.001;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>&, const std::function<bool()>&) const override {
        const PostProcessSettings& pp = scene.postProcess;
        const std::vector<std::uint32_t> source = pixels;
        const auto sample = [&](int x, int y) {
            return PixelToRgb(source[static_cast<std::size_t>(std::clamp(y, 0, height-1) * width + std::clamp(x, 0, width-1))]);
        };

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * width + x);
                const Vec3 color = PixelToRgb(source[idx]);
                const Vec3 blurred = (sample(x, y) * 4.0 + sample(x-1, y) + sample(x+1, y) + sample(x, y-1) + sample(x, y+1)) * 0.125;
                const Vec3 sharpened = Lerp(color, color + (color - blurred), Clamp(pp.sharpenAmount, 0.0, 1.0));
                pixels[idx] = RgbToPixel(ClampRgb(sharpened), PixelAlpha(source[idx]));
            }
        }
    }
};

class BloomEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.enabled
            && scene.postProcess.bloomIntensity > 0.001;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>&, const std::function<bool()>&) const override {
        const PostProcessSettings& pp = scene.postProcess;
        const std::vector<std::uint32_t> source = pixels;
        const auto sample = [&](int x, int y) {
            return PixelToRgb(source[static_cast<std::size_t>(std::clamp(y, 0, height-1) * width + std::clamp(x, 0, width-1))]);
        };

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * width + x);
                Vec3 bloom {}; double wSum = 0;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        Vec3 s = sample(x+dx, y+dy);
                        if (RgbLuminance(s) <= pp.bloomThreshold) continue;
                        double k = 1.0 / (1.0 + dx*dx + dy*dy);
                        bloom = bloom + s * k; wSum += k;
                    }
                }
                Vec3 color = PixelToRgb(source[idx]);
                if (wSum > 1e-6) color = color + (bloom * (1.0/wSum)) * pp.bloomIntensity;
                pixels[idx] = RgbToPixel(ClampRgb(color), PixelAlpha(source[idx]));
            }
        }
    }
};

class ColorTemperatureEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.colorTemperatureEnabled
            && std::abs(scene.postProcess.colorTemperature - 6500.0) > 10.0;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int /*width*/, int /*height*/, const std::vector<float>&, const std::function<bool()>&) const override {
        const PostProcessSettings& pp = scene.postProcess;
        const Vec3 tempColor = ColorTemperatureToRgb(pp.colorTemperature);
        const Vec3 neutral = ColorTemperatureToRgb(6500.0);
        const Vec3 scale = { tempColor.x / std::max(neutral.x, 0.001), tempColor.y / std::max(neutral.y, 0.001), tempColor.z / std::max(neutral.z, 0.001) };

        for (auto& pixel : pixels) {
            Vec3 color = PixelToRgb(pixel);
            color = { color.x * scale.x, color.y * scale.y, color.z * scale.z };
            pixel = RgbToPixel(ClampRgb(color), PixelAlpha(pixel));
        }
    }
};

class SaturationEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.saturationEnabled
            && std::abs(scene.postProcess.saturationBoost) > 0.001;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int /*width*/, int /*height*/, const std::vector<float>&, const std::function<bool()>&) const override {
        const PostProcessSettings& pp = scene.postProcess;
        for (auto& pixel : pixels) {
            Vec3 color = PixelToRgb(pixel);
            const double lum = RgbLuminance(color);
            color = Lerp(Vec3{lum, lum, lum}, color, 1.0 + pp.saturationBoost);
            pixel = RgbToPixel(ClampRgb(color), PixelAlpha(pixel));
        }
    }
};

class HueShiftEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.hueShiftEnabled
            && std::abs(scene.postProcess.hueShiftDegrees) > 0.001;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int /*width*/, int /*height*/, const std::vector<float>&, const std::function<bool()>&) const override {
        const PostProcessSettings& pp = scene.postProcess;
        for (auto& pixel : pixels) {
            Vec3 hsv = RgbToHsv(ClampRgb(PixelToRgb(pixel)));
            hsv.x += pp.hueShiftDegrees;
            pixel = RgbToPixel(HsvToRgb(hsv.x, hsv.y, hsv.z), PixelAlpha(pixel));
        }
    }
};

class CurvesEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.curvesEnabled;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int /*width*/, int /*height*/, const std::vector<float>&, const std::function<bool()>&) const override {
        for (auto& pixel : pixels) {
            pixel = RgbToPixel(ApplyLevelsCurve(PixelToRgb(pixel), scene.postProcess), PixelAlpha(pixel));
        }
    }
};

class ToneMappingEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.toneMappingEnabled
            && scene.postProcess.acesToneMap;
    }

    void Apply(const Scene& /*scene*/, std::vector<std::uint32_t>& pixels, int /*width*/, int /*height*/, const std::vector<float>&, const std::function<bool()>&) const override {
        for (auto& pixel : pixels) {
            pixel = RgbToPixel(ACESFilm(PixelToRgb(pixel)), PixelAlpha(pixel));
        }
    }
};

class FilmGrainEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.filmGrainEnabled
            && scene.postProcess.filmGrain > 0.001;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>&, const std::function<bool()>&) const override {
        const PostProcessSettings& pp = scene.postProcess;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * width + x);
                const double grainSeed = std::sin((x + 13.0) * 12.9898 + (y + 7.0) * 78.233) * 43758.5453;
                const double grain = ( (grainSeed - std::floor(grainSeed)) - 0.5) * pp.filmGrain * 0.15;
                Vec3 color = PixelToRgb(pixels[idx]);
                const double scale = 1.0 - Clamp(RgbLuminance(color) * 2.0, 0.0, 1.0);
                color = color + Vec3{grain * scale, grain * scale, grain * scale};
                pixels[idx] = RgbToPixel(ClampRgb(color), PixelAlpha(pixels[idx]));
            }
        }
    }
};

class VignetteEffect : public Effect {
public:
    bool IsEnabled(const Scene& scene) const override {
        return scene.postProcess.vignetteEnabled
            && scene.postProcess.vignetteIntensity > 0.001;
    }

    void Apply(const Scene& scene, std::vector<std::uint32_t>& pixels, int width, int height, const std::vector<float>&, const std::function<bool()>&) const override {
        const PostProcessSettings& pp = scene.postProcess;
        const double aspect = static_cast<double>(width) / height;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y * width + x);
                Vec2 uv = { (x + 0.5) / width - 0.5, (y + 0.5) / height - 0.5 };
                uv.x *= Lerp(1.0, aspect, pp.vignetteRoundness);
                const double dist = std::sqrt(uv.x * uv.x + uv.y * uv.y) * 1.4142;
                const double vignette = 1.0 - Smoothstep(0.4, 1.2, dist) * pp.vignetteIntensity;
                pixels[idx] = RgbToPixel(PixelToRgb(pixels[idx]) * vignette, PixelAlpha(pixels[idx]));
            }
        }
    }
};

} // namespace

std::unique_ptr<Effect> EffectFactory::CreateEffect(EffectStackStage stage) {
    switch (stage) {
        case EffectStackStage::Denoiser: return std::make_unique<DenoiserEffect>();
        case EffectStackStage::DepthOfField: return std::make_unique<DepthOfFieldEffect>();
        case EffectStackStage::ChromaticAberration: return std::make_unique<ChromaticAberrationEffect>();
        case EffectStackStage::Sharpen: return std::make_unique<SharpenEffect>();
        case EffectStackStage::Bloom: return std::make_unique<BloomEffect>();
        case EffectStackStage::ColorTemperature: return std::make_unique<ColorTemperatureEffect>();
        case EffectStackStage::Saturation: return std::make_unique<SaturationEffect>();
        case EffectStackStage::HueShift: return std::make_unique<HueShiftEffect>();
        case EffectStackStage::Curves: return std::make_unique<CurvesEffect>();
        case EffectStackStage::ToneMapping: return std::make_unique<ToneMappingEffect>();
        case EffectStackStage::FilmGrain: return std::make_unique<FilmGrainEffect>();
        case EffectStackStage::Vignette: return std::make_unique<VignetteEffect>();
        default: return nullptr;
    }
}

} // namespace radiary
