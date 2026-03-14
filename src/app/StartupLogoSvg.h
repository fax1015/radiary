#pragma once

#include <windows.h>

#include <filesystem>
#include <vector>

namespace radiary {

struct StartupLogoPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct StartupLogoStroke {
    std::vector<StartupLogoPoint> points;
    bool closed = false;
};

struct StartupLogoSvgData {
    std::vector<StartupLogoStroke> strokes;
    COLORREF color = RGB(115, 148, 235);
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

bool LoadStartupLogoSvg(const std::filesystem::path& path, StartupLogoSvgData& output);

}  // namespace radiary
