#include "app/AppWindow.h"
#include "app/AppWidgets.h"

#include <algorithm>
#include <string>

#include "imgui.h"

using namespace radiary;

namespace {

bool NearlyEqual(const double a, const double b, const double epsilon = 1.0e-6) {
    return std::abs(a - b) <= epsilon;
}

}  // namespace

namespace radiary {
void AppWindow::EnsureSelectionIsValid() {
    for (PathSettings& path : scene_.paths) {
        if (path.controlPoints.size() < 2) {
            path = CreateDefaultScene().paths.front();
        }
    }
    scene_.selectedTransform = scene_.transforms.empty() ? 0 : std::clamp(scene_.selectedTransform, 0, static_cast<int>(scene_.transforms.size()) - 1);
    scene_.selectedPath = scene_.paths.empty() ? 0 : std::clamp(scene_.selectedPath, 0, static_cast<int>(scene_.paths.size()) - 1);
    NormalizeLayerSelections();
    if (scene_.gradientStops.size() < 4) {
        scene_.gradientStops = CreateDefaultScene().gradientStops;
    }
}

void AppWindow::ResetScene(Scene scene) {
    scene_ = std::move(scene);
    scene_.timelineEndFrame = std::max(scene_.timelineStartFrame, scene_.timelineEndFrame);
    scene_.timelineFrame = Clamp(scene_.timelineFrame, static_cast<double>(scene_.timelineStartFrame), static_cast<double>(scene_.timelineEndFrame));
    scene_.timelineSeconds = TimelineSecondsForFrame(scene_, scene_.timelineFrame);
    SortKeyframes(scene_);
    if (!scene_.keyframes.empty()) {
        const Scene evaluated = EvaluateSceneAtFrame(scene_, scene_.timelineFrame);
        ApplyScenePose(scene_, CaptureScenePose(evaluated));
        scene_.keyframes = evaluated.keyframes;
    }
    scene_.selectedTransform = 0;
    scene_.selectedPath = 0;
    selectedTransformLayers_.clear();
    selectedPathLayers_.clear();
    ClearEffectSelections();
    selectedTimelineKeyframe_ = FindKeyframeIndex(scene_, static_cast<int>(std::round(scene_.timelineFrame)));
    inspectorTarget_ = InspectorTarget::FlameLayer;
    renameTarget_ = RenameTarget::None;
    renamingLayerIndex_ = -1;
    focusLayerRename_ = false;
    layerRenameBuffer_.clear();
    EnsureSelectionIsValid();
    ClearPendingUndoCapture();
    sceneDirty_ = false;
    sceneModifiedSinceAutoSave_ = false;
    lastAutoSave_ = std::chrono::steady_clock::now();
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    UpdateWindowTitle();
}

void AppWindow::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (effectsPanelActive_) {
            RemoveSelectedEffect();
            return;
        }
        if ((playbackPanelActive_ || keyframeListPanelActive_) && RemoveSelectedOrCurrentKeyframe()) {
            return;
        }
        if ((layersPanelActive_ || inspectorPanelActive_) && RemoveSelectedLayers()) {
            return;
        }
        if (RemoveSelectedLayers()) {
            return;
        }
        if (playbackPanelActive_ || keyframeListPanelActive_ || HasSelectedLayer()) {
            RemoveSelectedOrCurrentKeyframe();
        }
        return;
    }

    if (!io.KeyCtrl) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        if (effectsPanelActive_) {
            SelectAllEffects();
            statusText_ = L"All effects selected";
            return;
        }
        SelectAllLayers(inspectorTarget_);
        statusText_ = L"All layers selected";
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift) {
            Redo();
        } else {
            Undo();
        }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        Redo();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        CopySelectedLayer();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        PasteCopiedLayer();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        DuplicateSelectedLayer();
    }
}

void AppWindow::CaptureWidgetUndo(const Scene& beforeChange, const bool changed) {
    if (changed) {
        SyncCurrentKeyframeFromScene();
    }

    if (ImGui::IsItemActivated()) {
        pendingUndoScene_ = beforeChange;
        hasPendingUndoScene_ = true;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const Scene& undoScene = hasPendingUndoScene_ ? pendingUndoScene_ : beforeChange;
        PushUndoState(undoScene, DescribeSceneChange(undoScene, scene_));
        hasPendingUndoScene_ = false;
        return;
    }

    if (ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        hasPendingUndoScene_ = false;
    }

    if (changed && !ImGui::IsItemActive()) {
        const Scene& undoScene = hasPendingUndoScene_ ? pendingUndoScene_ : beforeChange;
        PushUndoState(undoScene, DescribeSceneChange(undoScene, scene_));
        hasPendingUndoScene_ = false;
    }
}

void AppWindow::SetCurrentHistoryLabel(std::string label) {
    if (!label.empty()) {
        currentHistoryLabel_ = std::move(label);
    }
}

