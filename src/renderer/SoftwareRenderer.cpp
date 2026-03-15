#include "renderer/SoftwareRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

namespace radiary {

namespace {

constexpr double kFlameDepthNear = 0.15;
constexpr double kFlameDepthRangePadding = 24.0;

struct Triangle2D {
    std::array<SoftwareRenderer::ProjectedPoint, 3> points {};
    Color fill {};
    Color wire {};
    Color point {};
    double depth = 0.0;
};

double FlameReconstructionKernel(const int dx, const int dy) {
    if (dx == 0 && dy == 0) {
        return 1.0;
    }
    return (std::abs(dx) + std::abs(dy) == 1) ? 0.16 : 0.06;
}

FlamePixel ReconstructFlamePixel(
    const std::vector<FlamePixel>& flamePixels,
    const int width,
    const int height,
    const int x,
    const int y) {
    const FlamePixel& center = flamePixels[static_cast<std::size_t>(y * width + x)];
    FlamePixel reconstructed = center;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }

            const int sampleX = x + dx;
            const int sampleY = y + dy;
            if (sampleX < 0 || sampleY < 0 || sampleX >= width || sampleY >= height) {
                continue;
            }

            const FlamePixel& sample = flamePixels[static_cast<std::size_t>(sampleY * width + sampleX)];
            const float kernel = static_cast<float>(FlameReconstructionKernel(dx, dy));
            reconstructed.density += sample.density * kernel;
            reconstructed.red += sample.red * kernel;
            reconstructed.green += sample.green * kernel;
            reconstructed.blue += sample.blue * kernel;
            reconstructed.depth += sample.depth * kernel;
        }
    }

    const float reconstructionBlend = static_cast<float>(Clamp((0.55 - center.density) / 0.55, 0.0, 1.0) * 0.18);
    FlamePixel blended = center;
    blended.density += (reconstructed.density - center.density) * reconstructionBlend;
    blended.red += (reconstructed.red - center.red) * reconstructionBlend;
    blended.green += (reconstructed.green - center.green) * reconstructionBlend;
    blended.blue += (reconstructed.blue - center.blue) * reconstructionBlend;
    blended.depth += (reconstructed.depth - center.depth) * reconstructionBlend;
    return blended;
}

Vec3 AxisVector(const PathAxis axis) {
    switch (axis) {
    case PathAxis::X:
        return {1.0, 0.0, 0.0};
    case PathAxis::Y:
        return {0.0, 1.0, 0.0};
    case PathAxis::Z:
    default:
        return {0.0, 0.0, 1.0};
    }
}

Vec3 RotateAroundAxis(const Vec3& value, const Vec3& axis, const double radians) {
    const Vec3 normalizedAxis = Normalize(axis);
    const double cosTheta = std::cos(radians);
    const double sinTheta = std::sin(radians);
    return value * cosTheta
        + Cross(normalizedAxis, value) * sinTheta
        + normalizedAxis * Dot(normalizedAxis, value) * (1.0 - cosTheta);
}

Color ScaleColor(const Color& color, const double factor) {
    return {
        static_cast<std::uint8_t>(Clamp(std::round(color.r * factor), 0.0, 255.0)),
        static_cast<std::uint8_t>(Clamp(std::round(color.g * factor), 0.0, 255.0)),
        static_cast<std::uint8_t>(Clamp(std::round(color.b * factor), 0.0, 255.0)),
        color.a
    };
}

Color AddColors(const Color& a, const Color& b) {
    return {
        static_cast<std::uint8_t>(std::min(255, static_cast<int>(a.r) + static_cast<int>(b.r))),
        static_cast<std::uint8_t>(std::min(255, static_cast<int>(a.g) + static_cast<int>(b.g))),
        static_cast<std::uint8_t>(std::min(255, static_cast<int>(a.b) + static_cast<int>(b.b))),
        255
    };
}

double Hash1(const double value) {
    return std::sin(value * 127.1 + 311.7) * 43758.5453123;
}

double Fract(const double value) {
    return value - std::floor(value);
}

double ValueNoise(const Vec3& point) {
    const Vec3 cell {std::floor(point.x), std::floor(point.y), std::floor(point.z)};
    const Vec3 local {Fract(point.x), Fract(point.y), Fract(point.z)};
    const auto hash = [&](const Vec3& offset) {
        return Fract(Hash1((cell.x + offset.x) * 17.0 + (cell.y + offset.y) * 59.0 + (cell.z + offset.z) * 113.0)) * 2.0 - 1.0;
    };

    const double u = Smoothstep(0.0, 1.0, local.x);
    const double v = Smoothstep(0.0, 1.0, local.y);
    const double w = Smoothstep(0.0, 1.0, local.z);

    const double c000 = hash({0.0, 0.0, 0.0});
    const double c100 = hash({1.0, 0.0, 0.0});
    const double c010 = hash({0.0, 1.0, 0.0});
    const double c110 = hash({1.0, 1.0, 0.0});
    const double c001 = hash({0.0, 0.0, 1.0});
    const double c101 = hash({1.0, 0.0, 1.0});
    const double c011 = hash({0.0, 1.0, 1.0});
    const double c111 = hash({1.0, 1.0, 1.0});

    const double x00 = Lerp(c000, c100, u);
    const double x10 = Lerp(c010, c110, u);
    const double x01 = Lerp(c001, c101, u);
    const double x11 = Lerp(c011, c111, u);
    const double y0 = Lerp(x00, x10, v);
    const double y1 = Lerp(x01, x11, v);
    return Lerp(y0, y1, w);
}

double FractalNoise(const Vec3& point, const FractalDisplacementSettings& settings) {
    double value = 0.0;
    double amplitude = 1.0;
    double frequency = std::max(0.01, settings.frequency * 0.01);
    const int octaves = std::max(1, settings.complexity);
    for (int octave = 0; octave < octaves; ++octave) {
        const double sample = ValueNoise(point * frequency);
        value += (settings.fractalType == FractalType::Turbulent ? std::abs(sample) : sample) * amplitude;
        frequency *= std::max(1.01, settings.octScale);
        amplitude *= settings.octMult;
    }
    return value;
}

Vec3 VectorNoise(const Vec3& point, const FractalDisplacementSettings& settings) {
    return {
        FractalNoise(point + Vec3{19.1, 7.4, 3.8}, settings),
        FractalNoise(point + Vec3{5.3, 29.7, 11.2}, settings),
        FractalNoise(point + Vec3{13.9, 17.6, 23.5}, settings)
    };
}

double ShapeNoise(const double sample, const double smoothenNormals) {
    const double sharpness = Clamp(1.45 - smoothenNormals * 0.28, 0.38, 1.45);
    const double magnitude = std::pow(std::abs(sample), sharpness);
    return sample < 0.0 ? -magnitude : magnitude;
}

double SpikeBias(const PathSettings& path) {
    const double smoothness = Clamp(path.fractalDisplacement.smoothenNormals / 4.0, 0.0, 1.0);
    const double aggression = Clamp(path.segment.randomness / 6.0 + (1.0 - smoothness) * 0.85, 0.0, 1.65);
    return aggression;
}

double CalculateThicknessProfile(const PathSettings& path, const double t) {
    const double baseTaper = 1.0 - path.taper * std::abs(t * 2.0 - 1.0) * 0.75;
    
    switch (path.segment.thicknessProfile) {
    case ThicknessProfile::Pulse: {
        const double freq = path.segment.thicknessPulseFrequency * kPi * 2.0;
        const double pulse = std::sin(t * freq) * 0.5 + 0.5;
        const double depth = path.segment.thicknessPulseDepth;
        return baseTaper * (1.0 - depth * 0.5 + pulse * depth);
    }
    case ThicknessProfile::Bezier: {
        // Bezier curve thickness: thin at ends, thick in middle with adjustable curve
        const double p0 = 0.2;
        const double p1 = 0.1;
        const double p2 = 1.0;
        const double p3 = 0.2;
        const double oneMinusT = 1.0 - t;
        const double bezier = oneMinusT * oneMinusT * oneMinusT * p0
            + 3.0 * oneMinusT * oneMinusT * t * p1
            + 3.0 * oneMinusT * t * t * p2
            + t * t * t * p3;
        return baseTaper * bezier;
    }
    case ThicknessProfile::Blobby: {
        // Gaussian-like blob centered at thicknessBlobCenter with width control
        const double center = path.segment.thicknessBlobCenter;
        const double width = std::max(0.05, path.segment.thicknessBlobWidth);
        const double dist = (t - center) / width;
        const double gaussian = std::exp(-dist * dist);
        return baseTaper * (0.4 + 0.6 * gaussian);
    }
    case ThicknessProfile::Linear:
    default:
        return baseTaper;
    }
}

Color MaterialBaseColor(const MaterialSettings& material, const double factor) {
    const Color ramp = Lerp(material.primaryColor, material.accentColor, Clamp(factor, 0.0, 1.0));
    switch (material.materialType) {
    case MaterialType::Flat:
        return ramp;
    case MaterialType::Matte:
        return Lerp(ramp, material.primaryColor, 0.35);
    case MaterialType::Glossy:
        return Lerp(ramp, material.accentColor, 0.28);
    case MaterialType::Metallic:
    default:
        return Lerp(material.primaryColor, material.accentColor, 0.22 + factor * 0.38);
    }
}

