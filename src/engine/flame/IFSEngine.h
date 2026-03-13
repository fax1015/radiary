#pragma once

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
    void Render(const Scene& scene, int width, int height, std::vector<FlamePixel>& output);

private:
    static Vec2 ApplyAffine(const TransformLayer& layer, const Vec2& point);
};

}  // namespace radiary
