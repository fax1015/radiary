#pragma once

#include <cstddef>
#include <vector>

#include "core/Scene.h"

namespace radiary {

struct PathDrawVertex {
    float position[3] {};
    float color[4] {};
};

struct PathDrawList {
    std::vector<PathDrawVertex> gridVertices;
    std::vector<PathDrawVertex> fillVertices;
    std::vector<PathDrawVertex> overlayVertices;
    std::vector<PathDrawVertex> pointVertices;

    std::size_t TotalVertexCount() const;
};

class PathDrawListBuilder {
public:
    static PathDrawList Build(const Scene& scene, int width, int height, bool renderGrid);
};

}  // namespace radiary