std::string AppWindow::DescribeSceneChange(const Scene& before, const Scene& after) const {
    if (before.name != after.name) {
        return "Rename Scene";
    }
    if (before.mode != after.mode) {
        return "Change Scene Mode";
    }
    if (before.previewIterations != after.previewIterations) {
        return "Change Render Iterations";
    }
    if (before.backgroundColor.r != after.backgroundColor.r
        || before.backgroundColor.g != after.backgroundColor.g
        || before.backgroundColor.b != after.backgroundColor.b
        || before.backgroundColor.a != after.backgroundColor.a) {
        return "Change Background";
    }
    if (!NearlyEqual(before.camera.frameWidth, after.camera.frameWidth) || !NearlyEqual(before.camera.frameHeight, after.camera.frameHeight)) {
        return "Adjust Camera Framing";
    }
    if (!NearlyEqual(before.camera.distance, after.camera.distance)
        || !NearlyEqual(before.camera.zoom2D, after.camera.zoom2D)
        || !NearlyEqual(before.camera.panX, after.camera.panX)
        || !NearlyEqual(before.camera.panY, after.camera.panY)
        || !NearlyEqual(before.camera.yaw, after.camera.yaw)
        || !NearlyEqual(before.camera.pitch, after.camera.pitch)) {
        return "Adjust Camera";
    }
    if (before.denoiser.enabled != after.denoiser.enabled) {
        return "Toggle Denoiser";
    }
    if (!NearlyEqual(before.denoiser.strength, after.denoiser.strength)) {
        return "Adjust Denoiser";
    }
    if (before.depthOfField.enabled != after.depthOfField.enabled) {
        return "Toggle Depth of Field";
    }
    if (!NearlyEqual(before.depthOfField.focusDepth, after.depthOfField.focusDepth)
        || !NearlyEqual(before.depthOfField.focusRange, after.depthOfField.focusRange)
        || !NearlyEqual(before.depthOfField.blurStrength, after.depthOfField.blurStrength)) {
        return "Adjust Depth of Field";
    }
    if (before.postProcess.enabled != after.postProcess.enabled) {
        return "Toggle Glow";
    }
    if (before.postProcess.curvesEnabled != after.postProcess.curvesEnabled) {
        return "Toggle Curves";
    }
    if (!NearlyEqual(before.postProcess.curveBlackPoint, after.postProcess.curveBlackPoint)
        || !NearlyEqual(before.postProcess.curveWhitePoint, after.postProcess.curveWhitePoint)
        || !NearlyEqual(before.postProcess.curveGamma, after.postProcess.curveGamma)) {
        return "Adjust Curves";
    }
    if (before.postProcess.sharpenEnabled != after.postProcess.sharpenEnabled) {
        return "Toggle Sharpen";
    }
    if (!NearlyEqual(before.postProcess.sharpenAmount, after.postProcess.sharpenAmount)) {
        return "Adjust Sharpen";
    }
    if (before.postProcess.hueShiftEnabled != after.postProcess.hueShiftEnabled) {
        return "Toggle Hue Shift";
    }
    if (!NearlyEqual(before.postProcess.hueShiftDegrees, after.postProcess.hueShiftDegrees)) {
        return "Adjust Hue Shift";
    }
    if (!NearlyEqual(before.postProcess.bloomIntensity, after.postProcess.bloomIntensity)
        || !NearlyEqual(before.postProcess.bloomThreshold, after.postProcess.bloomThreshold)) {
        return "Adjust Glow";
    }
    if (before.postProcess.chromaticAberrationEnabled != after.postProcess.chromaticAberrationEnabled) {
        return "Toggle Chromatic Aberration";
    }
    if (!NearlyEqual(before.postProcess.chromaticAberration, after.postProcess.chromaticAberration)) {
        return "Adjust Chromatic Aberration";
    }
    if (before.postProcess.vignetteEnabled != after.postProcess.vignetteEnabled) {
        return "Toggle Vignette";
    }
    if (!NearlyEqual(before.postProcess.vignetteIntensity, after.postProcess.vignetteIntensity)
        || !NearlyEqual(before.postProcess.vignetteRoundness, after.postProcess.vignetteRoundness)) {
        return "Adjust Vignette";
    }
    if (before.postProcess.toneMappingEnabled != after.postProcess.toneMappingEnabled) {
        return "Toggle Tone Mapping";
    }
    if (before.postProcess.filmGrainEnabled != after.postProcess.filmGrainEnabled) {
        return "Toggle Film Grain";
    }
    if (!NearlyEqual(before.postProcess.filmGrain, after.postProcess.filmGrain)) {
        return "Adjust Film Grain";
    }
    if (before.postProcess.colorTemperatureEnabled != after.postProcess.colorTemperatureEnabled) {
        return "Toggle Temperature";
    }
    if (!NearlyEqual(before.postProcess.colorTemperature, after.postProcess.colorTemperature)) {
        return "Adjust Temperature";
    }
    if (before.postProcess.saturationEnabled != after.postProcess.saturationEnabled) {
        return "Toggle Saturation";
    }
    if (!NearlyEqual(before.postProcess.saturationBoost, after.postProcess.saturationBoost)) {
        return "Adjust Saturation";
    }
    if (before.effectStack != after.effectStack) {
        return "Reorder Effects";
    }
    if (before.transforms.size() != after.transforms.size() || before.paths.size() != after.paths.size()) {
        return "Edit Layers";
    }
    if (before.keyframes.size() != after.keyframes.size()) {
        return "Edit Keyframes";
    }
    if (before.selectedTransform >= 0
        && before.selectedTransform < static_cast<int>(before.transforms.size())
        && before.selectedTransform < static_cast<int>(after.transforms.size())) {
        const TransformLayer& left = before.transforms[before.selectedTransform];
        const TransformLayer& right = after.transforms[before.selectedTransform];
        if (left.name != right.name
            || left.visible != right.visible
            || !NearlyEqual(left.weight, right.weight)
            || !NearlyEqual(left.rotationDegrees, right.rotationDegrees)
            || !NearlyEqual(left.scaleX, right.scaleX)
            || !NearlyEqual(left.scaleY, right.scaleY)
            || !NearlyEqual(left.translateX, right.translateX)
            || !NearlyEqual(left.translateY, right.translateY)
            || !NearlyEqual(left.shearX, right.shearX)
            || !NearlyEqual(left.shearY, right.shearY)
            || !NearlyEqual(left.colorIndex, right.colorIndex)
            || left.useCustomColor != right.useCustomColor
            || left.customColor.r != right.customColor.r
            || left.customColor.g != right.customColor.g
            || left.customColor.b != right.customColor.b
            || left.customColor.a != right.customColor.a) {
            return "Edit Flame Layer";
        }
    }
    if (before.selectedPath >= 0
        && before.selectedPath < static_cast<int>(before.paths.size())
        && before.selectedPath < static_cast<int>(after.paths.size())) {
        const PathSettings& left = before.paths[before.selectedPath];
        const PathSettings& right = after.paths[before.selectedPath];
        if (left.name != right.name
            || left.visible != right.visible
            || left.closed != right.closed
            || !NearlyEqual(left.thickness, right.thickness)
            || !NearlyEqual(left.taper, right.taper)
            || !NearlyEqual(left.twist, right.twist)
            || left.sampleCount != right.sampleCount
            || left.repeatCount != right.repeatCount) {
            return "Edit Path Layer";
        }
    }
    if (!NearlyEqual(before.flameRender.rotationXDegrees, after.flameRender.rotationXDegrees)
        || !NearlyEqual(before.flameRender.rotationYDegrees, after.flameRender.rotationYDegrees)
        || !NearlyEqual(before.flameRender.rotationZDegrees, after.flameRender.rotationZDegrees)
        || !NearlyEqual(before.flameRender.depthAmount, after.flameRender.depthAmount)
        || before.flameRender.symmetry != after.flameRender.symmetry
        || before.flameRender.symmetryOrder != after.flameRender.symmetryOrder
        || !NearlyEqual(before.flameRender.curveExposure, after.flameRender.curveExposure)
        || !NearlyEqual(before.flameRender.curveContrast, after.flameRender.curveContrast)
        || !NearlyEqual(before.flameRender.curveHighlights, after.flameRender.curveHighlights)
        || !NearlyEqual(before.flameRender.curveGamma, after.flameRender.curveGamma)) {
        return "Adjust Flame Render";
    }
    return "Edit Scene";
}