Color ShadeMaterial(
    const MaterialSettings& material,
    const Color& baseColor,
    const double lambert,
    const double depthFactor,
    const double specular,
    const double fresnel) {
    double diffuse = 0.0;
    double gloss = 0.0;
    double edge = 0.0;
    switch (material.materialType) {
    case MaterialType::Flat:
        diffuse = 0.92;
        gloss = 0.02;
        edge = 0.0;
        break;
    case MaterialType::Matte:
        diffuse = 0.34 + lambert * 0.48;
        gloss = specular * 0.06;
        edge = fresnel * 0.04;
        break;
    case MaterialType::Glossy:
        diffuse = 0.28 + lambert * 0.62;
        gloss = specular * 0.34;
        edge = fresnel * 0.18;
        break;
    case MaterialType::Metallic:
    default:
        diffuse = 0.12 + lambert * 0.42;
        gloss = specular * 0.72 + fresnel * 0.24;
        edge = fresnel * 0.82;
        break;
    }

    const Color lit = ScaleColor(baseColor, Clamp((diffuse + edge * 0.08) * depthFactor, 0.12, 1.05));
    const Color spec = ScaleColor(material.accentColor, Clamp((gloss + edge * 0.38) * depthFactor, 0.0, 1.0));
    return AddColors(lit, spec);
}

std::vector<Vec3> ResamplePath(const std::vector<Vec3>& path, const int count, const bool closed) {
    std::vector<Vec3> result;
    if (path.size() < 2 || count < 2) {
        return result;
    }

    std::vector<double> distances(path.size(), 0.0);
    double totalLength = 0.0;
    for (std::size_t index = 1; index < path.size(); ++index) {
        totalLength += Length3(path[index] - path[index - 1]);
        distances[index] = totalLength;
    }

    if (totalLength <= 1.0e-6) {
        result.assign(static_cast<std::size_t>(count), path.front());
        return result;
    }

    result.reserve(static_cast<std::size_t>(count));
    const int targetCount = closed ? count : std::max(2, count);
    for (int index = 0; index < targetCount; ++index) {
        const double t = closed
            ? static_cast<double>(index) / static_cast<double>(targetCount)
            : static_cast<double>(index) / static_cast<double>(targetCount - 1);
        const double targetDistance = totalLength * t;
        auto upper = std::lower_bound(distances.begin(), distances.end(), targetDistance);
        if (upper == distances.begin()) {
            result.push_back(path.front());
            continue;
        }
        if (upper == distances.end()) {
            result.push_back(path.back());
            continue;
        }

        const std::size_t upperIndex = static_cast<std::size_t>(upper - distances.begin());
        const std::size_t lowerIndex = upperIndex - 1;
        const double span = std::max(1.0e-6, distances[upperIndex] - distances[lowerIndex]);
        const double localT = Clamp((targetDistance - distances[lowerIndex]) / span, 0.0, 1.0);
        result.push_back(path[lowerIndex] + (path[upperIndex] - path[lowerIndex]) * localT);
    }

    return result;
}

std::vector<Vec2> BuildProfile(const SegmentMode, const int sides) {
    std::vector<Vec2> profile;
    const int clampedSides = std::max(3, sides);
    profile.reserve(static_cast<std::size_t>(clampedSides));
    for (int index = 0; index < clampedSides; ++index) {
        const double angle = (static_cast<double>(index) / static_cast<double>(clampedSides)) * kPi * 2.0;
        profile.push_back({std::cos(angle), std::sin(angle)});
    }
    return profile;
}

void RotateBasis(
    Vec3& axisX,
    Vec3& axisY,
    Vec3& axisZ,
    const double rotateXDegrees,
    const double rotateYDegrees,
    const double rotateZDegrees) {
    if (std::abs(rotateXDegrees) > 1.0e-6) {
        const double radians = DegreesToRadians(rotateXDegrees);
        axisY = RotateAroundAxis(axisY, axisX, radians);
        axisZ = RotateAroundAxis(axisZ, axisX, radians);
    }
    if (std::abs(rotateYDegrees) > 1.0e-6) {
        const double radians = DegreesToRadians(rotateYDegrees);
        axisX = RotateAroundAxis(axisX, axisY, radians);
        axisZ = RotateAroundAxis(axisZ, axisY, radians);
    }
    if (std::abs(rotateZDegrees) > 1.0e-6) {
        const double radians = DegreesToRadians(rotateZDegrees);
        axisX = RotateAroundAxis(axisX, axisZ, radians);
        axisY = RotateAroundAxis(axisY, axisZ, radians);
    }
}

void BuildFrame(
    const Vec3& tangent,
    const SegmentSettings& segment,
    Vec3& axisX,
    Vec3& axisY,
    Vec3& axisZ) {
    axisZ = segment.orientToPath ? Normalize(tangent) : AxisVector(segment.orientReferenceAxis);
    if (Length3(axisZ) < 1.0e-6) {
        axisZ = {0.0, 0.0, 1.0};
    }

    Vec3 reference = AxisVector(segment.orientReferenceAxis);
    if (Length3(Cross(reference, axisZ)) < 1.0e-4) {
        reference = std::abs(axisZ.y) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0};
    }

    axisX = Normalize(Cross(reference, axisZ));
    axisY = Normalize(Cross(axisZ, axisX));
}

void FillTriangle(
    std::vector<std::uint32_t>& pixels,
    const int width,
    const int height,
    const SoftwareRenderer::ProjectedPoint& a,
    const SoftwareRenderer::ProjectedPoint& b,
    const SoftwareRenderer::ProjectedPoint& c,
    const Color& color,
    const double alpha) {
    const double minX = std::min({a.x, b.x, c.x});
    const double maxX = std::max({a.x, b.x, c.x});
    const double minY = std::min({a.y, b.y, c.y});
    const double maxY = std::max({a.y, b.y, c.y});

    const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(maxX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(maxY)));

    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 1.0e-6) {
        return;
    }

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w0 = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
            const double w1 = (c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x);
            const double w2 = (a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x);
            const bool sameSign = (area > 0.0) ? (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0) : (w0 <= 0.0 && w1 <= 0.0 && w2 <= 0.0);
            if (sameSign) {
                SoftwareRenderer::Plot(pixels, width, height, x, y, color, alpha);
            }
        }
    }
}

void WriteDepthSample(std::vector<float>& depthBuffer, const int width, const int height, const int x, const int y, const float depth) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(y * width + x);
    depthBuffer[index] = std::min(depthBuffer[index], std::clamp(depth, 0.0f, 1.0f));
}

void RasterizeDepthTriangle(
    std::vector<float>& depthBuffer,
    const int width,
    const int height,
    const SoftwareRenderer::ProjectedPoint& a,
    const SoftwareRenderer::ProjectedPoint& b,
    const SoftwareRenderer::ProjectedPoint& c,
    const CameraState& camera) {
    const double minX = std::min({a.x, b.x, c.x});
    const double maxX = std::max({a.x, b.x, c.x});
    const double minY = std::min({a.y, b.y, c.y});
    const double maxY = std::max({a.y, b.y, c.y});
    const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(maxX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(maxY)));
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 1.0e-6) {
        return;
    }

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w0 = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
            const double w1 = (c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x);
            const double w2 = (a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x);
            const bool sameSign = (area > 0.0) ? (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0) : (w0 <= 0.0 && w1 <= 0.0 && w2 <= 0.0);
            if (!sameSign) {
                continue;
            }

            const double alpha = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            const double beta = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            const double gamma = 1.0 - alpha - beta;
            const float depth = static_cast<float>(SoftwareRenderer::NormalizeProjectedDepth(alpha * a.depth + beta * b.depth + gamma * c.depth, camera));
            WriteDepthSample(depthBuffer, width, height, x, y, depth);
        }
    }
}

double DistancePointToSegment(const double px, const double py, const double ax, const double ay, const double bx, const double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1.0e-9) {
        const double ox = px - ax;
        const double oy = py - ay;
        return std::sqrt(ox * ox + oy * oy);
    }
    const double t = Clamp(((px - ax) * dx + (py - ay) * dy) / lengthSquared, 0.0, 1.0);
    const double cx = ax + dx * t;
    const double cy = ay + dy * t;
    const double ox = px - cx;
    const double oy = py - cy;
    return std::sqrt(ox * ox + oy * oy);
}

void RasterizeDepthLine(
    std::vector<float>& depthBuffer,
    const int width,
    const int height,
    const SoftwareRenderer::PathLine& line,
    const CameraState& camera) {
    const double radius = std::max(1.0, line.thickness * 0.5);
    const double minX = std::min(line.start.x, line.end.x) - radius;
    const double maxX = std::max(line.start.x, line.end.x) + radius;
    const double minY = std::min(line.start.y, line.end.y) - radius;
    const double maxY = std::max(line.start.y, line.end.y) + radius;
    const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(maxX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(maxY)));
    const float depth = static_cast<float>(SoftwareRenderer::NormalizeProjectedDepth(line.depth, camera));

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (DistancePointToSegment(
                x + 0.5,
                y + 0.5,
                line.start.x,
                line.start.y,
                line.end.x,
                line.end.y) <= radius) {
                WriteDepthSample(depthBuffer, width, height, x, y, depth);
            }
        }
    }
}

