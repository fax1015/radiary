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

    SceneKeyframe keyframe;
    keyframe.frame = 12;
    keyframe.pose = CaptureScenePose(scene);
    keyframe.pose.mode = SceneMode::Hybrid;
    keyframe.pose.gridVisible = true;
    keyframe.pose.denoiser.enabled = true;
    keyframe.pose.denoiser.strength = 0.19;
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
    RADIARY_CHECK_EQ(loaded->keyframes.size(), std::size_t{1});
    RADIARY_CHECK_EQ(loaded->keyframes.front().pose.gridVisible, keyframe.pose.gridVisible);
    RADIARY_CHECK_EQ(loaded->keyframes.front().pose.denoiser.enabled, keyframe.pose.denoiser.enabled);
    RADIARY_CHECK_NEAR(loaded->keyframes.front().pose.denoiser.strength, keyframe.pose.denoiser.strength, 1e-9);
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