void AppWindow::PushUndoState(const Scene& snapshot, std::string label) {
    undoStack_.push_back({snapshot, currentHistoryLabel_});
    EnforceUndoStackLimits();
    redoStack_.clear();
    if (label.empty()) {
        label = DescribeSceneChange(snapshot, scene_);
    }
    currentHistoryLabel_ = std::move(label);
    sceneDirty_ = true;
    sceneModifiedSinceAutoSave_ = true;
    UpdateWindowTitle();
}

bool AppWindow::CanUndo() const {
    return !undoStack_.empty();
}

bool AppWindow::CanRedo() const {
    return !redoStack_.empty();
}

void AppWindow::Undo() {
    if (!CanUndo()) {
        return;
    }

    redoStack_.push_back({scene_, currentHistoryLabel_});
    const HistoryEntry entry = undoStack_.back();
    scene_ = entry.snapshot;
    undoStack_.pop_back();
    currentHistoryLabel_ = entry.label;
    selectedTransformLayers_.clear();
    selectedPathLayers_.clear();
    ClearEffectSelections();
    EnsureSelectionIsValid();
    ClearPendingUndoCapture();
    renameTarget_ = RenameTarget::None;
    renamingLayerIndex_ = -1;
    focusLayerRename_ = false;
    layerRenameBuffer_.clear();
    sceneDirty_ = true;
    sceneModifiedSinceAutoSave_ = true;
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    UpdateWindowTitle();
    statusText_ = L"Undo";
}

void AppWindow::Redo() {
    if (!CanRedo()) {
        return;
    }

    undoStack_.push_back({scene_, currentHistoryLabel_});
    EnforceUndoStackLimits();
    const HistoryEntry entry = redoStack_.back();
    scene_ = entry.snapshot;
    redoStack_.pop_back();
    currentHistoryLabel_ = entry.label;
    selectedTransformLayers_.clear();
    selectedPathLayers_.clear();
    ClearEffectSelections();
    EnsureSelectionIsValid();
    ClearPendingUndoCapture();
    renameTarget_ = RenameTarget::None;
    renamingLayerIndex_ = -1;
    focusLayerRename_ = false;
    layerRenameBuffer_.clear();
    sceneDirty_ = true;
    sceneModifiedSinceAutoSave_ = true;
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    UpdateWindowTitle();
    statusText_ = L"Redo";
}

void AppWindow::ClearPendingUndoCapture() {
    hasPendingUndoScene_ = false;
    viewportInteractionCaptured_ = false;
}

void AppWindow::EnforceUndoStackLimits() {
    undoHistoryLimit_ = std::clamp(undoHistoryLimit_, kMinUndoHistoryLimit, kMaxUndoHistoryLimit);
    const std::size_t historyLimit = static_cast<std::size_t>(undoHistoryLimit_);
    while (undoStack_.size() > historyLimit) {
        undoStack_.erase(undoStack_.begin());
    }
    while (redoStack_.size() > historyLimit) {
        redoStack_.erase(redoStack_.begin());
    }
}

void AppWindow::NormalizeLayerSelections() {
    NormalizeIndices(selectedTransformLayers_, static_cast<int>(scene_.transforms.size()), scene_.selectedTransform);
    NormalizeIndices(selectedPathLayers_, static_cast<int>(scene_.paths.size()), scene_.selectedPath);
    transformSelectionAnchor_ = scene_.transforms.empty()
        ? 0
        : std::clamp(transformSelectionAnchor_, 0, static_cast<int>(scene_.transforms.size()) - 1);
    pathSelectionAnchor_ = scene_.paths.empty()
        ? 0
        : std::clamp(pathSelectionAnchor_, 0, static_cast<int>(scene_.paths.size()) - 1);
    layerSelectionAnchorGlobal_ = std::clamp(layerSelectionAnchorGlobal_, 0, std::max(0, GlobalLayerCount() - 1));
}

void AppWindow::NormalizeEffectSelections() {
    int primaryIndex = selectedEffect_ < 0 ? 0 : selectedEffect_;
    NormalizeIndices(selectedEffects_, static_cast<int>(scene_.effectStack.size()), primaryIndex);
    if (scene_.effectStack.empty() || selectedEffects_.empty()) {
        selectedEffects_.clear();
        selectedEffect_ = -1;
        effectSelectionAnchor_ = 0;
        if (rightClickedEffectIndex_ >= static_cast<int>(scene_.effectStack.size())) {
            rightClickedEffectIndex_ = -1;
        }
        return;
    }

    selectedEffect_ = primaryIndex;
    effectSelectionAnchor_ = std::clamp(effectSelectionAnchor_, 0, static_cast<int>(scene_.effectStack.size()) - 1);
    if (!ContainsIndex(selectedEffects_, effectSelectionAnchor_)) {
        effectSelectionAnchor_ = selectedEffect_;
    }
    if (rightClickedEffectIndex_ >= static_cast<int>(scene_.effectStack.size())) {
        rightClickedEffectIndex_ = -1;
    }
}

void AppWindow::ClearEffectSelections() {
    selectedEffects_.clear();
    selectedEffect_ = -1;
    effectSelectionAnchor_ = 0;
    rightClickedEffectIndex_ = -1;
}

void AppWindow::SetEffectSelection(std::vector<int> indices, const int primaryIndex, const int anchorIndex) {
    selectedEffects_ = std::move(indices);
    selectedEffect_ = primaryIndex;
    effectSelectionAnchor_ = anchorIndex;
    NormalizeEffectSelections();
}

void AppWindow::SelectSingleEffect(const int index) {
    if (scene_.effectStack.empty()) {
        return;
    }
    ClearLayerSelections();
    const int clampedIndex = std::clamp(index, 0, static_cast<int>(scene_.effectStack.size()) - 1);
    SetEffectSelection({clampedIndex}, clampedIndex, clampedIndex);
}

void AppWindow::ToggleEffectSelection(const int index) {
    if (scene_.effectStack.empty()) {
        return;
    }
    ClearLayerSelections();
    const int clampedIndex = std::clamp(index, 0, static_cast<int>(scene_.effectStack.size()) - 1);
    std::vector<int> selection = selectedEffects_;
    auto it = std::find(selection.begin(), selection.end(), clampedIndex);
    if (it != selection.end()) {
        if (selection.size() > 1) {
            selection.erase(it);
        }
    } else {
        selection.push_back(clampedIndex);
    }

    int nextPrimary = clampedIndex;
    if (!ContainsIndex(selection, clampedIndex)) {
        nextPrimary = selection.empty() ? -1 : selection.front();
    }
    SetEffectSelection(std::move(selection), nextPrimary, clampedIndex);
}