void RasterizeDepthPoint(
    std::vector<float>& depthBuffer,
    const int width,
    const int height,
    const SoftwareRenderer::PathPointSprite& point,
    const CameraState& camera) {
    const double radius = std::max(1.0, point.size * 0.5);
    const int x0 = std::max(0, static_cast<int>(std::floor(point.point.x - radius)));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(point.point.x + radius)));
    const int y0 = std::max(0, static_cast<int>(std::floor(point.point.y - radius)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(point.point.y + radius)));
    const float depth = static_cast<float>(SoftwareRenderer::NormalizeProjectedDepth(point.depth, camera));

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = (x + 0.5) - point.point.x;
            const double dy = (y + 0.5) - point.point.y;
            if (dx * dx + dy * dy <= radius * radius) {
                WriteDepthSample(depthBuffer, width, height, x, y, depth);
            }
        }
    }
}

Color UnpackBgra(const std::uint32_t pixel) {
    return {
        static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(pixel & 0xFFU),
        static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU)
    };
}

void EmitTriangle(
    std::vector<Triangle2D>& triangles,
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Color& baseColor,
    const MaterialSettings& material,
    const CameraState& camera,
    const int width,
    const int height) {
    const SoftwareRenderer::ProjectedPoint pa = SoftwareRenderer::Project(a, camera, width, height);
    const SoftwareRenderer::ProjectedPoint pb = SoftwareRenderer::Project(b, camera, width, height);
    const SoftwareRenderer::ProjectedPoint pc = SoftwareRenderer::Project(c, camera, width, height);
    if (!pa.visible || !pb.visible || !pc.visible) {
        return;
    }

    const Vec3 normal = Normalize(Cross(b - a, c - a));
    const Vec3 lightDir = Normalize(Vec3{-0.45, 0.8, 0.55});
    const Vec3 viewDir = Normalize(Vec3{0.0, 0.0, -1.0});
    const Vec3 halfVector = Normalize(lightDir + viewDir);
    const double lambert = std::abs(Dot(normal, lightDir));
    const double specular = std::pow(Clamp(std::abs(Dot(normal, halfVector)), 0.0, 1.0), 20.0);
    const double fresnel = std::pow(1.0 - Clamp(std::abs(Dot(normal, viewDir)), 0.0, 1.0), 3.0);
    const double depth = (pa.depth + pb.depth + pc.depth) / 3.0;
    const double depthFactor = Clamp(1.18 - depth / 14.0, 0.46, 1.0);
    const Color fill = ShadeMaterial(material, baseColor, lambert, depthFactor, specular, fresnel);
    const Color wire = Lerp(material.wireColor, material.accentColor, Clamp(specular * 0.45 + lambert * 0.2, 0.0, 0.55));

    triangles.push_back({
        {pa, pb, pc},
        fill,
        wire,
        material.wireColor,
        depth
    });
}

Vec3 BuildNoiseInput(
    const Vec3& center,
    const Vec2& profilePoint,
    const double t,
    const FractalDisplacementSettings& settings) {
    if (settings.space == FractalSpace::Local) {
        const double loopAngle = t * kPi * 2.0;
        return settings.seamlessLoop
            ? Vec3{std::cos(loopAngle) + profilePoint.x, std::sin(loopAngle) + profilePoint.y, settings.evolution}
            : Vec3{t * 6.0 + profilePoint.x, profilePoint.y, settings.evolution};
    }

    return {
        center.x + settings.offsetX,
        center.y + settings.offsetY,
        center.z + settings.offsetZ + settings.evolution
    };
}

std::vector<Vec3> BuildSectionVertices(
    const Vec3& center,
    const Vec3& tangent,
    const double t,
    const PathSettings& path,
    const std::vector<Vec2>& profile,
    const int sectionIndex) {
    Vec3 axisX {};
    Vec3 axisY {};
    Vec3 axisZ {};
    BuildFrame(tangent, path.segment, axisX, axisY, axisZ);
    RotateBasis(
        axisX,
        axisY,
        axisZ,
        path.segment.rotateX,
        path.segment.rotateY,
        path.segment.rotateZ + path.segment.twistZ * (t - 0.5) + path.twist * 35.0 * t);

    const double baseScale = std::max(0.02, path.thickness) * path.segment.size * 0.05;
    // Phase 1: Variable thickness profile
    const double profileFactor = CalculateThicknessProfile(path, t);
    const double scaleX = baseScale * (path.segment.sizeX / 100.0) * profileFactor;
    const double scaleY = baseScale * (path.segment.sizeY / 100.0) * profileFactor;
    const double randomness = path.segment.randomness;
    const double randomScale = 1.0 + (Fract(Hash1(sectionIndex * 3.0 + 5.0)) - 0.5) * randomness * 0.35;
    const double spikeBias = SpikeBias(path);

    std::vector<Vec3> vertices;
    vertices.reserve(profile.size());
    for (std::size_t pointIndex = 0; pointIndex < profile.size(); ++pointIndex) {
        const Vec2 point = profile[pointIndex];
        const double bladeFactor = 1.0 + std::pow(std::abs(point.x * point.y), 0.75) * spikeBias * 0.55;
        const Vec3 radial =
            (axisX * (point.x * scaleX * randomScale) + axisY * (point.y * scaleY * randomScale)) * bladeFactor;
        const Vec3 noiseInput = BuildNoiseInput(center, point, t, path.fractalDisplacement);
        const Vec3 vectorNoise = VectorNoise(noiseInput, path.fractalDisplacement);
        const double amplitude = (path.fractalDisplacement.amplitude / 100.0) * baseScale;
        const Vec3 shapedNoise {
            ShapeNoise(vectorNoise.x, path.fractalDisplacement.smoothenNormals),
            ShapeNoise(vectorNoise.y, path.fractalDisplacement.smoothenNormals),
            ShapeNoise(vectorNoise.z, path.fractalDisplacement.smoothenNormals)
        };
        // Phase 3: Tube warp - additional chaotic displacement perpendicular to path
        const double warpAmplitude = (path.segment.tubeWarp / 100.0) * baseScale;
        const double warpFreq = path.segment.tubeWarpFrequency * 10.0;
        const double warpSeed = t * warpFreq + sectionIndex * 0.5;
        const Vec3 warpOffset = warpAmplitude > 0.001
            ? axisX * std::sin(warpSeed) * warpAmplitude + axisY * std::cos(warpSeed * 1.3) * warpAmplitude
            : Vec3{0.0, 0.0, 0.0};
        const double spikeSeed = Fract(Hash1(sectionIndex * 31.0 + static_cast<double>(pointIndex) * 17.0 + t * 53.0));
        const double spikeThreshold = Clamp(0.78 - spikeBias * 0.17, 0.42, 0.78);
        const double spike = spikeSeed > spikeThreshold
            ? std::pow((spikeSeed - spikeThreshold) / std::max(1.0e-6, 1.0 - spikeThreshold), 2.0 + spikeBias * 0.8)
                * amplitude * (2.2 + spikeBias * 1.85)
            : 0.0;
        const Vec3 chaoticOffset =
            axisX * (shapedNoise.x * amplitude * (1.3 + spikeBias * 0.45)) +
            axisY * (shapedNoise.y * amplitude * (1.3 + spikeBias * 0.45)) +
            axisZ * (shapedNoise.z * amplitude * (1.15 + spikeBias * 0.6));
        const Vec3 spikeDirection = Normalize(
            axisX * (vectorNoise.x + 0.2) +
            axisY * (vectorNoise.y - 0.15) +
            axisZ * (vectorNoise.z + 0.35));
        vertices.push_back(center + radial + chaoticOffset + warpOffset + spikeDirection * spike);
    }
    return vertices;
}

void EmitCap(
    std::vector<Triangle2D>& triangles,
    const std::vector<Vec3>& ring,
    const Color& color,
    const MaterialSettings& material,
    const CameraState& camera,
    const int width,
    const int height,
    const bool reverse) {
    if (ring.size() < 3) {
        return;
    }

    Vec3 center {};
    for (const Vec3& point : ring) {
        center = center + point;
    }
    center = center * (1.0 / static_cast<double>(ring.size()));

    for (std::size_t index = 0; index < ring.size(); ++index) {
        const std::size_t next = (index + 1) % ring.size();
        if (reverse) {
            EmitTriangle(triangles, center, ring[next], ring[index], color, material, camera, width, height);
        } else {
            EmitTriangle(triangles, center, ring[index], ring[next], color, material, camera, width, height);
        }
    }
}

void EmitExtrudeGeometry(
    std::vector<Triangle2D>& triangles,
    const std::vector<std::vector<Vec3>>& rings,
    const PathSettings& path,
    const CameraState& camera,
    const int width,
    const int height) {
    if (rings.size() < 2 || rings.front().size() < 3) {
        return;
    }

    const bool closedPath = path.closed;
    const std::size_t ringCount = rings.size();
    const std::size_t sideCount = rings.front().size();
    const std::size_t connectionCount = closedPath ? ringCount : ringCount - 1;

    for (std::size_t ringIndex = 0; ringIndex < connectionCount; ++ringIndex) {
        const std::size_t nextRing = (ringIndex + 1) % ringCount;
        const Color color = MaterialBaseColor(path.material, static_cast<double>(ringIndex) / std::max<std::size_t>(1, connectionCount - 1));
        for (std::size_t side = 0; side < sideCount; ++side) {
            const std::size_t nextSide = (side + 1) % sideCount;
            Vec3 a = rings[ringIndex][side];
            Vec3 b = rings[ringIndex][nextSide];
            Vec3 c = rings[nextRing][nextSide];
            Vec3 d = rings[nextRing][side];

            if (path.segment.breakSides) {
                const Vec3 frontCenter = (a + b) * 0.5;
                const Vec3 backCenter = (d + c) * 0.5;
                const double gap = 0.18;
                a = a + (frontCenter - a) * gap;
                b = b + (frontCenter - b) * gap;
                c = c + (backCenter - c) * gap;
                d = d + (backCenter - d) * gap;
            }

            EmitTriangle(triangles, a, b, c, color, path.material, camera, width, height);
            EmitTriangle(triangles, a, c, d, color, path.material, camera, width, height);
        }
    }

    if (path.segment.caps && !closedPath && !path.segment.breakSides) {
        EmitCap(triangles, rings.front(), MaterialBaseColor(path.material, 0.0), path.material, camera, width, height, true);
        EmitCap(triangles, rings.back(), MaterialBaseColor(path.material, 1.0), path.material, camera, width, height, false);
    }
}

