#include "app/AppWindow.h"
#include "app/AppWidgets.h"
#include "app/CameraUtils.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <random>
#include <sstream>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

using namespace radiary;

namespace {

constexpr float kOverlayPanelMarginX = 16.0f;
constexpr float kOverlayPanelMarginY = 12.0f;
constexpr float kDefaultToolbarSplit = 0.055f;
constexpr float kDefaultBottomPanelSplit = 0.22f;
constexpr float kDefaultLeftPanelSplit = 0.3f;
constexpr float kDefaultRightPanelSplit = 0.47f;

}  // namespace

namespace radiary {

void AppWindow::DrawBlockingOverlay() const {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }

    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.03f, 0.04f, 0.78f));
    ImGui::Begin(
        "##BlockingOverlay",
        nullptr,
        ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoInputs);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    const float panelWidth = std::clamp(viewport->Size.x - 48.0f, 320.0f, 440.0f);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(panelWidth, 0.0f),
        ImVec2(panelWidth, std::max(160.0f, viewport->Size.y - 48.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::Begin(
        "##BlockingOverlayPanel",
        nullptr,
        ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar(3);

    const std::string title = WideToUtf8(exportProgressTitle_.empty() ? L"Exporting" : exportProgressTitle_);
    const std::string detail = WideToUtf8(exportProgressDetail_);
    const std::string eta = WideToUtf8(exportProgressEta_);
    ImGui::TextUnformatted(title.c_str());
    ImGui::Spacing();
    ImGui::ProgressBar(std::clamp(exportProgress_, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f));
    if (!detail.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", detail.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!eta.empty()) {
        ImGui::TextDisabled("%s", eta.c_str());
    }
    ImGui::Spacing();
    const bool canCancel = exportProgress_ < 0.999f;
    if (!canCancel) {
        ImGui::BeginDisabled();
    }
    const ImVec2 cancelSize(std::max(144.0f, ImGui::CalcTextSize("Cancel Export").x + 26.0f), 0.0f);
    if (ImGui::Button("Cancel Export", cancelSize)) {
        const_cast<AppWindow*>(this)->exportCancelRequested_ = true;
    }
    if (!canCancel) {
        ImGui::EndDisabled();
    }

    ImGui::End();
    ImGui::End();
}

void AppWindow::DrawDockspace() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    const ImVec2 dockTabFramePadding(ImGui::GetStyle().FramePadding.x, 4.0f);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dockTabFramePadding);
    ImGui::Begin("RadiaryDockspace", nullptr, flags);
    ImGui::PopStyleVar(4);
    const ImGuiID dockspaceId = ImGui::GetID("RadiaryDockspaceNode");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    if (!defaultLayoutBuilt_) {
        BuildDefaultLayout();
    }
    ImGui::End();
}

void AppWindow::BuildDefaultLayout() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr || viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f) {
        return;
    }

    const ImGuiID dockspaceId = ImGui::GetID("RadiaryDockspaceNode");
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

    ImGuiID centerId = dockspaceId;
    ImGuiID topId = 0;
    ImGuiID bottomId = 0;
    ImGuiID leftId = 0;
    ImGuiID rightId = 0;
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Up, kDefaultToolbarSplit, &topId, &centerId);
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, kDefaultBottomPanelSplit, &bottomId, &centerId);
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, kDefaultLeftPanelSplit, &leftId, &centerId);
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, kDefaultRightPanelSplit, &rightId, &centerId);

    ImGui::DockBuilderDockWindow("Toolbar", topId);
    ImGui::DockBuilderDockWindow("Layers", leftId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    ImGui::DockBuilderDockWindow("Playback", bottomId);
    ImGui::DockBuilderDockWindow("Preview", bottomId);
    ImGui::DockBuilderDockWindow("Camera", bottomId);
    ImGui::DockBuilderDockWindow("Viewport", centerId);

    if (ImGuiDockNode* topNode = ImGui::DockBuilderGetNode(topId)) {
        topNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        topNode->LocalFlags |= ImGuiDockNodeFlags_NoResize;
    }
    ImGui::DockBuilderFinish(dockspaceId);
    defaultLayoutBuilt_ = true;
}

