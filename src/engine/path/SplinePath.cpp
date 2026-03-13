#include "engine/path/SplinePath.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace radiary {

namespace {

// Phase 5: PathLayout generators

std::vector<Vec3> GenerateRadialCluster(const PathSettings& path, const double time) {
    std::vector<Vec3> points;
    const int nodeCount = std::max(3, path.layoutNodes);
    const double radius = path.layoutRadius;
    const double randomness = path.layoutRandomness;
    
    std::mt19937 rng(static_cast<unsigned int>(nodeCount * 12345 + static_cast<int>(time * 100)));
    std::uniform_real_distribution<double> rngOffset(-randomness, randomness);
    
    // Central hub
    points.push_back({rngOffset(rng) * 0.5, rngOffset(rng) * 0.5, rngOffset(rng) * 0.5});
    
    // Orbiting nodes
    for (int i = 0; i < nodeCount; ++i) {
        const double angle = (static_cast<double>(i) / nodeCount) * kPi * 2.0 + time * 0.2;
        const double r = radius * (1.0 + rngOffset(rng) * 0.3);
        const double x = std::cos(angle) * r + rngOffset(rng);
        const double y = std::sin(angle) * r * 0.6 + rngOffset(rng);  // Flattened Y
        const double z = std::sin(angle * 1.3) * r * 0.4 + rngOffset(rng);
        points.push_back({x, y, z});
    }
    
    return points;
}

std::vector<Vec3> GenerateNetwork(const PathSettings& path, const double time) {
    std::vector<Vec3> points;
    const int nodeCount = std::max(4, path.layoutNodes);
    const double radius = path.layoutRadius;
    const double randomness = path.layoutRandomness;
    
    std::mt19937 rng(static_cast<unsigned int>(nodeCount * 54321 + static_cast<int>(time * 100)));
    std::uniform_real_distribution<double> rngPos(-radius, radius);
    std::uniform_real_distribution<double> rngOffset(-randomness, randomness);
    
    // Generate random points in 3D space
    for (int i = 0; i < nodeCount; ++i) {
        Vec3 p = {rngPos(rng), rngPos(rng) * 0.7, rngPos(rng) * 0.7};
        p.x += rngOffset(rng);
        p.y += rngOffset(rng);
        p.z += rngOffset(rng);
        points.push_back(p);
    }
    
    // Sort by distance from center for more structured connections
    const Vec3 center{0, 0, 0};
    std::sort(points.begin(), points.end(), [&center](const Vec3& a, const Vec3& b) {
        return Length3(a - center) < Length3(b - center);
    });
    
    return points;
}

std::vector<Vec3> GenerateTendrilBall(const PathSettings& path, const double time) {
    std::vector<Vec3> points;
    const int armCount = std::max(4, path.layoutNodes);
    const double radius = path.layoutRadius;
    const double randomness = path.layoutRandomness;
    
    std::mt19937 rng(static_cast<unsigned int>(armCount * 98765 + static_cast<int>(time * 100)));
    std::uniform_real_distribution<double> rngOffset(-randomness, randomness);
    
    // Central sphere
    points.push_back({0, 0, 0});
    
    // Radiating arms
    for (int i = 0; i < armCount; ++i) {
        // Fibonacci sphere distribution for even spread
        const double phi = kPi * (3.0 - std::sqrt(5.0));  // Golden angle
        const double y = 1.0 - (static_cast<double>(i) / (armCount - 1)) * 2.0;
        const double r = std::sqrt(1.0 - y * y);
        const double theta = phi * i + time * 0.1;
        
        const double x = std::cos(theta) * r;
        const double z = std::sin(theta) * r;
        
        // Add arm segments with organic curvature
        Vec3 armDir = Normalize(Vec3{x, y, z});
        Vec3 current = Vec3{0, 0, 0};
        
        for (int seg = 0; seg < 3; ++seg) {
            const double segLen = radius * (0.3 + seg * 0.35);
            // Add organic bend
            armDir.x += std::sin(time * 0.5 + i + seg) * 0.1 * randomness;
            armDir.y += std::cos(time * 0.3 + i + seg) * 0.1 * randomness;
            armDir = Normalize(armDir);
            
            current = current + armDir * segLen;
            points.push_back({
                current.x + rngOffset(rng),
                current.y + rngOffset(rng),
                current.z + rngOffset(rng)
            });
        }
    }
    
    return points;
}

}  // namespace