void EmitPrismGeometry(
    std::vector<Triangle2D>& triangles,
    const std::vector<Vec3>& front,
    const std::vector<Vec3>& back,
    const PathSettings& path,
    const Color& color,
    const CameraState& camera,
    const int width,
    const int height) {
    const std::size_t sideCount = front.size();
    if (sideCount < 3 || back.size() != sideCount) {
        return;
    }

    for (std::size_t side = 0; side < sideCount; ++side) {
        const std::size_t nextSide = (side + 1) % sideCount;
        Vec3 a = front[side];
        Vec3 b = front[nextSide];
        Vec3 c = back[nextSide];
        Vec3 d = back[side];

        if (path.segment.breakSides) {
            const Vec3 frontCenter = (a + b) * 0.5;
            const Vec3 backCenter = (d + c) * 0.5;
            const double gap = 0.18;
            a = a + (frontCenter - a) * gap;
            b = b + (frontCenter - b) * gap;
            c = c + (backCenter - c) * gap;
            d = d + (backCenter - d) * gap;
        }

        EmitTriangle(triangles, a, b, c, color, path.material, camera, width, height);
        EmitTriangle(triangles, a, c, d, color, path.material, camera, width, height);
    }

    if (path.segment.caps && !path.segment.breakSides) {
        EmitCap(triangles, front, color, path.material, camera, width, height, true);
        EmitCap(triangles, back, color, path.material, camera, width, height, false);
    }
}

void EmitSphereGeometry(
    std::vector<Triangle2D>& triangles,
    const Vec3& center,
    const Vec3& tangent,
    const double t,
    const PathSettings& path,
    const std::vector<Vec2>& profile,
    const int sectionIndex,
    const Color& color,
    const CameraState& camera,
    const int width,
    const int height) {
    Vec3 axisX {};
    Vec3 axisY {};
    Vec3 axisZ {};
    BuildFrame(tangent, path.segment, axisX, axisY, axisZ);
    RotateBasis(
        axisX,
        axisY,
        axisZ,
        path.segment.rotateX,
        path.segment.rotateY,
        path.segment.rotateZ + path.segment.twistZ * (t - 0.5));

    const double baseScale = std::max(0.02, path.thickness) * path.segment.size * 0.05;
    // Use thickness profile calculation for consistency
    const double profileFactor = CalculateThicknessProfile(path, t);
    const double radiusX = baseScale * (path.segment.sizeX / 100.0) * profileFactor;
    const double radiusY = baseScale * (path.segment.sizeY / 100.0) * profileFactor;
    const double radiusZ = baseScale * (path.segment.sizeZ / 100.0) * profileFactor;
    const double randomness = path.segment.randomness;
    const double randomScale = 1.0 + (Fract(Hash1(sectionIndex * 3.0 + 5.0)) - 0.5) * randomness * 0.35;
    const int latitudeBands = std::max(3, path.segment.sides / 2 + 2);
    const double spikeBias = SpikeBias(path);

    std::vector<std::vector<Vec3>> rings;
    rings.reserve(static_cast<std::size_t>(latitudeBands - 1));
    for (int latitude = 1; latitude < latitudeBands; ++latitude) {
        const double polar = kPi * static_cast<double>(latitude) / static_cast<double>(latitudeBands);
        const double ringScale = std::sin(polar);
        const double zOffset = std::cos(polar) * radiusZ * randomScale;

        std::vector<Vec3> ring;
        ring.reserve(profile.size());
        for (std::size_t pointIndex = 0; pointIndex < profile.size(); ++pointIndex) {
            const Vec2 point = profile[pointIndex];
            const double bladeFactor = 1.0 + std::pow(std::abs(point.x * point.y), 0.7) * spikeBias * 0.5;
            const Vec3 radial =
                (axisX * (point.x * radiusX * ringScale * randomScale)
                    + axisY * (point.y * radiusY * ringScale * randomScale))
                * bladeFactor;
            const Vec2 noiseProfile {point.x * ringScale, point.y * ringScale};
            const Vec3 noiseInput = BuildNoiseInput(center + axisZ * zOffset, noiseProfile, t + polar * 0.05, path.fractalDisplacement);
            const Vec3 vectorNoise = VectorNoise(noiseInput, path.fractalDisplacement);
            const double amplitude = (path.fractalDisplacement.amplitude / 100.0) * baseScale;
            const Vec3 shapedNoise {
                ShapeNoise(vectorNoise.x, path.fractalDisplacement.smoothenNormals),
                ShapeNoise(vectorNoise.y, path.fractalDisplacement.smoothenNormals),
                ShapeNoise(vectorNoise.z, path.fractalDisplacement.smoothenNormals)
            };
            const double spikeSeed = Fract(Hash1(sectionIndex * 47.0 + static_cast<double>(pointIndex) * 23.0 + polar * 37.0));
            const double spikeThreshold = Clamp(0.74 - spikeBias * 0.18, 0.40, 0.74);
            const double spike = spikeSeed > spikeThreshold
                ? std::pow((spikeSeed - spikeThreshold) / std::max(1.0e-6, 1.0 - spikeThreshold), 2.0 + spikeBias * 0.8)
                    * amplitude * (2.5 + spikeBias * 2.0)
                : 0.0;
            const Vec3 chaoticOffset =
                axisX * (shapedNoise.x * amplitude * (1.35 + spikeBias * 0.45)) +
                axisY * (shapedNoise.y * amplitude * (1.35 + spikeBias * 0.45)) +
                axisZ * (shapedNoise.z * amplitude * (1.2 + spikeBias * 0.7));
            const Vec3 spikeDirection = Normalize(
                axisX * (vectorNoise.x - 0.1) +
                axisY * (vectorNoise.y + 0.25) +
                axisZ * (vectorNoise.z + 0.35));
            ring.push_back(center + axisZ * zOffset + radial + chaoticOffset + spikeDirection * spike);
        }
        rings.push_back(std::move(ring));
    }

    const Vec3 top = center + axisZ * (radiusZ * randomScale);
    const Vec3 bottom = center - axisZ * (radiusZ * randomScale);

    if (!rings.empty()) {
        const std::vector<Vec3>& firstRing = rings.front();
        for (std::size_t side = 0; side < firstRing.size(); ++side) {
            const std::size_t nextSide = (side + 1) % firstRing.size();
            EmitTriangle(triangles, top, firstRing[side], firstRing[nextSide], color, path.material, camera, width, height);
        }
    }

    for (std::size_t ringIndex = 0; ringIndex + 1 < rings.size(); ++ringIndex) {
        const std::vector<Vec3>& front = rings[ringIndex];
        const std::vector<Vec3>& back = rings[ringIndex + 1];
        for (std::size_t side = 0; side < front.size(); ++side) {
            const std::size_t nextSide = (side + 1) % front.size();
            EmitTriangle(triangles, front[side], front[nextSide], back[nextSide], color, path.material, camera, width, height);
            EmitTriangle(triangles, front[side], back[nextSide], back[side], color, path.material, camera, width, height);
        }
    }

    if (!rings.empty()) {
        const std::vector<Vec3>& lastRing = rings.back();
        for (std::size_t side = 0; side < lastRing.size(); ++side) {
            const std::size_t nextSide = (side + 1) % lastRing.size();
            EmitTriangle(triangles, bottom, lastRing[nextSide], lastRing[side], color, path.material, camera, width, height);
        }
    }
}