void AppWindow::DrawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f, 6.0f));
    ImGui::Begin(
        "Toolbar",
        nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    const float settingsGap = 8.0f;
    const float settingsButtonWidth = ImGui::GetFrameHeight();

    ImGui::BeginChild(
        "ToolbarScroll",
        ImVec2(std::max(1.0f, toolbarWidth - settingsButtonWidth - settingsGap), 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus);

    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && io.MouseWheel != 0.0f) {
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * toolbarScrollStep_);
    }

    const auto drawDivider = []() {
        DrawSubtleToolbarDivider();
    };

    ImGui::AlignTextToFramePadding();
    if (brandFont_ != nullptr) {
        ImGui::PushFont(brandFont_);
    }
    ImGui::TextUnformatted("Radiary");
    if (brandFont_ != nullptr) {
        ImGui::PopFont();
    }

    ImGui::SameLine(0.0f, 12.0f);
    if (DrawActionButton("##undo_toolbar", "", IconGlyph::Undo, ActionTone::Slate, false, CanUndo(), 0.0f, "Undo (Ctrl+Z)")) {
        Undo();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (DrawActionButton("##redo_toolbar", "", IconGlyph::Redo, ActionTone::Slate, false, CanRedo(), 0.0f, "Redo (Ctrl+Y)")) {
        Redo();
    }

    drawDivider();
    DrawSectionChip("FILE", ImVec4(0.12f, 0.19f, 0.30f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f);

    if (DrawActionButton("##new_scene", "New", IconGlyph::NewScene, ActionTone::Slate)) {
        PushUndoState(scene_);
        Scene newScene = CreateDefaultScene();
        ApplyUserSceneDefaults(newScene);
        ResetScene(std::move(newScene));
        currentScenePath_.clear();
        statusText_ = L"New scene";
    }
    ImGui::SameLine();
    if (DrawActionButton("##open_scene", "Open", IconGlyph::OpenScene, ActionTone::Slate)) {
        Scene beforeLoad = scene_;
        if (LoadSceneFromDialog()) {
            PushUndoState(beforeLoad);
        }
    }
    ImGui::SameLine();
    if (DrawActionButton("##save_scene", "Save", IconGlyph::SaveScene, ActionTone::Accent)) {
        SaveSceneToDialog(currentScenePath_.empty());
    }
    ImGui::SameLine();
    if (DrawActionButton("##save_scene_as", "Save As", IconGlyph::SaveSceneAs, ActionTone::Accent, false, true, 112.0f)) {
        SaveSceneToDialog(true);
    }
    ImGui::SameLine();
    if (DrawActionButton("##export_scene", "Export", IconGlyph::ExportImage, ActionTone::Accent, exportPanelOpen_, true, 98.0f)) {
        exportPanelOpen_ = !exportPanelOpen_;
        if (exportPanelOpen_) {
            OpenExportPanel();
        }
    }
    exportButtonAnchorX_ = ImGui::GetItemRectMin().x;
    exportButtonAnchorY_ = ImGui::GetItemRectMax().y + kOverlayPanelMarginY;

    drawDivider();
    DrawSectionChip("PLAY", ImVec4(0.12f, 0.19f, 0.30f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f);
    if (DrawActionButton("##randomize_scene", "Randomize", IconGlyph::Randomize, ActionTone::Accent)) {
        PushUndoState(scene_);
        Scene randomizedScene = CreateRandomScene(static_cast<std::uint32_t>(GetTickCount64()));
        randomizedScene.gridVisible = scene_.gridVisible;
        ResetScene(std::move(randomizedScene));
        statusText_ = L"Random scene generated";
    }
    ImGui::SameLine();
    if (DrawActionButton("##toggle_playback", scene_.animatePath ? "Pause" : "Play", scene_.animatePath ? IconGlyph::Pause : IconGlyph::Play, ActionTone::Accent, scene_.animatePath)) {
        PushUndoState(scene_);
        scene_.animatePath = !scene_.animatePath;
        viewportDirty_ = true;
    }

    drawDivider();
    DrawSectionChip("VIEW", ImVec4(0.12f, 0.19f, 0.30f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f);
    if (DrawActionButton("##toggle_grid", "Grid", IconGlyph::Grid, ActionTone::Accent, scene_.gridVisible)) {
        PushUndoState(scene_);
        scene_.gridVisible = !scene_.gridVisible;
        viewportDirty_ = true;
    }
    ImGui::SameLine();

    const Scene beforeMode = scene_;
    int modeIndex = static_cast<int>(scene_.mode);
    const char* modes[] = {"Flame", "Path", "Hybrid"};
    ImGui::SetNextItemWidth(110.0f);
    const bool modeChanged = ComboWithMaterialArrow("##mode", &modeIndex, modes, IM_ARRAYSIZE(modes));
    if (modeChanged) {
        scene_.mode = static_cast<SceneMode>(modeIndex);
        viewportDirty_ = true;
    }
    CaptureWidgetUndo(beforeMode, modeChanged);

    drawDivider();
    DrawSectionChip("PRESET", ImVec4(0.12f, 0.19f, 0.30f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SetNextItemWidth(210.0f);
    if (BeginComboWithMaterialArrow("##preset", presetLibrary_.Count() > 0 ? presetLibrary_.NameAt(presetIndex_).c_str() : "(none)")) {
        for (std::size_t index = 0; index < presetLibrary_.Count(); ++index) {
            const bool selected = index == presetIndex_;
            if (ImGui::Selectable(presetLibrary_.NameAt(index).c_str(), selected)) {
                PushUndoState(scene_);
                LoadPreset(index);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    drawDivider();
    DrawSectionChip("SCENE", ImVec4(0.12f, 0.19f, 0.30f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SetNextItemWidth(220.0f);
    const Scene beforeName = scene_;
    const bool nameChanged = ImGui::InputText("##scene_name", &scene_.name);
    CaptureWidgetUndo(beforeName, nameChanged);

    ImGui::EndChild();
    ImGui::SameLine(0.0f, settingsGap);
    ImGui::BeginChild("ToolbarPinnedSettings", ImVec2(settingsButtonWidth, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavFocus);
    if (DrawActionButton("##settings_icon_pinned", "", IconGlyph::Settings, ActionTone::Slate, settingsPanelOpen_, true, 0.0f, "Settings")) {
        settingsPanelOpen_ = !settingsPanelOpen_;
    }
    settingsButtonAnchorX_ = ImGui::GetItemRectMin().x;
    settingsButtonAnchorY_ = ImGui::GetItemRectMax().y + kOverlayPanelMarginY;
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void AppWindow::DrawSettingsPanel() {
    if (!settingsPanelOpen_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }

    const float settingsPanelMarginX = kOverlayPanelMarginY + 10.0f;
    const float settingsPanelMarginY = kOverlayPanelMarginY + 18.0f;
    const ImRect bounds(
        ImVec2(viewport->Pos.x + settingsPanelMarginX, viewport->Pos.y + settingsPanelMarginY),
        ImVec2(viewport->Pos.x + viewport->Size.x - settingsPanelMarginX, viewport->Pos.y + viewport->Size.y - settingsPanelMarginY));
    const float topY = std::max(settingsButtonAnchorY_ + 8.0f, bounds.Min.y);
    const float maxWidth = std::max(1.0f, bounds.GetWidth());
    const float maxHeight = std::max(1.0f, bounds.Max.y - topY);
    const float panelWidth = std::min(760.0f, maxWidth);
    const float panelHeight = std::min(760.0f, maxHeight);
    const ImVec2 panelAnchor(bounds.Max.x - panelWidth, topY);

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(panelAnchor, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(520.0f, maxWidth), std::min(420.0f, maxHeight)), ImVec2(maxWidth, maxHeight));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);

    if (ImGui::Begin(
            "Settings",
            &settingsPanelOpen_,
            ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings)) {
        constexpr bool kDefaultGpuViewportPreview = true;
        constexpr bool kDefaultShowStatusOverlay = true;
        constexpr bool kDefaultExportHideGrid = true;
        const Scene defaultScene = CreateDefaultScene();
        static const char* kExportFormatLabels[] = {"PNG Image", "JPG Image", "PNG Sequence", "JPG Sequence", "AVI Video", "MP4 Video", "MOV Video"};

        ImGui::BeginChild("SettingsPanelScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_None);

        const auto fullWidthScalar = [&](const char* label, const char* id, ImGuiDataType dataType, void* value, const void* minValue, const void* maxValue, const char* format) {
            ImGui::TextDisabled("%s", label);
            ImGui::SetNextItemWidth(-FLT_MIN);
            return SliderScalarWithInput(id, dataType, value, minValue, maxValue, format);
        };

        ImGui::SeparatorText("Rendering");
        if (ImGui::Checkbox("GPU viewport preview", &gpuFlamePreviewEnabled_) || ResetValueOnDoubleClick(gpuFlamePreviewEnabled_, kDefaultGpuViewportPreview)) {
            viewportDirty_ = true;
        }
        if (gpuFlamePreviewEnabled_) {
            std::string gpuBackendSummary = "Flame ";
            gpuBackendSummary += gpuFlameRenderer_.IsReady() ? "D3D11 compute" : "lazy";
            gpuBackendSummary += " | Path ";
            gpuBackendSummary += gpuPathRenderer_.IsReady() ? "D3D11 raster" : "lazy";
            ImGui::TextDisabled("Viewport backend: %s", gpuBackendSummary.c_str());
            if (!gpuFlameRenderer_.LastError().empty()) {
                ImGui::TextWrapped("Flame GPU status: %s", gpuFlameRenderer_.LastError().c_str());
            }
            if (!gpuPathRenderer_.LastError().empty()) {
                ImGui::TextWrapped("Path GPU status: %s", gpuPathRenderer_.LastError().c_str());
            }
        } else {
            ImGui::TextDisabled("Viewport backend: CPU software renderer");
        }

        ImGui::SeparatorText("Hardware");
        ImGui::TextDisabled("Renderer");
        ImGui::SameLine();
        ImGui::TextUnformatted(usingWarpDevice_ ? "D3D11 WARP" : "D3D11 Hardware");
        ImGui::TextDisabled("Adapter");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", WideToUtf8(renderAdapterName_).c_str());
        if (!adapterOptions_.empty()) {
            std::string selectedAdapterLabel = "Select GPU";
            if (selectedAdapterIndex_ >= 0 && selectedAdapterIndex_ < static_cast<int>(adapterOptions_.size())) {
                const AdapterOption& option = adapterOptions_[static_cast<std::size_t>(selectedAdapterIndex_)];
                selectedAdapterLabel = WideToUtf8(option.name);
                if (option.dedicatedVideoMemoryMb > 0) {
                    selectedAdapterLabel += " | " + std::to_string(option.dedicatedVideoMemoryMb) + " MB";
                }
                if (option.software) {
                    selectedAdapterLabel += " | Software";
                }
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Preferred GPU");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (BeginComboWithMaterialArrow("##gpu_adapter", selectedAdapterLabel.c_str())) {
                for (std::size_t index = 0; index < adapterOptions_.size(); ++index) {
                    const AdapterOption& option = adapterOptions_[index];
                    std::string label = WideToUtf8(option.name);
                    if (option.dedicatedVideoMemoryMb > 0) {
                        label += " | " + std::to_string(option.dedicatedVideoMemoryMb) + " MB";
                    }
                    if (option.software) {
                        label += " | Software";
                    }
                    if (static_cast<int>(index) == activeAdapterIndex_) {
                        label += " | Active";
                    }
                    const bool selected = static_cast<int>(index) == selectedAdapterIndex_;
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        selectedAdapterIndex_ = static_cast<int>(index);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            const bool canApplyGpu = selectedAdapterIndex_ >= 0 && selectedAdapterIndex_ != activeAdapterIndex_;
            if (DrawActionButton("##apply_gpu_adapter", "Apply GPU", IconGlyph::Settings, ActionTone::Accent, false, canApplyGpu, 132.0f)) {
                graphicsDeviceChangePending_ = true;
                statusText_ = L"Applying GPU change";
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh GPU List")) {
                EnumerateAdapters();
            }
            if (graphicsDeviceChangePending_) {
                ImGui::TextDisabled("GPU switch queued for the next frame.");
            }
        }

        ImGui::SeparatorText("Graphics");
        Scene beforeGraphics = scene_;
        bool graphicsChanged = ImGui::Checkbox("Show viewport grid", &scene_.gridVisible);
        graphicsChanged = ResetValueOnDoubleClick(scene_.gridVisible, defaultScene.gridVisible) || graphicsChanged;
        if (graphicsChanged) {
            viewportDirty_ = true;
        }
        CaptureWidgetUndo(beforeGraphics, graphicsChanged);
        if (ImGui::Checkbox("Show status overlay", &showStatusOverlay_) || ResetValueOnDoubleClick(showStatusOverlay_, kDefaultShowStatusOverlay)) {
            MarkViewportDirty();
        }
        if (ImGui::Checkbox("Hide grid on export", &exportHideGrid_) || ResetValueOnDoubleClick(exportHideGrid_, kDefaultExportHideGrid)) {
            MarkViewportDirty();
        }

        ImGui::SeparatorText("Defaults");
        constexpr int kMinNewSceneEndFrame = 24;
        constexpr int kMaxNewSceneEndFrame = 2400;
        constexpr double kMinNewSceneFps = 1.0;
        constexpr double kMaxNewSceneFps = 120.0;
        int exportFormatIndex = static_cast<int>(exportFormat_);
        if (ComboWithMaterialArrow("Default export format", &exportFormatIndex, kExportFormatLabels, IM_ARRAYSIZE(kExportFormatLabels))) {
            exportFormat_ = static_cast<ExportFormat>(exportFormatIndex);
        }
        if (ImGui::Checkbox("Use GPU for export by default", &exportUseGpu_)) {
            MarkViewportDirty();
        }
        if (fullWidthScalar(
                "New scene end frame",
                "##new_scene_end_frame",
                ImGuiDataType_S32,
                &newSceneEndFrameDefault_,
                &kMinNewSceneEndFrame,
                &kMaxNewSceneEndFrame,
                "%d")) {
            newSceneEndFrameDefault_ = std::clamp(newSceneEndFrameDefault_, kMinNewSceneEndFrame, kMaxNewSceneEndFrame);
        }
        if (fullWidthScalar(
                "New scene FPS",
                "##new_scene_fps",
                ImGuiDataType_Double,
                &newSceneFrameRateDefault_,
                &kMinNewSceneFps,
                &kMaxNewSceneFps,
                "%.2f")) {
            newSceneFrameRateDefault_ = std::clamp(newSceneFrameRateDefault_, kMinNewSceneFps, kMaxNewSceneFps);
        }

        ImGui::SeparatorText("Layout");
        if (ImGui::Button("Restore default layout")) {
            defaultLayoutBuilt_ = false;
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void AppWindow::DrawExportPanel() {
    if (!exportPanelOpen_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }

    const float exportPanelMarginX = kOverlayPanelMarginY + 10.0f;
    const float exportPanelMarginY = kOverlayPanelMarginY + 18.0f;
    const ImRect bounds(
        ImVec2(viewport->Pos.x + exportPanelMarginX, viewport->Pos.y + exportPanelMarginY),
        ImVec2(viewport->Pos.x + viewport->Size.x - exportPanelMarginX, viewport->Pos.y + viewport->Size.y - exportPanelMarginY));
    const float topY = std::max(exportButtonAnchorY_ + 8.0f, bounds.Min.y);
    const float maxWidth = std::max(1.0f, bounds.GetWidth());
    const float maxHeight = std::max(1.0f, bounds.Max.y - topY);
    const float panelWidth = std::min(640.0f, maxWidth);
    const float panelHeight = std::min(520.0f, maxHeight);
    const float clampedX = std::clamp(
        exportButtonAnchorX_,
        bounds.Min.x,
        bounds.Max.x - panelWidth);
    const ImVec2 panelAnchor(clampedX, topY);

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(panelAnchor, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(560.0f, maxWidth), std::min(320.0f, maxHeight)), ImVec2(maxWidth, maxHeight));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);

    if (ImGui::Begin(
            "Export",
            &exportPanelOpen_,
            ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings)) {
        static const char* kFormats[] = {"PNG Image", "JPG Image", "PNG Sequence", "JPG Sequence", "AVI Video", "MP4 Video", "MOV Video"};

        ImGui::BeginChild("ExportPanelScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_None);
        ImGui::SeparatorText("Format");
        int formatIndex = static_cast<int>(exportFormat_);
        if (ComboWithMaterialArrow("File Type", &formatIndex, kFormats, IM_ARRAYSIZE(kFormats))) {
            exportFormat_ = static_cast<ExportFormat>(formatIndex);
            if (exportFormat_ == ExportFormat::Jpeg || exportFormat_ == ExportFormat::JpegSequence || exportFormat_ == ExportFormat::Avi) {
                exportTransparentBackground_ = false;
            }
        }
        const bool rendersMultipleFrames = exportFormat_ == ExportFormat::PngSequence
            || exportFormat_ == ExportFormat::JpegSequence
            || exportFormat_ == ExportFormat::Avi
            || exportFormat_ == ExportFormat::Mp4
            || exportFormat_ == ExportFormat::Mov;

        ImGui::SeparatorText("Output");
        const std::vector<ExportResolutionPreset> resolutionPresets = BuildExportResolutionPresets(scene_.camera);
        const int activeResolutionPreset = FindExportResolutionPresetIndex(resolutionPresets, exportWidth_, exportHeight_);
        const std::string resolutionPreview =
            activeResolutionPreset >= 0
            ? resolutionPresets[static_cast<std::size_t>(activeResolutionPreset)].label
            : "Custom";
        if (BeginComboWithMaterialArrow("Resolution Preset", resolutionPreview.c_str())) {
            for (std::size_t index = 0; index < resolutionPresets.size(); ++index) {
                const bool selected = static_cast<int>(index) == activeResolutionPreset;
                if (ImGui::Selectable(resolutionPresets[index].label.c_str(), selected)) {
                    exportWidth_ = resolutionPresets[index].width;
                    exportHeight_ = resolutionPresets[index].height;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Camera gate");
        ImGui::SameLine();
        ImGui::TextUnformatted(CameraAspectSummary(scene_.camera).c_str());
        const int previousExportHeight = exportHeight_;
        exportWidth_ = std::max(2, exportWidth_);
        exportHeight_ = std::max(2, exportHeight_);
        ImGui::InputInt("Width", &exportWidth_, 64, 256);
        ImGui::InputInt("Height", &exportHeight_, 64, 256);
        const bool heightChanged = exportHeight_ != previousExportHeight;
        if (heightChanged) {
            ConstrainExportResolutionToCamera(scene_.camera, exportWidth_, exportHeight_, false);
        } else {
            ConstrainExportResolutionToCamera(scene_.camera, exportWidth_, exportHeight_, true);
        }
        ImGui::TextDisabled("Export resolution is locked to the camera aspect.");
        if (rendersMultipleFrames) {
            exportFrameStart_ = std::max(scene_.timelineStartFrame, exportFrameStart_);
            exportFrameEnd_ = std::max(exportFrameStart_, exportFrameEnd_);
            ImGui::InputInt("Start Frame", &exportFrameStart_, 1, 10);
            ImGui::InputInt("End Frame", &exportFrameEnd_, 1, 10);
            exportFrameStart_ = std::clamp(exportFrameStart_, scene_.timelineStartFrame, scene_.timelineEndFrame);
            exportFrameEnd_ = std::clamp(exportFrameEnd_, exportFrameStart_, scene_.timelineEndFrame);
            if (ImGui::Button("Use Timeline Range")) {
                exportFrameStart_ = scene_.timelineStartFrame;
                exportFrameEnd_ = scene_.timelineEndFrame;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d frames", std::max(1, exportFrameEnd_ - exportFrameStart_ + 1));
        }

        ImGui::SeparatorText("Background");
        ImGui::Checkbox("Hide grid on export", &exportHideGrid_);
        const bool transparencySupported = exportFormat_ == ExportFormat::Png || exportFormat_ == ExportFormat::PngSequence;
        if (!transparencySupported) {
            ImGui::BeginDisabled();
        }
        ImGui::Checkbox("Transparent background", &exportTransparentBackground_);
        if (!transparencySupported) {
            ImGui::EndDisabled();
            ImGui::TextDisabled("Transparency is only available for PNG outputs.");
        }

        ImGui::SeparatorText("Render");
        ImGui::Checkbox("Use GPU for export", &exportUseGpu_);
        ImGui::TextDisabled("Falls back to CPU if the GPU export path is unavailable.");
        constexpr std::uint32_t kMinExportIterations = 20000u;
        constexpr std::uint32_t kMaxExportIterations = 100000000u;
        exportIterations_ = std::clamp(exportIterations_, kMinExportIterations, kMaxExportIterations);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputScalar("Iterations", ImGuiDataType_U32, &exportIterations_, &kMinExportIterations, &kMinExportIterations, "%u");
        exportIterations_ = std::clamp(exportIterations_, kMinExportIterations, kMaxExportIterations);
        ImGui::TextDisabled("Used for export renders only. Higher values increase render time, with diminishing quality returns.");

        ImGui::Spacing();
        if (DrawActionButton("##export_confirm", "Export File", IconGlyph::ExportImage, ActionTone::Accent, false, true, 132.0f)) {
            ExportViewportToDialog();
        }
        ImGui::SameLine();
        if (DrawActionButton("##export_cancel", "Close", IconGlyph::Remove, ActionTone::Slate, false, true, 96.0f)) {
            exportPanelOpen_ = false;
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void AppWindow::DrawEasingPanel() {
    if (!easingPanelOpen_) {
        return;
    }

    const KeyframeOwnerType currentOwnerType = inspectorTarget_ == InspectorTarget::PathLayer ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
    const int currentOwnerIndex = inspectorTarget_ == InspectorTarget::PathLayer ? scene_.selectedPath : scene_.selectedTransform;
    const int editableKeyframeIndex =
        selectedTimelineKeyframe_ >= 0
        ? selectedTimelineKeyframe_
        : FindKeyframeIndex(scene_, static_cast<int>(std::round(scene_.timelineFrame)), currentOwnerType, currentOwnerIndex);
    if (editableKeyframeIndex < 0 || editableKeyframeIndex >= static_cast<int>(scene_.keyframes.size())) {
        easingPanelOpen_ = false;
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }

    const ImGuiWindow* timelineWindow = ImGui::FindWindowByName("Playback");
    const ImGuiWindow* previewWindow = ImGui::FindWindowByName("Preview");
    const ImGuiWindow* viewportWindow = ImGui::FindWindowByName("Viewport");
    const ImGuiWindow* lowerPanelWindow = timelineWindow != nullptr ? timelineWindow : previewWindow;
    const ImRect bounds(
        ImVec2(
            viewport->Pos.x + kOverlayPanelMarginX,
            (viewportWindow != nullptr ? viewportWindow->InnerRect.Min.y : viewport->Pos.y) + kOverlayPanelMarginY),
        ImVec2(
            viewport->Pos.x + viewport->Size.x - kOverlayPanelMarginX,
            (lowerPanelWindow != nullptr ? lowerPanelWindow->InnerRect.Max.y : viewport->Pos.y + viewport->Size.y) - kOverlayPanelMarginY));
    const float maxWidth = std::max(1.0f, bounds.GetWidth() - kOverlayPanelMarginX * 2.0f);
    const float maxHeight = std::max(1.0f, easingButtonAnchorY_ - bounds.Min.y - kOverlayPanelMarginY);
    const float panelWidth = std::min(248.0f, maxWidth);
    const float panelHeight = std::min(188.0f, maxHeight);
    const float clampedX = std::clamp(
        easingButtonAnchorX_,
        bounds.Min.x + kOverlayPanelMarginX,
        bounds.Max.x - kOverlayPanelMarginX - panelWidth);
    const float panelY = std::clamp(
        easingButtonAnchorY_ - panelHeight,
        bounds.Min.y + kOverlayPanelMarginY,
        bounds.Max.y - panelHeight);

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(ImVec2(clampedX, panelY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(248.0f, maxWidth), std::min(188.0f, maxHeight)), ImVec2(maxWidth, maxHeight));

    if (ImGui::Begin(
        "Easing",
        &easingPanelOpen_,
        ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings)) {
        SceneKeyframe& keyframe = scene_.keyframes[static_cast<std::size_t>(editableKeyframeIndex)];
        Scene beforeCurve = scene_;
        const bool curveChanged = DrawBezierCurveEditor("##timeline_curve_editor_popup", keyframe);
        if (curveChanged) {
            RefreshTimelinePose();
        }
        CaptureWidgetUndo(beforeCurve, curveChanged);
    }
    ImGui::End();
}

void AppWindow::DrawLayersPanel() {
    EnsureSelectionIsValid();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Layers", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    layersPanelActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    ImGui::SeparatorText("Layer Stack");

    if (DrawActionButton("##add_flame_layer", "Add Flame", IconGlyph::Add, ActionTone::Accent, false, true, 118.0f)) {
        FinishLayerRename(false);
        PushUndoState(scene_);
        EnsureSelectionIsValid();
        TransformLayer layer = scene_.transforms.empty()
            ? CreateDefaultScene().transforms.front()
            : scene_.transforms[scene_.selectedTransform];
        layer.name = "Layer " + std::to_string(scene_.transforms.size() + 1);
        layer.visible = true;
        scene_.transforms.push_back(layer);
        SelectSingleLayer(InspectorTarget::FlameLayer, static_cast<int>(scene_.transforms.size()) - 1);
        viewportDirty_ = true;
    }
    ImGui::SameLine();
    if (DrawActionButton("##add_path_layer", "Add Path", IconGlyph::Add, ActionTone::Accent, false, true, 112.0f)) {
        FinishLayerRename(false);
        PushUndoState(scene_);
        EnsureSelectionIsValid();
        PathSettings path = scene_.paths.empty()
            ? CreateDefaultScene().paths.front()
            : scene_.paths[scene_.selectedPath];
        path.name = "Path Layer " + std::to_string(scene_.paths.size() + 1);
        path.visible = true;
        scene_.paths.push_back(path);
        SelectSingleLayer(InspectorTarget::PathLayer, static_cast<int>(scene_.paths.size()) - 1);
        viewportDirty_ = true;
    }
    ImGui::SameLine();
    const bool canRemoveLayer = CanRemoveSelectedLayers();
    if (DrawActionButton("##remove_layer", "Remove", IconGlyph::Remove, ActionTone::Accent, false, canRemoveLayer, 108.0f) && canRemoveLayer) {
        RemoveSelectedLayers();
    }

    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    const auto drawLayerList = [&](const InspectorTarget target, const RenameTarget renameTarget, const int idOffset, const auto& names) {
        for (std::size_t index = 0; index < names.size(); ++index) {
            ImGui::PushID(idOffset + static_cast<int>(index));
            bool& visible = target == InspectorTarget::PathLayer
                ? scene_.paths[index].visible
                : scene_.transforms[index].visible;
            const bool selected = IsLayerSelected(target, static_cast<int>(index));
            const ImVec4 headerColor = selected ? ImVec4(0.15f, 0.24f, 0.37f, target == InspectorTarget::PathLayer ? 0.96f : 0.92f) : ImVec4(0.11f, 0.15f, 0.22f, target == InspectorTarget::PathLayer ? 0.82f : 0.78f);
            const ImVec4 hoverColor = selected ? ImVec4(0.20f, 0.31f, 0.46f, 1.0f) : ImVec4(0.15f, 0.21f, 0.31f, 0.92f);
            const ImVec4 activeColor = selected ? ImVec4(0.24f, 0.36f, 0.53f, 1.0f) : ImVec4(0.18f, 0.26f, 0.38f, 0.96f);
            const bool dimLabel = !visible;
            ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, activeColor);
            const float itemWidth = ImGui::GetContentRegionAvail().x;
            const float visibilityButtonSize = std::max(18.0f, ImGui::GetFrameHeight() - 4.0f);
            const float visibilityGap = 6.0f;
            const float labelWidth = std::max(1.0f, itemWidth - visibilityButtonSize - visibilityGap);
            const std::string itemLabel = visible
                ? names[index].get()
                : names[index].get() + " (hidden)";
            if (dimLabel) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.78f, 0.84f, 0.58f));
            }
            if (renameTarget_ == renameTarget && renamingLayerIndex_ == static_cast<int>(index)) {
                if (focusLayerRename_) {
                    ImGui::SetKeyboardFocusHere();
                    focusLayerRename_ = false;
                }
                ImGuiInputTextFlags renameFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                const char* inputId = renameTarget == RenameTarget::Path ? "##path_layer_rename" : "##layer_rename";
                ImGui::SetNextItemWidth(labelWidth);
                const bool submitted = ImGui::InputText(inputId, &layerRenameBuffer_, renameFlags);
                const bool cancelRename = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
                const bool commitRename = submitted || (ImGui::IsItemDeactivated() && !cancelRename);
                if (cancelRename) {
                    FinishLayerRename(false);
                } else if (commitRename) {
                    FinishLayerRename(true);
                }
            } else {
                if (ImGui::Selectable(itemLabel.c_str(), selected, 0, ImVec2(labelWidth, 0.0f))) {
                    if (ImGui::GetIO().KeyShift) {
                        SelectLayerRange(target, static_cast<int>(index));
                    } else if (ImGui::GetIO().KeyCtrl) {
                        ToggleLayerSelection(target, static_cast<int>(index));
                    } else {
                        SelectSingleLayer(target, static_cast<int>(index));
                    }
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    SelectSingleLayer(target, static_cast<int>(index));
                    BeginLayerRename(renameTarget, static_cast<int>(index));
                }
            }
            ImGui::SameLine(0.0f, visibilityGap);
            const ImVec2 visibilityPos = ImGui::GetCursorScreenPos();
            const ImVec2 visibilitySize(visibilityButtonSize, visibilityButtonSize);
            const bool togglePressed = ImGui::InvisibleButton("##visibility_toggle", visibilitySize);
            const bool toggleHovered = ImGui::IsItemHovered();
            const bool toggleHeld = ImGui::IsItemActive();
            if (togglePressed) {
                PushUndoState(scene_);
                visible = !visible;
                viewportDirty_ = true;
                statusText_ = visible ? L"Layer shown" : L"Layer hidden";
            }
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImRect visibilityRect(visibilityPos, ImVec2(visibilityPos.x + visibilitySize.x, visibilityPos.y + visibilitySize.y));
            if (toggleHovered || toggleHeld) {
                const ImVec4 eyeFill = toggleHeld
                    ? ImVec4(0.21f, 0.27f, 0.36f, 0.92f)
                    : ImVec4(0.18f, 0.22f, 0.30f, 0.74f);
                drawList->AddRectFilled(visibilityRect.Min, visibilityRect.Max, ImGui::GetColorU32(eyeFill), 6.0f);
            }
            const ImU32 eyeColor = ImGui::GetColorU32(visible
                ? ImVec4(0.86f, 0.90f, 0.95f, 0.96f)
                : ImVec4(0.64f, 0.69f, 0.77f, 0.78f));
            const IconGlyph eyeGlyph = visible ? IconGlyph::VisibilityOn : IconGlyph::VisibilityOff;
            if (!DrawMaterialFontIcon(drawList, visibilityRect, eyeGlyph, eyeColor, 0.92f)) {
                DrawIconGlyph(drawList, visibilityRect, eyeGlyph, eyeColor);
            }
            if (dimLabel) {
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor(3);
            ImGui::PopID();
        }
    };

    std::vector<std::reference_wrapper<const std::string>> transformNames;
    transformNames.reserve(scene_.transforms.size());
    for (const TransformLayer& layer : scene_.transforms) {
        transformNames.emplace_back(layer.name);
    }
    drawLayerList(InspectorTarget::FlameLayer, RenameTarget::Transform, 0, transformNames);

    std::vector<std::reference_wrapper<const std::string>> pathNames;
    pathNames.reserve(scene_.paths.size());
    for (const PathSettings& path : scene_.paths) {
        pathNames.emplace_back(path.name);
    }
    drawLayerList(InspectorTarget::PathLayer, RenameTarget::Path, 1000, pathNames);
    ImGui::PopStyleVar();
    ImGui::End();
}

void AppWindow::DrawInspectorPanel() {
    EnsureSelectionIsValid();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    inspectorPanelActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    if (SelectedLayerCount() == 0) {
        ImGui::TextDisabled("No layers");
        ImGui::End();
        return;
    }
    if (HasMultipleLayersSelected()) {
        ImGui::TextDisabled("Multiple layers selected");
        ImGui::End();
        return;
    }
    const auto captureEdit = [&](const Scene& before, const bool changed) {
        if (changed) {
            AutoKeyCurrentFrame();
            viewportDirty_ = true;
        }
        CaptureWidgetUndo(before, changed);
    };
    const double weightMin = 0.1;
    const double weightMax = 4.0;
    const double rotationMin = -360.0;
    const double rotationMax = 360.0;
    const double scaleMin = 0.05;
    const double scaleMax = 2.5;
    const double offsetMin = -3.0;
    const double offsetMax = 3.0;
    const double shearMin = -2.0;
    const double shearMax = 2.0;
    const double colorIndexMin = 0.0;
    const double colorIndexMax = 1.0;
    const double pathThicknessMin = 0.02;
    const double pathThicknessMax = 1.2;
    const double taperMin = 0.0;
    const double taperMax = 0.9;
    const double twistMin = 0.0;
    const double twistMax = 8.0;
    const double variationMin = 0.0;
    const double variationMax = 2.0;
    const double segmentSizeMin = 1.0;
    const double segmentSizeMax = 260.0;
    const double rotation3DMin = -180.0;
    const double rotation3DMax = 180.0;
    const double flameDepthMin = 0.0;
    const double flameDepthMax = 3.0;
    const double randomnessMin = 0.0;
    const double randomnessMax = 6.0;
    const double fractalAmplitudeMin = 0.0;
    const double fractalAmplitudeMax = 180.0;
    const double fractalFrequencyMin = 1.0;
    const double fractalFrequencyMax = 900.0;
    const double fractalEvolutionMin = -20.0;
    const double fractalEvolutionMax = 20.0;
    const double fractalOffsetMin = -20.0;
    const double fractalOffsetMax = 20.0;
    const double octScaleMin = 1.0;
    const double octScaleMax = 6.0;
    const double octMultMin = 0.0;
    const double octMultMax = 1.5;
    const double smoothNormalsMin = 0.0;
    const double smoothNormalsMax = 4.0;
    const double controlPointMin = -12.0;
    const double controlPointMax = 12.0;
    const double pointSizeMin = 1.0;
    const double pointSizeMax = 12.0;
    const Scene defaultScene = CreateDefaultScene();
    const PathSettings& defaultPath = defaultScene.paths.front();
    const TransformLayer& defaultLayer = defaultScene.transforms.front();
    const SegmentSettings defaultSegment {};
    const FractalDisplacementSettings defaultFractal {};
    const MaterialSettings defaultMaterial {};
    const FlameRenderSettings defaultFlameRender {};
    const auto checkboxWithReset = [&](const char* label, bool* value, const bool defaultValue) {
        const bool changed = ImGui::Checkbox(label, value);
        return ResetValueOnDoubleClick(*value, defaultValue) || changed;
    };
    const auto sliderDoubleWithReset = [&](const char* label, double* value, const double* minValue, const double* maxValue, const char* format, const double defaultValue) {
        const bool changed = SliderScalarWithInput(label, ImGuiDataType_Double, value, minValue, maxValue, format);
        return ResetValueOnDoubleClick(*value, defaultValue) || changed;
    };
    const auto sliderIntWithReset = [&](const char* label, int* value, const int minimum, const int maximum, const int defaultValue) {
        const bool changed = SliderIntWithInput(label, value, minimum, maximum);
        return ResetValueOnDoubleClick(*value, defaultValue) || changed;
    };
    const auto comboWithReset = [&](const char* label, int* value, const char* const items[], const int itemCount, const int defaultValue) {
        const bool changed = ComboWithMaterialArrow(label, value, items, itemCount);
        return ResetValueOnDoubleClick(*value, defaultValue) || changed;
    };
    const auto colorEditWithReset = [&](const char* label, float color[3], const Color& defaultColor) {
        const bool changed = ImGui::ColorEdit3(label, color, ImGuiColorEditFlags_DisplayRGB);
        return ResetColorOnDoubleClick(color, defaultColor) || changed;
    };
    const auto toggleVisibility = [&](bool& value) {
        const bool previous = value;
        if (ImGui::Checkbox("Visible", &value)) {
            PushUndoState(scene_);
            viewportDirty_ = true;
            statusText_ = value ? L"Layer shown" : L"Layer hidden";
            return previous != value;
        }
        return false;
    };

    if (inspectorTarget_ == InspectorTarget::PathLayer) {
        static const char* kSegmentModes[] = {"Extrude N-gon", "Repeat N-gon", "Repeat Sphere"};
        static const char* kAxes[] = {"X", "Y", "Z"};
        static const char* kTessellateModes[] = {"Triangles", "Lines"};
        static const char* kFractalTypes[] = {"Regular", "Turbulent"};
        static const char* kFractalSpaces[] = {"World", "Local"};
        static const char* kRenderModes[] = {"Solid", "Solid + Wire", "Wireframe", "Points"};
        static const char* kMaterialTypes[] = {"Metallic", "Flat", "Matte", "Glossy"};
        PathSettings& path = scene_.paths[scene_.selectedPath];
        const Scene proceduralReference = CreatePresetScene("Procedural Forge");

        ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.93f, 1.0f), "%s", path.name.c_str());
        ImGui::SeparatorText("Path Controls");

        Scene before = scene_;
        bool changed = checkboxWithReset("Closed Path", &path.closed, defaultPath.closed);
        captureEdit(before, changed);

        toggleVisibility(path.visible);

        before = scene_;
        changed = sliderDoubleWithReset("Thickness", &path.thickness, &pathThicknessMin, &pathThicknessMax, "%.2f", defaultPath.thickness);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Taper", &path.taper, &taperMin, &taperMax, "%.2f", defaultPath.taper);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Twist", &path.twist, &twistMin, &twistMax, "%.2f", defaultPath.twist);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderIntWithReset("Samples", &path.sampleCount, 24, 256, defaultPath.sampleCount);
        captureEdit(before, changed);

        if (ImGui::Button("Procedural Path")) {
            PushUndoState(scene_);
            const std::size_t templateIndex = static_cast<std::size_t>(scene_.selectedPath) % proceduralReference.paths.size();
            const std::string currentName = path.name;
            path = proceduralReference.paths[templateIndex];
            path.name = currentName;
            viewportDirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Procedural Finish")) {
            PushUndoState(scene_);
            const std::size_t templateIndex = static_cast<std::size_t>(scene_.selectedPath) % proceduralReference.paths.size();
            const PathSettings& templatePath = proceduralReference.paths[templateIndex];
            path.thickness = templatePath.thickness;
            path.taper = templatePath.taper;
            path.twist = templatePath.twist;
            path.repeatCount = templatePath.repeatCount;
            path.sampleCount = templatePath.sampleCount;
            path.segment = templatePath.segment;
            path.fractalDisplacement = templatePath.fractalDisplacement;
            path.material = templatePath.material;
            viewportDirty_ = true;
        }

        if (ImGui::Button("Straight Path")) {
            PushUndoState(scene_);
            path.controlPoints = {
                {-5.6, 0.0, 0.0},
                {-1.9, 0.0, 0.0},
                {1.9, 0.0, 0.0},
                {5.6, 0.0, 0.0}
            };
            viewportDirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Random Path")) {
            PushUndoState(scene_);
            std::mt19937 generator(static_cast<std::uint32_t>(GetTickCount64()));
            std::uniform_real_distribution<double> jitterX(-5.5, 5.5);
            std::uniform_real_distribution<double> jitterY(-4.6, 4.6);
            std::uniform_real_distribution<double> jitterZ(-5.4, 5.4);
            std::uniform_real_distribution<double> thicknessProfileDist(0.0, 1.0);
            std::uniform_real_distribution<double> junctionDist(0.0, 0.6);
            std::uniform_real_distribution<double> warpDist(0.0, 0.8);
            std::uniform_real_distribution<double> tendrilDist(0, 8);
            std::uniform_real_distribution<double> layoutDist(0, 1.0);
            std::uniform_real_distribution<double> hueDist(0.0, 360.0);
            std::uniform_real_distribution<double> satDist(0.3, 0.9);
            std::uniform_real_distribution<double> valDist(0.4, 0.95);
            path.controlPoints.clear();
            for (int index = 0; index < 6; ++index) {
                const double t = static_cast<double>(index) / 5.0;
                path.controlPoints.push_back({
                    Lerp(-5.0, 5.0, t) + jitterX(generator) * 0.22,
                    jitterY(generator),
                    jitterZ(generator)
                });
            }
            // Randomize procedural parameters
            const double profileRoll = thicknessProfileDist(generator);
            if (profileRoll < 0.5) {
                path.segment.thicknessProfile = ThicknessProfile::Linear;
            } else if (profileRoll < 0.7) {
                path.segment.thicknessProfile = ThicknessProfile::Pulse;
                path.segment.thicknessPulseFrequency = 1.0 + thicknessProfileDist(generator) * 4.0;
                path.segment.thicknessPulseDepth = 0.2 + thicknessProfileDist(generator) * 0.5;
            } else if (profileRoll < 0.85) {
                path.segment.thicknessProfile = ThicknessProfile::Bezier;
            } else {
                path.segment.thicknessProfile = ThicknessProfile::Blobby;
                path.segment.thicknessBlobCenter = 0.3 + thicknessProfileDist(generator) * 0.4;
                path.segment.thicknessBlobWidth = 0.15 + thicknessProfileDist(generator) * 0.25;
            }
            path.segment.junctionSize = junctionDist(generator);
            path.segment.junctionBlend = junctionDist(generator) * 0.5;
            path.segment.tubeWarp = warpDist(generator);
            path.segment.tubeWarpFrequency = 0.5 + warpDist(generator) * 3.0;
            path.segment.tendrilCount = static_cast<int>(tendrilDist(generator));
            if (path.segment.tendrilCount > 0) {
                path.segment.tendrilLength = 0.5 + warpDist(generator) * 2.0;
                path.segment.tendrilThickness = 0.1 + warpDist(generator) * 0.4;
                path.segment.tendrilWarp = warpDist(generator) * 0.8;
            }
            const double layoutRoll = layoutDist(generator);
            if (layoutRoll < 0.6) {
                path.layout = PathLayout::UserDefined;
            } else if (layoutRoll < 0.75) {
                path.layout = PathLayout::RadialCluster;
            } else if (layoutRoll < 0.9) {
                path.layout = PathLayout::Network;
            } else {
                path.layout = PathLayout::TendrilBall;
            }
            if (path.layout != PathLayout::UserDefined) {
                path.layoutRadius = 2.0 + layoutDist(generator) * 4.0;
                path.layoutNodes = 4 + static_cast<int>(layoutDist(generator) * 12);
                path.layoutRandomness = layoutDist(generator) * 0.6;
            }
            // Randomize colors
            std::uniform_int_distribution<int> colorChannel(60, 240);
            path.material.primaryColor = {static_cast<std::uint8_t>(colorChannel(generator)), static_cast<std::uint8_t>(colorChannel(generator)), static_cast<std::uint8_t>(colorChannel(generator)), 255};
            path.material.accentColor = {static_cast<std::uint8_t>(colorChannel(generator)), static_cast<std::uint8_t>(colorChannel(generator)), static_cast<std::uint8_t>(colorChannel(generator)), 255};
            path.material.wireColor = {static_cast<std::uint8_t>(colorChannel(generator)), static_cast<std::uint8_t>(colorChannel(generator)), static_cast<std::uint8_t>(colorChannel(generator)), 255};
            viewportDirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Point")) {
            PushUndoState(scene_);
            const Vec3 last = path.controlPoints.empty() ? Vec3{} : path.controlPoints.back();
            path.controlPoints.push_back({last.x + 2.2, last.y * -0.75, last.z + 1.6});
            viewportDirty_ = true;
        }

        if (ImGui::CollapsingHeader("Control Points", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (std::size_t pointIndex = 0; pointIndex < path.controlPoints.size(); ++pointIndex) {
                Vec3& point = path.controlPoints[pointIndex];
                const std::string label = "Point " + std::to_string(pointIndex + 1);
                if (ImGui::TreeNode(label.c_str())) {
                    before = scene_;
                    const Vec3 defaultPoint = pointIndex < defaultPath.controlPoints.size() ? defaultPath.controlPoints[pointIndex] : Vec3{};
                    changed = sliderDoubleWithReset(("X##pointx" + std::to_string(pointIndex)).c_str(), &point.x, &controlPointMin, &controlPointMax, "%.2f", defaultPoint.x);
                    captureEdit(before, changed);
                    before = scene_;
                    changed = sliderDoubleWithReset(("Y##pointy" + std::to_string(pointIndex)).c_str(), &point.y, &controlPointMin, &controlPointMax, "%.2f", defaultPoint.y);
                    captureEdit(before, changed);
                    before = scene_;
                    changed = sliderDoubleWithReset(("Z##pointz" + std::to_string(pointIndex)).c_str(), &point.z, &controlPointMin, &controlPointMax, "%.2f", defaultPoint.z);
                    captureEdit(before, changed);
                    if (path.controlPoints.size() > 2 && ImGui::Button(("Delete##point" + std::to_string(pointIndex)).c_str())) {
                        PushUndoState(scene_);
                        path.controlPoints.erase(path.controlPoints.begin() + static_cast<std::ptrdiff_t>(pointIndex));
                        viewportDirty_ = true;
                        ImGui::TreePop();
                        break;
                    }
                    ImGui::TreePop();
                }
            }
        }

        ImGui::SeparatorText("Segment");
        int segmentMode = static_cast<int>(path.segment.mode);
        before = scene_;
        changed = comboWithReset("Segment Mode", &segmentMode, kSegmentModes, IM_ARRAYSIZE(kSegmentModes), static_cast<int>(defaultPath.segment.mode));
        if (changed) {
            path.segment.mode = static_cast<SegmentMode>(segmentMode);
        }
        captureEdit(before, changed);

        before = scene_;
        changed = sliderIntWithReset("Segments", &path.segment.segments, 2, 64, defaultSegment.segments);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderIntWithReset("Sides", &path.segment.sides, 3, 12, defaultSegment.sides);
        captureEdit(before, changed);

        before = scene_;
        changed = checkboxWithReset("Break Sides", &path.segment.breakSides, defaultSegment.breakSides);
        captureEdit(before, changed);

        before = scene_;
        changed = checkboxWithReset("Chamfer", &path.segment.chamfer, defaultSegment.chamfer);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Chamfer Size", &path.segment.chamferSize, &segmentSizeMin, &segmentSizeMax, "%.1f", defaultSegment.chamferSize);
        captureEdit(before, changed);

        before = scene_;
        changed = checkboxWithReset("Caps", &path.segment.caps, defaultSegment.caps);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Size", &path.segment.size, &segmentSizeMin, &segmentSizeMax, "%.1f", defaultSegment.size);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Size X", &path.segment.sizeX, &segmentSizeMin, &segmentSizeMax, "%.1f", defaultSegment.sizeX);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Size Y", &path.segment.sizeY, &segmentSizeMin, &segmentSizeMax, "%.1f", defaultSegment.sizeY);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Size Z", &path.segment.sizeZ, &segmentSizeMin, &segmentSizeMax, "%.1f", defaultSegment.sizeZ);
        captureEdit(before, changed);

        before = scene_;
        changed = checkboxWithReset("Orient to Path", &path.segment.orientToPath, defaultSegment.orientToPath);
        captureEdit(before, changed);

        int axisIndex = static_cast<int>(path.segment.orientReferenceAxis);
        before = scene_;
        changed = comboWithReset("Orient Reference Axis", &axisIndex, kAxes, IM_ARRAYSIZE(kAxes), static_cast<int>(defaultSegment.orientReferenceAxis));
        if (changed) {
            path.segment.orientReferenceAxis = static_cast<PathAxis>(axisIndex);
        }
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Rotate X", &path.segment.rotateX, &rotation3DMin, &rotation3DMax, "%.1f", defaultSegment.rotateX);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Rotate Y", &path.segment.rotateY, &rotation3DMin, &rotation3DMax, "%.1f", defaultSegment.rotateY);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Rotate Z", &path.segment.rotateZ, &rotation3DMin, &rotation3DMax, "%.1f", defaultSegment.rotateZ);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Twist Z", &path.segment.twistZ, &rotation3DMin, &rotation3DMax, "%.1f", defaultSegment.twistZ);
        captureEdit(before, changed);

        before = scene_;
        changed = checkboxWithReset("Debug Normals", &path.segment.debugNormals, defaultSegment.debugNormals);
        captureEdit(before, changed);

        int tessellationMode = static_cast<int>(path.segment.tessellate);
        before = scene_;
        changed = comboWithReset("Tessellate", &tessellationMode, kTessellateModes, IM_ARRAYSIZE(kTessellateModes), static_cast<int>(defaultSegment.tessellate));
        if (changed) {
            path.segment.tessellate = static_cast<TessellationMode>(tessellationMode);
        }
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Randomness", &path.segment.randomness, &randomnessMin, &randomnessMax, "%.2f", defaultSegment.randomness);
        captureEdit(before, changed);

        // Procedural section
        ImGui::SeparatorText("Procedural");
        static const char* kThicknessProfiles[] = {"Linear", "Pulse", "Bezier", "Blobby"};
        int thicknessProfile = static_cast<int>(path.segment.thicknessProfile);
        before = scene_;
        changed = comboWithReset("Thickness Profile", &thicknessProfile, kThicknessProfiles, IM_ARRAYSIZE(kThicknessProfiles), static_cast<int>(defaultSegment.thicknessProfile));
        if (changed) {
            path.segment.thicknessProfile = static_cast<ThicknessProfile>(thicknessProfile);
        }
        captureEdit(before, changed);

        if (path.segment.thicknessProfile == ThicknessProfile::Pulse) {
            before = scene_;
            changed = sliderDoubleWithReset("Pulse Freq", &path.segment.thicknessPulseFrequency, &taperMin, &twistMax, "%.2f", defaultSegment.thicknessPulseFrequency);
            captureEdit(before, changed);
            
            before = scene_;
            changed = sliderDoubleWithReset("Pulse Depth", &path.segment.thicknessPulseDepth, &taperMin, &taperMax, "%.2f", defaultSegment.thicknessPulseDepth);
            captureEdit(before, changed);
        }

        if (path.segment.thicknessProfile == ThicknessProfile::Blobby) {
            before = scene_;
            changed = sliderDoubleWithReset("Blob Center", &path.segment.thicknessBlobCenter, &taperMin, &taperMax, "%.2f", defaultSegment.thicknessBlobCenter);
            captureEdit(before, changed);
            
            before = scene_;
            changed = sliderDoubleWithReset("Blob Width", &path.segment.thicknessBlobWidth, &taperMin, &taperMax, "%.2f", defaultSegment.thicknessBlobWidth);
            captureEdit(before, changed);
        }

        before = scene_;
        changed = sliderDoubleWithReset("Junction Size", &path.segment.junctionSize, &taperMin, &taperMax, "%.2f", defaultSegment.junctionSize);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Junction Blend", &path.segment.junctionBlend, &taperMin, &taperMax, "%.2f", defaultSegment.junctionBlend);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Tube Warp", &path.segment.tubeWarp, &taperMin, &taperMax, "%.2f", defaultSegment.tubeWarp);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Tube Warp Freq", &path.segment.tubeWarpFrequency, &taperMin, &twistMax, "%.2f", defaultSegment.tubeWarpFrequency);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderIntWithReset("Tendril Count", &path.segment.tendrilCount, 0, 20, defaultSegment.tendrilCount);
        captureEdit(before, changed);

        if (path.segment.tendrilCount > 0) {
            before = scene_;
            changed = sliderDoubleWithReset("Tendril Length", &path.segment.tendrilLength, &taperMin, &twistMax, "%.2f", defaultSegment.tendrilLength);
            captureEdit(before, changed);
            
            before = scene_;
            changed = sliderDoubleWithReset("Tendril Thick", &path.segment.tendrilThickness, &taperMin, &taperMax, "%.2f", defaultSegment.tendrilThickness);
            captureEdit(before, changed);
            
            before = scene_;
            changed = sliderDoubleWithReset("Tendril Warp", &path.segment.tendrilWarp, &taperMin, &taperMax, "%.2f", defaultSegment.tendrilWarp);
            captureEdit(before, changed);
        }

        // Path Layout section
        static const char* kPathLayouts[] = {"User Defined", "Radial Cluster", "Network", "Tendril Ball"};
        int pathLayout = static_cast<int>(path.layout);
        before = scene_;
        changed = comboWithReset("Path Layout", &pathLayout, kPathLayouts, IM_ARRAYSIZE(kPathLayouts), static_cast<int>(defaultPath.layout));
        if (changed) {
            path.layout = static_cast<PathLayout>(pathLayout);
        }
        captureEdit(before, changed);

        if (path.layout != PathLayout::UserDefined) {
            before = scene_;
            changed = sliderDoubleWithReset("Layout Radius", &path.layoutRadius, &taperMin, &twistMax, "%.2f", defaultPath.layoutRadius);
            captureEdit(before, changed);
            
            before = scene_;
            changed = sliderIntWithReset("Layout Nodes", &path.layoutNodes, 3, 24, defaultPath.layoutNodes);
            captureEdit(before, changed);
            
            before = scene_;
            changed = sliderDoubleWithReset("Layout Random", &path.layoutRandomness, &taperMin, &taperMax, "%.2f", defaultPath.layoutRandomness);
            captureEdit(before, changed);
        }

        int fractalSpace = static_cast<int>(path.fractalDisplacement.space);
        before = scene_;
        changed = comboWithReset("Space", &fractalSpace, kFractalSpaces, IM_ARRAYSIZE(kFractalSpaces), static_cast<int>(defaultFractal.space));
        if (changed) {
            path.fractalDisplacement.space = static_cast<FractalSpace>(fractalSpace);
        }
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Amplitude", &path.fractalDisplacement.amplitude, &fractalAmplitudeMin, &fractalAmplitudeMax, "%.2f", defaultFractal.amplitude);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Frequency", &path.fractalDisplacement.frequency, &fractalFrequencyMin, &fractalFrequencyMax, "%.2f", defaultFractal.frequency);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Evolution", &path.fractalDisplacement.evolution, &fractalEvolutionMin, &fractalEvolutionMax, "%.2f", defaultFractal.evolution);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Offset X", &path.fractalDisplacement.offsetX, &fractalOffsetMin, &fractalOffsetMax, "%.2f", defaultFractal.offsetX);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Offset Y", &path.fractalDisplacement.offsetY, &fractalOffsetMin, &fractalOffsetMax, "%.2f", defaultFractal.offsetY);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Offset Z", &path.fractalDisplacement.offsetZ, &fractalOffsetMin, &fractalOffsetMax, "%.2f", defaultFractal.offsetZ);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderIntWithReset("Complexity", &path.fractalDisplacement.complexity, 1, 6, defaultFractal.complexity);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Oct Scale", &path.fractalDisplacement.octScale, &octScaleMin, &octScaleMax, "%.2f", defaultFractal.octScale);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Oct Mult", &path.fractalDisplacement.octMult, &octMultMin, &octMultMax, "%.2f", defaultFractal.octMult);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Smoothen Normals", &path.fractalDisplacement.smoothenNormals, &smoothNormalsMin, &smoothNormalsMax, "%.2f", defaultFractal.smoothenNormals);
        captureEdit(before, changed);

        before = scene_;
        changed = checkboxWithReset("Seamless Loop", &path.fractalDisplacement.seamlessLoop, defaultFractal.seamlessLoop);
        captureEdit(before, changed);

        ImGui::SeparatorText("Materials");
        int renderMode = static_cast<int>(path.material.renderMode);
        before = scene_;
        changed = comboWithReset("Render Mode", &renderMode, kRenderModes, IM_ARRAYSIZE(kRenderModes), static_cast<int>(defaultMaterial.renderMode));
        if (changed) {
            path.material.renderMode = static_cast<PathRenderMode>(renderMode);
        }
        captureEdit(before, changed);

        int materialType = static_cast<int>(path.material.materialType);
        before = scene_;
        changed = comboWithReset("Material", &materialType, kMaterialTypes, IM_ARRAYSIZE(kMaterialTypes), static_cast<int>(defaultMaterial.materialType));
        if (changed) {
            path.material.materialType = static_cast<MaterialType>(materialType);
        }
        captureEdit(before, changed);

        float primaryColor[3] = {
            path.material.primaryColor.r / 255.0f,
            path.material.primaryColor.g / 255.0f,
            path.material.primaryColor.b / 255.0f
        };
        before = scene_;
        changed = colorEditWithReset("Primary Color", primaryColor, defaultMaterial.primaryColor);
        if (changed) {
            path.material.primaryColor = ToColor(primaryColor);
        }
        captureEdit(before, changed);

        float accentColor[3] = {
            path.material.accentColor.r / 255.0f,
            path.material.accentColor.g / 255.0f,
            path.material.accentColor.b / 255.0f
        };
        before = scene_;
        changed = colorEditWithReset("Accent Color", accentColor, defaultMaterial.accentColor);
        if (changed) {
            path.material.accentColor = ToColor(accentColor);
        }
        captureEdit(before, changed);

        float wireColor[3] = {
            path.material.wireColor.r / 255.0f,
            path.material.wireColor.g / 255.0f,
            path.material.wireColor.b / 255.0f
        };
        before = scene_;
        changed = colorEditWithReset("Wire Color", wireColor, defaultMaterial.wireColor);
        if (changed) {
            path.material.wireColor = ToColor(wireColor);
        }
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Point Size", &path.material.pointSize, &pointSizeMin, &pointSizeMax, "%.1f", defaultMaterial.pointSize);
        captureEdit(before, changed);
    } else {
        TransformLayer& layer = scene_.transforms[scene_.selectedTransform];
        ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.93f, 1.0f), "%s", layer.name.c_str());
        ImGui::SeparatorText("Transform Controls");

        Scene before = scene_;
        bool changed = false;

        toggleVisibility(layer.visible);

        before = scene_;
        changed = sliderDoubleWithReset("Weight", &layer.weight, &weightMin, &weightMax, "%.2f", defaultLayer.weight);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Rotation", &layer.rotationDegrees, &rotationMin, &rotationMax, "%.2f", defaultLayer.rotationDegrees);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Scale X", &layer.scaleX, &scaleMin, &scaleMax, "%.2f", defaultLayer.scaleX);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Scale Y", &layer.scaleY, &scaleMin, &scaleMax, "%.2f", defaultLayer.scaleY);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Offset X", &layer.translateX, &offsetMin, &offsetMax, "%.2f", defaultLayer.translateX);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Offset Y", &layer.translateY, &offsetMin, &offsetMax, "%.2f", defaultLayer.translateY);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Shear X", &layer.shearX, &shearMin, &shearMax, "%.2f", defaultLayer.shearX);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Shear Y", &layer.shearY, &shearMin, &shearMax, "%.2f", defaultLayer.shearY);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Color Index", &layer.colorIndex, &colorIndexMin, &colorIndexMax, "%.2f", defaultLayer.colorIndex);
        captureEdit(before, changed);

        ImGui::SeparatorText("Layer Color");
        before = scene_;
        changed = checkboxWithReset("Use Layer Color", &layer.useCustomColor, defaultLayer.useCustomColor);
        captureEdit(before, changed);

        float layerColor[3] = {
            layer.customColor.r / 255.0f,
            layer.customColor.g / 255.0f,
            layer.customColor.b / 255.0f
        };
        ImGui::BeginDisabled(!layer.useCustomColor);
        before = scene_;
        changed = colorEditWithReset("Layer Color", layerColor, defaultLayer.customColor);
        if (changed) {
            layer.customColor = ToColor(layerColor);
        }
        captureEdit(before, changed);
        ImGui::EndDisabled();

        ImGui::SeparatorText("Flame Space");

        before = scene_;
        changed = sliderDoubleWithReset("Flame Rotate X", &scene_.flameRender.rotationXDegrees, &rotation3DMin, &rotation3DMax, "%.1f", defaultFlameRender.rotationXDegrees);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Flame Rotate Y", &scene_.flameRender.rotationYDegrees, &rotation3DMin, &rotation3DMax, "%.1f", defaultFlameRender.rotationYDegrees);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Flame Rotate Z", &scene_.flameRender.rotationZDegrees, &rotation3DMin, &rotation3DMax, "%.1f", defaultFlameRender.rotationZDegrees);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Flame Depth", &scene_.flameRender.depthAmount, &flameDepthMin, &flameDepthMax, "%.2f", defaultFlameRender.depthAmount);
        captureEdit(before, changed);

        ImGui::SeparatorText("Flame Curve");
        const double flameCurveExposureMin = 0.25;
        const double flameCurveExposureMax = 2.5;
        const double flameCurveContrastMin = 0.45;
        const double flameCurveContrastMax = 2.0;
        const double flameCurveHighlightsMin = 0.0;
        const double flameCurveHighlightsMax = 2.0;
        const double flameCurveGammaMin = 0.45;
        const double flameCurveGammaMax = 1.8;

        before = scene_;
        changed = sliderDoubleWithReset("Exposure", &scene_.flameRender.curveExposure, &flameCurveExposureMin, &flameCurveExposureMax, "%.2f", defaultFlameRender.curveExposure);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Contrast", &scene_.flameRender.curveContrast, &flameCurveContrastMin, &flameCurveContrastMax, "%.2f", defaultFlameRender.curveContrast);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Highlights", &scene_.flameRender.curveHighlights, &flameCurveHighlightsMin, &flameCurveHighlightsMax, "%.2f", defaultFlameRender.curveHighlights);
        captureEdit(before, changed);

        before = scene_;
        changed = sliderDoubleWithReset("Gamma", &scene_.flameRender.curveGamma, &flameCurveGammaMin, &flameCurveGammaMax, "%.2f", defaultFlameRender.curveGamma);
        captureEdit(before, changed);

        ImGui::SeparatorText("Palette");
        for (int paletteIndex = 0; paletteIndex < 3; ++paletteIndex) {
            const std::size_t stopIndex = GradientStopIndexForPaletteColor(paletteIndex);
            const char* labels[] = {"Shadow", "Accent", "Glow"};
            float color[3] = {
                scene_.gradientStops[stopIndex].color.r / 255.0f,
                scene_.gradientStops[stopIndex].color.g / 255.0f,
                scene_.gradientStops[stopIndex].color.b / 255.0f
            };
            before = scene_;
            changed = colorEditWithReset(labels[paletteIndex], color, defaultScene.gradientStops[stopIndex].color);
            if (changed) {
                scene_.gradientStops[stopIndex].color = ToColor(color);
            }
            captureEdit(before, changed);
        }

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.13f, 0.19f, 0.32f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.26f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.31f, 0.50f, 1.0f));
        if (ImGui::CollapsingHeader("Variations", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (std::size_t variation = 0; variation < kVariationCount; ++variation) {
                const std::string label = ToString(static_cast<VariationType>(variation));
                before = scene_;
                changed = sliderDoubleWithReset(label.c_str(), &layer.variations[variation], &variationMin, &variationMax, "%.2f", defaultLayer.variations[variation]);
                captureEdit(before, changed);
            }
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::End();
}

void AppWindow::DrawTimelinePanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Playback", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    playbackPanelActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    const Scene defaultScene = CreateDefaultScene();
    const float playbackTopGap = 1.0f;
    const float playbackSectionGap = 3.0f;
    ImGui::Dummy(ImVec2(0.0f, playbackTopGap));
    const auto drawHeaderDivider = []() {
        DrawSubtleToolbarDivider(8.0f);
    };
    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const KeyframeOwnerType currentOwnerType = inspectorTarget_ == InspectorTarget::PathLayer ? KeyframeOwnerType::Path : KeyframeOwnerType::Transform;
    const int currentOwnerIndex = inspectorTarget_ == InspectorTarget::PathLayer ? scene_.selectedPath : scene_.selectedTransform;
    const int currentKeyframeIndex = FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex);
    int previousKeyframeIndex = -1;
    int nextKeyframeIndex = -1;
    for (std::size_t index = 0; index < scene_.keyframes.size(); ++index) {
        const int keyframeFrame = scene_.keyframes[index].frame;
        if (keyframeFrame < currentFrame) {
            previousKeyframeIndex = static_cast<int>(index);
        } else if (keyframeFrame > currentFrame) {
            nextKeyframeIndex = static_cast<int>(index);
            break;
        }
    }

    if (DrawActionButton("##timeline_play", scene_.animatePath ? "Pause" : "Play", scene_.animatePath ? IconGlyph::Pause : IconGlyph::Play, ActionTone::Accent, scene_.animatePath, true, 108.0f)) {
        PushUndoState(scene_);
        scene_.animatePath = !scene_.animatePath;
        viewportDirty_ = true;
    }
    ImGui::SameLine();
    if (DrawActionButton("##timeline_prev_key", "", IconGlyph::PreviousKeyframe, ActionTone::Slate, false, previousKeyframeIndex >= 0, 0.0f, "Snap to previous keyframe")) {
        SetTimelineFrame(static_cast<double>(scene_.keyframes[static_cast<std::size_t>(previousKeyframeIndex)].frame), false);
        selectedTimelineKeyframe_ = previousKeyframeIndex;
    }
    ImGui::SameLine();
    if (DrawActionButton("##timeline_next_key", "", IconGlyph::NextKeyframe, ActionTone::Slate, false, nextKeyframeIndex >= 0, 0.0f, "Snap to next keyframe")) {
        SetTimelineFrame(static_cast<double>(scene_.keyframes[static_cast<std::size_t>(nextKeyframeIndex)].frame), false);
        selectedTimelineKeyframe_ = nextKeyframeIndex;
    }
    drawHeaderDivider();
    if (DrawActionButton("##timeline_add_key", "", IconGlyph::Add, ActionTone::Accent, false, true, 0.0f, "Add keyframe at current frame")) {
        PushUndoState(scene_);
        const int frame = currentFrame;
        const int existingIndex = FindKeyframeIndex(scene_, frame, currentOwnerType, currentOwnerIndex);
        SceneKeyframe keyframe;
        keyframe.frame = frame;
        keyframe.markerColor = inspectorTarget_ == InspectorTarget::PathLayer ? Color{96, 188, 224, 255} : Color{248, 164, 88, 255};
        AssignCurrentLayerToKeyframe(keyframe);
        if (existingIndex >= 0) {
            keyframe.easing = scene_.keyframes[static_cast<std::size_t>(existingIndex)].easing;
            keyframe.easeX1 = scene_.keyframes[static_cast<std::size_t>(existingIndex)].easeX1;
            keyframe.easeY1 = scene_.keyframes[static_cast<std::size_t>(existingIndex)].easeY1;
            keyframe.easeX2 = scene_.keyframes[static_cast<std::size_t>(existingIndex)].easeX2;
            keyframe.easeY2 = scene_.keyframes[static_cast<std::size_t>(existingIndex)].easeY2;
        } else {
            ApplyKeyframeEasingPreset(keyframe, KeyframeEasing::Linear);
        }
        keyframe.pose = CaptureScenePose(scene_);
        if (existingIndex >= 0) {
            scene_.keyframes[static_cast<std::size_t>(existingIndex)] = keyframe;
        } else {
            scene_.keyframes.push_back(std::move(keyframe));
            SortKeyframes(scene_);
        }
        selectedTimelineKeyframe_ = FindKeyframeIndex(scene_, frame, currentOwnerType, currentOwnerIndex);
        RefreshTimelinePose();
    }
    ImGui::SameLine();
    if (DrawActionButton("##timeline_remove_key", "", IconGlyph::Remove, ActionTone::Slate, false, CanRemoveSelectedOrCurrentKeyframe(), 0.0f, "Remove selected/current keyframe")) {
        RemoveSelectedOrCurrentKeyframe();
    }
    ImGui::SameLine();
    if (DrawActionButton("##reset_camera", "Reset Camera", IconGlyph::ResetCamera, ActionTone::Accent, false, true, 138.0f)) {
        PushUndoState(scene_);
        scene_.camera = CameraState{};
        SyncCurrentKeyframeFromScene();
        viewportDirty_ = true;
    }

    drawHeaderDivider();
    static const char* kKeyframeEasingLabels[] = {"Linear", "Ease In", "Ease Out", "Ease In Out", "Hold", "Custom"};
    const int editableKeyframeIndex = selectedTimelineKeyframe_ >= 0 ? selectedTimelineKeyframe_ : currentKeyframeIndex;
    if (editableKeyframeIndex >= 0 && editableKeyframeIndex < static_cast<int>(scene_.keyframes.size())) {
        Scene beforeEasing = scene_;
        int easingIndex = static_cast<int>(scene_.keyframes[static_cast<std::size_t>(editableKeyframeIndex)].easing);
        ImGui::SetNextItemWidth(118.0f);
        if (ComboWithMaterialArrow("##timeline_easing", &easingIndex, kKeyframeEasingLabels, IM_ARRAYSIZE(kKeyframeEasingLabels))) {
            ApplyKeyframeEasingPreset(scene_.keyframes[static_cast<std::size_t>(editableKeyframeIndex)], static_cast<KeyframeEasing>(easingIndex));
            RefreshTimelinePose();
        }
        CaptureWidgetUndo(beforeEasing, ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemEdited());
        ImGui::SameLine();
        if (DrawActionButton("##timeline_curve_popup", "", IconGlyph::Settings, ActionTone::Slate, easingPanelOpen_, true, 0.0f, "Open easing curve editor")) {
            easingPanelOpen_ = !easingPanelOpen_;
        }
        easingButtonAnchorX_ = ImGui::GetItemRectMin().x;
        easingButtonAnchorY_ = ImGui::GetItemRectMin().y - kOverlayPanelMarginY;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Easing");
        ImGui::SameLine();
    } else {
        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(118.0f);
        int easingIndex = 0;
        ComboWithMaterialArrow("##timeline_easing", &easingIndex, kKeyframeEasingLabels, IM_ARRAYSIZE(kKeyframeEasingLabels));
        ImGui::SameLine();
        DrawActionButton("##timeline_curve_popup_disabled", "", IconGlyph::Settings, ActionTone::Slate, false, false, 0.0f, "Open easing curve editor");
        ImGui::EndDisabled();
        easingPanelOpen_ = false;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Easing");
        ImGui::SameLine();
    }
    char playbackSummary[96];
    std::snprintf(
        playbackSummary,
        sizeof(playbackSummary),
        "Frame %d / %d at %.2f fps",
        currentFrame,
        scene_.timelineEndFrame,
        scene_.timelineFrameRate);
    constexpr float kTimelineValueWidth = 82.0f;
    constexpr float kTimelineFpsWidth = 76.0f;
    constexpr float kTimelineLabelGap = 6.0f;
    constexpr float kTimelinePairGap = 14.0f;
    const float summaryWidth = ImGui::CalcTextSize(playbackSummary).x;
    const float rightToolbarWidth =
        summaryWidth
        + 18.0f
        + ImGui::CalcTextSize("Start").x + kTimelineLabelGap + kTimelineValueWidth
        + kTimelinePairGap
        + ImGui::CalcTextSize("End").x + kTimelineLabelGap + kTimelineValueWidth
        + kTimelinePairGap
        + ImGui::CalcTextSize("FPS").x + kTimelineLabelGap + kTimelineFpsWidth
        + kTimelinePairGap
        + ImGui::CalcTextSize("Frame").x + kTimelineLabelGap + kTimelineValueWidth;
    const float currentToolbarX = ImGui::GetCursorPosX();
    const float rightAlignedToolbarX = std::max(currentToolbarX, ImGui::GetWindowContentRegionMax().x - rightToolbarWidth);
    ImGui::SameLine(rightAlignedToolbarX);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", playbackSummary);
    drawHeaderDivider();

    Scene beforeTimeline = scene_;
    bool timelineChanged = false;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Start");
    ImGui::SameLine(0.0f, kTimelineLabelGap);
    ImGui::SetNextItemWidth(kTimelineValueWidth);
    timelineChanged = DragIntWithInput("##timeline_start", &scene_.timelineStartFrame, 1.0f, -100000, 100000) || timelineChanged;
    timelineChanged = ResetValueOnDoubleClick(scene_.timelineStartFrame, defaultScene.timelineStartFrame) || timelineChanged;
    ImGui::SameLine(0.0f, kTimelinePairGap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("End");
    ImGui::SameLine(0.0f, kTimelineLabelGap);
    ImGui::SetNextItemWidth(kTimelineValueWidth);
    timelineChanged = DragIntWithInput("##timeline_end", &scene_.timelineEndFrame, 1.0f, -100000, 100000) || timelineChanged;
    timelineChanged = ResetValueOnDoubleClick(scene_.timelineEndFrame, defaultScene.timelineEndFrame) || timelineChanged;
    ImGui::SameLine(0.0f, kTimelinePairGap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("FPS");
    ImGui::SameLine(0.0f, kTimelineLabelGap);
    ImGui::SetNextItemWidth(kTimelineFpsWidth);
    timelineChanged = DragScalarWithInput("##timeline_fps", ImGuiDataType_Double, &scene_.timelineFrameRate, 0.1f, nullptr, nullptr, "%.2f") || timelineChanged;
    timelineChanged = ResetValueOnDoubleClick(scene_.timelineFrameRate, defaultScene.timelineFrameRate) || timelineChanged;
    ImGui::SameLine(0.0f, kTimelinePairGap);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Frame");
    ImGui::SameLine(0.0f, kTimelineLabelGap);
    int playheadFrame = currentFrame;
    ImGui::SetNextItemWidth(kTimelineValueWidth);
    if (DragIntWithInput("##timeline_frame", &playheadFrame, 1.0f, scene_.timelineStartFrame, std::max(scene_.timelineStartFrame, scene_.timelineEndFrame))
        || ResetValueOnDoubleClick(playheadFrame, static_cast<int>(defaultScene.timelineFrame))) {
        SetTimelineFrame(static_cast<double>(playheadFrame), false);
        timelineChanged = true;
    }
    scene_.timelineEndFrame = std::max(scene_.timelineStartFrame, scene_.timelineEndFrame);
    scene_.timelineFrameRate = std::max(1.0, scene_.timelineFrameRate);
    if (timelineChanged) {
        scene_.timelineFrame = Clamp(scene_.timelineFrame, static_cast<double>(scene_.timelineStartFrame), static_cast<double>(scene_.timelineEndFrame));
        scene_.timelineSeconds = TimelineSecondsForFrame(scene_, scene_.timelineFrame);
        viewportDirty_ = true;
    }
    CaptureWidgetUndo(beforeTimeline, timelineChanged);

    ImGui::Dummy(ImVec2(0.0f, playbackSectionGap));
    struct TimelineLaneInfo {
        KeyframeOwnerType ownerType = KeyframeOwnerType::Transform;
        int ownerIndex = 0;
    };
    const auto normalizedLaneForKeyframe = [&](const SceneKeyframe& keyframe) {
        TimelineLaneInfo lane;
        lane.ownerType = keyframe.ownerType;
        if (lane.ownerType == KeyframeOwnerType::Path) {
            lane.ownerIndex = std::clamp(keyframe.ownerIndex, 0, std::max(0, static_cast<int>(scene_.paths.size()) - 1));
        } else {
            lane.ownerType = KeyframeOwnerType::Transform;
            lane.ownerIndex = std::clamp(keyframe.ownerIndex, 0, std::max(0, static_cast<int>(scene_.transforms.size()) - 1));
        }
        return lane;
    };
    std::vector<TimelineLaneInfo> timelineLanes;
    timelineLanes.reserve(scene_.keyframes.size());
    for (const SceneKeyframe& keyframe : scene_.keyframes) {
        const TimelineLaneInfo lane = normalizedLaneForKeyframe(keyframe);
        const bool exists = std::any_of(timelineLanes.begin(), timelineLanes.end(), [&](const TimelineLaneInfo& current) {
            return current.ownerType == lane.ownerType && current.ownerIndex == lane.ownerIndex;
        });
        if (!exists) {
            timelineLanes.push_back(lane);
        }
    }
    std::sort(timelineLanes.begin(), timelineLanes.end(), [](const TimelineLaneInfo& left, const TimelineLaneInfo& right) {
        if (left.ownerType != right.ownerType) {
            return left.ownerType < right.ownerType;
        }
        return left.ownerIndex < right.ownerIndex;
    });
    const auto laneIndexForKeyframe = [&](const SceneKeyframe& keyframe) {
        const TimelineLaneInfo lane = normalizedLaneForKeyframe(keyframe);
        for (std::size_t index = 0; index < timelineLanes.size(); ++index) {
            if (timelineLanes[index].ownerType == lane.ownerType && timelineLanes[index].ownerIndex == lane.ownerIndex) {
                return static_cast<int>(index);
            }
        }
        return 0;
    };
    const float laneSpacing = 14.0f;
    const float trackHeight = std::max(72.0f, 42.0f + std::max(1, static_cast<int>(timelineLanes.size())) * laneSpacing);
    ImGui::BeginChild("TimelineTrack", ImVec2(0.0f, trackHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    const ImVec2 trackMin = ImGui::GetWindowPos();
    const ImVec2 trackMax(trackMin.x + ImGui::GetWindowSize().x, trackMin.y + ImGui::GetWindowSize().y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 border = ImGui::GetColorU32(ImVec4(0.30f, 0.29f, 0.28f, 0.34f));
    const ImU32 guide = ImGui::GetColorU32(ImVec4(0.19f, 0.18f, 0.17f, 0.82f));
    const ImU32 laneGuide = ImGui::GetColorU32(ImVec4(0.15f, 0.16f, 0.18f, 0.88f));
    const ImU32 playhead = ImGui::GetColorU32(ImVec4(0.74f, 0.72f, 0.68f, 0.92f));
    const ImU32 fill = ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
    drawList->AddRectFilled(trackMin, trackMax, fill, 6.0f);
    drawList->AddRect(trackMin, trackMax, border, 6.0f);
    const float leftPad = 18.0f;
    const float rightPad = 18.0f;
    const float topPad = 10.0f;
    const float bottomPad = 18.0f;
    const float trackLeft = trackMin.x + leftPad;
    const float trackRight = trackMax.x - rightPad;
    const float usableWidth = std::max(1.0f, trackRight - trackLeft);
    const int startFrame = scene_.timelineStartFrame;
    const int endFrame = std::max(scene_.timelineStartFrame, scene_.timelineEndFrame);
    const int spanFrames = std::max(1, endFrame - startFrame);
    const auto frameToX = [&](const double frameValue) {
        const double normalized = Clamp((frameValue - static_cast<double>(startFrame)) / static_cast<double>(spanFrames), 0.0, 1.0);
        return trackLeft + static_cast<float>(normalized) * usableWidth;
    };

    for (int frame = startFrame; frame <= endFrame; frame += std::max(1, spanFrames / 10)) {
        const float x = frameToX(static_cast<double>(frame));
        drawList->AddLine(ImVec2(x, trackMin.y + topPad), ImVec2(x, trackMax.y - bottomPad), guide);
        drawList->AddText(ImVec2(x + 3.0f, trackMin.y + 4.0f), ImGui::GetColorU32(ImVec4(0.60f, 0.58f, 0.54f, 0.90f)), std::to_string(frame).c_str());
    }
    const auto markerYForLane = [&](const int laneIndex) {
        return trackMax.y - bottomPad - static_cast<float>(laneIndex) * laneSpacing;
    };
    for (std::size_t laneIndex = 0; laneIndex < timelineLanes.size(); ++laneIndex) {
        const float markerY = markerYForLane(static_cast<int>(laneIndex));
        drawList->AddLine(ImVec2(trackLeft, markerY), ImVec2(trackRight, markerY), laneGuide);
    }
    ImGui::SetCursorScreenPos(trackMin);
    ImGui::InvisibleButton("##timeline_track_input", ImGui::GetWindowSize());
    const bool trackHovered = ImGui::IsItemHovered();
    if (trackHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        selectedTimelineKeyframe_ = -1;
        for (std::size_t index = 0; index < scene_.keyframes.size(); ++index) {
            const float markerX = frameToX(static_cast<double>(scene_.keyframes[index].frame));
            const float markerY = markerYForLane(laneIndexForKeyframe(scene_.keyframes[index]));
            if (std::abs(mouse.x - markerX) <= 7.0f && std::abs(mouse.y - markerY) <= 8.0f) {
                selectedTimelineKeyframe_ = static_cast<int>(index);
                timelineDraggingKeyframe_ = true;
                PushUndoState(scene_);
                break;
            }
        }
        if (selectedTimelineKeyframe_ < 0) {
            timelineDraggingPlayhead_ = true;
            const double normalized = Clamp((mouse.x - trackLeft) / std::max(1.0f, usableWidth), 0.0, 1.0);
            SetTimelineFrame(static_cast<double>(startFrame) + normalized * static_cast<double>(spanFrames), false);
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        timelineDraggingPlayhead_ = false;
        timelineDraggingKeyframe_ = false;
    }
    if (timelineDraggingPlayhead_) {
        const double normalized = Clamp((ImGui::GetIO().MousePos.x - trackLeft) / std::max(1.0f, usableWidth), 0.0, 1.0);
        SetTimelineFrame(static_cast<double>(startFrame) + normalized * static_cast<double>(spanFrames), false);
    } else if (timelineDraggingKeyframe_ && selectedTimelineKeyframe_ >= 0 && selectedTimelineKeyframe_ < static_cast<int>(scene_.keyframes.size())) {
        const double normalized = Clamp((ImGui::GetIO().MousePos.x - trackLeft) / std::max(1.0f, usableWidth), 0.0, 1.0);
        const int targetFrame = static_cast<int>(std::round(static_cast<double>(startFrame) + normalized * static_cast<double>(spanFrames)));
        bool occupied = false;
        const SceneKeyframe& selectedKeyframe = scene_.keyframes[static_cast<std::size_t>(selectedTimelineKeyframe_)];
        for (std::size_t index = 0; index < scene_.keyframes.size(); ++index) {
            if (static_cast<int>(index) != selectedTimelineKeyframe_
                && scene_.keyframes[index].frame == targetFrame
                && scene_.keyframes[index].ownerType == selectedKeyframe.ownerType
                && scene_.keyframes[index].ownerIndex == selectedKeyframe.ownerIndex) {
                occupied = true;
                break;
            }
        }
        if (!occupied) {
            scene_.keyframes[static_cast<std::size_t>(selectedTimelineKeyframe_)].frame = targetFrame;
            SortKeyframes(scene_);
            selectedTimelineKeyframe_ = FindKeyframeIndex(scene_, targetFrame, selectedKeyframe.ownerType, selectedKeyframe.ownerIndex);
            RefreshTimelinePose();
        }
    }

    for (std::size_t index = 0; index < scene_.keyframes.size(); ++index) {
        const SceneKeyframe& keyframe = scene_.keyframes[index];
        const float markerX = frameToX(static_cast<double>(keyframe.frame));
        const float markerY = markerYForLane(laneIndexForKeyframe(keyframe));
        const float markerSize = static_cast<int>(index) == selectedTimelineKeyframe_ ? 7.0f : 5.5f;
        const ImU32 markerColor = IM_COL32(keyframe.markerColor.r, keyframe.markerColor.g, keyframe.markerColor.b, keyframe.markerColor.a);
        drawList->AddQuadFilled(
            ImVec2(markerX, markerY - markerSize),
            ImVec2(markerX + markerSize, markerY),
            ImVec2(markerX, markerY + markerSize),
            ImVec2(markerX - markerSize, markerY),
            markerColor);
    }

    const float playheadX = frameToX(scene_.timelineFrame);
    drawList->AddLine(ImVec2(playheadX, trackMin.y + topPad), ImVec2(playheadX, trackMax.y - bottomPad), playhead, 2.0f);
    drawList->AddTriangleFilled(
        ImVec2(playheadX - 5.0f, trackMin.y + 8.0f),
        ImVec2(playheadX + 5.0f, trackMin.y + 8.0f),
        ImVec2(playheadX, trackMin.y + 16.0f),
        playhead);
    ImGui::EndChild();

    SyncCurrentKeyframeFromScene();
    ImGui::End();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Preview", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    const std::uint32_t minInteractiveIterations = 10000;
    const std::uint32_t maxInteractiveIterations = 2000000;
    const std::uint32_t minIterations = 20000;
    const std::uint32_t maxIterations = 100000000u;
    const auto setPreviewColumnItemWidth = [&]() {
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    const auto drawPreviewFieldLabel = [&](const char* label) {
        ImGui::TextDisabled("%s", label);
    };
    const auto beginPreviewGrid = [&](const char* id) {
        return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings);
    };

    ImGui::SeparatorText("Iterations");
    if (beginPreviewGrid("##preview_iterations_grid")) {
        ImGui::TableNextColumn();
        drawPreviewFieldLabel("Navigation Iterations");
        setPreviewColumnItemWidth();
        if (SliderScalarWithInput("##preview_navigation_iterations", ImGuiDataType_U32, &interactivePreviewIterations_, &minInteractiveIterations, &maxInteractiveIterations, "%u")
            || ResetValueOnDoubleClick(interactivePreviewIterations_, static_cast<std::uint32_t>(60000))) {
            MarkViewportDirty();
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Used while navigating the viewport.");
        ImGui::PopTextWrapPos();

        ImGui::TableNextColumn();
        drawPreviewFieldLabel("Render Iterations");
        setPreviewColumnItemWidth();
        const Scene beforePreview = scene_;
        const bool previewChanged = SliderScalarWithInput("##preview_render_iterations", ImGuiDataType_U32, &scene_.previewIterations, &minIterations, &maxIterations, "%u")
            || ResetValueOnDoubleClick(scene_.previewIterations, defaultScene.previewIterations);
        if (previewChanged) {
            viewportDirty_ = true;
        }
        CaptureWidgetUndo(beforePreview, previewChanged);

        ImGui::EndTable();
    }

    ImGui::SeparatorText("Viewport");
    if (beginPreviewGrid("##preview_viewport_grid")) {
        ImGui::TableNextColumn();
        drawPreviewFieldLabel("Viewport Background");
        setPreviewColumnItemWidth();
        float backgroundColor[3] = {
            scene_.backgroundColor.r / 255.0f,
            scene_.backgroundColor.g / 255.0f,
            scene_.backgroundColor.b / 255.0f
        };
        const Scene beforeBackground = scene_;
        const bool backgroundChanged = ImGui::ColorEdit3("##preview_background", backgroundColor, ImGuiColorEditFlags_DisplayRGB)
            || ResetColorOnDoubleClick(backgroundColor, defaultScene.backgroundColor);
        if (backgroundChanged) {
            scene_.backgroundColor = ToColor(backgroundColor);
            viewportDirty_ = true;
        }
        CaptureWidgetUndo(beforeBackground, backgroundChanged);

        const Scene beforeDofEnabled = scene_;
        bool dofEnabledChanged = ImGui::Checkbox("Depth of Field", &scene_.depthOfField.enabled);
        dofEnabledChanged = ResetValueOnDoubleClick(scene_.depthOfField.enabled, defaultScene.depthOfField.enabled) || dofEnabledChanged;
        if (dofEnabledChanged) {
            viewportDirty_ = true;
        }
        CaptureWidgetUndo(beforeDofEnabled, dofEnabledChanged);

        ImGui::TableNextColumn();
        const Scene beforeDenoiserEnabled = scene_;
        bool denoiserEnabledChanged = ImGui::Checkbox("Denoiser", &scene_.denoiser.enabled);
        denoiserEnabledChanged = ResetValueOnDoubleClick(scene_.denoiser.enabled, defaultScene.denoiser.enabled) || denoiserEnabledChanged;
        if (denoiserEnabledChanged) {
            viewportDirty_ = true;
        }
        CaptureWidgetUndo(beforeDenoiserEnabled, denoiserEnabledChanged);

        ImGui::EndTable();
    }

    if (scene_.denoiser.enabled) {
        const double denoiserStrengthMin = 0.0;
        const double denoiserStrengthMax = 1.0;

        ImGui::SeparatorText("Denoising");
        if (beginPreviewGrid("##preview_denoise_grid")) {
            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Denoiser Strength");
            setPreviewColumnItemWidth();
            Scene beforeDenoise = scene_;
            bool denoiseChanged = SliderScalarWithInput("##preview_denoiser_strength", ImGuiDataType_Double, &scene_.denoiser.strength, &denoiserStrengthMin, &denoiserStrengthMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.denoiser.strength, defaultScene.denoiser.strength);
            if (denoiseChanged) {
                viewportDirty_ = true;
            }
            CaptureWidgetUndo(beforeDenoise, denoiseChanged);

            ImGui::EndTable();
        }
    }

    if (scene_.depthOfField.enabled) {
        const double focusDepthMin = 0.0;
        const double focusDepthMax = 1.0;
        const double focusRangeMin = 0.01;
        const double focusRangeMax = 0.4;
        const double blurStrengthMin = 0.0;
        const double blurStrengthMax = 1.0;

        ImGui::SeparatorText("Depth of Field");
        if (beginPreviewGrid("##preview_dof_grid")) {
            ImGui::TableNextColumn();
            drawPreviewFieldLabel("DOF Focus Depth");
            setPreviewColumnItemWidth();
            Scene beforeDof = scene_;
            bool dofChanged = SliderScalarWithInput("##preview_dof_focus_depth", ImGuiDataType_Double, &scene_.depthOfField.focusDepth, &focusDepthMin, &focusDepthMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.depthOfField.focusDepth, defaultScene.depthOfField.focusDepth);
            if (dofChanged) {
                viewportDirty_ = true;
            }
            CaptureWidgetUndo(beforeDof, dofChanged);

            ImGui::TableNextColumn();
            drawPreviewFieldLabel("DOF Focus Range");
            setPreviewColumnItemWidth();
            beforeDof = scene_;
            dofChanged = SliderScalarWithInput("##preview_dof_focus_range", ImGuiDataType_Double, &scene_.depthOfField.focusRange, &focusRangeMin, &focusRangeMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.depthOfField.focusRange, defaultScene.depthOfField.focusRange);
            if (dofChanged) {
                viewportDirty_ = true;
            }
            CaptureWidgetUndo(beforeDof, dofChanged);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawPreviewFieldLabel("DOF Blur Strength");
            setPreviewColumnItemWidth();
            beforeDof = scene_;
            dofChanged = SliderScalarWithInput("##preview_dof_blur_strength", ImGuiDataType_Double, &scene_.depthOfField.blurStrength, &blurStrengthMin, &blurStrengthMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.depthOfField.blurStrength, defaultScene.depthOfField.blurStrength);
            if (dofChanged) {
                viewportDirty_ = true;
            }
            CaptureWidgetUndo(beforeDof, dofChanged);

            ImGui::EndTable();
        }
    }
    {
        const Scene beforePostProcessEnabled = scene_;
        bool postProcessEnabledChanged = ImGui::Checkbox("Post-Processing", &scene_.postProcess.enabled);
        postProcessEnabledChanged = ResetValueOnDoubleClick(scene_.postProcess.enabled, defaultScene.postProcess.enabled) || postProcessEnabledChanged;
        if (postProcessEnabledChanged) {
            viewportDirty_ = true;
        }
        CaptureWidgetUndo(beforePostProcessEnabled, postProcessEnabledChanged);
    }

    if (scene_.postProcess.enabled) {
        const double bloomIntensityMin = 0.0, bloomIntensityMax = 2.0;
        const double bloomThresholdMin = 0.0, bloomThresholdMax = 2.0;
        const double chromaticMin = 0.0, chromaticMax = 5.0;
        const double vignetteIntensityMin = 0.0, vignetteIntensityMax = 1.5;
        const double vignetteRoundnessMin = 0.0, vignetteRoundnessMax = 1.0;
        const double filmGrainMin = 0.0, filmGrainMax = 1.0;
        const double colorTempMin = 2000.0, colorTempMax = 12000.0;
        const double saturationMin = -1.0, saturationMax = 1.0;

        ImGui::SeparatorText("Post-Processing");
        if (beginPreviewGrid("##preview_postprocess_grid")) {
            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Bloom Intensity");
            setPreviewColumnItemWidth();
            Scene beforePP = scene_;
            bool ppChanged = SliderScalarWithInput("##pp_bloom_intensity", ImGuiDataType_Double, &scene_.postProcess.bloomIntensity, &bloomIntensityMin, &bloomIntensityMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.postProcess.bloomIntensity, defaultScene.postProcess.bloomIntensity);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Bloom Threshold");
            setPreviewColumnItemWidth();
            beforePP = scene_;
            ppChanged = SliderScalarWithInput("##pp_bloom_threshold", ImGuiDataType_Double, &scene_.postProcess.bloomThreshold, &bloomThresholdMin, &bloomThresholdMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.postProcess.bloomThreshold, defaultScene.postProcess.bloomThreshold);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Chromatic Aberration");
            setPreviewColumnItemWidth();
            beforePP = scene_;
            ppChanged = SliderScalarWithInput("##pp_chromatic", ImGuiDataType_Double, &scene_.postProcess.chromaticAberration, &chromaticMin, &chromaticMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.postProcess.chromaticAberration, defaultScene.postProcess.chromaticAberration);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Vignette");
            setPreviewColumnItemWidth();
            beforePP = scene_;
            ppChanged = SliderScalarWithInput("##pp_vignette", ImGuiDataType_Double, &scene_.postProcess.vignetteIntensity, &vignetteIntensityMin, &vignetteIntensityMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.postProcess.vignetteIntensity, defaultScene.postProcess.vignetteIntensity);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Vignette Roundness");
            setPreviewColumnItemWidth();
            beforePP = scene_;
            ppChanged = SliderScalarWithInput("##pp_vignette_round", ImGuiDataType_Double, &scene_.postProcess.vignetteRoundness, &vignetteRoundnessMin, &vignetteRoundnessMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.postProcess.vignetteRoundness, defaultScene.postProcess.vignetteRoundness);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Film Grain");
            setPreviewColumnItemWidth();
            beforePP = scene_;
            ppChanged = SliderScalarWithInput("##pp_film_grain", ImGuiDataType_Double, &scene_.postProcess.filmGrain, &filmGrainMin, &filmGrainMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.postProcess.filmGrain, defaultScene.postProcess.filmGrain);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Color Temperature");
            setPreviewColumnItemWidth();
            beforePP = scene_;
            ppChanged = SliderScalarWithInput("##pp_color_temp", ImGuiDataType_Double, &scene_.postProcess.colorTemperature, &colorTempMin, &colorTempMax, "%.0f K")
                || ResetValueOnDoubleClick(scene_.postProcess.colorTemperature, defaultScene.postProcess.colorTemperature);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::TableNextColumn();
            drawPreviewFieldLabel("Saturation Boost");
            setPreviewColumnItemWidth();
            beforePP = scene_;
            ppChanged = SliderScalarWithInput("##pp_saturation", ImGuiDataType_Double, &scene_.postProcess.saturationBoost, &saturationMin, &saturationMax, "%.2f")
                || ResetValueOnDoubleClick(scene_.postProcess.saturationBoost, defaultScene.postProcess.saturationBoost);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            drawPreviewFieldLabel("ACES Tone Mapping");
            beforePP = scene_;
            ppChanged = ImGui::Checkbox("##pp_aces", &scene_.postProcess.acesToneMap)
                || ResetValueOnDoubleClick(scene_.postProcess.acesToneMap, defaultScene.postProcess.acesToneMap);
            if (ppChanged) viewportDirty_ = true;
            CaptureWidgetUndo(beforePP, ppChanged);

            ImGui::EndTable();
        }
    }

    const int previewWidth = std::max(1, uploadedViewportWidth_);
    const int previewHeight = std::max(1, uploadedViewportHeight_);
    const std::uint32_t targetPreviewIterations =
        (interactivePreview_ && adaptiveInteractivePreview_)
        ? std::min(scene_.previewIterations, interactivePreviewIterations_)
        : scene_.previewIterations;
    const char* previewBackend = "Preview";
    switch (displayedPreviewBackend_) {
    case PreviewBackend::CpuFlame:
        previewBackend = "CPU Flame";
        break;
    case PreviewBackend::CpuPath:
        previewBackend = "CPU Path";
        break;
    case PreviewBackend::CpuHybrid:
        previewBackend = "CPU Hybrid";
        break;
    case PreviewBackend::GpuFlame:
        previewBackend = "GPU Flame";
        break;
    case PreviewBackend::GpuDof:
        previewBackend = "GPU DOF";
        break;
    case PreviewBackend::GpuPath:
        previewBackend = "GPU Path";
        break;
    case PreviewBackend::GpuHybrid:
        previewBackend = "GPU Flame + GPU Path";
        break;
    case PreviewBackend::GpuDenoised:
        previewBackend = "GPU Denoised";
        break;
    }
    ImGui::TextDisabled(
        "Preview %dx%d | Iter %u/%u | %s | FPS %.1f",
        previewWidth,
        previewHeight,
        displayedPreviewIterations_,
        targetPreviewIterations,
        previewBackend,
        fpsSmoothed_);
    ImGui::End();
}

void AppWindow::DrawCameraPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();

    const Scene defaultScene = CreateDefaultScene();
    const CameraState defaultCamera = defaultScene.camera;
    const double frameMin = 0.1;
    const double frameMax = 64.0;
    const double distanceMin = 0.05;
    const double zoomMin = 0.2;
    const double panMin = -4096.0;
    const double panMax = 4096.0;
    const double yawMin = -3.14;
    const double yawMax = 3.14;
    const double pitchMin = -1.45;
    const double pitchMax = 1.45;
    const auto captureCameraEdit = [&](const Scene& before, const bool changed) {
        if (changed) {
            AutoKeyCurrentFrame();
            viewportDirty_ = true;
        }
        CaptureWidgetUndo(before, changed);
    };
    const auto beginFieldGrid = [&](const char* id) {
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const float labelWidth = std::clamp(contentWidth * 0.10f, 72.0f, 92.0f);
        const float inputWidth = std::max(120.0f, (contentWidth - labelWidth * 2.0f) * 0.5f);
        ImGui::Columns(4, id, false);
        ImGui::SetColumnWidth(0, labelWidth);
        ImGui::SetColumnWidth(1, inputWidth);
        ImGui::SetColumnWidth(2, labelWidth);
    };
    const auto endFieldGrid = [&]() {
        ImGui::Columns(1);
    };
    const auto fieldLabel = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::NextColumn();
    };
    const auto fieldWidget = [&](const auto& drawWidget) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed = drawWidget();
        ImGui::NextColumn();
        return changed;
    };

    ImGui::BeginChild("CameraPanelScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_None);

    ImGui::SeparatorText("Framing");
    beginFieldGrid("##camera_framing_grid");
    const int currentAspectPreset = FindCameraAspectPresetIndex(scene_.camera);
    const char* aspectPreview = currentAspectPreset >= 0
        ? kCameraAspectPresets[static_cast<std::size_t>(currentAspectPreset)].label
        : "Custom";
    Scene beforeAspectPreset = scene_;
    bool aspectPresetChanged = false;
    fieldLabel("Aspect");
    fieldWidget([&]() {
        if (BeginComboWithMaterialArrow("##camera_aspect_ratio", aspectPreview)) {
            for (std::size_t index = 0; index < kCameraAspectPresets.size(); ++index) {
                const bool selected = static_cast<int>(index) == currentAspectPreset;
                if (ImGui::Selectable(kCameraAspectPresets[index].label, selected)) {
                    ApplyCameraAspectPreset(scene_.camera, static_cast<int>(index));
                    aspectPresetChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return aspectPresetChanged;
    });
    fieldLabel("Gate");
    fieldWidget([&]() {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", CameraAspectSummary(scene_.camera).c_str());
        return false;
    });
    captureCameraEdit(beforeAspectPreset, aspectPresetChanged);

    Scene beforeFraming = scene_;
    fieldLabel("Width");
    bool framingChanged = fieldWidget([&]() {
        bool changed = DragScalarWithInput("##camera_frame_width", ImGuiDataType_Double, &scene_.camera.frameWidth, 0.05f, &frameMin, &frameMax, "%.2f");
        return ResetValueOnDoubleClick(scene_.camera.frameWidth, defaultCamera.frameWidth) || changed;
    });
    fieldLabel("Height");
    framingChanged = fieldWidget([&]() {
        bool changed = DragScalarWithInput("##camera_frame_height", ImGuiDataType_Double, &scene_.camera.frameHeight, 0.05f, &frameMin, &frameMax, "%.2f");
        return ResetValueOnDoubleClick(scene_.camera.frameHeight, defaultCamera.frameHeight) || changed;
    }) || framingChanged;
    scene_.camera.frameWidth = std::clamp(scene_.camera.frameWidth, frameMin, frameMax);
    scene_.camera.frameHeight = std::clamp(scene_.camera.frameHeight, frameMin, frameMax);
    captureCameraEdit(beforeFraming, framingChanged);
    endFieldGrid();
    ImGui::TextDisabled("Viewport matte and export both follow this gate.");

    ImGui::SeparatorText("Transform");
    beginFieldGrid("##camera_transform_grid");
    Scene beforeTransform = scene_;
    fieldLabel("Distance");
    bool transformChanged = fieldWidget([&]() {
        bool changed = DragScalarWithInput("##camera_distance", ImGuiDataType_Double, &scene_.camera.distance, 0.05f, &distanceMin, nullptr, "%.2f");
        return ResetValueOnDoubleClick(scene_.camera.distance, defaultCamera.distance) || changed;
    });
    fieldLabel("Zoom");
    transformChanged = fieldWidget([&]() {
        bool changed = DragScalarWithInput("##camera_zoom2d", ImGuiDataType_Double, &scene_.camera.zoom2D, 0.02f, &zoomMin, nullptr, "%.2f");
        return ResetValueOnDoubleClick(scene_.camera.zoom2D, defaultCamera.zoom2D) || changed;
    }) || transformChanged;
    fieldLabel("Pan X");
    transformChanged = fieldWidget([&]() {
        bool changed = DragScalarWithInput("##camera_panx", ImGuiDataType_Double, &scene_.camera.panX, 1.0f, &panMin, &panMax, "%.0f px");
        return ResetValueOnDoubleClick(scene_.camera.panX, defaultCamera.panX) || changed;
    }) || transformChanged;
    fieldLabel("Pan Y");
    transformChanged = fieldWidget([&]() {
        bool changed = DragScalarWithInput("##camera_pany", ImGuiDataType_Double, &scene_.camera.panY, 1.0f, &panMin, &panMax, "%.0f px");
        return ResetValueOnDoubleClick(scene_.camera.panY, defaultCamera.panY) || changed;
    }) || transformChanged;
    fieldLabel("Yaw");
    transformChanged = fieldWidget([&]() {
        bool changed = SliderScalarWithInput("##camera_yaw", ImGuiDataType_Double, &scene_.camera.yaw, &yawMin, &yawMax, "%.2f");
        return ResetValueOnDoubleClick(scene_.camera.yaw, defaultCamera.yaw) || changed;
    }) || transformChanged;
    fieldLabel("Pitch");
    transformChanged = fieldWidget([&]() {
        bool changed = SliderScalarWithInput("##camera_pitch", ImGuiDataType_Double, &scene_.camera.pitch, &pitchMin, &pitchMax, "%.2f");
        return ResetValueOnDoubleClick(scene_.camera.pitch, defaultCamera.pitch) || changed;
    }) || transformChanged;
    captureCameraEdit(beforeTransform, transformChanged);
    endFieldGrid();

    ImGui::Spacing();
    if (DrawActionButton("##camera_panel_reset_camera", "Reset Camera", IconGlyph::ResetCamera, ActionTone::Accent, false, true, 150.0f)) {
        PushUndoState(scene_);
        scene_.camera = CameraState {};
        AutoKeyCurrentFrame();
        viewportDirty_ = true;
    }
    ImGui::SameLine();
    if (DrawActionButton("##camera_panel_export", "Open Export", IconGlyph::ExportImage, ActionTone::Accent, exportPanelOpen_, true, 146.0f)) {
        exportPanelOpen_ = !exportPanelOpen_;
        if (exportPanelOpen_) {
            OpenExportPanel();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void AppWindow::DrawViewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(10.0f / 255.0f, 10.0f / 255.0f, 13.0f / 255.0f, 1.0f));
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int width = std::max(1, static_cast<int>(available.x));
    const int height = std::max(1, static_cast<int>(available.y));
    const bool gpuPreviewRequested = gpuFlamePreviewEnabled_;
    const bool allowViewportRender = !bootstrapUiFramePending_;

    if (allowViewportRender && (gpuPreviewRequested || !viewportSrv_ || uploadedViewportWidth_ != width || uploadedViewportHeight_ != height)) {
        RenderViewportIfNeeded(width, height);
    }

    const bool preferGpuPreview =
        gpuFlamePreviewEnabled_
        && (displayedPreviewBackend_ == PreviewBackend::GpuFlame
            || displayedPreviewBackend_ == PreviewBackend::GpuDof
            || displayedPreviewBackend_ == PreviewBackend::GpuDenoised
            || displayedPreviewBackend_ == PreviewBackend::GpuPath
            || displayedPreviewBackend_ == PreviewBackend::GpuHybrid
            || displayedPreviewBackend_ == PreviewBackend::GpuPostProcessed);

    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    const ImVec2 imageMax(imageMin.x + available.x, imageMin.y + available.y);
    bool hovered = false;

    const auto drawViewportPlaceholder = [&]() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 fillColor = ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
        const ImU32 borderColor = ImGui::GetColorU32(ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
        drawList->AddRectFilled(imageMin, imageMax, fillColor, 6.0f);
        drawList->AddRect(imageMin, imageMax, borderColor, 6.0f, 0, 1.0f);
        const char* label = bootstrapUiFramePending_ ? "Loading UI..." : "Loading preview...";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 textPos(
            imageMin.x + std::max(0.0f, (available.x - textSize.x) * 0.5f),
            imageMin.y + std::max(0.0f, (available.y - textSize.y) * 0.5f));
        drawList->AddText(textPos, ImGui::GetColorU32(ImVec4(0.78f, 0.79f, 0.82f, 1.0f)), label);
    };

    if (preferGpuPreview) {
        if (scene_.mode == SceneMode::Flame
            && displayedPreviewBackend_ == PreviewBackend::GpuFlame
            && gpuFlameRenderer_.ShaderResourceView() != nullptr) {
            ImGui::Image(reinterpret_cast<ImTextureID>(gpuFlameRenderer_.ShaderResourceView()), available);
            hovered = ImGui::IsItemHovered();
        } else if (displayedPreviewBackend_ == PreviewBackend::GpuPostProcessed
            && gpuPostProcess_.ShaderResourceView() != nullptr) {
            ImGui::Image(reinterpret_cast<ImTextureID>(gpuPostProcess_.ShaderResourceView()), available);
            hovered = ImGui::IsItemHovered();
        } else if (displayedPreviewBackend_ == PreviewBackend::GpuDof
            && gpuDofRenderer_.ShaderResourceView() != nullptr) {
            ImGui::Image(reinterpret_cast<ImTextureID>(gpuDofRenderer_.ShaderResourceView()), available);
            hovered = ImGui::IsItemHovered();
        } else if (displayedPreviewBackend_ == PreviewBackend::GpuDenoised
            && gpuDenoiser_.ShaderResourceView() != nullptr) {
            ImGui::Image(reinterpret_cast<ImTextureID>(gpuDenoiser_.ShaderResourceView()), available);
            hovered = ImGui::IsItemHovered();
        } else if (scene_.mode == SceneMode::Path
            && displayedPreviewBackend_ == PreviewBackend::GpuPath
            && gpuPathRenderer_.ShaderResourceView() != nullptr) {
            ImGui::Image(reinterpret_cast<ImTextureID>(gpuPathRenderer_.ShaderResourceView()), available);
            hovered = ImGui::IsItemHovered();
        } else if ((scene_.mode == SceneMode::Flame || scene_.mode == SceneMode::Hybrid)
            && displayedPreviewBackend_ == PreviewBackend::GpuHybrid
            && gpuFlameRenderer_.ShaderResourceView() != nullptr) {
            const bool hasGridLayer = scene_.gridVisible && gpuGridRenderer_.ShaderResourceView() != nullptr;
            if (hasGridLayer) {
                ImGui::Image(reinterpret_cast<ImTextureID>(gpuGridRenderer_.ShaderResourceView()), available);
            } else {
                ImGui::Image(reinterpret_cast<ImTextureID>(gpuFlameRenderer_.ShaderResourceView()), available);
            }
            hovered = ImGui::IsItemHovered();
            if (hasGridLayer) {
                ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(gpuFlameRenderer_.ShaderResourceView()), imageMin, imageMax);
            }
            if (scene_.mode == SceneMode::Hybrid && gpuPathRenderer_.ShaderResourceView() != nullptr) {
                ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(gpuPathRenderer_.ShaderResourceView()), imageMin, imageMax);
            }
            hovered = hovered || ImGui::IsMouseHoveringRect(imageMin, imageMax);
        } else if (viewportSrv_) {
            ImGui::Image(reinterpret_cast<ImTextureID>(viewportSrv_.Get()), available);
            hovered = ImGui::IsItemHovered();
        } else {
            drawViewportPlaceholder();
        }
    } else if (viewportSrv_) {
        ImGui::Image(reinterpret_cast<ImTextureID>(viewportSrv_.Get()), available);
        hovered = ImGui::IsItemHovered();
    } else {
        drawViewportPlaceholder();
    }

    if (available.x > 1.0f && available.y > 1.0f) {
        const ImRect imageRect(imageMin, imageMax);
        const ImRect cameraRect = CameraFrameRectInBounds(scene_.camera, imageRect);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 matteColor = ImGui::GetColorU32(ImVec4(0.02f, 0.02f, 0.03f, 0.26f));
        const ImU32 outlineColor = ImGui::GetColorU32(ImVec4(0.86f, 0.87f, 0.90f, 0.42f));
        if (cameraRect.Min.x > imageRect.Min.x + 1.0f) {
            drawList->AddRectFilled(imageRect.Min, ImVec2(cameraRect.Min.x, imageRect.Max.y), matteColor);
            drawList->AddRectFilled(ImVec2(cameraRect.Max.x, imageRect.Min.y), imageRect.Max, matteColor);
        }
        if (cameraRect.Min.y > imageRect.Min.y + 1.0f) {
            drawList->AddRectFilled(ImVec2(cameraRect.Min.x, imageRect.Min.y), ImVec2(cameraRect.Max.x, cameraRect.Min.y), matteColor);
            drawList->AddRectFilled(ImVec2(cameraRect.Min.x, cameraRect.Max.y), ImVec2(cameraRect.Max.x, imageRect.Max.y), matteColor);
        }
        drawList->AddRect(cameraRect.Min, cameraRect.Max, outlineColor, 0.0f, 0, 1.0f);
    }

    if (gpuFlamePreviewEnabled_ && available.x > 1.0f && available.y > 1.0f) {
        const std::uint32_t targetIterations = scene_.previewIterations;
        const std::uint32_t accumulated = displayedPreviewIterations_;
        if (targetIterations > 0 && accumulated < targetIterations) {
            const float progress = static_cast<float>(accumulated) / static_cast<float>(targetIterations);
            constexpr float barHeight = 2.0f;
            const ImVec2 barMin(imageMin.x, imageMax.y - barHeight);
            const ImVec2 barMax(imageMin.x + available.x * progress, imageMax.y);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(barMin, barMax, ImGui::GetColorU32(ImVec4(0.45f, 0.58f, 0.92f, 0.55f)));
        }
    }

    HandleViewportInteraction(allowViewportRender && hovered);

    if (allowViewportRender && viewportDirty_) {
        RenderViewportIfNeeded(width, height);
    }

    ImGui::End();
}

void AppWindow::DrawStatusBar() {
    if (ImGuiWindow* viewportWindow = ImGui::FindWindowByName("Viewport")) {
        const ImVec2 margin(16.0f, 12.0f);
        const ImVec2 statusAnchor(viewportWindow->InnerRect.Max.x - margin.x, viewportWindow->InnerRect.Min.y + margin.y);
        const float maxWidth = std::max(1.0f, viewportWindow->InnerRect.GetWidth() - margin.x * 2.0f);
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(statusAnchor, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(maxWidth, FLT_MAX));
    }

    ImGui::Begin(
        "Status",
        nullptr,
        ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted(WideToUtf8(statusText_).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| Mode %s", ToString(scene_.mode).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| Preview %dx%d", std::max(1, uploadedViewportWidth_), std::max(1, uploadedViewportHeight_));
    ImGui::End();
}

}  // namespace radiary
