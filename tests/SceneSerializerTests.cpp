#include "TestHarness.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "core/Scene.h"
#include "io/SceneSerializer.h"

namespace {

std::filesystem::path MakeTempScenePath() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("radiary-scene-test-" + std::to_string(stamp) + ".json");
}

}  // namespace

RADIARY_TEST(SceneSerializerRoundTripPreservesSceneAndPoseSettings) {
    using namespace radiary;

    Scene scene = CreateDefaultScene();
    scene.name = "verification-scene";
    scene.mode = SceneMode::Path;
    scene.gridVisible = false;
    scene.previewIterations = 2048;
    scene.timelineFrameRate = 30.0;
    scene.timelineFrame = 45.0;
    scene.timelineSeconds = -1.0;
    scene.denoiser.enabled = true;
    scene.denoiser.strength = 0.72;
    scene.postProcess.curveUseCustom = true;
    scene.postProcess.curveControlPoints = {{0.0, 0.0}, {0.33, 0.24}, {0.66, 0.78}, {1.0, 1.0}};
    scene.postProcess.hueShiftSaturation = 1.4;
    scene.postProcess.filmGrainScale = 0.65;
    scene.postProcess.saturationVibrance = 0.35;

    SceneKeyframe keyframe;
    keyframe.frame = 12;
    keyframe.ownerType = KeyframeOwnerType::Effect;
    keyframe.ownerIndex = static_cast<int>(EffectStackStage::Curves);
    keyframe.pose = CaptureScenePose(scene);
    keyframe.pose.mode = SceneMode::Hybrid;
    keyframe.pose.gridVisible = true;
    keyframe.pose.denoiser.enabled = true;
    keyframe.pose.denoiser.strength = 0.19;
    keyframe.pose.postProcess.curveUseCustom = true;
    keyframe.pose.postProcess.curveControlPoints = {{0.0, 0.0}, {0.2, 0.12}, {0.8, 0.92}, {1.0, 1.0}};
    keyframe.pose.postProcess.hueShiftSaturation = 1.7;
    keyframe.pose.postProcess.filmGrainScale = 0.42;
    keyframe.pose.postProcess.saturationVibrance = 0.61;
    scene.keyframes = {keyframe};

    const std::filesystem::path path = MakeTempScenePath();
    const auto cleanup = [&]() { std::error_code error; std::filesystem::remove(path, error); };

    SceneSerializer serializer;
    std::string error;
    RADIARY_CHECK(serializer.Save(scene, path, error));
    const std::optional<Scene> loaded = serializer.Load(path, error);
    cleanup();

    RADIARY_CHECK(loaded.has_value());
    RADIARY_CHECK_EQ(loaded->name, scene.name);
    RADIARY_CHECK_EQ(loaded->mode, scene.mode);
    RADIARY_CHECK_EQ(loaded->gridVisible, scene.gridVisible);
    RADIARY_CHECK_EQ(loaded->previewIterations, scene.previewIterations);
    RADIARY_CHECK_EQ(loaded->denoiser.enabled, scene.denoiser.enabled);
    RADIARY_CHECK_NEAR(loaded->denoiser.strength, scene.denoiser.strength, 1e-9);
    RADIARY_CHECK_EQ(loaded->postProcess.curveUseCustom, scene.postProcess.curveUseCustom);
    RADIARY_CHECK_EQ(loaded->postProcess.curveControlPoints.size(), scene.postProcess.curveControlPoints.size());
    RADIARY_CHECK_NEAR(loaded->postProcess.curveControlPoints[1].x, scene.postProcess.curveControlPoints[1].x, 1e-9);
    RADIARY_CHECK_NEAR(loaded->postProcess.curveControlPoints[2].y, scene.postProcess.curveControlPoints[2].y, 1e-9);
    RADIARY_CHECK_NEAR(loaded->postProcess.hueShiftSaturation, scene.postProcess.hueShiftSaturation, 1e-9);
    RADIARY_CHECK_NEAR(loaded->postProcess.filmGrainScale, scene.postProcess.filmGrainScale, 1e-9);
    RADIARY_CHECK_NEAR(loaded->postProcess.saturationVibrance, scene.postProcess.saturationVibrance, 1e-9);
    RADIARY_CHECK_EQ(loaded->keyframes.size(), std::size_t{1});
    RADIARY_CHECK_EQ(loaded->keyframes.front().ownerType, keyframe.ownerType);
    RADIARY_CHECK_EQ(loaded->keyframes.front().ownerIndex, keyframe.ownerIndex);
    RADIARY_CHECK_EQ(loaded->keyframes.front().pose.gridVisible, keyframe.pose.gridVisible);
    RADIARY_CHECK_EQ(loaded->keyframes.front().pose.denoiser.enabled, keyframe.pose.denoiser.enabled);
    RADIARY_CHECK_NEAR(loaded->keyframes.front().pose.denoiser.strength, keyframe.pose.denoiser.strength, 1e-9);
    RADIARY_CHECK_EQ(loaded->keyframes.front().pose.postProcess.curveUseCustom, keyframe.pose.postProcess.curveUseCustom);
    RADIARY_CHECK_EQ(loaded->keyframes.front().pose.postProcess.curveControlPoints.size(), keyframe.pose.postProcess.curveControlPoints.size());
    RADIARY_CHECK_NEAR(loaded->keyframes.front().pose.postProcess.curveControlPoints[1].x, keyframe.pose.postProcess.curveControlPoints[1].x, 1e-9);
    RADIARY_CHECK_NEAR(loaded->keyframes.front().pose.postProcess.curveControlPoints[2].y, keyframe.pose.postProcess.curveControlPoints[2].y, 1e-9);
    RADIARY_CHECK_NEAR(loaded->keyframes.front().pose.postProcess.hueShiftSaturation, keyframe.pose.postProcess.hueShiftSaturation, 1e-9);
    RADIARY_CHECK_NEAR(loaded->keyframes.front().pose.postProcess.filmGrainScale, keyframe.pose.postProcess.filmGrainScale, 1e-9);
    RADIARY_CHECK_NEAR(loaded->keyframes.front().pose.postProcess.saturationVibrance, keyframe.pose.postProcess.saturationVibrance, 1e-9);
    RADIARY_CHECK_NEAR(loaded->timelineSeconds, TimelineSecondsForFrame(*loaded, loaded->timelineFrame), 1e-9);
}

