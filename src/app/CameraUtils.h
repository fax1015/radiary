#pragma once

#include <array>
#include <chrono>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "core/Scene.h"

namespace radiary {

struct CameraAspectPreset {
    const char* label = "";
    double width = 1.0;
    double height = 1.0;
};

struct ExportResolutionPreset {
    std::string label;
    int width = 1920;
    int height = 1080;
};

constexpr std::array<CameraAspectPreset, 8> kCameraAspectPresets {{
    {"16:9 Widescreen", 16.0, 9.0},
    {"21:9 Ultrawide", 21.0, 9.0},
    {"2.39:1 Cinema", 2.39, 1.0},
    {"3:2 Photo", 3.0, 2.0},
    {"4:3 Classic", 4.0, 3.0},
    {"1:1 Square", 1.0, 1.0},
    {"4:5 Portrait", 4.0, 5.0},
    {"9:16 Vertical", 9.0, 16.0}
}};

double CameraFrameWidth(const CameraState& camera);
double CameraFrameHeight(const CameraState& camera);
double CameraAspectRatio(const CameraState& camera);
bool CameraAspectMatches(const CameraState& camera, double width, double height, double epsilon = 0.015);
int FindCameraAspectPresetIndex(const CameraState& camera);
void ApplyCameraAspectPreset(CameraState& camera, int presetIndex);
std::string CameraAspectSummary(const CameraState& camera);
ImVec2 FitCameraFrameToBounds(const CameraState& camera, float boundsWidth, float boundsHeight);
ImRect CameraFrameRectInBounds(const CameraState& camera, const ImRect& bounds);

int RoundToEven(double value);
std::vector<ExportResolutionPreset> BuildExportResolutionPresets(const CameraState& camera);
int FindExportResolutionPresetIndex(const std::vector<ExportResolutionPreset>& presets, int width, int height);
void ConstrainExportResolutionToCamera(const CameraState& camera, int& width, int& height, bool widthDrives);

Scene PrepareSceneForExport(const Scene& sourceScene, int previewWidth, int previewHeight, int exportWidth, int exportHeight, bool hideGrid);

std::wstring FormatEtaDuration(std::chrono::seconds duration);

}  // namespace radiary
