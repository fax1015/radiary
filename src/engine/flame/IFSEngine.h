#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "core/Math.h"
#include "core/Scene.h"

namespace radiary {

struct FlamePixel {
    float density = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float depth = 0.0f;
};

class IFSEngine {
public:
    bool Render(
        const Scene& scene,
        int width,
        int height,
        std::vector<FlamePixel>& output,
        const std::function<bool()>& shouldAbort = {},
        bool preserveTemporalState = false);
    void ResetTemporalState();

private:
    static Vec2 ApplyAffine(const TransformLayer& layer, const Vec2& point);

    std::mt19937 temporalGenerator_ {};
    Vec3 temporalPoint_ {};
    double temporalColorIndex_ = 0.5;
    std::uint32_t temporalBurnInRemaining_ = 0;
    std::size_t temporalTransformCount_ = 0;
    bool temporalStateValid_ = false;
};

}  // namespace radiary