void RenderPerspectiveGrid(
    std::vector<std::uint32_t>& pixels,
    const int width,
    const int height,
    const CameraState& camera) {
    constexpr int kGridHalfCount = 18;
    constexpr int kGridSamples = 48;
    constexpr double kGridExtent = 18.0;
    constexpr double kGridY = -2.4;

    const auto drawGridLine = [&](const Vec3& start, const Vec3& end, const Color& color, const double alpha, const double thickness) {
        SoftwareRenderer::ProjectedPoint previous = SoftwareRenderer::Project(start, camera, width, height);
        for (int sampleIndex = 1; sampleIndex <= kGridSamples; ++sampleIndex) {
            const double t = static_cast<double>(sampleIndex) / static_cast<double>(kGridSamples);
            const Vec3 currentWorld = start + (end - start) * t;
            const SoftwareRenderer::ProjectedPoint current = SoftwareRenderer::Project(currentWorld, camera, width, height);
            if (previous.visible && current.visible) {
                const double averageDepth = (previous.depth + current.depth) * 0.5;
                const double depthFade = Clamp(1.25 - averageDepth / 20.0, 0.10, 1.0);
                SoftwareRenderer::DrawLineAA(
                    pixels,
                    width,
                    height,
                    previous.x,
                    previous.y,
                    current.x,
                    current.y,
                    color,
                    alpha * depthFade,
                    thickness * (0.65 + depthFade * 0.55));
            }
            previous = current;
        }
    };

    for (int index = -kGridHalfCount; index <= kGridHalfCount; ++index) {
        const bool majorLine = (index % 4) == 0;
        const double coordinate = static_cast<double>(index);
        drawGridLine(
            {coordinate, kGridY, -kGridExtent},
            {coordinate, kGridY, kGridExtent},
            majorLine ? Color{48, 56, 72, 255} : Color{30, 35, 44, 255},
            majorLine ? 0.46 : 0.22,
            majorLine ? 1.25 : 0.90);
        drawGridLine(
            {-kGridExtent, kGridY, coordinate},
            {kGridExtent, kGridY, coordinate},
            majorLine ? Color{48, 56, 72, 255} : Color{30, 35, 44, 255},
            majorLine ? 0.46 : 0.22,
            majorLine ? 1.25 : 0.90);
    }

    drawGridLine({-kGridExtent, kGridY, 0.0}, {kGridExtent, kGridY, 0.0}, {76, 110, 170, 255}, 0.62, 1.55);
    drawGridLine({0.0, kGridY, -kGridExtent}, {0.0, kGridY, kGridExtent}, {94, 132, 194, 255}, 0.72, 1.75);
}

}  // namespace

void SoftwareRenderer::InvalidateAccumulation() {
    flameEngine_.ResetTemporalState();
}

void SoftwareRenderer::Clear(std::vector<std::uint32_t>& pixels, const int width, const int height, const Color& color) {
    pixels.assign(static_cast<std::size_t>(width * height), ToBgra(color));
}

