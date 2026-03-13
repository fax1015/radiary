#pragma once

#include <vector>

#include "core/Math.h"
#include "core/Scene.h"

namespace radiary {

class SplinePath {
public:
    std::vector<Vec3> Sample(const PathSettings& settings, double timeSeconds) const;

private:
    static Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double t);
};

}  // namespace radiary