void AppWindow::SelectEffectRange(const int index) {
    if (scene_.effectStack.empty()) {
        return;
    }
    ClearLayerSelections();
    const int clampedIndex = std::clamp(index, 0, static_cast<int>(scene_.effectStack.size()) - 1);
    const int anchorIndex = std::clamp(effectSelectionAnchor_, 0, static_cast<int>(scene_.effectStack.size()) - 1);
    const int rangeStart = std::min(anchorIndex, clampedIndex);
    const int rangeEnd = std::max(anchorIndex, clampedIndex);
    std::vector<int> selection;
    selection.reserve(static_cast<std::size_t>(rangeEnd - rangeStart + 1));
    for (int current = rangeStart; current <= rangeEnd; ++current) {
        selection.push_back(current);
    }
    SetEffectSelection(std::move(selection), clampedIndex, clampedIndex);
}

void AppWindow::SelectAllEffects() {
    ClearLayerSelections();
    selectedEffects_.clear();
    selectedEffects_.reserve(scene_.effectStack.size());
    for (int index = 0; index < static_cast<int>(scene_.effectStack.size()); ++index) {
        selectedEffects_.push_back(index);
    }
    if (selectedEffects_.empty()) {
        ClearEffectSelections();
        return;
    }
    selectedEffect_ = selectedEffects_.front();
    effectSelectionAnchor_ = selectedEffect_;
    NormalizeEffectSelections();
}

bool AppWindow::IsEffectSelected(const int index) const {
    return ContainsIndex(selectedEffects_, index);
}

int AppWindow::SelectedEffectCount() const {
    return static_cast<int>(selectedEffects_.size());
}

bool AppWindow::HasMultipleEffectsSelected() const {
    return SelectedEffectCount() > 1;
}

std::vector<int>& AppWindow::MutableLayerSelection(const InspectorTarget target) {
    return target == InspectorTarget::PathLayer ? selectedPathLayers_ : selectedTransformLayers_;
}

const std::vector<int>& AppWindow::LayerSelection(const InspectorTarget target) const {
    return target == InspectorTarget::PathLayer ? selectedPathLayers_ : selectedTransformLayers_;
}

int& AppWindow::PrimaryLayerIndex(const InspectorTarget target) {
    return target == InspectorTarget::PathLayer ? scene_.selectedPath : scene_.selectedTransform;
}

int AppWindow::PrimaryLayerIndex(const InspectorTarget target) const {
    return target == InspectorTarget::PathLayer ? scene_.selectedPath : scene_.selectedTransform;
}

int& AppWindow::LayerSelectionAnchor(const InspectorTarget target) {
    return target == InspectorTarget::PathLayer ? pathSelectionAnchor_ : transformSelectionAnchor_;
}

int AppWindow::LayerCount(const InspectorTarget target) const {
    return target == InspectorTarget::PathLayer
        ? static_cast<int>(scene_.paths.size())
        : static_cast<int>(scene_.transforms.size());
}

int AppWindow::GlobalLayerCount() const {
    return static_cast<int>(scene_.transforms.size() + scene_.paths.size());
}

int AppWindow::GlobalLayerIndex(const InspectorTarget target, const int index) const {
    if (target == InspectorTarget::PathLayer) {
        return static_cast<int>(scene_.transforms.size()) + index;
    }
    return index;
}

bool AppWindow::ResolveGlobalLayerIndex(const int globalIndex, InspectorTarget& target, int& index) const {
    if (globalIndex < 0 || globalIndex >= GlobalLayerCount()) {
        return false;
    }
    const int transformCount = static_cast<int>(scene_.transforms.size());
    if (globalIndex < transformCount) {
        target = InspectorTarget::FlameLayer;
        index = globalIndex;
        return true;
    }
    target = InspectorTarget::PathLayer;
    index = globalIndex - transformCount;
    return true;
}

void AppWindow::ClearLayerSelections() {
    selectedTransformLayers_.clear();
    selectedPathLayers_.clear();
}

void AppWindow::SetLayerSelection(const InspectorTarget target, std::vector<int> indices, const int primaryIndex, const int anchorIndex) {
    inspectorTarget_ = target;
    const int itemCount = LayerCount(target);
    std::vector<int>& selection = MutableLayerSelection(target);
    int& primary = PrimaryLayerIndex(target);
    int& anchor = LayerSelectionAnchor(target);

    selection = std::move(indices);
    std::sort(selection.begin(), selection.end());
    selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
    primary = primaryIndex;
    anchor = anchorIndex;

    NormalizeIndices(selection, itemCount, primary);
    if (itemCount <= 0) {
        anchor = 0;
        layerSelectionAnchorGlobal_ = std::clamp(layerSelectionAnchorGlobal_, 0, std::max(0, GlobalLayerCount() - 1));
        return;
    }

    if (!ContainsIndex(selection, std::clamp(anchor, 0, itemCount - 1))) {
        anchor = primary;
    } else {
        anchor = std::clamp(anchor, 0, itemCount - 1);
    }
    layerSelectionAnchorGlobal_ = GlobalLayerIndex(target, anchor);
}

void AppWindow::AssignCurrentLayerToKeyframe(SceneKeyframe& keyframe) const {
    const auto [ownerType, ownerIndex] = CurrentKeyframeOwner();
    keyframe.ownerType = ownerType;
    keyframe.ownerIndex = ownerIndex;
}

std::pair<KeyframeOwnerType, int> AppWindow::CurrentKeyframeOwner() const {
    if (selectedEffects_.size() == 1
        && selectedEffect_ >= 0
        && selectedEffect_ < static_cast<int>(scene_.effectStack.size())
        && ContainsIndex(selectedEffects_, selectedEffect_)) {
        return {KeyframeOwnerType::Effect, static_cast<int>(scene_.effectStack[static_cast<std::size_t>(selectedEffect_)])};
    }

    if (!HasSelectedLayer()) {
        return {KeyframeOwnerType::Scene, 0};
    }

    if (inspectorTarget_ == InspectorTarget::PathLayer) {
        return {KeyframeOwnerType::Path, std::clamp(scene_.selectedPath, 0, std::max(0, static_cast<int>(scene_.paths.size()) - 1))};
    }

    return {KeyframeOwnerType::Transform, std::clamp(scene_.selectedTransform, 0, std::max(0, static_cast<int>(scene_.transforms.size()) - 1))};
}

