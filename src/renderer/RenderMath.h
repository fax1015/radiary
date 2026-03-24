#pragma once

#include <algorithm>
#include <cmath>

#include "core/Math.h"
#include "core/Scene.h"

namespace radiary::render_math {

constexpr double kProjectionNearDepth = 0.15;
constexpr double kDepthRangePadding = 24.0;
constexpr double kProjectionScale = 240.0;
constexpr double kProjectionReferenceFrameHeight = 720.0;

struct CameraProjectionPoint {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;
    double perspective = 0.0;
    bool visible = false;
};

struct CameraFrameProjection {
    double minX = 0.0;
    double minY = 0.0;
    double width = 1.0;
    double height = 1.0;
    double scale = 1.0;
};

inline CameraFrameProjection ComputeCameraFrameProjection(
    const CameraState& camera,
    const int width,
    const int height) {
    const double safeWidth = std::max(1.0, static_cast<double>(width));
    const double safeHeight = std::max(1.0, static_cast<double>(height));
    const double aspect = std::max(0.001, camera.frameWidth) / std::max(0.001, camera.frameHeight);

    double frameWidth = safeWidth;
    double frameHeight = frameWidth / aspect;
    if (frameHeight > safeHeight) {
        frameHeight = safeHeight;
        frameWidth = frameHeight * aspect;
    }

    return {
        (safeWidth - frameWidth) * 0.5,
        (safeHeight - frameHeight) * 0.5,
        std::max(1.0, frameWidth),
        std::max(1.0, frameHeight),
        std::max(1.0, frameHeight) / kProjectionReferenceFrameHeight
    };
}

inline Vec3 RotateIntoCameraView(const Vec3& point, const CameraState& camera) {
    const double yawCos = std::cos(camera.yaw);
    const double yawSin = std::sin(camera.yaw);
    const double pitchCos = std::cos(camera.pitch);
    const double pitchSin = std::sin(camera.pitch);

    Vec3 rotated {
        point.x * yawCos + point.z * yawSin,
        point.y,
        -point.x * yawSin + point.z * yawCos
    };

    return {
        rotated.x,
        rotated.y * pitchCos - rotated.z * pitchSin,
        rotated.y * pitchSin + rotated.z * pitchCos
    };
}

inline CameraProjectionPoint ProjectCameraPoint(
    const Vec3& point,
    const CameraState& camera,
    const int width,
    const int height) {
    Vec3 rotated = RotateIntoCameraView(point, camera);
    if (!std::isfinite(rotated.x) || !std::isfinite(rotated.y) || !std::isfinite(rotated.z)) {
        return {};
    }

    rotated.z += camera.distance;
    if (rotated.z <= kProjectionNearDepth) {
        return {};
    }

    const CameraFrameProjection frame = ComputeCameraFrameProjection(camera, width, height);
    const double perspective = kProjectionScale * frame.scale * camera.zoom2D / rotated.z;
    if (!std::isfinite(perspective)) {
        return {};
    }

    return {
        frame.minX + frame.width * 0.5 + camera.panX * frame.scale + rotated.x * perspective,
        frame.minY + frame.height * 0.5 + camera.panY * frame.scale - rotated.y * perspective,
        rotated.z,
        perspective,
        true
    };
}

inline double ComputeFarDepth(const double cameraDistance) {
    return std::max(kProjectionNearDepth + 1.0, cameraDistance + kDepthRangePadding);
}

inline double NormalizeDepth(const double depth, const double farDepth) {
    const double normalized = (depth - kProjectionNearDepth)
        / std::max(1.0e-6, farDepth - kProjectionNearDepth);
    return std::clamp(normalized, 0.0, 1.0);
}

inline double NormalizeDepthForCamera(const double depth, const CameraState& camera) {
    return NormalizeDepth(depth, ComputeFarDepth(camera.distance));
}

}  // namespace radiary::render_math