void SoftwareRenderer::Plot(
    std::vector<std::uint32_t>& pixels,
    const int width,
    const int height,
    const int x,
    const int y,
    const Color& color,
    const double alpha) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(y * width + x);
    const std::uint32_t existing = pixels[index];
    const Color base {
        static_cast<std::uint8_t>((existing >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((existing >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(existing & 0xFFU),
        static_cast<std::uint8_t>((existing >> 24U) & 0xFFU)
    };
    pixels[index] = ToBgra(Lerp(base, color, Clamp(alpha, 0.0, 1.0)));
}

void SoftwareRenderer::DrawLine(
    std::vector<std::uint32_t>& pixels,
    const int width,
    const int height,
    int x0,
    int y0,
    const int x1,
    const int y1,
    const Color& color,
    const double alpha) {
    const int deltaX = std::abs(x1 - x0);
    const int deltaY = -std::abs(y1 - y0);
    const int stepX = x0 < x1 ? 1 : -1;
    const int stepY = y0 < y1 ? 1 : -1;
    int error = deltaX + deltaY;

    for (;;) {
        Plot(pixels, width, height, x0, y0, color, alpha);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int error2 = error * 2;
        if (error2 >= deltaY) {
            error += deltaY;
            x0 += stepX;
        }
        if (error2 <= deltaX) {
            error += deltaX;
            y0 += stepY;
        }
    }
}

void SoftwareRenderer::DrawLineAA(
    std::vector<std::uint32_t>& pixels,
    const int width,
    const int height,
    const double x0,
    const double y0,
    const double x1,
    const double y1,
    const Color& color,
    const double alpha,
    const double thickness) {
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.5) {
        return;
    }

    const double nx = -dy / length;
    const double ny = dx / length;
    const double halfThickness = thickness * 0.5;
    const int steps = static_cast<int>(std::ceil(length));

    for (int step = 0; step <= steps; ++step) {
        const double t = static_cast<double>(step) / static_cast<double>(std::max(1, steps));
        const double cx = x0 + dx * t;
        const double cy = y0 + dy * t;
        const int span = static_cast<int>(std::ceil(halfThickness)) + 1;
        for (int offsetIndex = -span; offsetIndex <= span; ++offsetIndex) {
            const double offset = static_cast<double>(offsetIndex);
            const double distance = std::abs(offset);
            if (distance > halfThickness + 1.0) {
                continue;
            }

            const double falloff = Clamp(1.0 - (distance - halfThickness + 0.5), 0.0, 1.0);
            Plot(
                pixels,
                width,
                height,
                static_cast<int>(std::round(cx + nx * offset)),
                static_cast<int>(std::round(cy + ny * offset)),
                color,
                alpha * falloff);
        }
    }
}

void SoftwareRenderer::BuildPathPrimitives(
    const Scene& scene,
    const int width,
    const int height,
    std::vector<PathTriangle>& fillTriangles,
    std::vector<PathLine>& lines,
    std::vector<PathPointSprite>& points) {
    fillTriangles.clear();
    lines.clear();
    points.clear();

    if (width <= 0 || height <= 0 || scene.mode == SceneMode::Flame) {
        return;
    }

    SplinePath splinePath;
    for (const PathSettings& path : scene.paths) {
        const std::vector<Vec3> densePath = splinePath.Sample(path, scene.timelineSeconds);
        if (densePath.size() < 2) {
            continue;
        }

        const bool closedPath = path.closed;
        const int sectionCount = std::max(
            2,
            path.segment.mode == SegmentMode::ExtrudeNGon
                ? (closedPath ? path.segment.segments : path.segment.segments + 1)
                : path.segment.segments);
        const std::vector<Vec3> centers = ResamplePath(densePath, sectionCount, closedPath);
        if (centers.size() < 2) {
            continue;
        }

        const std::vector<Vec2> profile = BuildProfile(path.segment.mode, path.segment.sides);
        std::vector<Triangle2D> triangles;

        if (path.segment.mode == SegmentMode::ExtrudeNGon) {
            std::vector<std::vector<Vec3>> rings;
            rings.reserve(centers.size());
            const std::size_t lastIndex = centers.size() - 1;
            for (std::size_t index = 0; index < centers.size(); ++index) {
                const Vec3 previous = centers[index == 0 ? (closedPath ? lastIndex : 0) : index - 1];
                const Vec3 next = centers[index == lastIndex ? (closedPath ? 0 : lastIndex) : index + 1];
                const double t = static_cast<double>(index) / std::max<std::size_t>(1, centers.size() - 1);
                std::vector<Vec3> ring = BuildSectionVertices(centers[index], next - previous, t, path, profile, static_cast<int>(index));
                if (path.segment.chamfer && !closedPath && (index == 0 || index == lastIndex)) {
                    const Vec3 center = centers[index];
                    const double shrink = 1.0 - path.segment.chamferSize / 100.0 * 0.45;
                    for (Vec3& vertex : ring) {
                        vertex = center + (vertex - center) * shrink;
                    }
                }
                rings.push_back(std::move(ring));
            }

            EmitExtrudeGeometry(triangles, rings, path, scene.camera, width, height);

            if (path.segment.debugNormals) {
                for (std::size_t index = 0; index < centers.size(); ++index) {
                    const Vec3 previous = centers[index == 0 ? (closedPath ? lastIndex : 0) : index - 1];
                    const Vec3 next = centers[index == lastIndex ? (closedPath ? 0 : lastIndex) : index + 1];
                    Vec3 axisX {};
                    Vec3 axisY {};
                    Vec3 axisZ {};
                    BuildFrame(next - previous, path.segment, axisX, axisY, axisZ);
                    RotateBasis(axisX, axisY, axisZ, path.segment.rotateX, path.segment.rotateY, path.segment.rotateZ);
                    const ProjectedPoint start = Project(centers[index], scene.camera, width, height);
                    const ProjectedPoint end = Project(centers[index] + axisY * 0.8, scene.camera, width, height);
                    if (start.visible && end.visible) {
                        lines.push_back({start, end, {220, 220, 220, 255}, 0.65, 1.5, (start.depth + end.depth) * 0.5});
                    }
                }
            }
        } else {
            for (std::size_t index = 0; index < centers.size(); ++index) {
                const Vec3 previous = centers[index == 0 ? 0 : index - 1];
                const Vec3 next = centers[index == centers.size() - 1 ? centers.size() - 1 : index + 1];
                const double t = centers.size() == 1 ? 0.0 : static_cast<double>(index) / static_cast<double>(centers.size() - 1);
                const Vec3 tangent = next - previous;
                const std::vector<Vec3> ring = BuildSectionVertices(centers[index], tangent, t, path, profile, static_cast<int>(index));

                Vec3 axisX {};
                Vec3 axisY {};
                Vec3 axisZ {};
                BuildFrame(tangent, path.segment, axisX, axisY, axisZ);
                RotateBasis(axisX, axisY, axisZ, path.segment.rotateX, path.segment.rotateY, path.segment.rotateZ + path.segment.twistZ * (t - 0.5));

                const double halfDepth = std::max(0.04, path.thickness * path.segment.size * (path.segment.sizeZ / 100.0) * 0.008);
                std::vector<Vec3> front = ring;
                std::vector<Vec3> back = ring;
                for (std::size_t vertexIndex = 0; vertexIndex < ring.size(); ++vertexIndex) {
                    front[vertexIndex] = ring[vertexIndex] - axisZ * halfDepth;
                    back[vertexIndex] = ring[vertexIndex] + axisZ * halfDepth;
                }

                if (path.segment.chamfer) {
                    const double shrink = 1.0 - path.segment.chamferSize / 100.0 * 0.25;
                    for (std::size_t vertexIndex = 0; vertexIndex < ring.size(); ++vertexIndex) {
                        front[vertexIndex] = centers[index] - axisZ * halfDepth + (front[vertexIndex] - (centers[index] - axisZ * halfDepth)) * shrink;
                        back[vertexIndex] = centers[index] + axisZ * halfDepth + (back[vertexIndex] - (centers[index] + axisZ * halfDepth)) * shrink;
                    }
                }

                if (path.segment.mode == SegmentMode::RepeatSphere) {
                    EmitSphereGeometry(
                        triangles,
                        centers[index],
                        tangent,
                        t,
                        path,
                        profile,
                        static_cast<int>(index),
                        MaterialBaseColor(path.material, t),
                        scene.camera,
                        width,
                        height);
                } else {
                    EmitPrismGeometry(
                        triangles,
                        front,
                        back,
                        path,
                        MaterialBaseColor(path.material, t),
                        scene.camera,
                        width,
                        height);
                }
            }
        }

        std::sort(triangles.begin(), triangles.end(), [](const Triangle2D& left, const Triangle2D& right) {
            return left.depth > right.depth;
        });

        // Phase 4: Emit tendrils (branching child paths)
        if (path.segment.tendrilCount > 0 && centers.size() >= 2) {
            const int tendrilCount = std::max(1, std::min(path.segment.tendrilCount, 20));
            const double tendrilLength = path.segment.tendrilLength;
            const double tendrilThickness = path.segment.tendrilThickness;
            const double tendrilWarp = path.segment.tendrilWarp;
            
            std::mt19937 tendrilRng(static_cast<unsigned int>(centers.size() * 12345));
            std::uniform_real_distribution<double> rngT(0.2, 0.8);
            std::uniform_real_distribution<double> rngAngle(0.0, kPi * 2.0);
            std::uniform_real_distribution<double> rngOffset(-0.5, 0.5);
            
            for (int tendrilIndex = 0; tendrilIndex < tendrilCount; ++tendrilIndex) {
                // Pick random point along path
                const double tStart = rngT(tendrilRng);
                const std::size_t startIdx = static_cast<std::size_t>(tStart * (centers.size() - 1));
                const Vec3& startPoint = centers[std::min(startIdx, centers.size() - 1)];
                
                // Calculate tangent at start point
                Vec3 tangent;
                if (startIdx == 0) {
                    tangent = centers[1] - centers[0];
                } else if (startIdx >= centers.size() - 1) {
                    tangent = centers.back() - centers[centers.size() - 2];
                } else {
                    tangent = centers[startIdx + 1] - centers[startIdx - 1];
                }
                
                // Build orthogonal frame for tendril direction
                Vec3 tAxisX {}, tAxisY {}, tAxisZ {};
                BuildFrame(tangent, path.segment, tAxisX, tAxisY, tAxisZ);
                
                // Random direction perpendicular to path
                const double angle = rngAngle(tendrilRng);
                const Vec3 dir = Normalize(tAxisX * std::cos(angle) + tAxisY * std::sin(angle) + tAxisZ * 0.3);
                
                // Generate tendril curve points
                std::vector<Vec3> tendrilPoints;
                tendrilPoints.reserve(8);
                tendrilPoints.push_back(startPoint);
                
                Vec3 current = startPoint;
                Vec3 currentDir = dir;
                for (int step = 0; step < 7; ++step) {
                    const double stepT = static_cast<double>(step) / 7.0;
                    // Add warp to direction
                    const double warpX = std::sin(stepT * kPi * 2.0 * tendrilWarp + tendrilIndex) * tendrilWarp;
                    const double warpY = std::cos(stepT * kPi * 1.7 * tendrilWarp + tendrilIndex) * tendrilWarp;
                    currentDir = Normalize(currentDir + tAxisX * warpX + tAxisY * warpY);
                    
                    const double stepLen = tendrilLength * (1.0 - stepT * 0.3);  // Taper length
                    current = current + currentDir * stepLen * 0.3;
                    tendrilPoints.push_back(current);
                }
                
                // Render tendril as thin tube
                if (tendrilPoints.size() >= 2) {
                    PathSettings tendrilPath = path;
                    tendrilPath.controlPoints = tendrilPoints;
                    tendrilPath.closed = false;
                    tendrilPath.thickness = path.thickness * tendrilThickness * 0.5;
                    tendrilPath.taper = 0.6;  // Strong taper for tendrils
                    tendrilPath.segment.junctionSize = 0.0;  // No junctions on tendrils
                    tendrilPath.segment.tendrilCount = 0;  // No recursive tendrils
                    tendrilPath.segment.mode = SegmentMode::ExtrudeNGon;
                    tendrilPath.segment.sides = std::max(3, path.segment.sides / 2);
                    tendrilPath.segment.segments = std::max(4, path.segment.segments / 2);
                    
                    // Sample tendril path
                    const std::vector<Vec3> tendrilDense = splinePath.Sample(tendrilPath, scene.timelineSeconds);
                    const int tendrilSectionCount = std::max(3, tendrilPath.segment.segments + 1);
                    const std::vector<Vec3> tendrilCenters = ResamplePath(tendrilDense, tendrilSectionCount, false);
                    
                    if (tendrilCenters.size() >= 2) {
                        std::vector<std::vector<Vec3>> tendrilRings;
                        tendrilRings.reserve(tendrilCenters.size());
                        const std::size_t tLastIdx = tendrilCenters.size() - 1;
                        
                        for (std::size_t tIdx = 0; tIdx < tendrilCenters.size(); ++tIdx) {
                            const Vec3 tPrev = tendrilCenters[tIdx == 0 ? 0 : tIdx - 1];
                            const Vec3 tNext = tendrilCenters[tIdx == tLastIdx ? tLastIdx : tIdx + 1];
                            const double tt = static_cast<double>(tIdx) / std::max<std::size_t>(1, tendrilCenters.size() - 1);
                            std::vector<Vec3> tRing = BuildSectionVertices(
                                tendrilCenters[tIdx], 
                                tNext - tPrev, 
                                tt, 
                                tendrilPath, 
                                profile, 
                                static_cast<int>(tIdx) + tendrilIndex * 100);
                            tendrilRings.push_back(std::move(tRing));
                        }
                        
                        EmitExtrudeGeometry(triangles, tendrilRings, tendrilPath, scene.camera, width, height);
                    }
                }
            }
        }
        if (path.segment.junctionSize > 0.01 && !path.controlPoints.empty()) {
            const double junctionRadius = path.segment.junctionSize * path.thickness * path.segment.size * 0.08;
            const double blendFactor = path.segment.junctionBlend;
            for (std::size_t cpIndex = 0; cpIndex < path.controlPoints.size(); ++cpIndex) {
                // Find closest point on resampled path to control point
                double closestDist = 1e10;
                std::size_t closestIndex = 0;
                for (std::size_t centerIndex = 0; centerIndex < centers.size(); ++centerIndex) {
                    const double dist = Length3(centers[centerIndex] - path.controlPoints[cpIndex]);
                    if (dist < closestDist) {
                        closestDist = dist;
                        closestIndex = centerIndex;
                    }
                }
                
                const double t = static_cast<double>(closestIndex) / std::max<std::size_t>(1, centers.size() - 1);
                const Vec3& junctionCenter = path.controlPoints[cpIndex];
                
                // Calculate tangent at junction for proper orientation
                Vec3 tangent;
                if (closestIndex == 0) {
                    tangent = centers[1] - centers[0];
                } else if (closestIndex >= centers.size() - 1) {
                    tangent = centers[centers.size() - 1] - centers[centers.size() - 2];
                } else {
                    tangent = centers[closestIndex + 1] - centers[closestIndex - 1];
                }
                
                // Blend junction sphere size with tube size
                const double tubeThicknessAtPoint = CalculateThicknessProfile(path, t) * path.thickness;
                const double effectiveRadius = std::max(junctionRadius, tubeThicknessAtPoint * blendFactor);
                
                // Create a temporary path with inflated thickness for junction
                PathSettings junctionPath = path;
                junctionPath.thickness = effectiveRadius / (path.segment.size * 0.05);
                junctionPath.segment.mode = SegmentMode::RepeatSphere;
                
                EmitSphereGeometry(
                    triangles,
                    junctionCenter,
                    tangent,
                    t,
                    junctionPath,
                    profile,
                    static_cast<int>(cpIndex) + 1000,  // Offset to avoid hash collision
                    MaterialBaseColor(path.material, static_cast<double>(cpIndex) / path.controlPoints.size()),
                    scene.camera,
                    width,
                    height);
            }
        }

        const bool lineOnly = path.material.renderMode == PathRenderMode::Wireframe
            || path.segment.tessellate == TessellationMode::Lines;
        const bool pointsOnly = path.material.renderMode == PathRenderMode::Points;
        const bool drawFill = !lineOnly && !pointsOnly && path.material.renderMode != PathRenderMode::Wireframe;
        const bool drawWire = path.material.renderMode == PathRenderMode::SolidWire
            || path.material.renderMode == PathRenderMode::Wireframe
            || path.segment.tessellate == TessellationMode::Lines;
        for (const Triangle2D& triangle : triangles) {
            if (drawFill) {
                fillTriangles.push_back({triangle.points, triangle.fill, 0.92, triangle.depth});
            }
            if (drawWire) {
                lines.push_back({triangle.points[0], triangle.points[1], triangle.wire, 0.45, 1.2, triangle.depth});
                lines.push_back({triangle.points[1], triangle.points[2], triangle.wire, 0.45, 1.2, triangle.depth});
                lines.push_back({triangle.points[2], triangle.points[0], triangle.wire, 0.45, 1.2, triangle.depth});
            }
            if (pointsOnly) {
                for (const ProjectedPoint& point : triangle.points) {
                    points.push_back({point, path.material.wireColor, 0.95, path.material.pointSize, triangle.depth});
                }
            }
        }
    }

    std::sort(fillTriangles.begin(), fillTriangles.end(), [](const PathTriangle& left, const PathTriangle& right) {
        return left.depth > right.depth;
    });
    std::sort(lines.begin(), lines.end(), [](const PathLine& left, const PathLine& right) {
        return left.depth > right.depth;
    });
    std::sort(points.begin(), points.end(), [](const PathPointSprite& left, const PathPointSprite& right) {
        return left.depth > right.depth;
    });
}

void SoftwareRenderer::BuildDepthMap(const Scene& scene, const int width, const int height, std::vector<float>& depthBuffer) {
    depthBuffer.assign(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), 1.0f);
    if (width <= 0 || height <= 0) {
        return;
    }

    if (scene.mode != SceneMode::Path) {
        IFSEngine flameEngine;
        std::vector<FlamePixel> flamePixels;
        flameEngine.Render(scene, width, height, flamePixels);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y * width + x);
                if (index >= depthBuffer.size() || index >= flamePixels.size()) {
                    continue;
                }
                const FlamePixel pixel = ReconstructFlamePixel(flamePixels, width, height, x, y);
                if (pixel.density > 0.0f) {
                    depthBuffer[index] = std::min(depthBuffer[index], std::clamp(pixel.depth / pixel.density, 0.0f, 1.0f));
                }
            }
        }
    }

    if (scene.mode == SceneMode::Flame) {
        return;
    }

    std::vector<PathTriangle> fillTriangles;
    std::vector<PathLine> lines;
    std::vector<PathPointSprite> points;
    BuildPathPrimitives(scene, width, height, fillTriangles, lines, points);
    for (const PathTriangle& triangle : fillTriangles) {
        RasterizeDepthTriangle(depthBuffer, width, height, triangle.points[0], triangle.points[1], triangle.points[2], scene.camera);
    }
    for (const PathLine& line : lines) {
        RasterizeDepthLine(depthBuffer, width, height, line, scene.camera);
    }
    for (const PathPointSprite& point : points) {
        RasterizeDepthPoint(depthBuffer, width, height, point, scene.camera);
    }
}

