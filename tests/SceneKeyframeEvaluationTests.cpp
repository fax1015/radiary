#include "TestHarness.h"

#include "core/Scene.h"

RADIARY_TEST(SceneKeyframeEvaluationKeepsEffectsStaticWithoutEffectTrack) {
    using namespace radiary;

    Scene scene = CreateDefaultScene();
    scene.timelineStartFrame = 0;
    scene.timelineEndFrame = 20;
    scene.camera.yaw = 0.35;
    scene.postProcess.hueShiftEnabled = true;
    scene.postProcess.hueShiftDegrees = 45.0;
    scene.postProcess.hueShiftSaturation = 1.25;

    SceneKeyframe first;
    first.frame = 0;
    first.ownerType = KeyframeOwnerType::Scene;
    first.ownerIndex = 0;
    first.pose = CaptureScenePose(scene);
    first.pose.camera.yaw = 0.10;
    first.pose.postProcess.hueShiftDegrees = -120.0;

    SceneKeyframe second;
    second.frame = 20;
    second.ownerType = KeyframeOwnerType::Scene;
    second.ownerIndex = 0;
    second.pose = CaptureScenePose(scene);
    second.pose.camera.yaw = 0.90;
    second.pose.postProcess.hueShiftDegrees = 120.0;

    scene.keyframes = {first, second};
    SortKeyframes(scene);

    const Scene evaluated = EvaluateSceneAtFrame(scene, 10.0);
    RADIARY_CHECK_NEAR(evaluated.camera.yaw, 0.50, 1e-9);
    RADIARY_CHECK_EQ(evaluated.postProcess.hueShiftEnabled, scene.postProcess.hueShiftEnabled);
    RADIARY_CHECK_NEAR(evaluated.postProcess.hueShiftDegrees, scene.postProcess.hueShiftDegrees, 1e-9);
    RADIARY_CHECK_NEAR(evaluated.postProcess.hueShiftSaturation, scene.postProcess.hueShiftSaturation, 1e-9);
}

RADIARY_TEST(SceneKeyframeEvaluationAppliesOnlyTheTargetEffectTrack) {
    using namespace radiary;

    Scene scene = CreateDefaultScene();
    scene.timelineStartFrame = 0;
    scene.timelineEndFrame = 20;
    scene.camera.yaw = 0.42;
    scene.postProcess.hueShiftEnabled = true;
    scene.postProcess.hueShiftDegrees = 15.0;
    scene.postProcess.hueShiftSaturation = 1.0;
    scene.postProcess.sharpenEnabled = true;
    scene.postProcess.sharpenAmount = 0.22;

    SceneKeyframe first;
    first.frame = 0;
    first.ownerType = KeyframeOwnerType::Effect;
    first.ownerIndex = static_cast<int>(EffectStackStage::HueShift);
    first.pose = CaptureScenePose(scene);
    first.pose.camera.yaw = -0.75;
    first.pose.postProcess.hueShiftDegrees = 10.0;
    first.pose.postProcess.hueShiftSaturation = 0.8;
    first.pose.postProcess.sharpenAmount = 0.90;

    SceneKeyframe second;
    second.frame = 20;
    second.ownerType = KeyframeOwnerType::Effect;
    second.ownerIndex = static_cast<int>(EffectStackStage::HueShift);
    second.pose = CaptureScenePose(scene);
    second.pose.camera.yaw = 1.25;
    second.pose.postProcess.hueShiftDegrees = 70.0;
    second.pose.postProcess.hueShiftSaturation = 1.6;
    second.pose.postProcess.sharpenAmount = 0.05;

    scene.keyframes = {first, second};
    SortKeyframes(scene);

    const Scene evaluated = EvaluateSceneAtFrame(scene, 10.0);
    RADIARY_CHECK_NEAR(evaluated.camera.yaw, scene.camera.yaw, 1e-9);
    RADIARY_CHECK_NEAR(evaluated.postProcess.hueShiftDegrees, 40.0, 1e-9);
    RADIARY_CHECK_NEAR(evaluated.postProcess.hueShiftSaturation, 1.2, 1e-9);
    RADIARY_CHECK_NEAR(evaluated.postProcess.sharpenAmount, scene.postProcess.sharpenAmount, 1e-9);
}
