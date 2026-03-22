#include "TestHarness.h"

#include <array>

#include "core/Scene.h"
#include "renderer/PathDrawListBuilder.h"

namespace {

void CheckVertexRange(const radiary::PathDrawVertex& vertex) {
    RADIARY_CHECK(vertex.position[2] >= 0.0f);
    RADIARY_CHECK(vertex.position[2] <= 1.0f);
    for (const float channel : vertex.color) {
        RADIARY_CHECK(channel >= 0.0f);
        RADIARY_CHECK(channel <= 1.0f);
    }
}

}  // namespace

RADIARY_TEST(PathDrawListBuilderRespectsGridVisibilityAndReportsTotals) {
    using namespace radiary;

    Scene scene = CreateDefaultScene();
    scene.mode = SceneMode::Path;

    const PathDrawList withoutGrid = PathDrawListBuilder::Build(scene, 640, 360, false);
    RADIARY_CHECK(withoutGrid.gridVertices.empty());

    const PathDrawList withGrid = PathDrawListBuilder::Build(scene, 640, 360, true);
    RADIARY_CHECK(!withGrid.gridVertices.empty());
    RADIARY_CHECK_EQ(withGrid.TotalVertexCount(), withGrid.gridVertices.size() + withGrid.fillVertices.size() + withGrid.overlayVertices.size() + withGrid.pointVertices.size());

    scene.gridVisible = false;
    const PathDrawList hiddenGrid = PathDrawListBuilder::Build(scene, 640, 360, true);
    RADIARY_CHECK(hiddenGrid.gridVertices.empty());
}

RADIARY_TEST(PathDrawListBuilderProducesExpectedBucketsForWireAndPointModes) {
    using namespace radiary;

    Scene wireScene = CreateDefaultScene();
    wireScene.mode = SceneMode::Path;
    wireScene.paths.front().material.renderMode = PathRenderMode::SolidWire;
    const PathDrawList wireList = PathDrawListBuilder::Build(wireScene, 800, 600, false);

    RADIARY_CHECK(!wireList.fillVertices.empty());
    RADIARY_CHECK(!wireList.overlayVertices.empty());
    RADIARY_CHECK(wireList.pointVertices.empty());
    RADIARY_CHECK_EQ(wireList.fillVertices.size() % 3, std::size_t{0});
    RADIARY_CHECK_EQ(wireList.overlayVertices.size() % 6, std::size_t{0});
    for (const auto& vertex : wireList.fillVertices) {
        CheckVertexRange(vertex);
    }
    for (const auto& vertex : wireList.overlayVertices) {
        CheckVertexRange(vertex);
    }

    Scene pointScene = CreateDefaultScene();
    pointScene.mode = SceneMode::Path;
    pointScene.paths.front().material.renderMode = PathRenderMode::Points;
    const PathDrawList pointList = PathDrawListBuilder::Build(pointScene, 800, 600, false);

    RADIARY_CHECK(!pointList.pointVertices.empty());
    RADIARY_CHECK_EQ(pointList.pointVertices.size() % 6, std::size_t{0});
    for (const auto& vertex : pointList.pointVertices) {
        CheckVertexRange(vertex);
    }
}
