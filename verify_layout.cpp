#include <cstdio>
#include <cstdint>
#include <cstddef>

struct Params {
    std::uint32_t width;
    std::uint32_t height;
    float bloomIntensity;
    float bloomThreshold;
    std::uint32_t curvesEnabled;
    std::uint32_t sharpenEnabled;
    std::uint32_t hueShiftEnabled;
    std::uint32_t chromaticAberrationEnabled;
    float curveBlackPoint;
    float curveWhitePoint;
    float curveGamma;
    std::uint32_t curveUseCustom;
    float curvePoints[16];
    std::uint32_t curvePointCount;
    float sharpenAmount;
    float hueShiftDegrees;
    float hueShiftSaturation;
    float chromaticAberration;
    float vignetteIntensity;
    float vignetteRoundness;
    std::uint32_t vignetteEnabled;
    std::uint32_t toneMappingEnabled;
    std::uint32_t filmGrainEnabled;
    std::uint32_t colorTemperatureEnabled;
    float filmGrain;
    float filmGrainScale;
    float colorTemperature;
    float saturationBoost;
    float saturationVibrance;
    std::uint32_t saturationEnabled;
    std::uint32_t randomSeed;
    std::uint32_t mipWidth;
    std::uint32_t mipHeight;
};

int main() {
    Params p{};
    #define OFF(m) printf("  %-30s offset=%3zu  size=%zu\n", #m, offsetof(Params, m), sizeof(p.m))
    OFF(width); OFF(height); OFF(bloomIntensity); OFF(bloomThreshold);
    OFF(curvesEnabled); OFF(sharpenEnabled); OFF(hueShiftEnabled); OFF(chromaticAberrationEnabled);
    OFF(curveBlackPoint); OFF(curveWhitePoint); OFF(curveGamma); OFF(curveUseCustom);
    OFF(curvePoints); OFF(curvePointCount);
    OFF(sharpenAmount); OFF(hueShiftDegrees); OFF(hueShiftSaturation); OFF(chromaticAberration);
    OFF(vignetteIntensity); OFF(vignetteRoundness); OFF(vignetteEnabled);
    OFF(toneMappingEnabled); OFF(filmGrainEnabled); OFF(colorTemperatureEnabled);
    OFF(filmGrain); OFF(filmGrainScale); OFF(colorTemperature);
    OFF(saturationBoost); OFF(saturationVibrance); OFF(saturationEnabled);
    OFF(randomSeed); OFF(mipWidth); OFF(mipHeight);
    printf("\nsizeof(Params) = %zu\n", sizeof(Params));
    return 0;
}