Color AppWindow::MarkerColorForKeyframeOwner(const KeyframeOwnerType ownerType, const int ownerIndex) const {
    switch (ownerType) {
    case KeyframeOwnerType::Scene:
        return {180, 160, 220, 255};
    case KeyframeOwnerType::Path:
        return {96, 188, 224, 255};
    case KeyframeOwnerType::Effect:
        switch (static_cast<EffectStackStage>(ownerIndex)) {
        case EffectStackStage::Denoiser: return {84, 214, 196, 255};
        case EffectStackStage::DepthOfField: return {108, 164, 246, 255};
        case EffectStackStage::Curves: return {244, 132, 188, 255};
        case EffectStackStage::Sharpen: return {250, 138, 108, 255};
        case EffectStackStage::HueShift: return {226, 188, 92, 255};
        case EffectStackStage::PostProcess: return {246, 208, 112, 255};
        case EffectStackStage::ChromaticAberration: return {180, 126, 246, 255};
        case EffectStackStage::ColorTemperature: return {255, 166, 118, 255};
        case EffectStackStage::Saturation: return {116, 220, 138, 255};
        case EffectStackStage::ToneMapping: return {110, 214, 236, 255};
        case EffectStackStage::FilmGrain: return {208, 188, 156, 255};
        case EffectStackStage::Vignette: return {148, 156, 184, 255};
        default:
            break;
        }
        return {196, 164, 236, 255};
    case KeyframeOwnerType::Transform:
    default:
        return {248, 164, 88, 255};
    }
}

