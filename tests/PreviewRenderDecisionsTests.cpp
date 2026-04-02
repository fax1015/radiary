#include "TestHarness.h"

#include "app/PreviewRenderDecisions.h"

RADIARY_TEST(PreviewResetReasonUsesExpectedPrecedence) {
    using namespace radiary;

    Scene before = CreateDefaultScene();
    Scene after = before;

    after.camera.yaw += 0.1;
    RADIARY_CHECK_EQ(DeterminePreviewResetReason(before, after), PreviewResetReason::CameraChanged);

    after = before;
    after.previewIterations += 1;
    after.camera.yaw += 0.1;
    RADIARY_CHECK_EQ(DeterminePreviewResetReason(before, after), PreviewResetReason::IterationChanged);

    after = before;
    after.mode = SceneMode::Hybrid;
    after.previewIterations += 1;
    after.camera.yaw += 0.1;
    RADIARY_CHECK_EQ(DeterminePreviewResetReason(before, after), PreviewResetReason::ModeChanged);

    after = before;
    after.gridVisible = !after.gridVisible;
    RADIARY_CHECK_EQ(DeterminePreviewResetReason(before, after), PreviewResetReason::SceneChanged);
}

RADIARY_TEST(PreviewRenderDecisionsMapContentAndResolvedStage) {
    using namespace radiary;

    RADIARY_CHECK_EQ(PreviewContentForMode(SceneMode::Flame), PreviewRenderContent::Flame);
    RADIARY_CHECK_EQ(PreviewContentForMode(SceneMode::Path), PreviewRenderContent::Path);
    RADIARY_CHECK_EQ(PreviewContentForMode(SceneMode::Hybrid), PreviewRenderContent::Hybrid);

    RADIARY_CHECK_EQ(DetermineResolvedRenderStage(false, false, false), PreviewRenderStage::Base);
    RADIARY_CHECK_EQ(DetermineResolvedRenderStage(true, false, false), PreviewRenderStage::Denoised);
    RADIARY_CHECK_EQ(DetermineResolvedRenderStage(true, true, false), PreviewRenderStage::DepthOfField);
    RADIARY_CHECK_EQ(DetermineResolvedRenderStage(true, true, true), PreviewRenderStage::PostProcessed);

    Scene legacyScene = CreateDefaultScene();
    legacyScene.effectStack.clear();
    legacyScene.depthOfField.enabled = true;
    legacyScene.postProcess.enabled = true;
    RADIARY_CHECK_EQ(DetermineResolvedRenderStage(legacyScene), PreviewRenderStage::PostProcessed);

    legacyScene.postProcess.enabled = false;
    RADIARY_CHECK_EQ(DetermineResolvedRenderStage(legacyScene), PreviewRenderStage::DepthOfField);
}