void SoftwareRenderer::ApplyDepthOfField(
    const Scene& scene,
    std::vector<std::uint32_t>& pixels,
    const int width,
    const int height,
    const std::vector<float>& depthBuffer) {
    if (!scene.depthOfField.enabled || width <= 0 || height <= 0 || pixels.empty() || depthBuffer.size() != pixels.size()) {
        return;
    }

    const int maxRadius = std::clamp(static_cast<int>(std::round(scene.depthOfField.blurStrength * 12.0)), 1, 12);
    const float focusDepth = static_cast<float>(scene.depthOfField.focusDepth);
    const float focusRange = std::max(0.01f, static_cast<float>(scene.depthOfField.focusRange));
    const float blurStrength = static_cast<float>(std::clamp(scene.depthOfField.blurStrength, 0.0, 1.0));
    const std::vector<std::uint32_t> source = pixels;
    static const std::array<Vec2, 8> kBlurOffsets = {
        Vec2{1.0, 0.0},
        Vec2{-1.0, 0.0},
        Vec2{0.0, 1.0},
        Vec2{0.0, -1.0},
        Vec2{0.707, 0.707},
        Vec2{-0.707, 0.707},
        Vec2{0.707, -0.707},
        Vec2{-0.707, -0.707}
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            const float depth = depthBuffer[index];
            const float blurAmount = std::clamp((std::abs(depth - focusDepth) - focusRange) / std::max(0.02f, 1.0f - focusRange), 0.0f, 1.0f) * blurStrength;
            if (blurAmount <= 0.001f) {
                continue;
            }

            const float radius = std::max(1.0f, blurAmount * static_cast<float>(maxRadius));
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 0.0f;
            float weightSum = 0.0f;

            const auto accumulate = [&](const int sx, const int sy, const float weight) {
                const int clampedX = std::clamp(sx, 0, width - 1);
                const int clampedY = std::clamp(sy, 0, height - 1);
                const Color color = UnpackBgra(source[static_cast<std::size_t>(clampedY * width + clampedX)]);
                r += static_cast<float>(color.r) * weight;
                g += static_cast<float>(color.g) * weight;
                b += static_cast<float>(color.b) * weight;
                a += static_cast<float>(color.a) * weight;
                weightSum += weight;
            };

            accumulate(x, y, 2.0f);
            for (const Vec2& offset : kBlurOffsets) {
                accumulate(
                    static_cast<int>(std::round(static_cast<double>(x) + offset.x * radius)),
                    static_cast<int>(std::round(static_cast<double>(y) + offset.y * radius)),
                    1.0f);
            }

            if (weightSum > 0.0f) {
                pixels[index] = ToBgra({
                    static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(r / weightSum)), 0, 255)),
                    static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(g / weightSum)), 0, 255)),
                    static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(b / weightSum)), 0, 255)),
                    static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(a / weightSum)), 0, 255))
                });
            }
        }
    }
}

