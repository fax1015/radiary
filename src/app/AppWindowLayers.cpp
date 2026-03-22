#include "app/AppWindow.h"
#include "app/AppWidgets.h"

#include <algorithm>
#include <string>

#include "imgui.h"

using namespace radiary;

namespace {

constexpr std::size_t kHistoryLimit = 50;

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
    selectedTimelineKeyframe_ = FindKeyframeIndex(scene_, static_cast<int>(std::round(scene_.timelineFrame)));
    inspectorTarget_ = InspectorTarget::FlameLayer;
    renameTarget_ = RenameTarget::None;
    renamingLayerIndex_ = -1;
    focusLayerRename_ = false;
    layerRenameBuffer_.clear();
    EnsureSelectionIsValid();
    ClearPendingUndoCapture();
    MarkViewportDirty(PreviewResetReason::SceneChanged);
}

void AppWindow::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (playbackPanelActive_ && RemoveSelectedOrCurrentKeyframe()) {
            return;
        }
        if ((layersPanelActive_ || inspectorPanelActive_) && RemoveSelectedLayers()) {
            return;
        }
        if (RemoveSelectedLayers()) {
            return;
        }
        RemoveSelectedOrCurrentKeyframe();
        return;
    }

    if (!io.KeyCtrl) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
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
        PushUndoState(hasPendingUndoScene_ ? pendingUndoScene_ : beforeChange);
        hasPendingUndoScene_ = false;
        return;
    }

    if (ImGui::IsItemDeactivated() && !ImGui::IsItemDeactivatedAfterEdit()) {
        hasPendingUndoScene_ = false;
    }

    if (changed && !ImGui::IsItemActive()) {
        PushUndoState(hasPendingUndoScene_ ? pendingUndoScene_ : beforeChange);
        hasPendingUndoScene_ = false;
    }
}

void AppWindow::PushUndoState(const Scene& snapshot) {
    undoStack_.push_back(snapshot);
    if (undoStack_.size() > kHistoryLimit) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
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

    redoStack_.push_back(scene_);
    scene_ = undoStack_.back();
    undoStack_.pop_back();
    selectedTransformLayers_.clear();
    selectedPathLayers_.clear();
    EnsureSelectionIsValid();
    ClearPendingUndoCapture();
    renameTarget_ = RenameTarget::None;
    renamingLayerIndex_ = -1;
    focusLayerRename_ = false;
    layerRenameBuffer_.clear();
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    statusText_ = L"Undo";
}

void AppWindow::Redo() {
    if (!CanRedo()) {
        return;
    }

    undoStack_.push_back(scene_);
    if (undoStack_.size() > kHistoryLimit) {
        undoStack_.erase(undoStack_.begin());
    }
    scene_ = redoStack_.back();
    redoStack_.pop_back();
    selectedTransformLayers_.clear();
    selectedPathLayers_.clear();
    EnsureSelectionIsValid();
    ClearPendingUndoCapture();
    renameTarget_ = RenameTarget::None;
    renamingLayerIndex_ = -1;
    focusLayerRename_ = false;
    layerRenameBuffer_.clear();
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    statusText_ = L"Redo";
}

void AppWindow::ClearPendingUndoCapture() {
    hasPendingUndoScene_ = false;
    viewportInteractionCaptured_ = false;
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
    if (selectedTransformLayers_.empty() && selectedPathLayers_.empty()) {
        if (inspectorTarget_ == InspectorTarget::PathLayer && !scene_.paths.empty()) {
            selectedPathLayers_.push_back(scene_.selectedPath);
        } else if (!scene_.transforms.empty()) {
            selectedTransformLayers_.push_back(scene_.selectedTransform);
        }
    }
    layerSelectionAnchorGlobal_ = std::clamp(layerSelectionAnchorGlobal_, 0, std::max(0, GlobalLayerCount() - 1));
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
    if (inspectorTarget_ == InspectorTarget::PathLayer) {
        keyframe.ownerType = KeyframeOwnerType::Path;
        keyframe.ownerIndex = std::clamp(scene_.selectedPath, 0, std::max(0, static_cast<int>(scene_.paths.size()) - 1));
        return;
    }

    keyframe.ownerType = KeyframeOwnerType::Transform;
    keyframe.ownerIndex = std::clamp(scene_.selectedTransform, 0, std::max(0, static_cast<int>(scene_.transforms.size()) - 1));
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
    const int clampedIndex = std::clamp(index, 0, std::max(0, LayerCount(target) - 1));
    ClearLayerSelections();
    SetLayerSelection(target, {clampedIndex}, clampedIndex, clampedIndex);
}

