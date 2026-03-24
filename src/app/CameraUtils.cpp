#include "app/CameraUtils.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace radiary {

double CameraFrameWidth(const CameraState& camera) {
    return std::max(0.1, camera.frameWidth);
}

double CameraFrameHeight(const CameraState& camera) {
    return std::max(0.1, camera.frameHeight);
}

double CameraAspectRatio(const CameraState& camera) {
    return CameraFrameWidth(camera) / CameraFrameHeight(camera);
}

bool CameraAspectMatches(const CameraState& camera, const double width, const double height, const double epsilon) {
    return std::abs(CameraAspectRatio(camera) - (width / std::max(0.1, height))) <= epsilon;
}

int FindCameraAspectPresetIndex(const CameraState& camera) {
    for (std::size_t index = 0; index < kCameraAspectPresets.size(); ++index) {
        if (CameraAspectMatches(camera, kCameraAspectPresets[index].width, kCameraAspectPresets[index].height)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void ApplyCameraAspectPreset(CameraState& camera, const int presetIndex) {
    if (presetIndex < 0 || presetIndex >= static_cast<int>(kCameraAspectPresets.size())) {
        return;
    }
    camera.frameWidth = kCameraAspectPresets[static_cast<std::size_t>(presetIndex)].width;
    camera.frameHeight = kCameraAspectPresets[static_cast<std::size_t>(presetIndex)].height;
}

std::string CameraAspectSummary(const CameraState& camera) {
    const auto formatPart = [](const double value) {
        std::ostringstream part;
        const double rounded = std::round(value);
        if (std::abs(value - rounded) < 0.01) {
            part << static_cast<int>(rounded);
        } else {
            part << std::fixed << std::setprecision(2) << value;
        }
        return part.str();
    };
    std::ostringstream stream;
    stream << formatPart(CameraFrameWidth(camera)) << ":" << formatPart(CameraFrameHeight(camera));
    return stream.str();
}

ImVec2 FitCameraFrameToBounds(const CameraState& camera, const float boundsWidth, const float boundsHeight) {
    const float safeWidth = std::max(1.0f, boundsWidth);
    const float safeHeight = std::max(1.0f, boundsHeight);
    const float aspect = static_cast<float>(CameraAspectRatio(camera));
    float frameWidth = safeWidth;
    float frameHeight = frameWidth / std::max(0.001f, aspect);
    if (frameHeight > safeHeight) {
        frameHeight = safeHeight;
        frameWidth = frameHeight * aspect;
    }
    return ImVec2(std::max(1.0f, frameWidth), std::max(1.0f, frameHeight));
}

ImRect CameraFrameRectInBounds(const CameraState& camera, const ImRect& bounds) {
    const ImVec2 size = FitCameraFrameToBounds(camera, bounds.GetWidth(), bounds.GetHeight());
    const ImVec2 center(
        (bounds.Min.x + bounds.Max.x) * 0.5f,
        (bounds.Min.y + bounds.Max.y) * 0.5f);
    const ImVec2 half(size.x * 0.5f, size.y * 0.5f);
    return ImRect(ImVec2(center.x - half.x, center.y - half.y), ImVec2(center.x + half.x, center.y + half.y));
}

int RoundToEven(const double value) {
    int rounded = static_cast<int>(std::round(value));
    if ((rounded & 1) != 0) {
        ++rounded;
    }
    return std::max(2, rounded);
}

std::vector<ExportResolutionPreset> BuildExportResolutionPresets(const CameraState& camera) {
    if (CameraAspectMatches(camera, 16.0, 9.0)) {
        return {
            {"HD 1280x720", 1280, 720},
            {"Full HD 1920x1080", 1920, 1080},
            {"QHD 2560x1440", 2560, 1440},
            {"4K UHD 3840x2160", 3840, 2160}
        };
    }
    if (CameraAspectMatches(camera, 21.0, 9.0)) {
        return {
            {"UWHD 2560x1080", 2560, 1080},
            {"UWQHD 3440x1440", 3440, 1440},
            {"5K2K 5120x2160", 5120, 2160}
        };
    }
    if (CameraAspectMatches(camera, 2.39, 1.0)) {
        return {
            {"2K Scope 2048x858", 2048, 858},
            {"UW Cinema 2880x1200", 2880, 1200},
            {"4K Scope 4096x1716", 4096, 1716}
        };
    }
    if (CameraAspectMatches(camera, 3.0, 2.0)) {
        return {
            {"Small 1440x960", 1440, 960},
            {"Photo 2160x1440", 2160, 1440},
            {"Large 2880x1920", 2880, 1920}
        };
    }
    if (CameraAspectMatches(camera, 4.0, 3.0)) {
        return {
            {"XGA 1024x768", 1024, 768},
            {"UXGA 1600x1200", 1600, 1200},
            {"QXGA 2048x1536", 2048, 1536}
        };
    }
    if (CameraAspectMatches(camera, 1.0, 1.0)) {
        return {
            {"Square 1080x1080", 1080, 1080},
            {"Square 2048x2048", 2048, 2048},
            {"Square 3072x3072", 3072, 3072}
        };
    }
    if (CameraAspectMatches(camera, 4.0, 5.0)) {
        return {
            {"Social 1080x1350", 1080, 1350},
            {"Social 2160x2700", 2160, 2700},
            {"Large 2880x3600", 2880, 3600}
        };
    }
    if (CameraAspectMatches(camera, 9.0, 16.0)) {
        return {
            {"Vertical 1080x1920", 1080, 1920},
            {"Vertical 1440x2560", 1440, 2560},
            {"Vertical 2160x3840", 2160, 3840}
        };
    }

    std::vector<ExportResolutionPreset> presets;
    const double aspect = CameraAspectRatio(camera);
    if (aspect >= 1.0) {
        for (const int width : {1280, 1920, 2560, 3840}) {
            const int height = RoundToEven(static_cast<double>(width) / aspect);
            presets.push_back({std::to_string(width) + "x" + std::to_string(height), width, height});
        }
    } else {
        for (const int height : {1280, 1920, 2560, 3840}) {
            const int width = RoundToEven(static_cast<double>(height) * aspect);
            presets.push_back({std::to_string(width) + "x" + std::to_string(height), width, height});
        }
    }
    return presets;
}

int FindExportResolutionPresetIndex(const std::vector<ExportResolutionPreset>& presets, const int width, const int height) {
    for (std::size_t index = 0; index < presets.size(); ++index) {
        if (presets[index].width == width && presets[index].height == height) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void ConstrainExportResolutionToCamera(const CameraState& camera, int& width, int& height, const bool widthDrives) {
    constexpr int kMinResolution = 2;
    constexpr int kMaxResolution = 8192;
    const double aspect = CameraAspectRatio(camera);
    width = std::clamp(width, kMinResolution, kMaxResolution);
    height = std::clamp(height, kMinResolution, kMaxResolution);
    if (widthDrives) {
        height = std::clamp(RoundToEven(static_cast<double>(width) / aspect), kMinResolution, kMaxResolution);
        width = std::clamp(RoundToEven(static_cast<double>(height) * aspect), kMinResolution, kMaxResolution);
        return;
    }
    width = std::clamp(RoundToEven(static_cast<double>(height) * aspect), kMinResolution, kMaxResolution);
    height = std::clamp(RoundToEven(static_cast<double>(width) / aspect), kMinResolution, kMaxResolution);
}

Scene PrepareSceneForExport(
    const Scene& sourceScene,
    const int previewWidth,
    const int previewHeight,
    const int exportWidth,
    const int exportHeight,
    const bool hideGrid) {
    Scene exportScene = sourceScene;
    exportScene.gridVisible = hideGrid ? false : exportScene.gridVisible;
    (void)previewWidth;
    (void)previewHeight;
    (void)exportWidth;
    (void)exportHeight;
    return exportScene;
}

std::wstring FormatEtaDuration(const std::chrono::seconds duration) {
    const auto totalSeconds = std::max<std::int64_t>(0, duration.count());
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds % 3600) / 60;
    const auto seconds = totalSeconds % 60;
    std::wstringstream stream;
    if (hours > 0) {
        stream << hours << L"h " << std::setw(2) << std::setfill(L'0') << minutes << L"m";
    } else {
        stream << minutes << L"m " << std::setw(2) << std::setfill(L'0') << seconds << L"s";
    }
    return stream.str();
}

}  // namespace radiary
