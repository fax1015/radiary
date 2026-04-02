#include <iostream>
#include <cstdint>
#include <cstddef>

struct Params {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float bloomIntensity = 0.35f;
    float bloomThreshold = 0.6f;
    std::uint32_t curvesEnabled = 0;
    std::uint32_t sharpenEnabled = 0;
    std::uint32_t hueShiftEnabled = 0;
    std::uint32_t chromaticAberrationEnabled = 0;
    float curveBlackPoint = 0.0f;
    float curveWhitePoint = 1.0f;
    float curveGamma = 1.0f;
    std::uint32_t curveUseCustom = 0;
    float curvePoints[16] = {};
    std::uint32_t curvePointCount = 0;
    float sharpenAmount = 0.0f;
    float hueShiftDegrees = 0.0f;
    float hueShiftSaturation = 1.0f;
    float chromaticAberration = 0.0f;
    float vignetteIntensity = 0.0f;
    float vignetteRoundness = 0.5f;
    std::uint32_t vignetteEnabled = 0;
    std::uint32_t toneMappingEnabled = 0;
    std::uint32_t filmGrainEnabled = 0;
    std::uint32_t colorTemperatureEnabled = 0;
    float filmGrain = 0.0f;
    float filmGrainScale = 1.0f;
    float colorTemperature = 6500.0f;
    float saturationBoost = 0.0f;
    float saturationVibrance = 0.0f;
    std::uint32_t saturationEnabled = 0;
    std::uint32_t randomSeed = 0;
    std::uint32_t mipWidth = 0;
    std::uint32_t mipHeight = 0;
};

int main() {
    std::cout << "sizeof(Params): " << sizeof(Params) << std::endl;
    return 0;
}