Vec3 SplinePath::CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, const double t) {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return {
        0.5 * ((2.0 * p1.x) + (-p0.x + p2.x) * t + (2.0 * p0.x - 5.0 * p1.x + 4.0 * p2.x - p3.x) * t2 + (-p0.x + 3.0 * p1.x - 3.0 * p2.x + p3.x) * t3),
        0.5 * ((2.0 * p1.y) + (-p0.y + p2.y) * t + (2.0 * p0.y - 5.0 * p1.y + 4.0 * p2.y - p3.y) * t2 + (-p0.y + 3.0 * p1.y - 3.0 * p2.y + p3.y) * t3),
        0.5 * ((2.0 * p1.z) + (-p0.z + p2.z) * t + (2.0 * p0.z - 5.0 * p1.z + 4.0 * p2.z - p3.z) * t2 + (-p0.z + 3.0 * p1.z - 3.0 * p2.z + p3.z) * t3)
    };
}

std::vector<Vec3> SplinePath::Sample(const PathSettings& settings, const double timeSeconds) const {
    // Phase 5: Use layout-generated control points if not user-defined
    std::vector<Vec3> controlPoints;
    if (settings.layout == PathLayout::UserDefined) {
        controlPoints = settings.controlPoints;
    } else {
        switch (settings.layout) {
        case PathLayout::RadialCluster:
            controlPoints = GenerateRadialCluster(settings, timeSeconds);
            break;
        case PathLayout::Network:
            controlPoints = GenerateNetwork(settings, timeSeconds);
            break;
        case PathLayout::TendrilBall:
            controlPoints = GenerateTendrilBall(settings, timeSeconds);
            break;
        default:
            controlPoints = settings.controlPoints;
            break;
        }
    }
    
    if (controlPoints.size() < 2) {
        return {};
    }

    const int segments = std::max(1, settings.sampleCount);
    std::vector<Vec3> samples;
    samples.reserve(static_cast<std::size_t>(segments) * 2U);
    const double scroll = timeSeconds * 0.35;

    auto getPoint = [&controlPoints, &settings](const int index) -> const Vec3& {
        if (settings.closed) {
            const int count = static_cast<int>(controlPoints.size());
            int wrapped = index % count;
            if (wrapped < 0) {
                wrapped += count;
            }
            return controlPoints[static_cast<std::size_t>(wrapped)];
        }
        const int clamped = std::clamp(index, 0, static_cast<int>(controlPoints.size()) - 1);
        return controlPoints[static_cast<std::size_t>(clamped)];
    };

    const int controlSegmentCount = settings.closed
        ? static_cast<int>(controlPoints.size())
        : static_cast<int>(controlPoints.size()) - 1;

    for (int segment = 0; segment < controlSegmentCount; ++segment) {
        for (int step = 0; step < segments; ++step) {
            const double t = static_cast<double>(step) / static_cast<double>(segments);
            Vec3 point = CatmullRom(
                getPoint(segment - 1),
                getPoint(segment),
                getPoint(segment + 1),
                getPoint(segment + 2),
                t);
            point.y += std::sin(scroll + segment * 0.55 + t * 2.0) * settings.taper;
            samples.push_back(point);
        }
    }

    if (!samples.empty()) {
        samples.push_back(settings.closed ? samples.front() : controlPoints.back());
    }
    return samples;
}

}  // namespace radiary
