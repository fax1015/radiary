#include "TestHarness.h"

#include <string>

#include "core/Scene.h"

RADIARY_TEST(SceneEffectHelpersReportActiveStagesAndNormalizeOrder) {
    using namespace radiary;

    Scene scene = CreateDefaultScene();
    RADIARY_CHECK(!HasActivePostProcess(scene.postProcess));

    scene.postProcess.saturationEnabled = true;
    RADIARY_CHECK(HasActivePostProcess(scene.postProcess));

    EffectStackOrder order = {
        EffectStackStage::HueShift,
        EffectStackStage::Curves,
        EffectStackStage::HueShift,
        EffectStackStage::Denoiser,
        EffectStackStage::Curves,
        EffectStackStage::Vignette
    };
    NormalizeEffectStackOrder(order);

    const EffectStackOrder expected = {
        EffectStackStage::HueShift,
        EffectStackStage::Curves,
        EffectStackStage::Denoiser,
        EffectStackStage::Vignette
    };
    RADIARY_CHECK_EQ(order, expected);
    RADIARY_CHECK_EQ(std::string(EffectStageDisplayName(EffectStackStage::PostProcess)), "Glow");
}

RADIARY_TEST(SceneEffectHelpersEnableAndDisableStagesConsistently) {
    using namespace radiary;

    Scene scene = CreateDefaultScene();
    for (const EffectStackStage stage : kAllEffectStages) {
        EnableEffectStage(scene, stage, false);
        RADIARY_CHECK(!IsEffectStageEnabled(scene, stage));
    }
    RADIARY_CHECK(!HasActivePostProcess(scene.postProcess));

    for (const EffectStackStage stage : kAllEffectStages) {
        EnableEffectStage(scene, stage, true);
        RADIARY_CHECK(IsEffectStageEnabled(scene, stage));
    }

    RADIARY_CHECK(scene.denoiser.enabled);
    RADIARY_CHECK(scene.depthOfField.enabled);
    RADIARY_CHECK(scene.postProcess.acesToneMap);
    RADIARY_CHECK(HasActivePostProcess(scene.postProcess));

    EnableEffectStage(scene, EffectStackStage::ToneMapping, false);
    RADIARY_CHECK(!scene.postProcess.toneMappingEnabled);
    RADIARY_CHECK(!scene.postProcess.acesToneMap);
}

RADIARY_TEST(ApplyScenePosePreservesVisibilityAndClampsSelections) {
    using namespace radiary;

    Scene scene = CreateDefaultScene();
    scene.transforms[0].visible = false;
    scene.paths[0].visible = false;
    scene.selectedTransform = 999;
    scene.selectedPath = 999;

    ScenePose pose = CaptureScenePose(scene);
    pose.transforms.resize(1);
    pose.paths.resize(1);
    pose.transforms[0].visible = true;
    pose.paths[0].visible = true;
    pose.effectStack = {
        EffectStackStage::HueShift,
        EffectStackStage::HueShift,
        EffectStackStage::Denoiser,
        EffectStackStage::Denoiser
    };

    ApplyScenePose(scene, pose);

    RADIARY_CHECK_EQ(scene.transforms.size(), std::size_t{1});
    RADIARY_CHECK_EQ(scene.paths.size(), std::size_t{1});
    RADIARY_CHECK_EQ(scene.transforms[0].visible, false);
    RADIARY_CHECK_EQ(scene.paths[0].visible, false);
    RADIARY_CHECK_EQ(scene.selectedTransform, 0);
    RADIARY_CHECK_EQ(scene.selectedPath, 0);

    const EffectStackOrder expected = {
        EffectStackStage::HueShift,
        EffectStackStage::Denoiser
    };
    RADIARY_CHECK_EQ(scene.effectStack, expected);
}

RADIARY_TEST(SceneKeyframeEvaluationFallsBackToNearestPoseWhenPosesCannotInterpolate) {
    using namespace radiary;

    Scene scene = CreateDefaultScene();
    scene.timelineStartFrame = 0;
    scene.timelineEndFrame = 20;

    SceneKeyframe first;
    first.frame = 0;
    first.ownerType = KeyframeOwnerType::Scene;
    first.ownerIndex = 0;
    first.pose = CaptureScenePose(scene);
    first.pose.camera.yaw = -0.25;
    first.pose.paths[0].controlPoints = {
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0}
    };
    first.pose.paths[0].thickness = 0.21;

    SceneKeyframe second;
    second.frame = 20;
    second.ownerType = KeyframeOwnerType::Scene;
    second.ownerIndex = 0;
    second.pose = CaptureScenePose(scene);
    second.pose.camera.yaw = 0.85;
    second.pose.paths[0].controlPoints = {
        {0.0, 0.0, 0.0},
        {0.5, 1.0, 0.0},
        {1.0, 0.0, 0.0}
    };
    second.pose.paths[0].thickness = 0.67;

    scene.keyframes = {first, second};
    SortKeyframes(scene);

    const Scene early = EvaluateSceneAtFrame(scene, 4.0);
    const Scene late = EvaluateSceneAtFrame(scene, 16.0);

    RADIARY_CHECK_NEAR(early.camera.yaw, first.pose.camera.yaw, 1e-9);
    RADIARY_CHECK_EQ(early.paths[0].controlPoints.size(), first.pose.paths[0].controlPoints.size());
    RADIARY_CHECK_NEAR(early.paths[0].thickness, first.pose.paths[0].thickness, 1e-9);

    RADIARY_CHECK_NEAR(late.camera.yaw, second.pose.camera.yaw, 1e-9);
    RADIARY_CHECK_EQ(late.paths[0].controlPoints.size(), second.pose.paths[0].controlPoints.size());
    RADIARY_CHECK_NEAR(late.paths[0].thickness, second.pose.paths[0].thickness, 1e-9);
}