void SoftwareRenderer::ApplyDenoising(
    const Scene& scene,
    std::vector<std::uint32_t>& pixels,
    const int width,
    const int height,
    const std::vector<float>& depthBuffer,
    const std::function<bool()>& shouldAbort) {
    if (!scene.denoiser.enabled || scene.denoiser.strength <= 0.0 || width <= 0 || height <= 0 || pixels.empty() || depthBuffer.size() != pixels.size()) {
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

    // Generate spatial weights once
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
        if (shouldAbort && shouldAbort()) {
            return;
        }

        // Ping-pong buffers
        const std::vector<std::uint32_t>& readBuffer = pass % 2 == 0 ? source : temp;
        std::vector<std::uint32_t>& writeBuffer = pass % 2 == 0 ? temp : source;

        #pragma omp parallel for schedule(dynamic)
        for (int y = 0; y < height; ++y) {
            if (shouldAbort && shouldAbort()) {
                continue;
            }
            for (int x = 0; x < width; ++x) {
                const std::size_t centerIndex = static_cast<std::size_t>(y * width + x);
                const Color centerColorRaw = UnpackBgra(readBuffer[centerIndex]);
                const float centerDepth = depthBuffer[centerIndex];
                
                const double cr = centerColorRaw.r / 255.0;
                const double cg = centerColorRaw.g / 255.0;
                const double cb = centerColorRaw.b / 255.0;
                const double ca = centerColorRaw.a / 255.0;

                // Pass 1: Local Mean and Variance
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
                        
                        // Only include actual flame pixels in the statistics to avoid biasing the mean towards the void
                        const double weight = spatialWeight * (sa > 0.01 ? 1.0 : 0.001);

                        sumR += sr * weight;
                        sumG += sg * weight;
                        sumB += sb * weight;
                        sumA += sa * weight;
                        
                        sumSqR += sr * sr * weight;
                        sumSqG += sg * sg * weight;
                        sumSqB += sb * sb * weight;
                        sumSqA += sa * sa * weight;

                        weightSum += weight;
                    }
                }

                const double meanR = sumR / weightSum;
                const double meanG = sumG / weightSum;
                const double meanB = sumB / weightSum;
                const double meanA = sumA / weightSum;

                const double varR = std::max(0.0, (sumSqR / weightSum) - (meanR * meanR));
                const double varG = std::max(0.0, (sumSqG / weightSum) - (meanG * meanG));
                const double varB = std::max(0.0, (sumSqB / weightSum) - (meanB * meanB));
                const double varA = std::max(0.0, (sumSqA / weightSum) - (meanA * meanA));

                const double devR = std::sqrt(varR);
                const double devG = std::sqrt(varG);
                const double devB = std::sqrt(varB);
                const double devA = std::sqrt(varA);

                const double minR = meanR - devR * thresholdMult;
                const double minG = meanG - devG * thresholdMult;
                const double minB = meanB - devB * thresholdMult;
                const double minA = meanA - devA * thresholdMult;

                const double maxR = meanR + devR * thresholdMult;
                const double maxG = meanG + devG * thresholdMult;
                const double maxB = meanB + devB * thresholdMult;
                const double maxA = meanA + devA * thresholdMult;

                const double clampedCr = Clamp(cr, minR, maxR);
                const double clampedCg = Clamp(cg, minG, maxG);
                const double clampedCb = Clamp(cb, minB, maxB);
                const double clampedCa = Clamp(ca, minA, maxA);

                // Pass 2: Blur with pre-clamped neighbors and depth edge-stopping
                double blurSumR = 0.0, blurSumG = 0.0, blurSumB = 0.0, blurSumA = 0.0;
                double blurWeightSum = 0.0;

                const int bMinY = std::max(0, y - blurRadius);
                const int bMaxY = std::min(height - 1, y + blurRadius);
                const int bMinX = std::max(0, x - blurRadius);
                const int bMaxX = std::min(width - 1, x + blurRadius);

                for (int sy = bMinY; sy <= bMaxY; ++sy) {
                    for (int sx = bMinX; sx <= bMaxX; ++sx) {
                        const std::size_t sampleIndex = static_cast<std::size_t>(sy * width + sx);
                        const Color sampleColorRaw = UnpackBgra(readBuffer[sampleIndex]);
                        const float sampleDepth = depthBuffer[sampleIndex];

                        // Clamp neighbor using the same local bounds to ignore its firefly status
                        const double sr = Clamp(sampleColorRaw.r / 255.0, minR, maxR);
                        const double sg = Clamp(sampleColorRaw.g / 255.0, minG, maxG);
                        const double sb = Clamp(sampleColorRaw.b / 255.0, minB, maxB);
                        const double sa = Clamp(sampleColorRaw.a / 255.0, minA, maxA);

                        const double spatialWeight = spatialWeightsSmall[static_cast<std::size_t>((sy - y + blurRadius) * (blurRadius * 2 + 1) + (sx - x + blurRadius))];
                        
                        const double depthDist = static_cast<double>(sampleDepth - centerDepth);
                        const double depthDistSq = depthDist * depthDist;
                        const double depthWeight = std::exp(-depthDistSq * invDepthVar);
                        
                        const double colorDistSq = (sr - clampedCr) * (sr - clampedCr) + (sg - clampedCg) * (sg - clampedCg) + (sb - clampedCb) * (sb - clampedCb);
                        const double colorWeight = std::exp(-colorDistSq * invColorVar);

                        const double weight = spatialWeight * depthWeight * colorWeight;

                        blurSumR += sr * weight;
                        blurSumG += sg * weight;
                        blurSumB += sb * weight;
                        blurSumA += sa * weight;
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
    }

    // Copy final result back to pixels
    pixels = passes % 2 == 1 ? temp : source;
}

SoftwareRenderer::ProjectedPoint SoftwareRenderer::Project(
    const Vec3& point,
    const CameraState& camera,
    const int width,
    const int height) {
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
        width * 0.5 + camera.panX + rotated.x * perspective,
        height * 0.5 + camera.panY - rotated.y * perspective,
        rotated.z,
        true
    };
}

double SoftwareRenderer::NormalizeProjectedDepth(const double depth, const CameraState& camera) {
    const double farDepth = std::max(kFlameDepthNear + 1.0, camera.distance + kFlameDepthRangePadding);
    return std::clamp((depth - kFlameDepthNear) / std::max(1.0e-6, farDepth - kFlameDepthNear), 0.0, 1.0);
}

Color SoftwareRenderer::ToneMap(const FlamePixel& pixel, const FlameRenderSettings& flameRender) {
    if (pixel.density <= 0.0f) {
        return {0, 0, 0, 0};
    }

    const double exposure = Clamp(flameRender.curveExposure, 0.25, 3.0);
    const double contrast = Clamp(flameRender.curveContrast, 0.45, 2.2);
    const double highlights = Clamp(flameRender.curveHighlights, 0.0, 2.0);
    const double gamma = Clamp(flameRender.curveGamma, 0.45, 1.8);
    const double logDensity = std::log1p(static_cast<double>(pixel.density));
    const double intensity = std::pow(Clamp((logDensity * exposure) / 4.05, 0.0, 1.72 + highlights * 0.18), 1.04);
    const double divisor = std::max(1.0f, pixel.density);
    const double rawR = Clamp((pixel.red / divisor) / 255.0, 0.0, 1.0);
    const double rawG = Clamp((pixel.green / divisor) / 255.0, 0.0, 1.0);
    const double rawB = Clamp((pixel.blue / divisor) / 255.0, 0.0, 1.0);
    const double maxChannel = std::max({rawR, rawG, rawB, 1.0e-6});
    const double saturationBoost = 1.0 + (1.0 - maxChannel) * (0.10 + highlights * 0.04);
    const double highlightLift = Smoothstep(0.42, 1.08, intensity) * highlights;
    
    const double alpha = Clamp(intensity * (1.04 + highlightLift * 0.14), 0.0, 1.0);
    const double bloom = 0.94 + intensity * 0.18 + highlightLift * 0.30;
    
    const double rf = Clamp(std::pow(rawR, 1.02) * bloom * saturationBoost, 0.0, 1.0);
    const double gf = Clamp(std::pow(rawG, 1.02) * bloom * saturationBoost, 0.0, 1.0);
    const double bf = Clamp(std::pow(rawB, 1.02) * bloom * saturationBoost, 0.0, 1.0);
    const auto applyCurve = [&](const double value) {
        const double gammaMapped = std::pow(Clamp(value, 0.0, 1.0), 0.58 / gamma);
        return Clamp((gammaMapped - 0.5) * contrast + 0.5, 0.0, 1.0);
    };
    return {
        static_cast<std::uint8_t>(Clamp(applyCurve(rf) * 255.0, 0.0, 255.0)),
        static_cast<std::uint8_t>(Clamp(applyCurve(gf) * 255.0, 0.0, 255.0)),
        static_cast<std::uint8_t>(Clamp(applyCurve(bf) * 255.0, 0.0, 255.0)),
        static_cast<std::uint8_t>(Clamp(alpha * 255.0, 0.0, 255.0))
    };
}

Color SoftwareRenderer::SurfaceColor(const double factor) {
    const Color start {28, 86, 214, 255};
    const Color mid {62, 192, 234, 255};
    const Color end {244, 178, 82, 255};
    if (factor < 0.5) {
        return Lerp(start, mid, Clamp(factor * 2.0, 0.0, 1.0));
    }
    return Lerp(mid, end, Clamp((factor - 0.5) * 2.0, 0.0, 1.0));
}

bool SoftwareRenderer::RenderViewport(const Scene& scene, const int width, const int height, std::vector<std::uint32_t>& pixels, const RenderOptions& options) {
    Clear(pixels, width, height, options.transparentBackground ? Color{0, 0, 0, 0} : scene.backgroundColor);
    if (width <= 0 || height <= 0) {
        return true;
    }

    if (options.renderGrid && scene.gridVisible) {
        RenderPerspectiveGrid(pixels, width, height, scene.camera);
    }

    if (options.renderFlame && scene.mode != SceneMode::Path) {
        std::vector<FlamePixel> flamePixels;
        if (!flameEngine_.Render(scene, width, height, flamePixels, options.shouldAbort, options.preserveFlameState)) {
            return false;
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y * width + x);
                const FlamePixel pixel = ReconstructFlamePixel(flamePixels, width, height, x, y);
                if (pixel.density <= 0.0f) {
                    continue;
                }

                const Color mapped = ToneMap(pixel, scene.flameRender);
                if (mapped.a == 0) {
                    continue;
                }

                const std::uint32_t existing = pixels[i];
                const int er = static_cast<int>((existing >> 16U) & 0xFFU);
                const int eg = static_cast<int>((existing >> 8U) & 0xFFU);
                const int eb = static_cast<int>(existing & 0xFFU);

                const int alpha = mapped.a;
                const int r = er + ((static_cast<int>(mapped.r) - er) * alpha >> 8);
                const int g = eg + ((static_cast<int>(mapped.g) - eg) * alpha >> 8);
                const int b = eb + ((static_cast<int>(mapped.b) - eb) * alpha >> 8);

                pixels[i] = static_cast<std::uint32_t>(b)
                    | (static_cast<std::uint32_t>(g) << 8U)
                    | (static_cast<std::uint32_t>(r) << 16U)
                    | 0xFF000000U;
            }
        }
    }

    if (!options.renderPaths || scene.mode == SceneMode::Flame) {
        if (!options.interactive && (scene.denoiser.enabled || scene.depthOfField.enabled)) {
            std::vector<float> depthBuffer;
            BuildDepthMap(scene, width, height, depthBuffer);
            if (scene.denoiser.enabled) {
                ApplyDenoising(scene, pixels, width, height, depthBuffer, options.shouldAbort);
            }
            if (scene.depthOfField.enabled) {
                ApplyDepthOfField(scene, pixels, width, height, depthBuffer);
            }
        }
        return true;
    }

    std::vector<PathTriangle> fillTriangles;
    std::vector<PathLine> lines;
    std::vector<PathPointSprite> points;
    BuildPathPrimitives(scene, width, height, fillTriangles, lines, points);

    for (const PathTriangle& triangle : fillTriangles) {
        FillTriangle(pixels, width, height, triangle.points[0], triangle.points[1], triangle.points[2], triangle.color, triangle.alpha);
    }
    for (const PathLine& line : lines) {
        DrawLineAA(pixels, width, height, line.start.x, line.start.y, line.end.x, line.end.y, line.color, line.alpha, line.thickness);
    }
    for (const PathPointSprite& point : points) {
        DrawLineAA(
            pixels,
            width,
            height,
            point.point.x,
            point.point.y,
            point.point.x + 0.01,
            point.point.y + 0.01,
            point.color,
            point.alpha,
            point.size);
    }

    if (!options.interactive && (scene.denoiser.enabled || scene.depthOfField.enabled)) {
        std::vector<float> depthBuffer;
        BuildDepthMap(scene, width, height, depthBuffer);
        if (scene.denoiser.enabled) {
            ApplyDenoising(scene, pixels, width, height, depthBuffer, options.shouldAbort);
        }
        if (scene.depthOfField.enabled) {
            ApplyDepthOfField(scene, pixels, width, height, depthBuffer);
        }
    }

    return true;
}

}  // namespace radiary