void AppWindow::ToggleLayerSelection(const InspectorTarget target, const int index) {
    const int itemCount = LayerCount(target);
    if (itemCount <= 0) {
        return;
    }
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

bool AppWindow::IsLayerSelected(const InspectorTarget target, const int index) const {
    if (target == InspectorTarget::PathLayer) {
        return ContainsIndex(selectedPathLayers_, index);
    }
    return ContainsIndex(selectedTransformLayers_, index);
}

int AppWindow::SelectedLayerCount() const {
    return static_cast<int>(selectedTransformLayers_.size() + selectedPathLayers_.size());
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
    if (inspectorTarget_ == InspectorTarget::PathLayer && !scene_.paths.empty()) {
        selectedPathLayers_.assign(1, scene_.selectedPath);
    } else if (!scene_.transforms.empty()) {
        selectedTransformLayers_.assign(1, scene_.selectedTransform);
        inspectorTarget_ = InspectorTarget::FlameLayer;
    } else if (!scene_.paths.empty()) {
        selectedPathLayers_.assign(1, scene_.selectedPath);
        inspectorTarget_ = InspectorTarget::PathLayer;
    }
    NormalizeLayerSelections();
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    statusText_ = removedCount > 1 ? L"Layers removed" : L"Layer removed";
    return true;
}

bool AppWindow::CanRemoveSelectedOrCurrentKeyframe() const {
    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const KeyframeOwnerType currentOwnerType = inspectorTarget_ == InspectorTarget::PathLayer ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
    const int currentOwnerIndex = inspectorTarget_ == InspectorTarget::PathLayer ? scene_.selectedPath : scene_.selectedTransform;
    return selectedTimelineKeyframe_ >= 0 || FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex) >= 0;
}

bool AppWindow::RemoveSelectedOrCurrentKeyframe() {
    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const KeyframeOwnerType currentOwnerType = inspectorTarget_ == InspectorTarget::PathLayer ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
    const int currentOwnerIndex = inspectorTarget_ == InspectorTarget::PathLayer ? scene_.selectedPath : scene_.selectedTransform;
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

void AppWindow::CopySelectedLayer() {
    FinishLayerRename(false);
    EnsureSelectionIsValid();

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
        const int insertIndex = InsertLayerCopy(scene_.paths, scene_.selectedPath, std::move(path));
        AdjustKeyframeOwnerIndicesForInsertedLayer(InspectorTarget::PathLayer, insertIndex);
        SelectSingleLayer(InspectorTarget::PathLayer, insertIndex);
        MarkViewportDirty(PreviewResetReason::SceneChanged);
        statusText_ = L"Path layer pasted";
        return;
    }

    TransformLayer layer = copiedTransformLayer_;
    layer.name = MakeUniqueCopyName(layer.name, "Layer", scene_.transforms);
    layer.visible = true;
    const int insertIndex = InsertLayerCopy(scene_.transforms, scene_.selectedTransform, std::move(layer));
    AdjustKeyframeOwnerIndicesForInsertedLayer(InspectorTarget::FlameLayer, insertIndex);
    SelectSingleLayer(InspectorTarget::FlameLayer, insertIndex);
    MarkViewportDirty(PreviewResetReason::SceneChanged);
    statusText_ = L"Layer pasted";
}

void AppWindow::DuplicateSelectedLayer() {
    FinishLayerRename(false);
    EnsureSelectionIsValid();
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
    if (scene_.keyframes.empty()) {
        return;
    }

    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const KeyframeOwnerType currentOwnerType = inspectorTarget_ == InspectorTarget::PathLayer ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
    const int currentOwnerIndex = inspectorTarget_ == InspectorTarget::PathLayer ? scene_.selectedPath : scene_.selectedTransform;
    if (FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex) >= 0) {
        return;
    }

    SceneKeyframe keyframe;
    keyframe.frame = currentFrame;
    keyframe.markerColor = inspectorTarget_ == InspectorTarget::PathLayer ? Color{96, 188, 224, 255} : Color{248, 164, 88, 255};
    AssignCurrentLayerToKeyframe(keyframe);
    keyframe.pose = CaptureScenePose(scene_);
    scene_.keyframes.push_back(std::move(keyframe));
    SortKeyframes(scene_);
    selectedTimelineKeyframe_ = FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex);
}

void AppWindow::SyncCurrentKeyframeFromScene() {
    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const KeyframeOwnerType currentOwnerType = inspectorTarget_ == InspectorTarget::PathLayer ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
    const int currentOwnerIndex = inspectorTarget_ == InspectorTarget::PathLayer ? scene_.selectedPath : scene_.selectedTransform;
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
    statusText_ = L"Loaded preset " + Utf8ToWide(scene_.name);
}


}  // namespace radiary