int AppWindow::EffectIndexForKeyframeOwner(const int ownerIndex) const {
    const EffectStackStage stage = static_cast<EffectStackStage>(ownerIndex);
    for (std::size_t index = 0; index < scene_.effectStack.size(); ++index) {
        if (scene_.effectStack[index] == stage) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void AppWindow::AdjustKeyframeOwnerIndicesForInsertedLayer(const InspectorTarget target, const int insertedIndex) {
    const KeyframeOwnerType ownerType = target == InspectorTarget::PathLayer ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
    for (SceneKeyframe& keyframe : scene_.keyframes) {
        if (keyframe.ownerType == ownerType && keyframe.ownerIndex >= insertedIndex) {
            ++keyframe.ownerIndex;
        }
    }
}

void AppWindow::AdjustKeyframeOwnerIndicesForRemovedLayers(const InspectorTarget target, const std::vector<int>& removedIndices) {
    if (removedIndices.empty()) {
        return;
    }

    const KeyframeOwnerType ownerType = target == InspectorTarget::PathLayer ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
    for (SceneKeyframe& keyframe : scene_.keyframes) {
        if (keyframe.ownerType != ownerType) {
            continue;
        }

        const int removedBefore = static_cast<int>(std::count_if(removedIndices.begin(), removedIndices.end(), [&](const int removedIndex) {
            return removedIndex < keyframe.ownerIndex;
        }));
        if (ContainsIndex(removedIndices, keyframe.ownerIndex)) {
            keyframe.ownerIndex = std::max(0, keyframe.ownerIndex - removedBefore - 1);
        } else {
            keyframe.ownerIndex = std::max(0, keyframe.ownerIndex - removedBefore);
        }
    }
}

void AppWindow::SelectSingleLayer(const InspectorTarget target, const int index) {
    if (LayerCount(target) <= 0) {
        return;
    }
    ClearEffectSelections();
    const int clampedIndex = std::clamp(index, 0, std::max(0, LayerCount(target) - 1));
    ClearLayerSelections();
    SetLayerSelection(target, {clampedIndex}, clampedIndex, clampedIndex);
}

void AppWindow::ToggleLayerSelection(const InspectorTarget target, const int index) {
    const int itemCount = LayerCount(target);
    if (itemCount <= 0) {
        return;
    }
    ClearEffectSelections();
    const int clampedIndex = std::clamp(index, 0, std::max(0, itemCount - 1));
    std::vector<int> selection = LayerSelection(target);
    auto it = std::find(selection.begin(), selection.end(), clampedIndex);
    if (it != selection.end()) {
        if (selection.size() > 1 || SelectedLayerCount() > 1) {
            selection.erase(it);
        }
    } else {
        selection.push_back(clampedIndex);
    }

    int nextPrimary = clampedIndex;
    if (!ContainsIndex(selection, clampedIndex)) {
        nextPrimary = selection.empty() ? 0 : selection.front();
    }
    SetLayerSelection(target, std::move(selection), nextPrimary, clampedIndex);
    if (LayerSelection(target).empty()) {
        inspectorTarget_ = target == InspectorTarget::PathLayer ? InspectorTarget::FlameLayer : InspectorTarget::PathLayer;
    }
}

void AppWindow::SelectLayerRange(const InspectorTarget target, const int index) {
    if (LayerCount(target) <= 0 || GlobalLayerCount() <= 0) {
        return;
    }
    ClearEffectSelections();
    const int clampedGlobalIndex = std::clamp(GlobalLayerIndex(target, index), 0, std::max(0, GlobalLayerCount() - 1));
    const int anchorGlobalIndex = std::clamp(layerSelectionAnchorGlobal_, 0, std::max(0, GlobalLayerCount() - 1));
    const int rangeStart = std::min(anchorGlobalIndex, clampedGlobalIndex);
    const int rangeEnd = std::max(anchorGlobalIndex, clampedGlobalIndex);
    ClearLayerSelections();
    for (int current = rangeStart; current <= rangeEnd; ++current) {
        InspectorTarget currentTarget = InspectorTarget::FlameLayer;
        int currentIndex = 0;
        if (!ResolveGlobalLayerIndex(current, currentTarget, currentIndex)) {
            continue;
        }
        MutableLayerSelection(currentTarget).push_back(currentIndex);
    }
    const int clampedIndex = std::clamp(index, 0, std::max(0, LayerCount(target) - 1));
    inspectorTarget_ = target;
    PrimaryLayerIndex(target) = clampedIndex;
    LayerSelectionAnchor(target) = clampedIndex;
    NormalizeLayerSelections();
}

void AppWindow::SelectAllLayers(const InspectorTarget target) {
    ClearEffectSelections();
    selectedTransformLayers_.clear();
    selectedTransformLayers_.reserve(scene_.transforms.size());
    for (int index = 0; index < static_cast<int>(scene_.transforms.size()); ++index) {
        selectedTransformLayers_.push_back(index);
    }
    selectedPathLayers_.clear();
    selectedPathLayers_.reserve(scene_.paths.size());
    for (int index = 0; index < static_cast<int>(scene_.paths.size()); ++index) {
        selectedPathLayers_.push_back(index);
    }
    inspectorTarget_ = target;
    if (GlobalLayerCount() > 0) {
        layerSelectionAnchorGlobal_ = std::clamp(GlobalLayerIndex(target, PrimaryLayerIndex(target)), 0, GlobalLayerCount() - 1);
    } else {
        layerSelectionAnchorGlobal_ = 0;
    }
    NormalizeLayerSelections();
}

bool AppWindow::ApplyLayerVisibilityToSelectionOrItem(const InspectorTarget target, const int index, const bool visible) {
    EnsureSelectionIsValid();

    std::vector<std::pair<InspectorTarget, int>> targets;
    if (IsLayerSelected(target, index) && SelectedLayerCount() > 1) {
        targets.reserve(static_cast<std::size_t>(SelectedLayerCount()));
        for (const int transformIndex : selectedTransformLayers_) {
            targets.emplace_back(InspectorTarget::FlameLayer, transformIndex);
        }
        for (const int pathIndex : selectedPathLayers_) {
            targets.emplace_back(InspectorTarget::PathLayer, pathIndex);
        }
    } else {
        targets.emplace_back(target, index);
    }

    bool changed = false;
    for (const auto& [targetType, targetIndex] : targets) {
        if (targetType == InspectorTarget::PathLayer) {
            if (targetIndex >= 0 && targetIndex < static_cast<int>(scene_.paths.size()) && scene_.paths[static_cast<std::size_t>(targetIndex)].visible != visible) {
                scene_.paths[static_cast<std::size_t>(targetIndex)].visible = visible;
                changed = true;
            }
            continue;
        }

        if (targetIndex >= 0 && targetIndex < static_cast<int>(scene_.transforms.size()) && scene_.transforms[static_cast<std::size_t>(targetIndex)].visible != visible) {
            scene_.transforms[static_cast<std::size_t>(targetIndex)].visible = visible;
            changed = true;
        }
    }
    return changed;
}

bool AppWindow::ToggleLayerVisibilityForSelectionOrItem(const InspectorTarget target, const int index) {
    EnsureSelectionIsValid();

    bool currentVisibility = true;
    if (target == InspectorTarget::PathLayer) {
        if (index < 0 || index >= static_cast<int>(scene_.paths.size())) {
            return false;
        }
        currentVisibility = scene_.paths[static_cast<std::size_t>(index)].visible;
    } else {
        if (index < 0 || index >= static_cast<int>(scene_.transforms.size())) {
            return false;
        }
        currentVisibility = scene_.transforms[static_cast<std::size_t>(index)].visible;
    }

    return ApplyLayerVisibilityToSelectionOrItem(target, index, !currentVisibility);
}

bool AppWindow::IsLayerSelected(const InspectorTarget target, const int index) const {
    if (target == InspectorTarget::PathLayer) {
        return ContainsIndex(selectedPathLayers_, index);
    }
    return ContainsIndex(selectedTransformLayers_, index);
}

int AppWindow::SelectedLayerCount() const {
    return static_cast<int>(selectedTransformLayers_.size() + selectedPathLayers_.size());
}

bool AppWindow::HasSelectedLayer() const {
    return SelectedLayerCount() > 0;
}

bool AppWindow::HasMultipleLayersSelected() const {
    return SelectedLayerCount() > 1;
}

bool AppWindow::CanRemoveSelectedLayers() const {
    return SelectedLayerCount() > 0;
}

bool AppWindow::RemoveSelectedLayers() {
    FinishLayerRename(false);
    EnsureSelectionIsValid();
    if (!CanRemoveSelectedLayers()) {
        return false;
    }

    const int removedCount = SelectedLayerCount();
    PushUndoState(scene_);
    std::vector<int> indices = selectedTransformLayers_;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::vector<int> pathIndices = selectedPathLayers_;
    std::sort(pathIndices.begin(), pathIndices.end());
    pathIndices.erase(std::unique(pathIndices.begin(), pathIndices.end()), pathIndices.end());
    AdjustKeyframeOwnerIndicesForRemovedLayers(InspectorTarget::PathLayer, pathIndices);
    for (auto it = pathIndices.rbegin(); it != pathIndices.rend(); ++it) {
        scene_.paths.erase(scene_.paths.begin() + *it);
    }
    AdjustKeyframeOwnerIndicesForRemovedLayers(InspectorTarget::FlameLayer, indices);
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        scene_.transforms.erase(scene_.transforms.begin() + *it);
    }
    scene_.selectedTransform = scene_.transforms.empty() ? 0 : std::clamp(scene_.selectedTransform, 0, static_cast<int>(scene_.transforms.size()) - 1);
    scene_.selectedPath = scene_.paths.empty() ? 0 : std::clamp(scene_.selectedPath, 0, static_cast<int>(scene_.paths.size()) - 1);
    ClearLayerSelections();
    NormalizeLayerSelections();
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    statusText_ = removedCount > 1 ? L"Layers removed" : L"Layer removed";
    return true;
}

bool AppWindow::CanRemoveSelectedEffect() const {
    return SelectedEffectCount() > 0;
}

bool AppWindow::RemoveSelectedEffect() {
    if (!CanRemoveSelectedEffect()) {
        return false;
    }

    NormalizeEffectSelections();
    if (selectedEffects_.empty()) {
        return false;
    }
    if (selectedEffects_.size() == 1) {
        const int effectIndex = selectedEffects_.front();
        return RemoveEffectAtIndex(effectIndex, std::string("Delete ") + EffectStageDisplayName(scene_.effectStack[effectIndex]));
    }

    const Scene beforeDelete = scene_;
    std::vector<int> indices = selectedEffects_;
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (*it >= 0 && *it < static_cast<int>(scene_.effectStack.size())) {
            EnableEffectStage(scene_, scene_.effectStack[*it], false);
            scene_.effectStack.erase(scene_.effectStack.begin() + *it);
        }
    }
    ClearEffectSelections();
    SyncCurrentKeyframeFromScene();
    PushUndoState(beforeDelete, "Delete Effects");
    MarkViewportDirty(DeterminePreviewResetReason(beforeDelete, scene_));
    return true;
}