RADIARY_TEST(SceneSerializerLoadSortsKeyframesAndKeepsDefaultsForMissingFields) {
    using namespace radiary;

    const std::filesystem::path path = MakeTempScenePath();
    {
        std::ofstream stream(path, std::ios::binary);
        stream << "{\n"
               << "  \"name\": \"legacy\",\n"
               << "  \"timelineFrameRate\": 24,\n"
               << "  \"timelineFrame\": 24,\n"
               << "  \"keyframes\": [\n"
               << "    {\"frame\": 20, \"ownerType\": \"transform\", \"ownerIndex\": 0, \"easing\": \"linear\"},\n"
               << "    {\"frame\": 10, \"ownerType\": \"transform\", \"ownerIndex\": 0, \"easing\": \"linear\"}\n"
               << "  ]\n"
               << "}\n";
    }

    const auto cleanup = [&]() { std::error_code error; std::filesystem::remove(path, error); };
    SceneSerializer serializer;
    std::string error;
    const std::optional<Scene> loaded = serializer.Load(path, error);
    cleanup();

    RADIARY_CHECK(loaded.has_value());
    RADIARY_CHECK_EQ(loaded->gridVisible, CreateDefaultScene().gridVisible);
    RADIARY_CHECK_EQ(loaded->denoiser.enabled, CreateDefaultScene().denoiser.enabled);
    RADIARY_CHECK_NEAR(loaded->denoiser.strength, CreateDefaultScene().denoiser.strength, 1e-9);
    RADIARY_CHECK_EQ(loaded->keyframes.size(), std::size_t{2});
    RADIARY_CHECK(loaded->keyframes[0].frame < loaded->keyframes[1].frame);
    RADIARY_CHECK_NEAR(loaded->timelineSeconds, 1.0, 1e-9);
}