bool AppWindow::RemoveEffectAtIndex(const int effectIndex, const std::string& actionLabel) {
    if (effectIndex < 0 || effectIndex >= static_cast<int>(scene_.effectStack.size())) {
        return false;
    }

    const Scene beforeDelete = scene_;
    EnableEffectStage(scene_, scene_.effectStack[effectIndex], false);
    scene_.effectStack.erase(scene_.effectStack.begin() + effectIndex);
    for (int& selectedIndex : selectedEffects_) {
        if (selectedIndex > effectIndex) {
            --selectedIndex;
        }
    }
    selectedEffects_.erase(std::remove(selectedEffects_.begin(), selectedEffects_.end(), effectIndex), selectedEffects_.end());
    if (selectedEffect_ > effectIndex) {
        --selectedEffect_;
    }
    if (rightClickedEffectIndex_ == effectIndex) {
        rightClickedEffectIndex_ = -1;
    } else if (rightClickedEffectIndex_ > effectIndex) {
        --rightClickedEffectIndex_;
    }
    NormalizeEffectSelections();
    SyncCurrentKeyframeFromScene();
    PushUndoState(beforeDelete, actionLabel);
    MarkViewportDirty(DeterminePreviewResetReason(beforeDelete, scene_));
    return true;
}

bool AppWindow::CanRemoveSelectedOrCurrentKeyframe() const {
    if (selectedTimelineKeyframe_ >= 0) {
        return selectedTimelineKeyframe_ < static_cast<int>(scene_.keyframes.size());
    }
    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const auto [currentOwnerType, currentOwnerIndex] = CurrentKeyframeOwner();
    return FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex) >= 0;
}

bool AppWindow::RemoveSelectedOrCurrentKeyframe() {
    if (selectedTimelineKeyframe_ < 0 && scene_.keyframes.empty()) {
        return false;
    }
    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const auto [currentOwnerType, currentOwnerIndex] = CurrentKeyframeOwner();
    const int currentKeyframeIndex = FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex);
    const int removeIndex = selectedTimelineKeyframe_ >= 0 ? selectedTimelineKeyframe_ : currentKeyframeIndex;
    if (removeIndex < 0 || removeIndex >= static_cast<int>(scene_.keyframes.size())) {
        return false;
    }

    PushUndoState(scene_);
    scene_.keyframes.erase(scene_.keyframes.begin() + removeIndex);
    selectedTimelineKeyframe_ = -1;
    RefreshTimelinePose();
    statusText_ = L"Keyframe removed";
    return true;
}

bool AppWindow::HasKeyframesForOwner(const KeyframeOwnerType ownerType, const int ownerIndex) const {
    return std::any_of(scene_.keyframes.begin(), scene_.keyframes.end(), [&](const SceneKeyframe& keyframe) {
        return keyframe.ownerType == ownerType
            && (ownerType == KeyframeOwnerType::Scene || keyframe.ownerIndex == ownerIndex);
    });
}

void AppWindow::CopySelectedLayer() {
    FinishLayerRename(false);
    EnsureSelectionIsValid();

    if (!HasSelectedLayer()) {
        statusText_ = L"No layer selected to copy";
        return;
    }

    if (inspectorTarget_ == InspectorTarget::PathLayer) {
        if (scene_.paths.empty()) {
            statusText_ = L"No path layer to copy";
            return;
        }
        copiedPathLayer_ = scene_.paths[static_cast<std::size_t>(scene_.selectedPath)];
        layerClipboardType_ = LayerClipboardType::Path;
        statusText_ = L"Path layer copied";
        return;
    }

    if (scene_.transforms.empty()) {
        statusText_ = L"No layer to copy";
        return;
    }
    copiedTransformLayer_ = scene_.transforms[static_cast<std::size_t>(scene_.selectedTransform)];
    layerClipboardType_ = LayerClipboardType::Transform;
    statusText_ = L"Layer copied";
}

void AppWindow::PasteCopiedLayer() {
    FinishLayerRename(false);
    EnsureSelectionIsValid();

    if (layerClipboardType_ == LayerClipboardType::None) {
        statusText_ = L"Nothing to paste";
        return;
    }

    PushUndoState(scene_);
    if (layerClipboardType_ == LayerClipboardType::Path) {
        PathSettings path = copiedPathLayer_;
        path.name = MakeUniqueCopyName(path.name, "Path Layer", scene_.paths);
        path.visible = true;
        const int insertAfterIndex = LayerSelection(InspectorTarget::PathLayer).empty()
            ? static_cast<int>(scene_.paths.size()) - 1
            : scene_.selectedPath;
        const int insertIndex = InsertLayerCopy(scene_.paths, insertAfterIndex, std::move(path));
        AdjustKeyframeOwnerIndicesForInsertedLayer(InspectorTarget::PathLayer, insertIndex);
        SelectSingleLayer(InspectorTarget::PathLayer, insertIndex);
        MarkViewportDirty(PreviewResetReason::SceneChanged);
        statusText_ = L"Path layer pasted";
        return;
    }

    TransformLayer layer = copiedTransformLayer_;
    layer.name = MakeUniqueCopyName(layer.name, "Layer", scene_.transforms);
    layer.visible = true;
    const int insertAfterIndex = LayerSelection(InspectorTarget::FlameLayer).empty()
        ? static_cast<int>(scene_.transforms.size()) - 1
        : scene_.selectedTransform;
    const int insertIndex = InsertLayerCopy(scene_.transforms, insertAfterIndex, std::move(layer));
    AdjustKeyframeOwnerIndicesForInsertedLayer(InspectorTarget::FlameLayer, insertIndex);
    SelectSingleLayer(InspectorTarget::FlameLayer, insertIndex);
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    statusText_ = L"Layer pasted";
}

void AppWindow::DuplicateSelectedLayer() {
    FinishLayerRename(false);
    EnsureSelectionIsValid();

    if (!HasSelectedLayer()) {
        statusText_ = L"No layer selected to duplicate";
        return;
    }

    PushUndoState(scene_);

    if (inspectorTarget_ == InspectorTarget::PathLayer) {
        if (scene_.paths.empty()) {
            statusText_ = L"No path layer to duplicate";
            return;
        }
        PathSettings path = scene_.paths[static_cast<std::size_t>(scene_.selectedPath)];
        path.name = MakeUniqueCopyName(path.name, "Path Layer", scene_.paths);
        path.visible = true;
        const int insertIndex = InsertLayerCopy(scene_.paths, scene_.selectedPath, std::move(path));
        AdjustKeyframeOwnerIndicesForInsertedLayer(InspectorTarget::PathLayer, insertIndex);
        SelectSingleLayer(InspectorTarget::PathLayer, insertIndex);
        MarkViewportDirty(PreviewResetReason::SceneChanged);
        statusText_ = L"Path layer duplicated";
        return;
    }

    if (scene_.transforms.empty()) {
        statusText_ = L"No layer to duplicate";
        return;
    }
    TransformLayer layer = scene_.transforms[static_cast<std::size_t>(scene_.selectedTransform)];
    layer.name = MakeUniqueCopyName(layer.name, "Layer", scene_.transforms);
    layer.visible = true;
    const int insertIndex = InsertLayerCopy(scene_.transforms, scene_.selectedTransform, std::move(layer));
    AdjustKeyframeOwnerIndicesForInsertedLayer(InspectorTarget::FlameLayer, insertIndex);
    SelectSingleLayer(InspectorTarget::FlameLayer, insertIndex);
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    statusText_ = L"Layer duplicated";
}

void AppWindow::BeginLayerRename(const RenameTarget target, const int index) {
    if (target == RenameTarget::Transform && (index < 0 || index >= static_cast<int>(scene_.transforms.size()))) {
        return;
    }
    if (target == RenameTarget::Path && (index < 0 || index >= static_cast<int>(scene_.paths.size()))) {
        return;
    }

    renameTarget_ = target;
    renamingLayerIndex_ = index;
    focusLayerRename_ = true;
    layerRenameBuffer_ = target == RenameTarget::Path
        ? scene_.paths[static_cast<std::size_t>(index)].name
        : scene_.transforms[static_cast<std::size_t>(index)].name;
    layerRenameSnapshot_ = scene_;
}

void AppWindow::FinishLayerRename(const bool commit) {
    const bool validTransform = renameTarget_ == RenameTarget::Transform
        && renamingLayerIndex_ >= 0
        && renamingLayerIndex_ < static_cast<int>(scene_.transforms.size());
    const bool validPath = renameTarget_ == RenameTarget::Path
        && renamingLayerIndex_ >= 0
        && renamingLayerIndex_ < static_cast<int>(scene_.paths.size());
    if (!validTransform && !validPath) {
        renameTarget_ = RenameTarget::None;
        renamingLayerIndex_ = -1;
        focusLayerRename_ = false;
        layerRenameBuffer_.clear();
        return;
    }

    if (commit) {
        std::string finalName = layerRenameBuffer_;
        if (finalName.empty()) {
            finalName = validPath
                ? scene_.paths[static_cast<std::size_t>(renamingLayerIndex_)].name
                : scene_.transforms[static_cast<std::size_t>(renamingLayerIndex_)].name;
        }
        const std::string& currentName = validPath
            ? scene_.paths[static_cast<std::size_t>(renamingLayerIndex_)].name
            : scene_.transforms[static_cast<std::size_t>(renamingLayerIndex_)].name;
        if (finalName != currentName) {
            PushUndoState(layerRenameSnapshot_);
            if (validPath) {
                scene_.paths[static_cast<std::size_t>(renamingLayerIndex_)].name = std::move(finalName);
            } else {
                scene_.transforms[static_cast<std::size_t>(renamingLayerIndex_)].name = std::move(finalName);
            }
            UpdateWindowTitle();
        }
    }

    renameTarget_ = RenameTarget::None;
    renamingLayerIndex_ = -1;
    focusLayerRename_ = false;
    layerRenameBuffer_.clear();
}

void AppWindow::SetTimelineFrame(const double frame, const bool captureUndo) {
    const double clampedFrame = Clamp(frame, static_cast<double>(scene_.timelineStartFrame), static_cast<double>(std::max(scene_.timelineStartFrame, scene_.timelineEndFrame)));
    if (std::abs(scene_.timelineFrame - clampedFrame) < 1.0e-6) {
        return;
    }
    if (captureUndo) {
        PushUndoState(scene_);
    }
    scene_.timelineFrame = clampedFrame;
    scene_.timelineSeconds = TimelineSecondsForFrame(scene_, scene_.timelineFrame);
    RefreshTimelinePose();
}

void AppWindow::RefreshTimelinePose() {
    const double clampedFrame = Clamp(scene_.timelineFrame, static_cast<double>(scene_.timelineStartFrame), static_cast<double>(std::max(scene_.timelineStartFrame, scene_.timelineEndFrame)));
    scene_.timelineFrame = clampedFrame;
    scene_.timelineSeconds = TimelineSecondsForFrame(scene_, scene_.timelineFrame);
    if (!scene_.keyframes.empty()) {
        const Scene evaluated = EvaluateSceneAtFrame(scene_, scene_.timelineFrame);
        ApplyScenePose(scene_, CaptureScenePose(evaluated));
        scene_.timelineFrame = clampedFrame;
        scene_.timelineSeconds = TimelineSecondsForFrame(scene_, scene_.timelineFrame);
        scene_.keyframes = evaluated.keyframes;
    }
    MarkViewportDirty(PreviewResetReason::SceneChanged);
}

void AppWindow::AutoKeyCurrentFrame() {
    const auto [currentOwnerType, currentOwnerIndex] = CurrentKeyframeOwner();
    AutoKeyCurrentFrame(currentOwnerType, currentOwnerIndex);
}

void AppWindow::AutoKeyCurrentFrame(const KeyframeOwnerType currentOwnerType, const int currentOwnerIndex) {
    if (scene_.keyframes.empty()) {
        return;
    }

    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    if (FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex) >= 0) {
        return;
    }

    SceneKeyframe keyframe;
    keyframe.frame = currentFrame;
    keyframe.markerColor = MarkerColorForKeyframeOwner(currentOwnerType, currentOwnerIndex);
    AssignCurrentLayerToKeyframe(keyframe);
    keyframe.pose = CaptureScenePose(scene_);
    scene_.keyframes.push_back(std::move(keyframe));
    SortKeyframes(scene_);
    selectedTimelineKeyframe_ = FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex);
}

void AppWindow::SyncCurrentKeyframeFromScene() {
    const auto [currentOwnerType, currentOwnerIndex] = CurrentKeyframeOwner();
    SyncCurrentKeyframeFromScene(currentOwnerType, currentOwnerIndex);
}

void AppWindow::SyncCurrentKeyframeFromScene(const KeyframeOwnerType currentOwnerType, const int currentOwnerIndex) {
    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const int keyframeIndex = FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex);
    if (keyframeIndex < 0) {
        return;
    }
    scene_.keyframes[static_cast<std::size_t>(keyframeIndex)].pose = CaptureScenePose(scene_);
    SortKeyframes(scene_);
}

void AppWindow::LoadPreset(const std::size_t index) {
    if (presetLibrary_.Count() == 0) {
        return;
    }
    presetIndex_ = index % presetLibrary_.Count();
    ResetScene(presetLibrary_.SceneAt(presetIndex_));
    currentScenePath_.clear();
    UpdateWindowTitle();
    statusText_ = L"Loaded preset " + Utf8ToWide(scene_.name);
}


}  // namespace radiary
