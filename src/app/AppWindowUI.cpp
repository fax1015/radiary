#include "app/AppWindow.h"
#include "app/AppWidgets.h"
#include "app/CameraUtils.h"
#include "app/ExportUtils.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <random>
#include <sstream>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#ifndef RADIARY_APP_VERSION
#define RADIARY_APP_VERSION "dev"
#endif

using namespace radiary;

namespace {

constexpr float kOverlayPanelMarginX = 12.0f;
constexpr float kOverlayPanelMarginY = 12.0f;
constexpr float kStatusPanelPaddingX = 12.0f;
constexpr float kStatusPanelPaddingY = 10.0f;
constexpr float kStatusPanelHeaderPaddingY = 8.0f;
constexpr float kStatusPanelSeparatorHeight = 1.0f;
constexpr float kStatusPanelMinWrapWidth = 240.0f;
constexpr float kToolbarWindowPaddingX = 8.0f;
constexpr float kToolbarWindowPaddingY = 7.0f;
constexpr float kToolbarFramePaddingX = 9.0f;
constexpr float kToolbarFramePaddingY = 6.0f;
constexpr float kDefaultBottomPanelHeight = 190.0f;
constexpr float kDefaultLeftPanelWidth = 464.0f;
constexpr float kDefaultRightPanelWidth = 520.0f;
constexpr float kWidePreviewGridMinWidth = 620.0f;
constexpr float kWideCameraGridMinWidth = 620.0f;
constexpr float kWideCameraActionsMinWidth = 340.0f;
constexpr float kWideInspectorMinWidth = 600.0f;
constexpr char kAboutPopupId[] = "About Radiary";
constexpr char kRadiaryAppVersion[] = RADIARY_APP_VERSION;

struct VariationCategory {
    const char* name = "";
    std::initializer_list<VariationType> types;
};

const VariationCategory kVariationCategories[] = {
    {"Geometric", {VariationType::Linear, VariationType::Spherical, VariationType::Horseshoe,
                   VariationType::Cylinder, VariationType::Perspective, VariationType::Conic,
                   VariationType::Astroid, VariationType::Lissajous, VariationType::Supershape}},
    {"Trigonometric", {VariationType::Sinusoidal, VariationType::Cosine, VariationType::Tangent,
                       VariationType::Sec, VariationType::Csc, VariationType::Cot, VariationType::Sech}},
    {"Fractal", {VariationType::Julia, VariationType::Spiral, VariationType::Swirl,
                 VariationType::Rings, VariationType::Rings2, VariationType::Fan, VariationType::Fan2,
                 VariationType::Droste, VariationType::Kaleidoscope, VariationType::GoldenSpiral}},
    {"Distortion", {VariationType::Curl, VariationType::Waves, VariationType::Popcorn,
                    VariationType::Bent, VariationType::Fold, VariationType::Split, VariationType::Vortex,
                    VariationType::Interference}},
    {"Organic", {VariationType::Heart, VariationType::Flower, VariationType::Blade, VariationType::Bubble,
                 VariationType::Eyefish, VariationType::Fisheye, VariationType::Handkerchief,
                 VariationType::Ex, VariationType::Blob}},
    {"Complex", {VariationType::Polar, VariationType::Disc, VariationType::Bipolar, VariationType::Wedge,
                 VariationType::Mobius, VariationType::PDJ, VariationType::TwinTrian, VariationType::Checkers,
                 VariationType::Power, VariationType::Exponential, VariationType::Hyperbolic,
                 VariationType::Diamond, VariationType::Ngon, VariationType::Arch, VariationType::Rays,
                 VariationType::Cross, VariationType::Blur}}
};

ImVec4 WithAlpha(const ImVec4& color, const float alpha) {
    return ImVec4(color.x, color.y, color.z, alpha);
}

ImVec4 Mix(const ImVec4& a, const ImVec4& b, const float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
}

std::string TitleCaseFromSnakeCase(std::string value) {
    bool capitalizeNext = true;
    for (char& ch : value) {
        if (ch == '_') {
            ch = ' ';
            capitalizeNext = true;
            continue;
        }
        if (capitalizeNext) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            capitalizeNext = false;
        }
    }
    return value;
}

float SplitRatioForPixels(const float pixels, const float available) {
    if (available <= 1.0f) {
        return 0.5f;
    }
    return std::clamp(pixels / available, 0.05f, 0.95f);
}

void DrawDockspaceBackdrop(const ImGuiViewport* viewport) {
    if (viewport == nullptr) {
        return;
    }

    const UiTheme& theme = GetUiTheme();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = viewport->Pos;
    const ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(theme.appBackgroundTop));
}

bool BeginPropertyGrid(const char* id) {
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const float labelWidth = std::clamp(contentWidth * 0.30f, 196.0f, 248.0f);
    if (!ImGui::BeginTable(
            id,
            2,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_PadOuterX,
            ImVec2(0.0f, 0.0f))) {
        return false;
    }

    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    return true;
}

template <typename DrawWidget>
bool DrawPropertyRow(const char* label, const DrawWidget& drawWidget, const char* detail = nullptr) {
    const UiTheme& theme = GetUiTheme();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.textMuted);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = drawWidget();
    if (detail != nullptr && detail[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        ImGui::TextUnformatted(detail);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    return changed;
}

struct StatusOverlayLayout {
    ImVec2 size {};
    ImVec2 titleSize {};
    ImVec2 bodySize {};
    float textAvailableWidth = 0.0f;
    float headerHeight = 0.0f;
};

StatusOverlayLayout BuildStatusOverlayLayout(
    const std::string& bodyText,
    const float maxPanelWidth,
    const float paddingX,
    const float paddingY,
    const float headerPaddingY,
    const float separatorHeight) {
    StatusOverlayLayout layout;
    layout.titleSize = ImGui::CalcTextSize("Status");
    layout.headerHeight = layout.titleSize.y + headerPaddingY * 2.0f;

    layout.bodySize = ImGui::CalcTextSize(bodyText.c_str(), nullptr, true);
    layout.size.x = std::min(
        maxPanelWidth,
        std::max(
            layout.titleSize.x + paddingX * 2.0f,
            layout.bodySize.x + paddingX * 2.0f));
    layout.textAvailableWidth = std::max(1.0f, layout.size.x - paddingX * 2.0f);
    layout.size.y = layout.headerHeight + separatorHeight + layout.bodySize.y + paddingY * 2.0f;
    return layout;
}

}  // namespace

namespace radiary {

bool AppWindow::DrawAnimatedLoadingIcon(ImDrawList* drawList, const ImVec2& center, const float maxExtent, const std::uint32_t color) const {
    if (drawList == nullptr) {
        return false;
    }
    if (startupLogoSvgStrokes_.empty()) {
        if (appLogoSrv_ == nullptr) {
            return false;
        }
        const float imageSize = std::max(20.0f, maxExtent);
        drawList->AddImage(
            reinterpret_cast<ImTextureID>(appLogoSrv_.Get()),
            ImVec2(center.x - imageSize * 0.5f, center.y - imageSize * 0.5f),
            ImVec2(center.x + imageSize * 0.5f, center.y + imageSize * 0.5f));
        return true;
    }

    const float boundsWidth = std::max(1.0f, startupLogoSvgMaxX_ - startupLogoSvgMinX_);
    const float boundsHeight = std::max(1.0f, startupLogoSvgMaxY_ - startupLogoSvgMinY_);
    const float scale = std::min(maxExtent / boundsWidth, maxExtent / boundsHeight) * 0.9f;
    const float drawWidth = boundsWidth * scale;
    const float drawHeight = boundsHeight * scale;
    const float left = center.x - drawWidth * 0.5f;
    const float top = center.y - drawHeight * 0.5f;
    const float strokeWidth = std::clamp(maxExtent / 24.0f, 2.0f, 4.2f);
    const float dashLength = std::max(3.0f, maxExtent * 0.07f);
    const float dashGap = std::max(2.0f, maxExtent * 0.035f);
    const float patternLength = dashLength + dashGap;
    const float dashOffset = std::fmod(static_cast<float>(ImGui::GetTime()) * 35.0f, patternLength);

    for (const StartupLogoStroke& stroke : startupLogoSvgStrokes_) {
        if (stroke.points.size() < 2) {
            continue;
        }

        float patternPos = dashOffset;
        for (std::size_t index = 1; index < stroke.points.size(); ++index) {
            const StartupLogoPoint& a = stroke.points[index - 1];
            const StartupLogoPoint& b = stroke.points[index];
            const ImVec2 segmentStart(
                left + (a.x - startupLogoSvgMinX_) * scale,
                top + (a.y - startupLogoSvgMinY_) * scale);
            const ImVec2 segmentEnd(
                left + (b.x - startupLogoSvgMinX_) * scale,
                top + (b.y - startupLogoSvgMinY_) * scale);
            const float dx = segmentEnd.x - segmentStart.x;
            const float dy = segmentEnd.y - segmentStart.y;
            const float segmentLength = std::sqrt(dx * dx + dy * dy);
            if (segmentLength <= 0.001f) {
                continue;
            }

            float consumed = 0.0f;
            while (consumed < segmentLength) {
                const bool drawDash = patternPos < dashLength;
                const float target = drawDash ? dashLength : patternLength;
                const float step = std::min(segmentLength - consumed, target - patternPos);
                if (drawDash && step > 0.001f) {
                    const float startT = consumed / segmentLength;
                    const float endT = (consumed + step) / segmentLength;
                    const ImVec2 dashStart(segmentStart.x + dx * startT, segmentStart.y + dy * startT);
                    const ImVec2 dashEnd(segmentStart.x + dx * endT, segmentStart.y + dy * endT);
                    drawList->AddLine(dashStart, dashEnd, color, strokeWidth);
                }
                consumed += step;
                patternPos += step;
                if (patternPos >= patternLength - 0.001f) {
                    patternPos = 0.0f;
                }
            }
        }
    }

    return true;
}

void AppWindow::DrawBlockingOverlay() const {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }

    const UiTheme& theme = GetUiTheme();

    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.overlayScrim);
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

    const float panelWidth = std::clamp(viewport->Size.x - ScaleUi(48.0f), ScaleUi(320.0f), ScaleUi(440.0f));
    const float iconExtent = ScaleUi(76.0f);
    const float iconGap = ScaleUi(22.0f);
    const float panelEstimatedHeight = ScaleUi(184.0f);
    const float groupHeight = iconExtent + iconGap + panelEstimatedHeight;
    const float groupTop = viewport->Pos.y + std::max(ScaleUi(24.0f), (viewport->Size.y - groupHeight) * 0.5f);
    const ImVec2 iconCenter(
        viewport->Pos.x + viewport->Size.x * 0.5f,
        groupTop + iconExtent * 0.5f);
    DrawAnimatedLoadingIcon(
        ImGui::GetWindowDrawList(),
        iconCenter,
        iconExtent,
        ImGui::GetColorU32(ImVec4(115.0f / 255.0f, 148.0f / 255.0f, 235.0f / 255.0f, 1.0f)));

    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, iconCenter.y + iconExtent * 0.5f + iconGap),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(panelWidth, 0.0f),
        ImVec2(panelWidth, std::max(ScaleUi(160.0f), viewport->Size.y - ScaleUi(48.0f))));
    PushFloatingPanelStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ScaleUi(18.0f), ScaleUi(16.0f)));
    ImGui::Begin(
        "##BlockingOverlayPanel",
        nullptr,
        ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    PopFloatingPanelStyle();

    const std::string title = WideToUtf8(exportProgressTitle_.empty() ? L"Exporting" : exportProgressTitle_);
    const std::string detail = WideToUtf8(exportProgressDetail_);
    const std::string eta = WideToUtf8(exportProgressEta_);
    ImGui::TextUnformatted(title.c_str());
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme.accent);
    ImGui::ProgressBar(std::clamp(exportProgress_, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f));
    ImGui::PopStyleColor();
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
    DrawDockspaceBackdrop(viewport);
    const ImGuiID dockspaceId = ImGui::GetID("RadiaryDockspaceNode");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    if (!defaultLayoutBuilt_ || rebuildLayoutNextFrame_) {
        BuildDefaultLayout();
        rebuildLayoutNextFrame_ = false;
    }
    ImGui::End();
}

void AppWindow::BuildDefaultLayout() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr || viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f) {
        return;
    }

    const float toolbarHeight = ToolbarHeight();
    const float bottomPanelHeight = ScaleUi(kDefaultBottomPanelHeight);
    const float leftPanelWidth = ScaleUi(kDefaultLeftPanelWidth);
    const float rightPanelWidth = ScaleUi(kDefaultRightPanelWidth);
    const ImGuiID dockspaceId = ImGui::GetID("RadiaryDockspaceNode");
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

    ImGuiID centerId = dockspaceId;
    ImGuiID topId = 0;
    ImGuiID bottomId = 0;
    ImGuiID leftId = 0;
    ImGuiID rightId = 0;
    const float toolbarRatio = SplitRatioForPixels(toolbarHeight, viewport->Size.y);
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Up, toolbarRatio, &topId, &centerId);

    const float remainingHeightAfterToolbar = std::max(1.0f, viewport->Size.y - toolbarHeight);
    const float bottomRatio = SplitRatioForPixels(bottomPanelHeight, remainingHeightAfterToolbar);
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, bottomRatio, &bottomId, &centerId);

    const float leftRatio = SplitRatioForPixels(leftPanelWidth, viewport->Size.x);
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, leftRatio, &leftId, &centerId);

    const float remainingWidthAfterLeft = std::max(1.0f, viewport->Size.x - leftPanelWidth);
    const float rightRatio = SplitRatioForPixels(rightPanelWidth, remainingWidthAfterLeft);
    ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, rightRatio, &rightId, &centerId);

    ImGui::DockBuilderDockWindow("Toolbar", topId);
    ImGui::DockBuilderDockWindow("Layers", leftId);
    ImGui::DockBuilderDockWindow("Keyframes", leftId);
    ImGui::DockBuilderDockWindow("History", leftId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    ImGui::DockBuilderDockWindow("Playback", bottomId);
    ImGui::DockBuilderDockWindow("Camera", rightId);
    ImGui::DockBuilderDockWindow("Preview", rightId);
    ImGui::DockBuilderDockWindow("Effects", rightId);
    ImGui::DockBuilderDockWindow("Histogram", rightId);
    ImGui::DockBuilderDockWindow("Viewport", centerId);

    if (ImGuiDockNode* topNode = ImGui::DockBuilderGetNode(topId)) {
        topNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        topNode->LocalFlags |= ImGuiDockNodeFlags_NoResize;
        topNode->SizeRef.y = toolbarHeight;
    }
    ImGui::DockBuilderFinish(dockspaceId);
    defaultLayoutBuilt_ = true;
}

float AppWindow::ToolbarHeight() const {
    const float frameHeight = ImGui::GetFontSize() + ScaleUi(kToolbarFramePaddingY * 2.0f);
    const float windowPaddingHeight = ScaleUi(kToolbarWindowPaddingY * 2.0f);
    return std::ceil(frameHeight + windowPaddingHeight);
}

void AppWindow::OpenAllDockPanels() {
    layersPanelOpen_ = true;
    keyframeListPanelOpen_ = true;
    historyPanelOpen_ = true;
    inspectorPanelOpen_ = true;
    playbackPanelOpen_ = true;
    previewPanelOpen_ = true;
    effectsPanelOpen_ = true;
    histogramPanelOpen_ = true;
    cameraPanelOpen_ = true;
    viewportPanelOpen_ = true;
}

void AppWindow::DrawDockPanelVisibilityMenuItems() {
    ImGui::MenuItem("Layers", nullptr, &layersPanelOpen_);
    ImGui::MenuItem("Keyframes", nullptr, &keyframeListPanelOpen_);
    ImGui::MenuItem("History", nullptr, &historyPanelOpen_);
    ImGui::MenuItem("Inspector", nullptr, &inspectorPanelOpen_);
    ImGui::MenuItem("Playback", nullptr, &playbackPanelOpen_);
    ImGui::MenuItem("Preview", nullptr, &previewPanelOpen_);
    ImGui::MenuItem("Effects", nullptr, &effectsPanelOpen_);
    ImGui::MenuItem("Histogram", nullptr, &histogramPanelOpen_);
    ImGui::MenuItem("Camera", nullptr, &cameraPanelOpen_);
    ImGui::MenuItem("Viewport", nullptr, &viewportPanelOpen_);
}

void AppWindow::DrawDockPanelTabContextMenu(const char* panelName, bool& panelOpen) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window == nullptr) {
        return;
    }

    const bool hasDockTab = window->DockIsActive
        && window->DC.DockTabItemRect.GetWidth() > 0.0f
        && window->DC.DockTabItemRect.GetHeight() > 0.0f;
    if (!hasDockTab) {
        return;
    }

    const ImRect tabRect = window->DC.DockTabItemRect;
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)
        && tabRect.Contains(io.MouseClickedPos[ImGuiMouseButton_Right])
        && tabRect.Contains(io.MousePos)) {
        ImGui::OpenPopup("##panel_tab_context_menu");
    }
    if (!ImGui::BeginPopup("##panel_tab_context_menu")) {
        return;
    }

    const std::string hideLabel = std::string("Hide ") + panelName;
    if (ImGui::MenuItem(hideLabel.c_str())) {
        panelOpen = false;
    }
    const bool anyHiddenPanels = !layersPanelOpen_
        || !keyframeListPanelOpen_
        || !historyPanelOpen_
        || !inspectorPanelOpen_
        || !playbackPanelOpen_
        || !previewPanelOpen_
        || !effectsPanelOpen_
        || !histogramPanelOpen_
        || !cameraPanelOpen_
        || !viewportPanelOpen_;
    if (ImGui::MenuItem("Show All Panels", nullptr, false, anyHiddenPanels)) {
        OpenAllDockPanels();
    }

    ImGui::EndPopup();
}

void AppWindow::DrawToolbar() {
    const UiTheme& theme = GetUiTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ScaleUi(kToolbarWindowPaddingX), ScaleUi(kToolbarWindowPaddingY)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ScaleUi(kToolbarFramePaddingX), ScaleUi(kToolbarFramePaddingY)));
    ImGui::Begin(
        "Toolbar",
        nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
    ImDrawList* toolbarDrawList = ImGui::GetWindowDrawList();
    const ImVec2 toolbarMin = ImGui::GetWindowPos();
    const ImVec2 toolbarMax(toolbarMin.x + ImGui::GetWindowSize().x, toolbarMin.y + ImGui::GetWindowSize().y);
    toolbarDrawList->AddRectFilled(toolbarMin, toolbarMax, ImGui::GetColorU32(WithAlpha(theme.panelBackgroundAlt, 0.92f)), 0.0f);
    toolbarDrawList->AddLine(
        ImVec2(toolbarMin.x, toolbarMax.y - 0.5f),
        ImVec2(toolbarMax.x, toolbarMax.y - 0.5f),
        ImGui::GetColorU32(theme.borderSubtle),
        1.0f);

    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    const float settingsGap = ScaleUi(8.0f);
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

    const float brandLogoSize = ImGui::GetFrameHeight();
    ImGui::BeginGroup();
    if (appLogoSrv_ != nullptr) {
        const float brandRowStartY = ImGui::GetCursorPosY();
        ImGui::Image(reinterpret_cast<ImTextureID>(appLogoSrv_.Get()), ImVec2(brandLogoSize, brandLogoSize));
        ImGui::SameLine(0.0f, ScaleUi(10.0f));
        ImGui::SetCursorPosY(brandRowStartY + std::max(0.0f, (brandLogoSize - ImGui::GetTextLineHeight()) * 0.5f));
    } else {
        ImGui::AlignTextToFramePadding();
    }
    if (brandFont_ != nullptr) {
        ImGui::PushFont(brandFont_);
    }
    ImGui::TextUnformatted("Radiary");
    if (brandFont_ != nullptr) {
        ImGui::PopFont();
    }
    ImGui::EndGroup();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        aboutPopupOpenRequested_ = true;
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
    DrawSectionChip("FILE", Mix(theme.accentSurface, theme.accent, 0.30f));
    ImGui::SameLine(0.0f, ScaleUi(8.0f));

    if (DrawActionButton("##new_scene", "New", IconGlyph::NewScene, ActionTone::Slate)) {
        PushUndoState(scene_, "New Scene");
        Scene newScene;
        newScene.name = "Untitled";
        newScene.animatePath = false;
        ApplyUserSceneDefaults(newScene);
        ResetScene(std::move(newScene));
        currentScenePath_.clear();
        UpdateWindowTitle();
        statusText_ = L"New scene";
    }
    ImGui::SameLine();
    if (DrawActionButton("##open_scene", "Open", IconGlyph::OpenScene, ActionTone::Slate)) {
        Scene beforeLoad = scene_;
        if (LoadSceneFromDialog()) {
            PushUndoState(beforeLoad, "Open Scene");
            sceneDirty_ = false;
            sceneModifiedSinceAutoSave_ = false;
            lastAutoSave_ = std::chrono::steady_clock::now();
            UpdateWindowTitle();
        }
    }
    ImGui::SameLine();
    if (DrawActionButton("##save_scene", "Save", IconGlyph::SaveScene, ActionTone::Accent)) {
        SaveSceneToDialog(currentScenePath_.empty());
    }
    ImGui::SameLine();
    if (DrawActionButton("##save_scene_as", "Save As", IconGlyph::SaveSceneAs, ActionTone::Accent, false, true)) {
        SaveSceneToDialog(true);
    }
    ImGui::SameLine();
    if (DrawActionButton("##export_scene", "Export", IconGlyph::ExportImage, ActionTone::Accent, exportPanelOpen_, true)) {
        exportPanelOpen_ = !exportPanelOpen_;
        if (exportPanelOpen_) {
            OpenExportPanel();
        }
    }
    exportButtonAnchorX_ = ImGui::GetItemRectMin().x;
    exportButtonAnchorY_ = ImGui::GetItemRectMax().y + ScaleUi(kOverlayPanelMarginY);

    drawDivider();
    DrawSectionChip("PLAY", Mix(theme.accentSurface, theme.accentHover, 0.42f));
    ImGui::SameLine(0.0f, 8.0f);
    if (DrawActionButton("##randomize_scene", "Randomize", IconGlyph::Randomize, ActionTone::Accent)) {
        PushUndoState(scene_, "Randomize Scene");
        Scene randomizedScene = CreateRandomScene(static_cast<std::uint32_t>(GetTickCount64()));
        randomizedScene.gridVisible = scene_.gridVisible;
        ResetScene(std::move(randomizedScene));
        statusText_ = L"Random scene generated";
    }
    ImGui::SameLine();
    if (DrawActionButton("##toggle_playback", scene_.animatePath ? "Pause" : "Play", scene_.animatePath ? IconGlyph::Pause : IconGlyph::Play, ActionTone::Accent, scene_.animatePath)) {
        PushUndoState(scene_, scene_.animatePath ? "Pause Playback" : "Start Playback");
        scene_.animatePath = !scene_.animatePath;
        MarkViewportDirty();
    }

    drawDivider();
    DrawSectionChip("VIEW", Mix(theme.panelBackgroundAlt, theme.textMuted, 0.18f));
    ImGui::SameLine(0.0f, 8.0f);
    if (DrawActionButton("##toggle_grid", "Grid", IconGlyph::Grid, ActionTone::Accent, scene_.gridVisible)) {
        PushUndoState(scene_, scene_.gridVisible ? "Hide Grid" : "Show Grid");
        scene_.gridVisible = !scene_.gridVisible;
        MarkViewportDirty();
    }
    ImGui::SameLine();

    const Scene beforeMode = scene_;
    int modeIndex = static_cast<int>(scene_.mode);
    const char* modes[] = {"Flame", "Path", "Hybrid"};
    ImGui::SetNextItemWidth(110.0f);
    const bool modeChanged = ComboWithMaterialArrow("##mode", &modeIndex, modes, IM_ARRAYSIZE(modes));
    if (modeChanged) {
        scene_.mode = static_cast<SceneMode>(modeIndex);
        MarkViewportDirty(PreviewResetReason::ModeChanged);
    }
    CaptureWidgetUndo(beforeMode, modeChanged);

    drawDivider();
    DrawSectionChip("PRESET", Mix(theme.accentSurface, theme.accent, 0.36f));
    ImGui::SameLine(0.0f, 8.0f);
    const bool canStepPresets = presetLibrary_.Count() > 1;
    if (DrawActionButton("##preset_prev", "", IconGlyph::ChevronLeft, ActionTone::Slate, false, canStepPresets, 0.0f, "Previous preset")) {
        PushUndoState(scene_);
        const std::size_t nextIndex = presetIndex_ == 0 ? presetLibrary_.Count() - 1 : presetIndex_ - 1;
        LoadPreset(nextIndex);
    }
    ImGui::SameLine(0.0f, ScaleUi(6.0f));
    const ImGuiStyle& style = ImGui::GetStyle();
    const float presetArrowWidth = ImGui::GetFrameHeight();
    float presetComboWidth = ScaleUi(170.0f);
    float presetPopupWidth = ScaleUi(170.0f);
    const float presetPopupMaxHeight = ScaleUi(320.0f);
    if (presetLibrary_.Count() > 0) {
        for (std::size_t index = 0; index < presetLibrary_.Count(); ++index) {
            const float textWidth = ImGui::CalcTextSize(presetLibrary_.NameAt(index).c_str()).x;
            presetComboWidth = std::max(presetComboWidth, textWidth + presetArrowWidth + style.FramePadding.x * 2.0f + ScaleUi(18.0f));
            presetPopupWidth = std::max(presetPopupWidth, textWidth + style.WindowPadding.x * 2.0f + style.FramePadding.x * 2.0f + ScaleUi(30.0f));
        }
    }
    ImGui::SetNextItemWidth(presetComboWidth);
    ImGui::SetNextWindowSizeConstraints(ImVec2(presetPopupWidth, 0.0f), ImVec2(presetPopupWidth, presetPopupMaxHeight));
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
    ImGui::SameLine(0.0f, ScaleUi(6.0f));
    if (DrawActionButton("##preset_next", "", IconGlyph::ChevronRight, ActionTone::Slate, false, canStepPresets, 0.0f, "Next preset")) {
        PushUndoState(scene_);
        const std::size_t nextIndex = (presetIndex_ + 1) % presetLibrary_.Count();
        LoadPreset(nextIndex);
    }

    drawDivider();
    DrawSectionChip("SCENE", Mix(theme.accentSurface, theme.accentActive, 0.34f));
    ImGui::SameLine(0.0f, ScaleUi(8.0f));
    ImGui::SetNextItemWidth(ScaleUi(220.0f));
    const Scene beforeName = scene_;
    const bool nameChanged = ImGui::InputText("##scene_name", &scene_.name);
    CaptureWidgetUndo(beforeName, nameChanged);
    if (nameChanged) {
        UpdateWindowTitle();
    }

    drawDivider();
    ImGui::SetNextItemWidth(ScaleUi(124.0f));
    if (BeginComboWithMaterialArrow("##toolbar_windows_menu", "Windows", ImGuiComboFlags_WidthFitPreview)) {
        DrawDockPanelVisibilityMenuItems();
        ImGui::Separator();
        if (ImGui::Selectable("Show All Panels", false)) {
            OpenAllDockPanels();
        }
        if (ImGui::Selectable("Restore Default Layout", false)) {
            OpenAllDockPanels();
            defaultLayoutBuilt_ = false;
        }
        ImGui::Separator();
        ImGui::MenuItem("Status Overlay", nullptr, &showStatusOverlay_);
        ImGui::EndCombo();
    }

    ImGui::EndChild();
    ImGui::SameLine(0.0f, settingsGap);
    ImGui::BeginChild("ToolbarPinnedSettings", ImVec2(settingsButtonWidth, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavFocus);
    if (DrawActionButton("##settings_icon_pinned", "", IconGlyph::Settings, ActionTone::Slate, settingsPanelOpen_, true, 0.0f, "Settings")) {
        settingsPanelOpen_ = !settingsPanelOpen_;
    }
    settingsButtonAnchorX_ = ImGui::GetItemRectMin().x;
    settingsButtonAnchorY_ = ImGui::GetItemRectMax().y + ScaleUi(kOverlayPanelMarginY);
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void AppWindow::DrawAboutPopup() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }
    const float aboutPopupWidth = std::clamp(viewport->Size.x - 32.0f, 320.0f, 440.0f);

    if (aboutPopupOpenRequested_) {
        ImGui::OpenPopup(kAboutPopupId);
        aboutPopupOpenRequested_ = false;
        aboutPopupPhysicsInitialized_ = false;
        aboutPopupGrabActive_ = false;
        aboutPopupVelocityX_ = 0.0f;
        aboutPopupVelocityY_ = 0.0f;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (!io.MouseDown[ImGuiMouseButton_Left]) {
        aboutPopupGrabActive_ = false;
    }
    const bool aboutPopupCanDetectGrab = aboutPopupPhysicsInitialized_
        && aboutPopupLastWindowHeight_ > 0.0f
        && aboutPopupLastTitleBarHeight_ > 0.0f;
    const ImRect aboutPopupTitleBarRect(
        ImVec2(aboutPopupPosX_, aboutPopupPosY_),
        ImVec2(aboutPopupPosX_ + aboutPopupWidth, aboutPopupPosY_ + aboutPopupLastTitleBarHeight_));
    const bool aboutPopupGrabAttempt = aboutPopupCanDetectGrab
        && io.MouseDown[ImGuiMouseButton_Left]
        && aboutPopupTitleBarRect.Contains(io.MousePos);
    if (aboutPopupGrabAttempt) {
        aboutPopupGrabActive_ = true;
    }
    if (aboutPopupGrabActive_) {
        aboutPopupVelocityX_ = 0.0f;
        aboutPopupVelocityY_ = 0.0f;
    }
    const bool aboutPopupAnimating = std::abs(aboutPopupVelocityX_) > 0.0f || std::abs(aboutPopupVelocityY_) > 0.0f;
    const bool aboutPopupPhysicsSuppressed = aboutPopupGrabActive_ && io.MouseDown[ImGuiMouseButton_Left];

    ImGui::SetNextWindowViewport(viewport->ID);
    if (aboutPopupPhysicsInitialized_ && aboutPopupAnimating && !aboutPopupPhysicsSuppressed) {
        ImGui::SetNextWindowPos(ImVec2(aboutPopupPosX_, aboutPopupPosY_), ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
            ImGuiCond_Appearing,
            ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(aboutPopupWidth, 0.0f), ImVec2(aboutPopupWidth, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(aboutPopupWidth, 0.0f), ImGuiCond_Appearing);

    PushFloatingPanelStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 20.0f));
    if (ImGui::BeginPopupModal(
            kAboutPopupId,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) {
        const UiTheme& theme = GetUiTheme();
        ImGuiWindow* popupWindow = ImGui::GetCurrentWindow();
        ImGuiContext& g = *GImGui;
        const float dt = std::clamp(io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f), 1.0f / 240.0f, 1.0f / 20.0f);
        if (!aboutPopupPhysicsInitialized_) {
            aboutPopupPhysicsInitialized_ = true;
            aboutPopupPosX_ = popupWindow->Pos.x;
            aboutPopupPosY_ = popupWindow->Pos.y;
            aboutPopupLastWindowPosX_ = popupWindow->Pos.x;
            aboutPopupLastWindowPosY_ = popupWindow->Pos.y;
        }

        const bool popupMoving = g.MovingWindow == popupWindow;
        aboutPopupLastWindowHeight_ = popupWindow->Size.y;
        aboutPopupLastTitleBarHeight_ = popupWindow->TitleBarHeight;
        if (popupMoving) {
            aboutPopupGrabActive_ = true;
            constexpr float kPopupReleaseVelocityScale = 0.42f;
            aboutPopupVelocityX_ = ((popupWindow->Pos.x - aboutPopupLastWindowPosX_) / dt) * kPopupReleaseVelocityScale;
            aboutPopupVelocityY_ = ((popupWindow->Pos.y - aboutPopupLastWindowPosY_) / dt) * kPopupReleaseVelocityScale;
            aboutPopupPosX_ = popupWindow->Pos.x;
            aboutPopupPosY_ = popupWindow->Pos.y;
        } else if (aboutPopupAnimating && !aboutPopupPhysicsSuppressed) {
            aboutPopupPosX_ += aboutPopupVelocityX_ * dt;
            aboutPopupPosY_ += aboutPopupVelocityY_ * dt;

            constexpr float kPopupViewportMargin = 8.0f;
            constexpr float kPopupBounce = 0.82f;
            constexpr float kPopupDrag = 0.982f;
            constexpr float kPopupStopSpeed = 24.0f;
            const float minX = viewport->Pos.x + kPopupViewportMargin;
            const float minY = viewport->Pos.y + kPopupViewportMargin;
            const float maxX = std::max(minX, viewport->Pos.x + viewport->Size.x - popupWindow->Size.x - kPopupViewportMargin);
            const float maxY = std::max(minY, viewport->Pos.y + viewport->Size.y - popupWindow->Size.y - kPopupViewportMargin);

            if (aboutPopupPosX_ < minX) {
                aboutPopupPosX_ = minX;
                aboutPopupVelocityX_ = std::abs(aboutPopupVelocityX_) * kPopupBounce;
            } else if (aboutPopupPosX_ > maxX) {
                aboutPopupPosX_ = maxX;
                aboutPopupVelocityX_ = -std::abs(aboutPopupVelocityX_) * kPopupBounce;
            }
            if (aboutPopupPosY_ < minY) {
                aboutPopupPosY_ = minY;
                aboutPopupVelocityY_ = std::abs(aboutPopupVelocityY_) * kPopupBounce;
            } else if (aboutPopupPosY_ > maxY) {
                aboutPopupPosY_ = maxY;
                aboutPopupVelocityY_ = -std::abs(aboutPopupVelocityY_) * kPopupBounce;
            }

            aboutPopupVelocityX_ *= kPopupDrag;
            aboutPopupVelocityY_ *= kPopupDrag;
            if (std::abs(aboutPopupVelocityX_) < kPopupStopSpeed) {
                aboutPopupVelocityX_ = 0.0f;
            }
            if (std::abs(aboutPopupVelocityY_) < kPopupStopSpeed) {
                aboutPopupVelocityY_ = 0.0f;
            }

            ImGui::SetWindowPos(ImVec2(aboutPopupPosX_, aboutPopupPosY_), ImGuiCond_Always);
        } else {
            aboutPopupPosX_ = popupWindow->Pos.x;
            aboutPopupPosY_ = popupWindow->Pos.y;
        }

        const float contentStartX = ImGui::GetCursorPosX();
        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float logoSize = std::clamp(availableWidth * 0.18f, 52.0f, 72.0f);
        const auto centerNextItem = [&](const float itemWidth) {
            ImGui::SetCursorPosX(contentStartX + std::max(0.0f, (availableWidth - itemWidth) * 0.5f));
        };

        if (appLogoSrv_ != nullptr) {
            centerNextItem(logoSize);
            ImGui::Image(reinterpret_cast<ImTextureID>(appLogoSrv_.Get()), ImVec2(logoSize, logoSize));
        }

        const char* brandLabel = "Radiary";
        float brandTextWidth = 0.0f;
        if (brandFont_ != nullptr) {
            ImGui::PushFont(brandFont_);
            brandTextWidth = ImGui::CalcTextSize(brandLabel).x;
            ImGui::PopFont();
        } else {
            brandTextWidth = ImGui::CalcTextSize(brandLabel).x;
        }
        centerNextItem(brandTextWidth);
        if (brandFont_ != nullptr) {
            ImGui::PushFont(brandFont_);
        }
        ImGui::TextUnformatted(brandLabel);
        if (brandFont_ != nullptr) {
            ImGui::PopFont();
        }

        const std::string versionLabel = std::string("Version ") + kRadiaryAppVersion;
        const float versionWidth = ImGui::CalcTextSize(versionLabel.c_str()).x;
        centerNextItem(versionWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textMuted);
        ImGui::TextUnformatted(versionLabel.c_str());
        ImGui::PopStyleColor();

        const char* summaryLabel = "Animated flame and path editor.";
        const float summaryWidth = ImGui::CalcTextSize(summaryLabel).x;
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        centerNextItem(summaryWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textMuted);
        ImGui::TextUnformatted(summaryLabel);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.roundingMedium);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.panelBackgroundAlt);
        if (ImGui::BeginChild(
                "##about_details",
                ImVec2(0.0f, 0.0f),
                ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textMuted);
            ImGui::TextUnformatted("Preview");
            ImGui::PopStyleColor();
            ImGui::TextUnformatted(gpuFlamePreviewEnabled_ ? "GPU (D3D11)" : "CPU");

            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textMuted);
            ImGui::TextUnformatted("Renderer");
            ImGui::PopStyleColor();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(WideToUtf8(renderAdapterName_).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        centerNextItem(132.0f);
        if (ImGui::Button("Close", ImVec2(132.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        aboutPopupLastWindowPosX_ = aboutPopupPosX_;
        aboutPopupLastWindowPosY_ = aboutPopupPosY_;
        ImGui::EndPopup();
    } else {
        aboutPopupPhysicsInitialized_ = false;
        aboutPopupGrabActive_ = false;
        aboutPopupVelocityX_ = 0.0f;
        aboutPopupVelocityY_ = 0.0f;
    }
    ImGui::PopStyleVar();
    PopFloatingPanelStyle();
}

void AppWindow::DrawSettingsPanel() {
    if (!settingsPanelOpen_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }

    const float settingsPanelMarginX = ScaleUi(kOverlayPanelMarginY + 10.0f);
    const float settingsPanelMarginY = ScaleUi(kOverlayPanelMarginY + 18.0f);
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

    PushFloatingPanelStyle();
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
        constexpr bool kDefaultExportStableFlameSampling = true;
        const Scene defaultScene = CreateDefaultScene();
        static const char* kExportFormatLabels[] = {"PNG Image", "JPG Image", "PNG Sequence", "JPG Sequence", "AVI Video", "MP4 Video", "MOV Video"};

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::BeginChild("SettingsPanelScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
        ImGui::TextDisabled("Preview behavior, GPU selection, and new-scene defaults.");

        ImGui::SeparatorText("Rendering");
        if (BeginPropertyGrid("##settings_rendering_grid")) {
            DrawPropertyRow(
                "GPU viewport preview",
                [&]() {
                    const bool changed = ImGui::Checkbox("##gpu_viewport_preview", &gpuFlamePreviewEnabled_)
                        || ResetValueOnDoubleClick(gpuFlamePreviewEnabled_, kDefaultGpuViewportPreview);
                    if (changed) {
                        MarkViewportDirty(PreviewResetReason::DeviceChanged);
                    }
                    return changed;
                },
                "Uses the D3D11 flame and path preview pipeline while editing.");

            std::string gpuBackendSummary = gpuFlamePreviewEnabled_ ? "Flame " : "CPU ";
            if (gpuFlamePreviewEnabled_) {
                gpuBackendSummary += gpuFlameRenderer_.IsReady() ? "D3D11 compute" : "lazy";
                gpuBackendSummary += " | Path ";
                gpuBackendSummary += gpuPathRenderer_.IsReady() ? "D3D11 raster" : "lazy";
            } else {
                gpuBackendSummary += "software renderer";
            }
            DrawPropertyRow("Viewport backend", [&]() {
                ImGui::AlignTextToFramePadding();
                ImGui::TextWrapped("%s", gpuBackendSummary.c_str());
                if (gpuFlamePreviewEnabled_ && !gpuFlameRenderer_.LastError().empty()) {
                    ImGui::TextDisabled("Flame: %s", gpuFlameRenderer_.LastError().c_str());
                }
                if (gpuFlamePreviewEnabled_ && !gpuPathRenderer_.LastError().empty()) {
                    ImGui::TextDisabled("Path: %s", gpuPathRenderer_.LastError().c_str());
                }
                return false;
            });
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Hardware");
        if (BeginPropertyGrid("##settings_hardware_grid")) {
            DrawPropertyRow("Renderer", [&]() {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(usingWarpDevice_ ? "D3D11 WARP" : "D3D11 Hardware");
                return false;
            });
            DrawPropertyRow("Adapter", [&]() {
                ImGui::AlignTextToFramePadding();
                ImGui::TextWrapped("%s", WideToUtf8(renderAdapterName_).c_str());
                return false;
            });
            ImGui::EndTable();
        }
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

            if (BeginPropertyGrid("##settings_adapter_grid")) {
                DrawPropertyRow(
                    "Preferred GPU",
                    [&]() {
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
                        return false;
                    },
                    "Choose the adapter used for viewport rendering and GPU export.");

                const bool canApplyGpu = selectedAdapterIndex_ >= 0 && selectedAdapterIndex_ != activeAdapterIndex_;
                DrawPropertyRow("GPU switch", [&]() {
                    bool pressed = false;
                    if (DrawActionButton("##apply_gpu_adapter", "Apply GPU", IconGlyph::Settings, ActionTone::Accent, false, canApplyGpu, 128.0f)) {
                        graphicsDeviceChangePending_ = true;
                        statusText_ = L"Applying GPU change";
                        pressed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Refresh GPU List")) {
                        EnumerateAdapters();
                    }
                    if (graphicsDeviceChangePending_) {
                        ImGui::TextDisabled("GPU switch queued for the next frame.");
                    }
                    return pressed;
                });
                ImGui::EndTable();
            }
        }

        ImGui::SeparatorText("Graphics");
        if (BeginPropertyGrid("##settings_graphics_grid")) {
            DrawPropertyRow("Viewport grid", [&]() {
                Scene beforeGraphics = scene_;
                bool graphicsChanged = ImGui::Checkbox("##show_viewport_grid", &scene_.gridVisible);
                graphicsChanged = ResetValueOnDoubleClick(scene_.gridVisible, defaultScene.gridVisible) || graphicsChanged;
                if (graphicsChanged) {
                    MarkViewportDirty(DeterminePreviewResetReason(beforeGraphics, scene_));
                }
                CaptureWidgetUndo(beforeGraphics, graphicsChanged);
                return graphicsChanged;
            });
            DrawPropertyRow("Status overlay", [&]() {
                const bool changed = ImGui::Checkbox("##show_status_overlay", &showStatusOverlay_)
                    || ResetValueOnDoubleClick(showStatusOverlay_, kDefaultShowStatusOverlay);
                if (changed) {
                    MarkViewportDirty();
                }
                return changed;
            });
            DrawPropertyRow("Hide grid on export", [&]() {
                const bool changed = ImGui::Checkbox("##hide_grid_on_export", &exportHideGrid_)
                    || ResetValueOnDoubleClick(exportHideGrid_, kDefaultExportHideGrid);
                if (changed) {
                    MarkViewportDirty();
                }
                return changed;
            });
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Defaults");
        constexpr int kMinNewSceneEndFrame = 24;
        constexpr int kMaxNewSceneEndFrame = 2400;
        constexpr double kMinNewSceneFps = 1.0;
        constexpr double kMaxNewSceneFps = 120.0;
        if (BeginPropertyGrid("##settings_defaults_grid")) {
            DrawPropertyRow("Default export format", [&]() {
                int exportFormatIndex = static_cast<int>(exportFormat_);
                if (ComboWithMaterialArrow("##default_export_format", &exportFormatIndex, kExportFormatLabels, IM_ARRAYSIZE(kExportFormatLabels))) {
                    exportFormat_ = static_cast<ExportFormat>(exportFormatIndex);
                    return true;
                }
                return false;
            });
            DrawPropertyRow("Use GPU for export", [&]() {
                const bool changed = ImGui::Checkbox("##default_use_gpu_export", &exportUseGpu_);
                if (changed) {
                    MarkViewportDirty();
                }
                return changed;
            });
            DrawPropertyRow(
                "Stable flame sampling",
                [&]() {
                    const bool changed = ImGui::Checkbox("##default_stable_flame_sampling", &exportStableFlameSampling_)
                        || ResetValueOnDoubleClick(exportStableFlameSampling_, kDefaultExportStableFlameSampling);
                    if (changed) {
                        MarkViewportDirty();
                    }
                    return changed;
                },
                "Keeps flame particles temporally anchored across exported frames.");
            DrawPropertyRow("New scene end frame", [&]() {
                const bool changed = SliderScalarWithInput(
                    "##new_scene_end_frame",
                    ImGuiDataType_S32,
                    &newSceneEndFrameDefault_,
                    &kMinNewSceneEndFrame,
                    &kMaxNewSceneEndFrame,
                    "%d");
                if (changed) {
                    newSceneEndFrameDefault_ = std::clamp(newSceneEndFrameDefault_, kMinNewSceneEndFrame, kMaxNewSceneEndFrame);
                }
                return changed;
            });
            DrawPropertyRow("New scene FPS", [&]() {
                const bool changed = SliderScalarWithInput(
                    "##new_scene_fps",
                    ImGuiDataType_Double,
                    &newSceneFrameRateDefault_,
                    &kMinNewSceneFps,
                    &kMaxNewSceneFps,
                    "%.2f");
                if (changed) {
                    newSceneFrameRateDefault_ = std::clamp(newSceneFrameRateDefault_, kMinNewSceneFps, kMaxNewSceneFps);
                }
                return changed;
            });
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Workflow");
        if (BeginPropertyGrid("##settings_workflow_grid")) {
            DrawPropertyRow(
                "Undo history",
                [&]() {
                    const bool changed = SliderScalarWithInput(
                        "##undo_history_limit",
                        ImGuiDataType_S32,
                        &undoHistoryLimit_,
                        &kMinUndoHistoryLimit,
                        &kMaxUndoHistoryLimit,
                        "%d");
                    if (changed) {
                        undoHistoryLimit_ = std::clamp(undoHistoryLimit_, kMinUndoHistoryLimit, kMaxUndoHistoryLimit);
                        EnforceUndoStackLimits();
                    }
                    return changed;
                },
                "Caps the number of undo and redo snapshots kept in memory.");
            DrawPropertyRow("Enable autosave", [&]() {
                const bool changed = ImGui::Checkbox("##autosave_enabled", &autoSaveEnabled_);
                if (changed && !autoSaveEnabled_) {
                    std::error_code removeError;
                    std::filesystem::remove(AutoSavePath(), removeError);
                }
                return changed;
            });
            DrawPropertyRow(
                "Autosave interval",
                [&]() {
                    const bool changed = SliderScalarWithInput(
                        "##autosave_interval_seconds",
                        ImGuiDataType_S32,
                        &autoSaveIntervalSeconds_,
                        &kMinAutoSaveIntervalSeconds,
                        &kMaxAutoSaveIntervalSeconds,
                        "%d sec");
                    if (changed) {
                        autoSaveIntervalSeconds_ = std::clamp(
                            autoSaveIntervalSeconds_,
                            kMinAutoSaveIntervalSeconds,
                            kMaxAutoSaveIntervalSeconds);
                    }
                    return changed;
                },
                autoSaveEnabled_ ? "How often Radiary saves crash-recovery snapshots." : "Enable autosave to adjust the snapshot interval.");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Layout");
        if (ImGui::Button("Restore default layout")) {
            defaultLayoutBuilt_ = false;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::End();
    PopFloatingPanelStyle();
}

void AppWindow::DrawExportPanel() {
    if (!exportPanelOpen_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }

    const float exportPanelMarginX = ScaleUi(kOverlayPanelMarginY + 10.0f);
    const float exportPanelMarginY = ScaleUi(kOverlayPanelMarginY + 18.0f);
    const ImRect bounds(
        ImVec2(viewport->Pos.x + exportPanelMarginX, viewport->Pos.y + exportPanelMarginY),
        ImVec2(viewport->Pos.x + viewport->Size.x - exportPanelMarginX, viewport->Pos.y + viewport->Size.y - exportPanelMarginY));
    const float topY = std::max(exportButtonAnchorY_ + 8.0f, bounds.Min.y);
    const float maxWidth = std::max(1.0f, bounds.GetWidth());
    const float maxHeight = std::max(1.0f, bounds.Max.y - topY);
    const float panelWidth = std::min(640.0f, maxWidth);
    const float panelHeight = std::min(520.0f, maxHeight);
    const float clampedXMin = bounds.Min.x;
    const float clampedXMax = std::max(clampedXMin, bounds.Max.x - panelWidth);
    const float clampedX = std::clamp(
        exportButtonAnchorX_,
        clampedXMin,
        clampedXMax);
    const ImVec2 panelAnchor(clampedX, topY);

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(panelAnchor, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(560.0f, maxWidth), std::min(320.0f, maxHeight)), ImVec2(maxWidth, maxHeight));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);

    PushFloatingPanelStyle();
    if (ImGui::Begin(
            "Export",
            &exportPanelOpen_,
            ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoDocking
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings)) {
        static const char* kFormats[] = {"PNG Image", "JPG Image", "PNG Sequence", "JPG Sequence", "AVI Video", "MP4 Video", "MOV Video"};

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::BeginChild("ExportPanelScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
        ImGui::TextDisabled("Output format, framing, background, and export quality.");

        ImGui::SeparatorText("Format");
        if (BeginPropertyGrid("##export_format_grid")) {
            DrawPropertyRow("File type", [&]() {
                int formatIndex = static_cast<int>(exportFormat_);
                if (ComboWithMaterialArrow("##export_file_type", &formatIndex, kFormats, IM_ARRAYSIZE(kFormats))) {
                    exportFormat_ = static_cast<ExportFormat>(formatIndex);
                    if (exportFormat_ == ExportFormat::Jpeg || exportFormat_ == ExportFormat::JpegSequence || exportFormat_ == ExportFormat::Avi) {
                        exportTransparentBackground_ = false;
                    }
                    return true;
                }
                return false;
            });
            ImGui::EndTable();
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
        if (BeginPropertyGrid("##export_output_grid")) {
            DrawPropertyRow("Resolution preset", [&]() {
                if (BeginComboWithMaterialArrow("##export_resolution_preset", resolutionPreview.c_str())) {
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
                return false;
            });
            DrawPropertyRow("Camera gate", [&]() {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(CameraAspectSummary(scene_.camera).c_str());
                return false;
            }, "Export resolution stays locked to this aspect.");
            DrawPropertyRow("Width", [&]() {
                exportWidth_ = std::max(2, exportWidth_);
                exportHeight_ = std::max(2, exportHeight_);
                const bool changed = ImGui::InputInt("##export_width", &exportWidth_, 64, 256);
                if (changed) {
                    ConstrainExportResolutionToCamera(scene_.camera, exportWidth_, exportHeight_, true);
                }
                return changed;
            });
            DrawPropertyRow("Height", [&]() {
                exportWidth_ = std::max(2, exportWidth_);
                exportHeight_ = std::max(2, exportHeight_);
                const bool changed = ImGui::InputInt("##export_height", &exportHeight_, 64, 256);
                if (changed) {
                    ConstrainExportResolutionToCamera(scene_.camera, exportWidth_, exportHeight_, false);
                }
                return changed;
            });
            if (rendersMultipleFrames) {
                DrawPropertyRow("Frame range", [&]() {
                    exportFrameStart_ = std::max(scene_.timelineStartFrame, exportFrameStart_);
                    exportFrameEnd_ = std::max(exportFrameStart_, exportFrameEnd_);
                    ImGui::SetNextItemWidth(92.0f);
                    const bool startChanged = ImGui::InputInt("##export_start_frame", &exportFrameStart_, 1, 10);
                    ImGui::SameLine();
                    ImGui::TextDisabled("to");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(92.0f);
                    const bool endChanged = ImGui::InputInt("##export_end_frame", &exportFrameEnd_, 1, 10);
                    ImGui::SameLine();
                    if (ImGui::Button("Use Timeline")) {
                        exportFrameStart_ = scene_.timelineStartFrame;
                        exportFrameEnd_ = scene_.timelineEndFrame;
                    }
                    exportFrameStart_ = std::clamp(exportFrameStart_, scene_.timelineStartFrame, scene_.timelineEndFrame);
                    exportFrameEnd_ = std::clamp(exportFrameEnd_, exportFrameStart_, scene_.timelineEndFrame);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%d frames", std::max(1, exportFrameEnd_ - exportFrameStart_ + 1));
                    return startChanged || endChanged;
                });
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Background");
        if (BeginPropertyGrid("##export_background_grid")) {
            DrawPropertyRow("Hide grid", [&]() {
                return ImGui::Checkbox("##export_hide_grid", &exportHideGrid_);
            });
            const bool transparencySupported = exportFormat_ == ExportFormat::Png || exportFormat_ == ExportFormat::PngSequence;
            DrawPropertyRow(
                "Transparent background",
                [&]() {
                    if (!transparencySupported) {
                        ImGui::BeginDisabled();
                    }
                    const bool changed = ImGui::Checkbox("##export_transparent_background", &exportTransparentBackground_);
                    if (!transparencySupported) {
                        ImGui::EndDisabled();
                    }
                    return changed;
                },
                transparencySupported ? "" : "Transparency is only available for PNG outputs.");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Render");
        const std::uint32_t exportBaselineIterations = CurrentPreviewSampleBaseline();
        const std::uint32_t exportDensityMatchedBaseline = CurrentExportDensityMatchedBaseline();
        exportIterations_ = CurrentExportIterationCount();
        constexpr float kMinExportIterationScale = 1.0f;
        constexpr float kMaxExportIterationScale = 8.0f;
        if (BeginPropertyGrid("##export_render_grid")) {
            DrawPropertyRow(
                "Use GPU",
                [&]() {
                    return ImGui::Checkbox("##export_use_gpu", &exportUseGpu_);
                },
                "CPU fallback if unavailable.");
            DrawPropertyRow(
                "Preview baseline",
                [&]() {
                    PushMonospaceFont();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%u", exportBaselineIterations);
                    PopMonospaceFont();
                    return false;
                });
            DrawPropertyRow(
                "Resolution baseline",
                [&]() {
                    PushMonospaceFont();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%u", exportDensityMatchedBaseline);
                    PopMonospaceFont();
                    return false;
                });
            DrawPropertyRow(
                "Sample scale",
                [&]() {
                    exportIterationScale_ = std::clamp(exportIterationScale_, kMinExportIterationScale, kMaxExportIterationScale);
                    return SliderScalarWithInput(
                        "##export_iteration_scale",
                        ImGuiDataType_Float,
                        &exportIterationScale_,
                        &kMinExportIterationScale,
                        &kMaxExportIterationScale,
                        "%.1fx");
                });
            DrawPropertyRow(
                "Export iterations",
                [&]() {
                    PushMonospaceFont();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%u", exportIterations_);
                    PopMonospaceFont();
                    return false;
                });
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (DrawActionButton("##export_confirm", "Export File", IconGlyph::ExportImage, ActionTone::Accent, false, true, 132.0f)) {
            ExportViewportToDialog();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::End();
    PopFloatingPanelStyle();
}

void AppWindow::DrawEasingPanel() {
    if (!easingPanelOpen_) {
        return;
    }

    const auto [currentOwnerType, currentOwnerIndex] = CurrentKeyframeOwner();
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
            viewport->Pos.x + ScaleUi(kOverlayPanelMarginX),
            (viewportWindow != nullptr ? viewportWindow->InnerRect.Min.y : viewport->Pos.y) + ScaleUi(kOverlayPanelMarginY)),
        ImVec2(
            viewport->Pos.x + viewport->Size.x - ScaleUi(kOverlayPanelMarginX),
            (lowerPanelWindow != nullptr ? lowerPanelWindow->InnerRect.Max.y : viewport->Pos.y + viewport->Size.y) - ScaleUi(kOverlayPanelMarginY)));
    const float overlayMarginX = ScaleUi(kOverlayPanelMarginX);
    const float overlayMarginY = ScaleUi(kOverlayPanelMarginY);
    const float maxWidth = std::max(1.0f, bounds.GetWidth() - overlayMarginX * 2.0f);
    const float maxHeight = std::max(1.0f, easingButtonAnchorY_ - bounds.Min.y - overlayMarginY);
    const float panelWidth = std::min(ScaleUi(248.0f), maxWidth);
    const float panelHeight = std::min(ScaleUi(188.0f), maxHeight);
    const float panelXMin = bounds.Min.x + overlayMarginX;
    const float panelXMax = std::max(panelXMin, bounds.Max.x - overlayMarginX - panelWidth);
    const float clampedX = std::clamp(
        easingButtonAnchorX_,
        panelXMin,
        panelXMax);
    const float panelYMin = bounds.Min.y + overlayMarginY;
    const float panelYMax = std::max(panelYMin, bounds.Max.y - panelHeight);
    const float panelY = std::clamp(
        easingButtonAnchorY_ - panelHeight,
        panelYMin,
        panelYMax);

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(ImVec2(clampedX, panelY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(248.0f, maxWidth), std::min(188.0f, maxHeight)), ImVec2(maxWidth, maxHeight));

    PushFloatingPanelStyle(true);
    if (ImGui::Begin(
        "Easing",
        &easingPanelOpen_,
        ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings)) {
        SceneKeyframe& keyframe = scene_.keyframes[static_cast<std::size_t>(editableKeyframeIndex)];
        ImGui::Spacing();
        Scene beforeCurve = scene_;
        const bool curveChanged = DrawBezierCurveEditor("##timeline_curve_editor_popup", keyframe);
        if (curveChanged) {
            RefreshTimelinePose();
        }
        CaptureWidgetUndo(beforeCurve, curveChanged);
    }
    ImGui::End();
    PopFloatingPanelStyle();
}

void AppWindow::DrawLayersPanel() {
    if (!layersPanelOpen_) {
        layersPanelActive_ = false;
        return;
    }

    const UiTheme& theme = GetUiTheme();
    EnsureSelectionIsValid();
    const ImGuiIO& io = ImGui::GetIO();
    bool rightClickStartedOnLayerControl = false;
    const auto markLayerControlRect = [&](const ImRect& rect) {
        if (rect.Contains(io.MouseClickedPos[ImGuiMouseButton_Right])) {
            rightClickStartedOnLayerControl = true;
        }
    };
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Layers", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    layersPanelActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    DrawDockPanelTabContextMenu("Layers", layersPanelOpen_);
    ImGui::SeparatorText("Layer Stack");

    if (DrawActionButton("##add_flame_layer", "Add Flame", IconGlyph::Add, ActionTone::Accent, false, true)) {
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
        MarkViewportDirty(PreviewResetReason::SceneChanged);
    }
    markLayerControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
    ImGui::SameLine();
    if (DrawActionButton("##add_path_layer", "Add Path", IconGlyph::Add, ActionTone::Accent, false, true)) {
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
        MarkViewportDirty(PreviewResetReason::SceneChanged);
    }
    markLayerControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
    ImGui::SameLine();
    const bool canRemoveLayer = CanRemoveSelectedLayers();
    if (DrawActionButton("##remove_layer", "Remove", IconGlyph::Remove, ActionTone::Accent, false, canRemoveLayer) && canRemoveLayer) {
        RemoveSelectedLayers();
    }
    markLayerControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));

    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    const auto drawLayerList = [&](const InspectorTarget target, const RenameTarget renameTarget, const int idOffset, const auto& names) {
        for (std::size_t index = 0; index < names.size(); ++index) {
            ImGui::PushID(idOffset + static_cast<int>(index));
            bool& visible = target == InspectorTarget::PathLayer
                ? scene_.paths[index].visible
                : scene_.transforms[index].visible;
            const bool selected = IsLayerSelected(target, static_cast<int>(index));
            const ImVec4 headerColor = selected
                ? WithAlpha(Mix(theme.accentSurface, theme.accent, 0.18f), target == InspectorTarget::PathLayer ? 0.96f : 0.92f)
                : WithAlpha(Mix(theme.panelBackgroundAlt, theme.accentSurface, 0.48f), target == InspectorTarget::PathLayer ? 0.82f : 0.78f);
            const ImVec4 hoverColor = selected
                ? Mix(theme.accentSurface, theme.accentHover, 0.24f)
                : Mix(theme.frameBackgroundHover, theme.accentSurface, 0.30f);
            const ImVec4 activeColor = selected
                ? Mix(theme.accentSurface, theme.accentHover, 0.32f)
                : Mix(theme.frameBackgroundActive, theme.accentSurface, 0.34f);
            const bool dimLabel = !visible;
            ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, activeColor);
            const float itemWidth = ImGui::GetContentRegionAvail().x;
            const float visibilityButtonSize = std::max(18.0f, ImGui::GetFrameHeight() - 4.0f);
            const float visibilityGap = 6.0f;
            const float dotRadius = 3.5f;
            const float dotWidth = dotRadius * 2.0f + 10.0f;
            const float labelWidth = std::max(1.0f, itemWidth - visibilityButtonSize - visibilityGap - dotWidth);
            const std::string itemLabel = visible
                ? names[index].get()
                : names[index].get() + " (hidden)";
            const ImVec4 dotColor = target == InspectorTarget::PathLayer
                ? ImVec4(0.38f, 0.74f, 0.88f, 0.95f)
                : ImVec4(0.97f, 0.64f, 0.34f, 0.95f);
            const char* dotTooltip = target == InspectorTarget::PathLayer ? "Path layer" : "Flame layer";
            ImRect layerRowRect;
            if (dimLabel) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.78f, 0.84f, 0.58f));
            }
            if (renameTarget_ == renameTarget && renamingLayerIndex_ == static_cast<int>(index)) {
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotWidth);
                if (focusLayerRename_) {
                    ImGui::SetKeyboardFocusHere();
                    focusLayerRename_ = false;
                }
                ImGuiInputTextFlags renameFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                const char* inputId = renameTarget == RenameTarget::Path ? "##path_layer_rename" : "##layer_rename";
                ImGui::SetNextItemWidth(labelWidth);
                const bool submitted = ImGui::InputText(inputId, &layerRenameBuffer_, renameFlags);
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                layerRowRect = itemRect;
                const ImVec2 dotCenter(rowStart.x + dotRadius + 2.0f, std::floor((itemRect.Min.y + itemRect.Max.y) * 0.5f) + 0.5f);
                drawList->AddCircleFilled(dotCenter, dotRadius, ImGui::GetColorU32(dotColor));
                const ImRect dotRect(
                    ImVec2(dotCenter.x - dotRadius - 2.0f, dotCenter.y - dotRadius - 2.0f),
                    ImVec2(dotCenter.x + dotRadius + 2.0f, dotCenter.y + dotRadius + 2.0f));
                if (ImGui::IsMouseHoveringRect(dotRect.Min, dotRect.Max)) {
                    ImGui::SetTooltip("%s", dotTooltip);
                }
                markLayerControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
                const bool cancelRename = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
                const bool commitRename = submitted || (ImGui::IsItemDeactivated() && !cancelRename);
                if (cancelRename) {
                    FinishLayerRename(false);
                } else if (commitRename) {
                    FinishLayerRename(true);
                }
            } else {
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotWidth);
                if (ImGui::Selectable(itemLabel.c_str(), selected, 0, ImVec2(labelWidth, 0.0f))) {
                    if (ImGui::GetIO().KeyShift) {
                        SelectLayerRange(target, static_cast<int>(index));
                    } else if (ImGui::GetIO().KeyCtrl) {
                        ToggleLayerSelection(target, static_cast<int>(index));
                    } else {
                        SelectSingleLayer(target, static_cast<int>(index));
                    }
                }
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                layerRowRect = itemRect;
                const ImVec2 dotCenter(rowStart.x + dotRadius + 2.0f, std::floor((itemRect.Min.y + itemRect.Max.y) * 0.5f) + 0.5f);
                drawList->AddCircleFilled(dotCenter, dotRadius, ImGui::GetColorU32(dotColor));
                const ImRect dotRect(
                    ImVec2(dotCenter.x - dotRadius - 2.0f, dotCenter.y - dotRadius - 2.0f),
                    ImVec2(dotCenter.x + dotRadius + 2.0f, dotCenter.y + dotRadius + 2.0f));
                if (ImGui::IsMouseHoveringRect(dotRect.Min, dotRect.Max)) {
                    ImGui::SetTooltip("%s", dotTooltip);
                }
                markLayerControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    FinishLayerRename(false);
                    SelectSingleLayer(target, static_cast<int>(index));
                    ImGui::OpenPopup("##layer_context_menu");
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    SelectSingleLayer(target, static_cast<int>(index));
                    BeginLayerRename(renameTarget, static_cast<int>(index));
                }
                if (ImGui::BeginPopup("##layer_context_menu")) {
                    const bool canPasteLayer = layerClipboardType_ != LayerClipboardType::None;
                    const bool affectsSelection = IsLayerSelected(target, static_cast<int>(index)) && SelectedLayerCount() > 1;
                    const char* visibilityLabel = visible
                        ? (affectsSelection ? "Hide Selected Layers" : "Hide Layer")
                        : (affectsSelection ? "Show Selected Layers" : "Show Layer");
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                        DuplicateSelectedLayer();
                    }
                    if (ImGui::MenuItem("Rename")) {
                        BeginLayerRename(renameTarget, static_cast<int>(index));
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                        CopySelectedLayer();
                    }
                    if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPasteLayer)) {
                        PasteCopiedLayer();
                    }
                    if (ImGui::MenuItem(visibilityLabel)) {
                        const bool nextVisibility = !visible;
                        const Scene beforeVisibility = scene_;
                        if (ApplyLayerVisibilityToSelectionOrItem(target, static_cast<int>(index), nextVisibility)) {
                            PushUndoState(beforeVisibility);
                            MarkViewportDirty(PreviewResetReason::SceneChanged);
                            statusText_ = nextVisibility ? L"Layer shown" : L"Layer hidden";
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete", "Delete", false, CanRemoveSelectedLayers())) {
                        RemoveSelectedLayers();
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::SameLine(0.0f, visibilityGap);
            const ImVec2 linePos = ImGui::GetCursorScreenPos();
            const ImVec2 visibilitySize(visibilityButtonSize, visibilityButtonSize);
            const float centeredVisibilityY = std::floor(layerRowRect.Min.y + (layerRowRect.GetHeight() - visibilitySize.y) * 0.5f) + 0.5f;
            const ImVec2 visibilityPos(linePos.x, centeredVisibilityY);
            ImGui::SetCursorScreenPos(visibilityPos);
            const bool togglePressed = ImGui::InvisibleButton("##visibility_toggle", visibilitySize);
            markLayerControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
            const bool toggleHovered = ImGui::IsItemHovered();
            const bool toggleHeld = ImGui::IsItemActive();
            if (togglePressed) {
                const bool nextVisibility = !visible;
                const Scene beforeVisibility = scene_;
                if (ApplyLayerVisibilityToSelectionOrItem(target, static_cast<int>(index), nextVisibility)) {
                    PushUndoState(beforeVisibility);
                    MarkViewportDirty(PreviewResetReason::SceneChanged);
                    statusText_ = nextVisibility ? L"Layer shown" : L"Layer hidden";
                }
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
    if (scene_.transforms.empty() && scene_.paths.empty()) {
        ImGui::TextDisabled("No layers");
    }
    const bool layerContextPopupOpen = ImGui::IsPopupOpen("##layer_context_menu", ImGuiPopupFlags_None);
    const bool panelTabContextPopupOpen = ImGui::IsPopupOpen("##panel_tab_context_menu", ImGuiPopupFlags_None);
    ImGuiWindow* layersWindow = ImGui::GetCurrentWindow();
    const ImRect blankMenuRect = layersWindow != nullptr ? layersWindow->InnerRect : ImRect();
    if (!rightClickStartedOnLayerControl
        && !layerContextPopupOpen
        && !panelTabContextPopupOpen
        && blankMenuRect.Contains(io.MouseClickedPos[ImGuiMouseButton_Right])
        && blankMenuRect.Contains(io.MousePos)
        && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##layers_blank_context_menu");
    }
    if (ImGui::BeginPopup("##layers_blank_context_menu")) {
        const bool canPasteLayer = layerClipboardType_ != LayerClipboardType::None;
        if (ImGui::MenuItem("Add Flame Layer")) {
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
            MarkViewportDirty(PreviewResetReason::SceneChanged);
        }
        if (ImGui::MenuItem("Add Path Layer")) {
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
            MarkViewportDirty(PreviewResetReason::SceneChanged);
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPasteLayer)) {
            PasteCopiedLayer();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

void AppWindow::DrawKeyframeListPanel() {
    if (!keyframeListPanelOpen_) {
        keyframeListPanelActive_ = false;
        return;
    }

    if (ImGuiWindow* layersWindow = ImGui::FindWindowByName("Layers")) {
        if (layersWindow->DockId != 0) {
            ImGui::SetNextWindowDockID(layersWindow->DockId, ImGuiCond_FirstUseEver);
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Keyframes", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    keyframeListPanelActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    DrawDockPanelTabContextMenu("Keyframes", keyframeListPanelOpen_);

    if (scene_.keyframes.empty()) {
        ImGui::TextDisabled("No keyframes in the scene.");
        ImGui::End();
        return;
    }

    const auto ownerLabel = [](const KeyframeOwnerType ownerType) {
        switch (ownerType) {
        case KeyframeOwnerType::Effect: return "Effect";
        case KeyframeOwnerType::Path: return "Path";
        case KeyframeOwnerType::Scene: return "Scene";
        default: return "Flame";
        }
    };
    const auto layerNameForKeyframe = [&](const SceneKeyframe& keyframe) {
        if (keyframe.ownerType == KeyframeOwnerType::Scene) {
            return std::string("Scene");
        }
        if (keyframe.ownerType == KeyframeOwnerType::Effect) {
            return std::string(EffectStageDisplayName(static_cast<EffectStackStage>(keyframe.ownerIndex)));
        }
        if (keyframe.ownerType == KeyframeOwnerType::Path) {
            if (keyframe.ownerIndex >= 0 && keyframe.ownerIndex < static_cast<int>(scene_.paths.size())) {
                return scene_.paths[static_cast<std::size_t>(keyframe.ownerIndex)].name;
            }
            return std::string("Path ") + std::to_string(keyframe.ownerIndex + 1);
        }
        if (keyframe.ownerIndex >= 0 && keyframe.ownerIndex < static_cast<int>(scene_.transforms.size())) {
            return scene_.transforms[static_cast<std::size_t>(keyframe.ownerIndex)].name;
        }
        return std::string("Layer ") + std::to_string(keyframe.ownerIndex + 1);
    };
    const auto easingLabelForKeyframe = [](const SceneKeyframe& keyframe) {
        return TitleCaseFromSnakeCase(ToString(keyframe.easing));
    };
    std::vector<int> sortedKeyframeIndices(scene_.keyframes.size());
    for (std::size_t index = 0; index < sortedKeyframeIndices.size(); ++index) {
        sortedKeyframeIndices[index] = static_cast<int>(index);
    }
    const auto compareKeyframes = [&](const int leftIndex, const int rightIndex) {
        const SceneKeyframe& left = scene_.keyframes[static_cast<std::size_t>(leftIndex)];
        const SceneKeyframe& right = scene_.keyframes[static_cast<std::size_t>(rightIndex)];
        const auto compareStrings = [&](const std::string& leftValue, const std::string& rightValue) {
            if (leftValue == rightValue) {
                return 0;
            }
            return leftValue < rightValue ? -1 : 1;
        };
        int comparison = 0;
        switch (keyframeListSortColumn_) {
        case KeyframeListSortColumn::Type:
            comparison = compareStrings(ownerLabel(left.ownerType), ownerLabel(right.ownerType));
            break;
        case KeyframeListSortColumn::Layer:
            comparison = compareStrings(layerNameForKeyframe(left), layerNameForKeyframe(right));
            break;
        case KeyframeListSortColumn::Easing:
            comparison = compareStrings(easingLabelForKeyframe(left), easingLabelForKeyframe(right));
            break;
        case KeyframeListSortColumn::Frame:
        default:
            if (left.frame != right.frame) {
                comparison = left.frame < right.frame ? -1 : 1;
            }
            break;
        }
        if (comparison == 0 && left.frame != right.frame) {
            comparison = left.frame < right.frame ? -1 : 1;
        }
        if (comparison == 0) {
            comparison = leftIndex < rightIndex ? -1 : (leftIndex > rightIndex ? 1 : 0);
        }
        return keyframeListSortAscending_ ? comparison < 0 : comparison > 0;
    };
    std::stable_sort(sortedKeyframeIndices.begin(), sortedKeyframeIndices.end(), compareKeyframes);

    const UiTheme& theme = GetUiTheme();
    const ImU32 selectedRowBg = ImGui::GetColorU32(WithAlpha(theme.accentSurface, 0.94f));
    const ImU32 hoveredRowBg = ImGui::GetColorU32(WithAlpha(theme.frameBackgroundHover, 0.72f));
    const ImVec4 transparentHeader(0.0f, 0.0f, 0.0f, 0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.roundingMedium);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.panelBackgroundInset);
    ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(theme.borderStrong, 0.72f));
    const bool keyframeListVisible = ImGui::BeginChild("##keyframe_list_container", ImVec2(0.0f, 0.0f), true);
    if (keyframeListVisible) {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3.0f, 3.0f));
        if (ImGui::BeginTable(
                "##keyframe_list_table",
                4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_PadOuterX,
                ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Easing", ImGuiTableColumnFlags_WidthFixed, 104.0f);
        const float compactHeaderHeight = ImGui::GetTextLineHeight() + 6.0f;
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers, compactHeaderHeight);
        const auto drawSortHeader = [&](const int columnIndex, const char* label, const KeyframeListSortColumn sortColumn) {
            ImGui::TableSetColumnIndex(columnIndex);
            ImGui::PushID(columnIndex);
            const float textInsetX = 8.0f;
            const float fillInsetX = 0;
            const float fillInsetY = 1.0f;
            const ImVec2 size(
                std::max(0.0f, ImGui::GetContentRegionAvail().x),
                compactHeaderHeight);
            const bool pressed = ImGui::InvisibleButton("##sort_header_hit", size);
            const ImRect headerRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const bool hovered = ImGui::IsItemHovered();
            if (pressed) {
                if (keyframeListSortColumn_ == sortColumn) {
                    keyframeListSortAscending_ = !keyframeListSortAscending_;
                } else {
                    keyframeListSortColumn_ = sortColumn;
                    keyframeListSortAscending_ = true;
                }
            }
            if (keyframeListSortColumn_ == sortColumn || hovered) {
                const ImRect fillRect(
                    ImVec2(headerRect.Min.x + fillInsetX, headerRect.Min.y + fillInsetY),
                    ImVec2(headerRect.Max.x - fillInsetX, headerRect.Max.y - fillInsetY));
                ImGui::GetWindowDrawList()->AddRectFilled(
                    fillRect.Min,
                    fillRect.Max,
                    ImGui::GetColorU32(keyframeListSortColumn_ == sortColumn ? theme.frameBackgroundActive : theme.frameBackgroundHover),
                    theme.roundingSmall);
            }
            const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const ImVec2 textPos(
                headerRect.Min.x + textInsetX,
                headerRect.Min.y + std::max(0.0f, (headerRect.GetHeight() - textSize.y) * 0.5f));
            ImGui::GetWindowDrawList()->AddText(textPos, textColor, label);
            if (keyframeListSortColumn_ == sortColumn) {
                const float centerY = (headerRect.Min.y + headerRect.Max.y) * 0.5f;
                const float arrowHalfWidth = 4.0f;
                const float arrowHalfHeight = 3.0f;
                const float arrowCenterX = headerRect.Max.x - 12.0f;
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImU32 arrowColor = ImGui::GetColorU32(ImGuiCol_Text);
                if (keyframeListSortAscending_) {
                    drawList->AddTriangleFilled(
                        ImVec2(arrowCenterX, centerY - arrowHalfHeight),
                        ImVec2(arrowCenterX - arrowHalfWidth, centerY + arrowHalfHeight),
                        ImVec2(arrowCenterX + arrowHalfWidth, centerY + arrowHalfHeight),
                        arrowColor);
                } else {
                    drawList->AddTriangleFilled(
                        ImVec2(arrowCenterX - arrowHalfWidth, centerY - arrowHalfHeight),
                        ImVec2(arrowCenterX + arrowHalfWidth, centerY - arrowHalfHeight),
                        ImVec2(arrowCenterX, centerY + arrowHalfHeight),
                        arrowColor);
                }
            }
            ImGui::PopID();
        };
        drawSortHeader(0, "Frame", KeyframeListSortColumn::Frame);
        drawSortHeader(1, "Type", KeyframeListSortColumn::Type);
        drawSortHeader(2, "Layer", KeyframeListSortColumn::Layer);
        drawSortHeader(3, "Easing", KeyframeListSortColumn::Easing);

        for (const int keyframeIndex : sortedKeyframeIndices) {
            const SceneKeyframe& keyframe = scene_.keyframes[static_cast<std::size_t>(keyframeIndex)];
            const bool selected = keyframeIndex == selectedTimelineKeyframe_;

            ImGui::TableNextRow();
            ImGui::PushID(keyframeIndex);
            ImGui::TableSetColumnIndex(0);
            const ImGuiStyle& style = ImGui::GetStyle();
            const std::string frameLabel = std::to_string(keyframe.frame);
            ImGui::PushStyleColor(ImGuiCol_Header, transparentHeader);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, transparentHeader);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, transparentHeader);
            const bool rowPressed = ImGui::Selectable("##keyframe_row_hit", false, ImGuiSelectableFlags_SpanAllColumns);
            const bool rowHovered = ImGui::IsItemHovered();
            const ImRect rowRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::PopStyleColor(3);
            if (rowPressed) {
                selectedTimelineKeyframe_ = keyframeIndex;
                if (keyframe.ownerType == KeyframeOwnerType::Scene) {
                    ClearLayerSelections();
                    ClearEffectSelections();
                } else if (keyframe.ownerType == KeyframeOwnerType::Effect) {
                    ClearLayerSelections();
                    const int effectIndex = EffectIndexForKeyframeOwner(keyframe.ownerIndex);
                    if (effectIndex >= 0) {
                        SelectSingleEffect(effectIndex);
                    } else {
                        ClearEffectSelections();
                    }
                } else if (keyframe.ownerType == KeyframeOwnerType::Path) {
                    SelectSingleLayer(InspectorTarget::PathLayer, keyframe.ownerIndex);
                } else {
                    SelectSingleLayer(InspectorTarget::FlameLayer, keyframe.ownerIndex);
                }
                SetTimelineFrame(static_cast<double>(keyframe.frame), false);
            }
            const auto drawRowFillForCurrentColumn = [&](const bool roundLeft, const bool roundRight) {
                if (!selected && !rowHovered) {
                    return;
                }
                const ImVec2 cellCursor = ImGui::GetCursorScreenPos();
                const float cellWidth = ImGui::GetContentRegionAvail().x + style.CellPadding.x * 2.0f;
                ImRect cellRect(
                    ImVec2(cellCursor.x - style.CellPadding.x, rowRect.Min.y + 1.0f),
                    ImVec2(cellCursor.x - style.CellPadding.x + cellWidth, rowRect.Max.y - 1.0f));
                if (roundLeft) {
                    cellRect.Min.x += 1.0f;
                }
                if (roundRight) {
                    cellRect.Max.x -= 1.0f;
                }
                ImDrawFlags roundingFlags = ImDrawFlags_None;
                if (roundLeft && roundRight) {
                    roundingFlags = ImDrawFlags_RoundCornersAll;
                } else if (roundLeft) {
                    roundingFlags = ImDrawFlags_RoundCornersLeft;
                } else if (roundRight) {
                    roundingFlags = ImDrawFlags_RoundCornersRight;
                }
                ImGui::GetWindowDrawList()->AddRectFilled(
                    cellRect.Min,
                    cellRect.Max,
                    selected ? selectedRowBg : hoveredRowBg,
                    roundingFlags == ImDrawFlags_None ? 0.0f : theme.roundingSmall,
                    roundingFlags);
            };
            drawRowFillForCurrentColumn(true, false);
            PushMonospaceFont();
            ImFont* monospaceFont = ImGui::GetFont();
            const float monospaceFontSize = ImGui::GetFontSize();
            PopMonospaceFont();
            const ImU32 frameTextColor = ImGui::GetColorU32(ImGuiCol_Text);
            const ImVec2 frameTextSize = monospaceFont->CalcTextSizeA(monospaceFontSize, FLT_MAX, 0.0f, frameLabel.c_str());
            const ImVec2 frameTextPos(
                rowRect.Min.x + style.FramePadding.x,
                rowRect.Min.y + std::max(0.0f, (rowRect.GetHeight() - frameTextSize.y) * 0.5f));
            ImGui::GetWindowDrawList()->AddText(monospaceFont, monospaceFontSize, frameTextPos, frameTextColor, frameLabel.c_str());

            ImGui::TableSetColumnIndex(1);
            drawRowFillForCurrentColumn(false, false);
            ImGui::TextUnformatted(ownerLabel(keyframe.ownerType));

            ImGui::TableSetColumnIndex(2);
            drawRowFillForCurrentColumn(false, false);
            const std::string layerLabel = layerNameForKeyframe(keyframe);
            ImGui::TextUnformatted(layerLabel.c_str());

            ImGui::TableSetColumnIndex(3);
            drawRowFillForCurrentColumn(false, true);
            const std::string easingLabel = easingLabelForKeyframe(keyframe);
            ImGui::TextUnformatted(easingLabel.c_str());
            ImGui::PopID();
        }

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::End();
}

void AppWindow::DrawHistoryPanel() {
    if (!historyPanelOpen_) {
        return;
    }

    if (ImGuiWindow* layersWindow = ImGui::FindWindowByName("Layers")) {
        if (layersWindow->DockId != 0) {
            ImGui::SetNextWindowDockID(layersWindow->DockId, ImGuiCond_FirstUseEver);
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("History", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    DrawDockPanelTabContextMenu("History", historyPanelOpen_);

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Click a state to step backward or forward.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const std::size_t undoCount = undoStack_.size();
    const std::size_t redoCount = redoStack_.size();
    bool historyJumped = false;
    const UiTheme& theme = GetUiTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.roundingMedium);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.roundingSmall);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.panelBackgroundInset);
    ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(theme.borderStrong, 0.72f));
    const bool historyVisible = ImGui::BeginChild("##history_entries", ImVec2(0.0f, 0.0f), true);
    if (historyVisible) {
        const auto drawHistoryEntry = [&](const char* id, const std::string& label, const bool selected, const bool dimmed) {
            const ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 size(
                std::max(0.0f, ImGui::GetContentRegionAvail().x),
                ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f);
            const bool pressed = ImGui::InvisibleButton(id, size);
            const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (selected || hovered) {
                const ImU32 fillColor = ImGui::GetColorU32(
                    selected
                        ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)
                        : ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
                drawList->AddRectFilled(rect.Min, rect.Max, fillColor, theme.roundingSmall);
            }
            const ImU32 textColor = ImGui::GetColorU32(
                dimmed
                    ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                    : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
            const ImVec2 textPos(
                rect.Min.x + style.FramePadding.x,
                rect.Min.y + std::max(0.0f, (rect.GetHeight() - textSize.y) * 0.5f));
            drawList->AddText(textPos, textColor, label.c_str());
            return pressed;
        };

        if (undoCount == 0 && redoCount == 0) {
            drawHistoryEntry("##history_empty_current", currentHistoryLabel_, true, false);
        } else {
            for (std::size_t index = 0; index < undoCount; ++index) {
                ImGui::PushID(static_cast<int>(index));
                if (drawHistoryEntry("##history_undo_entry", undoStack_[index].label, false, false)) {
                    const std::size_t undoSteps = undoCount - index;
                    for (std::size_t step = 0; step < undoSteps && CanUndo(); ++step) {
                        Undo();
                    }
                    historyJumped = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            if (!historyJumped) {
                drawHistoryEntry("##history_current_entry", currentHistoryLabel_, true, false);

                for (std::size_t displayIndex = 0; displayIndex < redoCount; ++displayIndex) {
                    const std::size_t redoIndex = redoCount - 1 - displayIndex;
                    ImGui::PushID(static_cast<int>(redoIndex + undoCount + 1));
                    if (drawHistoryEntry("##history_redo_entry", redoStack_[redoIndex].label, false, true)) {
                        const std::size_t redoSteps = displayIndex + 1;
                        for (std::size_t step = 0; step < redoSteps && CanRedo(); ++step) {
                            Redo();
                        }
                        historyJumped = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(6);

    ImGui::End();
}

void AppWindow::DrawInspectorPanel() {
    if (!inspectorPanelOpen_) {
        inspectorPanelActive_ = false;
        return;
    }

    const UiTheme& theme = GetUiTheme();
    EnsureSelectionIsValid();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    inspectorPanelActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    const float inspectorWidth = ImGui::GetContentRegionAvail().x;
    const bool useWideInspector = inspectorWidth >= ScaleUi(kWideInspectorMinWidth);
    DrawDockPanelTabContextMenu("Inspector", inspectorPanelOpen_);
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
            MarkViewportDirty(DeterminePreviewResetReason(before, scene_));
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
            MarkViewportDirty(PreviewResetReason::SceneChanged);
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

        ImGui::TextColored(theme.accentHover, "%s", path.name.c_str());
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
            MarkViewportDirty(PreviewResetReason::SceneChanged);
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
            MarkViewportDirty(PreviewResetReason::SceneChanged);
        }

        if (ImGui::Button("Straight Path")) {
            PushUndoState(scene_);
            path.controlPoints = {
                {-5.6, 0.0, 0.0},
                {-1.9, 0.0, 0.0},
                {1.9, 0.0, 0.0},
                {5.6, 0.0, 0.0}
            };
            MarkViewportDirty(PreviewResetReason::SceneChanged);
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
            MarkViewportDirty(PreviewResetReason::SceneChanged);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Point")) {
            PushUndoState(scene_);
            const Vec3 last = path.controlPoints.empty() ? Vec3{} : path.controlPoints.back();
            path.controlPoints.push_back({last.x + 2.2, last.y * -0.75, last.z + 1.6});
            MarkViewportDirty(PreviewResetReason::SceneChanged);
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
                        MarkViewportDirty(PreviewResetReason::SceneChanged);
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
        const auto drawPrimaryColor = [&]() {
            float primaryColor[3] = {
                path.material.primaryColor.r / 255.0f,
                path.material.primaryColor.g / 255.0f,
                path.material.primaryColor.b / 255.0f
            };
            Scene beforePrimary = scene_;
            bool primaryChanged = colorEditWithReset("Primary Color", primaryColor, defaultMaterial.primaryColor);
            if (primaryChanged) {
                path.material.primaryColor = ToColor(primaryColor);
            }
            captureEdit(beforePrimary, primaryChanged);
        };
        const auto drawAccentColor = [&]() {
            float accentColor[3] = {
                path.material.accentColor.r / 255.0f,
                path.material.accentColor.g / 255.0f,
                path.material.accentColor.b / 255.0f
            };
            Scene beforeAccent = scene_;
            bool accentChanged = colorEditWithReset("Accent Color", accentColor, defaultMaterial.accentColor);
            if (accentChanged) {
                path.material.accentColor = ToColor(accentColor);
            }
            captureEdit(beforeAccent, accentChanged);
        };
        const auto drawWireColor = [&]() {
            float wireColor[3] = {
                path.material.wireColor.r / 255.0f,
                path.material.wireColor.g / 255.0f,
                path.material.wireColor.b / 255.0f
            };
            Scene beforeWire = scene_;
            bool wireChanged = colorEditWithReset("Wire Color", wireColor, defaultMaterial.wireColor);
            if (wireChanged) {
                path.material.wireColor = ToColor(wireColor);
            }
            captureEdit(beforeWire, wireChanged);
        };
        const auto drawPointSize = [&]() {
            Scene beforePointSize = scene_;
            const bool pointSizeChanged = sliderDoubleWithReset("Point Size", &path.material.pointSize, &pointSizeMin, &pointSizeMax, "%.1f", defaultMaterial.pointSize);
            captureEdit(beforePointSize, pointSizeChanged);
        };
        if (useWideInspector) {
            if (ImGui::BeginTable("##path_material_grid", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                drawPrimaryColor();
                drawAccentColor();
                ImGui::TableNextColumn();
                drawWireColor();
                drawPointSize();
                ImGui::EndTable();
            }
        } else {
            drawPrimaryColor();
            drawAccentColor();
            drawWireColor();
            drawPointSize();
        }
    } else {
        TransformLayer& layer = scene_.transforms[scene_.selectedTransform];
        ImGui::TextColored(theme.accentHover, "%s", layer.name.c_str());
        ImGui::SeparatorText("Transform Controls");

        Scene before = scene_;
        bool changed = false;

        toggleVisibility(layer.visible);

        const auto drawWeightControl = [&]() {
            Scene beforeWeight = scene_;
            const bool weightChanged = sliderDoubleWithReset("Weight", &layer.weight, &weightMin, &weightMax, "%.2f", defaultLayer.weight);
            captureEdit(beforeWeight, weightChanged);
        };
        const auto drawRotationControl = [&]() {
            Scene beforeRotation = scene_;
            const bool rotationChanged = sliderDoubleWithReset("Rotation", &layer.rotationDegrees, &rotationMin, &rotationMax, "%.2f", defaultLayer.rotationDegrees);
            captureEdit(beforeRotation, rotationChanged);
        };
        const auto drawScaleXControl = [&]() {
            Scene beforeScaleX = scene_;
            const bool scaleXChanged = sliderDoubleWithReset("Scale X", &layer.scaleX, &scaleMin, &scaleMax, "%.2f", defaultLayer.scaleX);
            captureEdit(beforeScaleX, scaleXChanged);
        };
        const auto drawScaleYControl = [&]() {
            Scene beforeScaleY = scene_;
            const bool scaleYChanged = sliderDoubleWithReset("Scale Y", &layer.scaleY, &scaleMin, &scaleMax, "%.2f", defaultLayer.scaleY);
            captureEdit(beforeScaleY, scaleYChanged);
        };
        const auto drawOffsetXControl = [&]() {
            Scene beforeOffsetX = scene_;
            const bool offsetXChanged = sliderDoubleWithReset("Offset X", &layer.translateX, &offsetMin, &offsetMax, "%.2f", defaultLayer.translateX);
            captureEdit(beforeOffsetX, offsetXChanged);
        };
        const auto drawOffsetYControl = [&]() {
            Scene beforeOffsetY = scene_;
            const bool offsetYChanged = sliderDoubleWithReset("Offset Y", &layer.translateY, &offsetMin, &offsetMax, "%.2f", defaultLayer.translateY);
            captureEdit(beforeOffsetY, offsetYChanged);
        };
        const auto drawShearXControl = [&]() {
            Scene beforeShearX = scene_;
            const bool shearXChanged = sliderDoubleWithReset("Shear X", &layer.shearX, &shearMin, &shearMax, "%.2f", defaultLayer.shearX);
            captureEdit(beforeShearX, shearXChanged);
        };
        const auto drawShearYControl = [&]() {
            Scene beforeShearY = scene_;
            const bool shearYChanged = sliderDoubleWithReset("Shear Y", &layer.shearY, &shearMin, &shearMax, "%.2f", defaultLayer.shearY);
            captureEdit(beforeShearY, shearYChanged);
        };
        const auto drawColorIndexControl = [&]() {
            Scene beforeColorIndex = scene_;
            const bool colorIndexChanged = sliderDoubleWithReset("Color Index", &layer.colorIndex, &colorIndexMin, &colorIndexMax, "%.2f", defaultLayer.colorIndex);
            captureEdit(beforeColorIndex, colorIndexChanged);
        };
        if (useWideInspector) {
            if (ImGui::BeginTable("##transform_grid", 2, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                drawWeightControl();
                drawRotationControl();
                drawScaleXControl();
                drawScaleYControl();
                ImGui::TableNextColumn();
                drawOffsetXControl();
                drawOffsetYControl();
                drawShearXControl();
                drawShearYControl();
                drawColorIndexControl();
                ImGui::EndTable();
            }
        } else {
            drawWeightControl();
            drawRotationControl();
            drawScaleXControl();
            drawScaleYControl();
            drawOffsetXControl();
            drawOffsetYControl();
            drawShearXControl();
            drawShearYControl();
            drawColorIndexControl();
        }

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

        ImGui::SeparatorText("Symmetry");
        static const char* kSymmetryModes[] = {"None", "Bilateral", "Rotational", "Bilateral + Rotational"};
        int symmetryMode = static_cast<int>(scene_.flameRender.symmetry);
        before = scene_;
        changed = comboWithReset("Mode", &symmetryMode, kSymmetryModes, IM_ARRAYSIZE(kSymmetryModes), static_cast<int>(defaultFlameRender.symmetry));
        if (changed) {
            scene_.flameRender.symmetry = static_cast<SymmetryMode>(symmetryMode);
        }
        captureEdit(before, changed);
        if (scene_.flameRender.symmetry == SymmetryMode::Rotational
            || scene_.flameRender.symmetry == SymmetryMode::BilateralRotational) {
            before = scene_;
            changed = sliderIntWithReset("Symmetry Order", &scene_.flameRender.symmetryOrder, 2, 12, defaultFlameRender.symmetryOrder);
            captureEdit(before, changed);
        }

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

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, Mix(theme.accentSurface, theme.accent, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Mix(theme.accentSurface, theme.accentHover, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Mix(theme.accentSurface, theme.accentHover, 0.36f));
        if (ImGui::CollapsingHeader("Variations", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const VariationCategory& category : kVariationCategories) {
                int activeCount = 0;
                for (const VariationType type : category.types) {
                    if (layer.variations[static_cast<std::size_t>(type)] != 0.0) {
                        ++activeCount;
                    }
                }
                char categoryLabel[64];
                if (activeCount > 0) {
                    std::snprintf(categoryLabel, sizeof(categoryLabel), "%s (%d)", category.name, activeCount);
                } else {
                    std::snprintf(categoryLabel, sizeof(categoryLabel), "%s", category.name);
                }
                if (ImGui::TreeNode(categoryLabel)) {
                    for (const VariationType type : category.types) {
                        const std::size_t variationIndex = static_cast<std::size_t>(type);
                        const std::string label = TitleCaseFromSnakeCase(ToString(type));
                        before = scene_;
                        changed = sliderDoubleWithReset(label.c_str(), &layer.variations[variationIndex], &variationMin, &variationMax, "%.2f", defaultLayer.variations[variationIndex]);
                        captureEdit(before, changed);
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::End();
}

void AppWindow::DrawTimelinePanel() {
    if (!playbackPanelOpen_) {
        playbackPanelActive_ = false;
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Playback", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    playbackPanelActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    DrawDockPanelTabContextMenu("Playback", playbackPanelOpen_);
    const Scene defaultScene = CreateDefaultScene();
    const float playbackSectionGap = 0;
    const float playbackHeaderTopMargin = 0;
    const float playbackHeaderHeight = std::max(1.0f, ToolbarHeight() - ImGui::GetStyle().WindowPadding.y * 1.5f);
    ImGui::Dummy(ImVec2(0.0f, playbackHeaderTopMargin));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetStyle().WindowPadding.x, 0.0f));
    ImGui::BeginChild(
        "PlaybackToolbarScroll",
        ImVec2(0.0f, playbackHeaderHeight),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar();
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && io.MouseWheel != 0.0f) {
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * toolbarScrollStep_);
    }

    const auto drawHeaderDivider = []() {
        DrawSubtleToolbarDivider(8.0f);
    };
    const int currentFrame = static_cast<int>(std::round(scene_.timelineFrame));
    const auto [currentOwnerType, currentOwnerIndex] = CurrentKeyframeOwner();
    const int currentKeyframeIndex = FindKeyframeIndex(scene_, currentFrame, currentOwnerType, currentOwnerIndex);
    int previousKeyframeIndex = -1;
    int nextKeyframeIndex = -1;
    for (std::size_t index = 0; index < scene_.keyframes.size(); ++index) {
        if (scene_.keyframes[index].ownerType != currentOwnerType || scene_.keyframes[index].ownerIndex != currentOwnerIndex) {
            continue;
        }
        const int keyframeFrame = scene_.keyframes[index].frame;
        if (keyframeFrame < currentFrame) {
            previousKeyframeIndex = static_cast<int>(index);
        } else if (keyframeFrame > currentFrame) {
            nextKeyframeIndex = static_cast<int>(index);
            break;
        }
    }

    constexpr float kTimelineZoomMin = 1.0f;
    constexpr float kTimelineZoomMax = 64.0f;
    constexpr float kTimelineZoomStep = 1.25f;
    const double timelineRangeStartFrame = static_cast<double>(scene_.timelineStartFrame);
    const double timelineRangeEndFrame = static_cast<double>(std::max(scene_.timelineStartFrame, scene_.timelineEndFrame));
    const double timelineRangeSpanFrames = std::max(1.0, timelineRangeEndFrame - timelineRangeStartFrame);
    const auto timelineRangeMidpoint = [&]() {
        return (timelineRangeStartFrame + timelineRangeEndFrame) * 0.5;
    };
    const auto clampTimelineViewCenter = [&](const double visibleSpanFrames) {
        const double halfVisibleSpan = visibleSpanFrames * 0.5;
        const double minCenter = timelineRangeStartFrame + halfVisibleSpan;
        const double maxCenter = timelineRangeEndFrame - halfVisibleSpan;
        if (minCenter > maxCenter) {
            timelineViewCenterFrame_ = timelineRangeMidpoint();
            return;
        }
        timelineViewCenterFrame_ = Clamp(timelineViewCenterFrame_, minCenter, maxCenter);
    };
    const auto zoomTimelineView = [&](const float zoomFactor, const double focusFrame) {
        const float currentZoom = std::clamp(timelineZoom_, kTimelineZoomMin, kTimelineZoomMax);
        const float nextZoom = std::clamp(currentZoom * zoomFactor, kTimelineZoomMin, kTimelineZoomMax);
        if (std::abs(nextZoom - currentZoom) < 1.0e-4f) {
            return;
        }

        const double oldVisibleSpan = timelineRangeSpanFrames / static_cast<double>(currentZoom);
        const double newVisibleSpan = timelineRangeSpanFrames / static_cast<double>(nextZoom);
        if (timelineZoom_ <= kTimelineZoomMin + 1.0e-4f) {
            timelineViewCenterFrame_ = timelineRangeMidpoint();
        }
        timelineZoom_ = nextZoom;
        timelineViewCenterFrame_ = focusFrame + (timelineViewCenterFrame_ - focusFrame) * (newVisibleSpan / oldVisibleSpan);
        clampTimelineViewCenter(newVisibleSpan);
    };
    const auto resetTimelineZoom = [&]() {
        timelineZoom_ = 1.0f;
        timelineViewCenterFrame_ = timelineRangeMidpoint();
    };
    if (!io.WantTextInput && playbackPanelActive_ && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
            zoomTimelineView(kTimelineZoomStep, scene_.timelineFrame);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
            zoomTimelineView(1.0f / kTimelineZoomStep, scene_.timelineFrame);
        } else if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) {
            resetTimelineZoom();
        }
    }

    if (DrawActionButton("##timeline_play", scene_.animatePath ? "Pause" : "Play", scene_.animatePath ? IconGlyph::Pause : IconGlyph::Play, ActionTone::Accent, scene_.animatePath, true, 108.0f)) {
        PushUndoState(scene_);
        scene_.animatePath = !scene_.animatePath;
        MarkViewportDirty(PreviewResetReason::SceneChanged);
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
        keyframe.markerColor = MarkerColorForKeyframeOwner(currentOwnerType, currentOwnerIndex);
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
        if (cameraAspectLocked_) {
            cameraAspectLockedRatio_ = std::max(1.0e-6, scene_.camera.frameWidth) / std::max(1.0e-6, scene_.camera.frameHeight);
        }
        SyncCurrentKeyframeFromScene();
        MarkViewportDirty(PreviewResetReason::CameraChanged);
    }

    drawHeaderDivider();
    static const char* kKeyframeEasingLabels[] = {"Linear", "Ease In", "Ease Out", "Ease In Out", "Hold", "Custom"};
    const int editableKeyframeIndex = selectedTimelineKeyframe_ >= 0 ? selectedTimelineKeyframe_ : currentKeyframeIndex;
    if (editableKeyframeIndex >= 0 && editableKeyframeIndex < static_cast<int>(scene_.keyframes.size())) {
        Scene beforeEasing = scene_;
        int easingIndex = static_cast<int>(scene_.keyframes[static_cast<std::size_t>(editableKeyframeIndex)].easing);
        ImGui::SetNextItemWidth(156.0f);
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
        easingButtonAnchorY_ = ImGui::GetItemRectMin().y - ScaleUi(kOverlayPanelMarginY);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Easing");
        ImGui::SameLine();
    } else {
        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(156.0f);
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
        "Frame %d / %d at %.2f fps | %.1fx",
        currentFrame,
        scene_.timelineEndFrame,
        scene_.timelineFrameRate,
        timelineZoom_);
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
    (void)rightToolbarWidth;
    drawHeaderDivider();
    PushMonospaceFont();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", playbackSummary);
    PopMonospaceFont();
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
        MarkViewportDirty(PreviewResetReason::SceneChanged);
    }
    CaptureWidgetUndo(beforeTimeline, timelineChanged);
    ImGui::EndChild();

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
        } else if (lane.ownerType == KeyframeOwnerType::Effect) {
            lane.ownerIndex = std::clamp(keyframe.ownerIndex, 0, static_cast<int>(kEffectStackStageCount) - 1);
        } else if (lane.ownerType == KeyframeOwnerType::Scene) {
            lane.ownerIndex = 0;
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
    const float minTrackHeight = std::max(72.0f, 42.0f + std::max(1, static_cast<int>(timelineLanes.size())) * laneSpacing);
    const float trackHeight = std::max(minTrackHeight, ImGui::GetContentRegionAvail().y);
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
    const float topPad = 24.0f;
    const float bottomPad = 18.0f;
    const float trackLeft = trackMin.x + leftPad;
    const float trackRight = trackMax.x - rightPad;
    const float usableWidth = std::max(1.0f, trackRight - trackLeft);
    timelineZoom_ = std::clamp(timelineZoom_, kTimelineZoomMin, kTimelineZoomMax);
    const double visibleSpanFrames = std::max(1.0, timelineRangeSpanFrames / static_cast<double>(timelineZoom_));
    if (timelineZoom_ <= kTimelineZoomMin + 1.0e-4f) {
        timelineViewCenterFrame_ = timelineRangeMidpoint();
    }
    clampTimelineViewCenter(visibleSpanFrames);
    const double halfVisibleSpan = visibleSpanFrames * 0.5;
    const double visibleStartFrame = std::max(timelineRangeStartFrame, timelineViewCenterFrame_ - halfVisibleSpan);
    const double visibleEndFrame = std::min(timelineRangeEndFrame, visibleStartFrame + visibleSpanFrames);
    const double displayedSpanFrames = std::max(1.0, visibleEndFrame - visibleStartFrame);
    const auto frameToX = [&](const double frameValue) {
        const double normalized = (frameValue - visibleStartFrame) / displayedSpanFrames;
        return trackLeft + static_cast<float>(normalized) * usableWidth;
    };
    const auto xToFrame = [&](const float x, const bool clampToVisibleRange = true) {
        const double normalized = Clamp((x - trackLeft) / std::max(1.0f, usableWidth), 0.0, 1.0);
        if (clampToVisibleRange) {
            return visibleStartFrame + normalized * displayedSpanFrames;
        }
        const double rawNormalized = static_cast<double>(x - trackLeft) / std::max(1.0f, usableWidth);
        return visibleStartFrame + rawNormalized * displayedSpanFrames;
    };
    const auto timelineTickStep = [&](const double span) {
        static const int kTickSteps[] = {
            1, 2, 5, 10, 20, 50, 100, 200, 500,
            1000, 2000, 5000, 10000, 20000, 50000, 100000
        };
        const double targetStep = std::max(1.0, span / 10.0);
        for (const int step : kTickSteps) {
            if (step >= targetStep) {
                return step;
            }
        }
        int step = 100000;
        while (static_cast<double>(step) < targetStep) {
            step *= 10;
        }
        return step;
    };
    const int tickStep = timelineTickStep(displayedSpanFrames);
    const int firstTick = static_cast<int>(std::ceil(visibleStartFrame / static_cast<double>(tickStep))) * tickStep;
    const float tickHeight = 6.0f;
    drawList->AddLine(
        ImVec2(trackLeft, trackMin.y + topPad),
        ImVec2(trackRight, trackMin.y + topPad),
        guide,
        1.0f);
    for (int frame = firstTick; static_cast<double>(frame) <= visibleEndFrame; frame += tickStep) {
        const float x = frameToX(static_cast<double>(frame));
        drawList->AddLine(ImVec2(x, trackMin.y + topPad), ImVec2(x, trackMax.y - bottomPad), guide);
        drawList->AddLine(ImVec2(x, trackMin.y + topPad - tickHeight), ImVec2(x, trackMin.y + topPad), guide);
        const float labelY = trackMin.y + 4.0f;
        PushMonospaceFont();
        drawList->AddText(
            ImVec2(x + 3.0f, labelY),
            ImGui::GetColorU32(ImVec4(0.60f, 0.58f, 0.54f, 0.90f)),
            std::to_string(frame).c_str());
        PopMonospaceFont();
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
    if (trackHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        timelineDraggingView_ = true;
        timelineViewDragLastMouseX_ = io.MousePos.x;
    }
    if (trackHovered && !io.WantTextInput && io.KeyCtrl && io.MouseWheel != 0.0f) {
        zoomTimelineView(io.MouseWheel > 0.0f ? kTimelineZoomStep : 1.0f / kTimelineZoomStep, xToFrame(io.MousePos.x));
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        timelineDraggingView_ = false;
    }
    if (timelineDraggingView_) {
        const float mouseDeltaX = io.MousePos.x - timelineViewDragLastMouseX_;
        timelineViewDragLastMouseX_ = io.MousePos.x;
        if (std::abs(mouseDeltaX) > 0.0f) {
            timelineViewCenterFrame_ -= static_cast<double>(mouseDeltaX) / std::max(1.0f, usableWidth) * displayedSpanFrames;
            clampTimelineViewCenter(visibleSpanFrames);
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (!timelineDraggingView_ && trackHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        selectedTimelineKeyframe_ = -1;
        for (std::size_t index = 0; index < scene_.keyframes.size(); ++index) {
            const float markerX = frameToX(static_cast<double>(scene_.keyframes[index].frame));
            const float markerY = markerYForLane(laneIndexForKeyframe(scene_.keyframes[index]));
            if (markerX >= trackLeft - 7.0f
                && markerX <= trackRight + 7.0f
                && std::abs(mouse.x - markerX) <= 7.0f
                && std::abs(mouse.y - markerY) <= 8.0f) {
                selectedTimelineKeyframe_ = static_cast<int>(index);
                timelineDraggingKeyframe_ = true;
                PushUndoState(scene_);
                break;
            }
        }
        if (selectedTimelineKeyframe_ < 0) {
            timelineDraggingPlayhead_ = true;
            SetTimelineFrame(xToFrame(mouse.x), false);
        }
    }
    if (!timelineDraggingView_ && trackHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (std::size_t index = 0; index < scene_.keyframes.size(); ++index) {
            const float markerX = frameToX(static_cast<double>(scene_.keyframes[index].frame));
            const float markerY = markerYForLane(laneIndexForKeyframe(scene_.keyframes[index]));
            if (std::abs(mouse.x - markerX) <= 7.0f && std::abs(mouse.y - markerY) <= 8.0f) {
                selectedTimelineKeyframe_ = static_cast<int>(index);
                ImGui::OpenPopup("##timeline_keyframe_context");
                break;
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        timelineDraggingPlayhead_ = false;
        timelineDraggingKeyframe_ = false;
    }
    if (timelineDraggingPlayhead_) {
        SetTimelineFrame(xToFrame(ImGui::GetIO().MousePos.x), false);
    } else if (timelineDraggingKeyframe_ && selectedTimelineKeyframe_ >= 0 && selectedTimelineKeyframe_ < static_cast<int>(scene_.keyframes.size())) {
        const int targetFrame = static_cast<int>(std::round(xToFrame(ImGui::GetIO().MousePos.x)));
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
        if (markerX < trackLeft - 8.0f || markerX > trackRight + 8.0f) {
            continue;
        }
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
    if (ImGui::BeginPopup("##timeline_keyframe_context")) {
        if (selectedTimelineKeyframe_ >= 0 && selectedTimelineKeyframe_ < static_cast<int>(scene_.keyframes.size())) {
            const SceneKeyframe keyframe = scene_.keyframes[static_cast<std::size_t>(selectedTimelineKeyframe_)];
            ImGui::TextDisabled("Frame %d", keyframe.frame);
            ImGui::Separator();
            if (ImGui::MenuItem("Snap Playhead Here")) {
                SetTimelineFrame(static_cast<double>(keyframe.frame), false);
            }
            if (ImGui::MenuItem("Duplicate Keyframe")) {
                PushUndoState(scene_);
                SceneKeyframe copy = keyframe;
                int duplicateFrame = keyframe.frame + 1;
                while (FindKeyframeIndex(scene_, duplicateFrame, keyframe.ownerType, keyframe.ownerIndex) >= 0) {
                    ++duplicateFrame;
                }
                copy.frame = duplicateFrame;
                scene_.keyframes.push_back(std::move(copy));
                SortKeyframes(scene_);
                selectedTimelineKeyframe_ = FindKeyframeIndex(scene_, duplicateFrame, keyframe.ownerType, keyframe.ownerIndex);
                RefreshTimelinePose();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Keyframe")) {
                RemoveSelectedOrCurrentKeyframe();
            }
        }
        ImGui::EndPopup();
    }

    const float playheadX = frameToX(scene_.timelineFrame);
    if (playheadX >= trackLeft - 6.0f && playheadX <= trackRight + 6.0f) {
        drawList->AddLine(ImVec2(playheadX, trackMin.y + topPad), ImVec2(playheadX, trackMax.y - bottomPad), playhead, 2.0f);
        drawList->AddTriangleFilled(
            ImVec2(playheadX - 5.0f, trackMin.y + 8.0f),
            ImVec2(playheadX + 5.0f, trackMin.y + 8.0f),
            ImVec2(playheadX, trackMin.y + 16.0f),
            playhead);
    }
    ImGui::EndChild();

    SyncCurrentKeyframeFromScene();
    ImGui::End();
}

void AppWindow::DrawPreviewPanel() {
    if (!previewPanelOpen_) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Preview", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    DrawDockPanelTabContextMenu("Preview", previewPanelOpen_);
    const Scene defaultScene = CreateDefaultScene();
    const int previewGridColumns = ImGui::GetContentRegionAvail().x >= ScaleUi(kWidePreviewGridMinWidth) ? 2 : 1;
    const std::uint32_t minInteractiveIterations = 10000;
    const std::uint32_t maxInteractiveIterations = 2000000;
    const std::uint32_t minIterations = 20000;
    const std::uint32_t maxIterations = 100000000u;
    const auto setPreviewColumnItemWidth = [&]() {
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    const auto drawPreviewFieldLabel = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
    };
    const auto beginPreviewGrid = [&](const char* id) {
        return ImGui::BeginTable(id, previewGridColumns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings);
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

        ImGui::TableNextColumn();
        drawPreviewFieldLabel("Render Iterations");
        setPreviewColumnItemWidth();
        const Scene beforePreview = scene_;
        const bool previewChanged = SliderScalarWithInput("##preview_render_iterations", ImGuiDataType_U32, &scene_.previewIterations, &minIterations, &maxIterations, "%u")
            || ResetValueOnDoubleClick(scene_.previewIterations, defaultScene.previewIterations);
        if (previewChanged) {
            MarkViewportDirty(DeterminePreviewResetReason(beforePreview, scene_));
        }
        CaptureWidgetUndo(beforePreview, previewChanged);

        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Used while navigating the viewport.");
    ImGui::PopTextWrapPos();

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
            MarkViewportDirty(DeterminePreviewResetReason(beforeBackground, scene_));
        }
        CaptureWidgetUndo(beforeBackground, backgroundChanged);

        ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PopTextWrapPos();

    const int previewWidth = std::max(1, UploadedViewportWidth());
    const int previewHeight = std::max(1, UploadedViewportHeight());
    const PreviewProgressState& preview = previewProgress_;
    std::ostringstream previewSummary;
    previewSummary
        << "Preview " << previewWidth << "x" << previewHeight
        << " | Iter " << preview.displayedIterations << "/" << preview.targetIterations
        << " | " << PreviewRenderDeviceLabel(preview.presentation.device)
        << " " << PreviewRenderContentLabel(preview.presentation.content)
        << " " << PreviewRenderStageLabel(preview.presentation.stage)
        << " | " << PreviewProgressPhaseLabel(preview.phase);
    std::ostringstream previewFpsLine;
    previewFpsLine << "FPS " << std::fixed << std::setprecision(1) << previewFpsSmoothed_;
    ImGui::Separator();
    PushMonospaceFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::SetWindowFontScale(0.94f);
    ImGui::TextDisabled("%s", previewSummary.str().c_str());
    ImGui::TextDisabled("%s", previewFpsLine.str().c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopTextWrapPos();
    PopMonospaceFont();
    ImGui::End();
}

void AppWindow::DrawEffectsPanel() {
    if (!effectsPanelOpen_) {
        effectsPanelActive_ = false;
        return;
    }

    const UiTheme& theme = GetUiTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Effects", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    effectsPanelActive_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    DrawDockPanelTabContextMenu("Effects", effectsPanelOpen_);
    const ImGuiIO& effectsIo = ImGui::GetIO();
    bool rightClickStartedOnEffectControl = false;
    const auto markEffectControlRect = [&](const ImRect& rect) {
        if (rect.Contains(effectsIo.MouseClickedPos[ImGuiMouseButton_Right])) {
            rightClickStartedOnEffectControl = true;
        }
    };
    NormalizeEffectSelections();

    const Scene defaultScene = CreateDefaultScene();

    if (scene_.effectStack.empty()) {
        ImGui::Spacing();
        ImGui::Spacing();
        const float availWidth = ImGui::GetContentRegionAvail().x;
        const float textWidth = ImGui::CalcTextSize("No effects").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
        ImGui::TextDisabled("No effects");
        ImGui::Spacing();
    }

    ImGui::Spacing();
    const float addButtonWidth = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(WithAlpha(theme.accent, 0.18f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(WithAlpha(theme.accent, 0.35f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(WithAlpha(theme.accent, 0.50f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f));
    if (ImGui::Button("+ Add Effect", ImVec2(addButtonWidth, 0.0f))) {
        ImGui::OpenPopup("##add_effect_popup");
        effectsPopupSearchText_[0] = '\0';
    }
    markEffectControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 100.0f), ImVec2(400.0f, 400.0f));
    if (ImGui::BeginPopup("##add_effect_popup")) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##effect_search", "Search effects...", effectsPopupSearchText_, sizeof(effectsPopupSearchText_));
        ImGui::Spacing();

        const std::string filterLower = [&]() {
            std::string s(effectsPopupSearchText_);
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }();

        std::array<bool, kEffectStackStageCount> alreadyAdded {};
        for (const EffectStackStage stage : scene_.effectStack) {
            const std::size_t idx = static_cast<std::size_t>(stage);
            if (idx < kEffectStackStageCount) alreadyAdded[idx] = true;
        }

        bool anyVisible = false;
        for (const EffectStackStage stage : kAllEffectStages) {
            const char* displayName = EffectStageDisplayName(stage);
            if (!filterLower.empty()) {
                std::string nameLower(displayName);
                for (char& c : nameLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (nameLower.find(filterLower) == std::string::npos) {
                    continue;
                }
            }
            anyVisible = true;
            const bool added = alreadyAdded[static_cast<std::size_t>(stage)];
            if (added) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
            const bool selected = ImGui::Selectable(displayName, false, added ? ImGuiSelectableFlags_Disabled : 0);
            ImGui::PopStyleVar();
            if (added) {
                ImGui::PopStyleColor();
            }
            if (selected && !added) {
                const Scene beforeAdd = scene_;
                scene_.effectStack.push_back(stage);
                EnableEffectStage(scene_, stage, true);
                ClearLayerSelections();
                SelectSingleEffect(static_cast<int>(scene_.effectStack.size()) - 1);
                SyncCurrentKeyframeFromScene();
                PushUndoState(beforeAdd, std::string("Add ") + displayName);
                MarkViewportDirty(DeterminePreviewResetReason(beforeAdd, scene_));
                ImGui::CloseCurrentPopup();
            }
        }
        if (!anyVisible) {
            ImGui::TextDisabled("No matching effects");
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    int requestedMoveFrom = -1;
    int requestedMoveTo = -1;
    bool requestOpenEffectsContextMenu = false;
    const auto drawEffectFieldLabel = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
    };
    const auto setEffectItemWidth = [&]() {
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    const auto beginEffectFieldGrid = [&](const char* id) {
        const int columns = ImGui::GetContentRegionAvail().x >= 360.0f ? 2 : 1;
        return ImGui::BeginTable(id, columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings);
    };
    const auto beginEffectSubsection = [&](const char* id, const char* title, const bool defaultOpen = true) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetColorU32(WithAlpha(theme.frameBackgroundHover, 0.26f)));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetColorU32(WithAlpha(Mix(theme.frameBackgroundHover, theme.textMuted, 0.30f), 0.50f)));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetColorU32(WithAlpha(theme.frameBackgroundActive, 0.28f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 5.0f));
        ImGui::SetNextItemOpen(defaultOpen, ImGuiCond_Once);
        return ImGui::TreeNodeEx(id, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s", title);
    };
    const auto endEffectSubsection = [&](const bool open) {
        (void)open;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    };
    bool effectContentIndented = false;
    struct EffectHeaderState {
        bool contentOpen = false;
        bool resetPressed = false;
        bool enabledChanged = false;
        bool enabled = false;
    };
    const auto beginEffectCard = [&](const char* id, const char* title, const bool defaultOpen, const bool enabled, const int stackIndex) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID(id);

        const bool selected = IsEffectSelected(stackIndex);
        const ImVec4 headerColor = selected
            ? (enabled
                ? Mix(theme.accentSurface, theme.accent, 0.18f)
                : Mix(Mix(theme.accentSurface, theme.accent, 0.18f), theme.panelBackgroundAlt, 0.45f))
            : (enabled
                ? Mix(theme.panelBackgroundAlt, theme.accentSurface, 0.72f)
                : Mix(theme.panelBackgroundAlt, theme.accentSurface, 0.54f));
        const ImVec4 hoverColor = selected
            ? (enabled
                ? Mix(theme.accentSurface, theme.accentHover, 0.48f)
                : Mix(Mix(theme.accentSurface, theme.accentHover, 0.48f), theme.frameBackgroundHover, 0.22f))
            : (enabled
                ? Mix(theme.panelBackgroundAlt, theme.frameBackgroundHover, 0.56f)
                : Mix(theme.panelBackgroundAlt, theme.frameBackgroundHover, 0.42f));
        const ImVec4 activeColor = selected
            ? (enabled
                ? Mix(theme.accentSurface, theme.accentHover, 0.36f)
                : Mix(Mix(theme.accentSurface, theme.accentHover, 0.36f), theme.frameBackgroundActive, 0.35f))
            : (enabled
                ? Mix(theme.panelBackgroundAlt, theme.frameBackgroundActive, 0.60f)
                : Mix(theme.panelBackgroundAlt, theme.frameBackgroundActive, 0.46f));
        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, activeColor);
        if (!enabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
        ImGui::SetNextItemOpen(defaultOpen, ImGuiCond_Once);
        ImGui::SetNextItemAllowOverlap();
        EffectHeaderState headerState;
        headerState.contentOpen = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
        const bool headerLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool headerRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const ImRect headerRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        markEffectControlRect(headerRect);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
            ImGui::SetDragDropPayload("effects_stack_stage", &stackIndex, sizeof(stackIndex));
            ImGui::TextUnformatted(title);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("effects_stack_stage", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->IsPreview() || payload->IsDelivery()) {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const float inset = 1.5f;
                    const ImVec2 outlineMin(headerRect.Min.x + inset, headerRect.Min.y + inset);
                    const ImVec2 outlineMax(headerRect.Max.x - inset, headerRect.Max.y - inset);
                    drawList->AddRectFilled(outlineMin, outlineMax, ImGui::GetColorU32(WithAlpha(theme.accent, 0.10f)), theme.roundingMedium);
                    drawList->AddRect(outlineMin, outlineMax, ImGui::GetColorU32(WithAlpha(theme.accent, 0.92f)), theme.roundingMedium, 0, 1.5f);
                }
                const int payloadIndex = *static_cast<const int*>(payload->Data);
                if (payloadIndex != stackIndex) {
                    requestedMoveFrom = payloadIndex;
                    requestedMoveTo = stackIndex;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopStyleVar();
        if (!enabled) {
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor(3);

        // Overlay controls on the header using screen-space coords
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float headerH = headerRect.GetHeight();
        const float headerCenterY = headerRect.Min.y + headerH * 0.5f;

        // Toggle switch — rightmost, vertically centered
        const float toggleW = 36.0f;
        const float toggleH = 18.0f;
        const float toggleMarginRight = 12.0f;
        const ImVec2 toggleMin(headerRect.Max.x - toggleMarginRight - toggleW, headerCenterY - toggleH * 0.5f);
        const ImVec2 toggleMax(toggleMin.x + toggleW, toggleMin.y + toggleH);
        headerState.enabled = enabled;
        bool togglePressed = false;
        {
            ImGui::SetCursorScreenPos(toggleMin);
            togglePressed = ImGui::InvisibleButton("##toggle", ImVec2(toggleW, toggleH));
            markEffectControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
            const bool toggleHovered = ImGui::IsItemHovered();
            if (togglePressed) {
                headerState.enabled = !headerState.enabled;
                headerState.enabledChanged = true;
            }
            const float toggleR = toggleH * 0.5f;
            const float knobR = toggleR - 3.0f;
            const ImU32 trackColor = headerState.enabled
                ? ImGui::GetColorU32(WithAlpha(theme.accent, toggleHovered ? 0.95f : 0.80f))
                : ImGui::GetColorU32(WithAlpha(theme.textMuted, toggleHovered ? 0.40f : 0.28f));
            const float knobX = headerState.enabled ? toggleMax.x - toggleR : toggleMin.x + toggleR;
            dl->AddRectFilled(toggleMin, toggleMax, trackColor, toggleR);
            dl->AddRect(toggleMin, toggleMax, ImGui::GetColorU32(WithAlpha(theme.panelBackgroundAlt, headerState.enabled ? 0.14f : 0.28f)), toggleR, 0, 1.0f);
            dl->AddCircleFilled(ImVec2(knobX, headerCenterY), knobR, ImGui::GetColorU32(ImVec4(0.95f, 0.95f, 0.97f, 1.0f)));
        }

        // Reset button — left of toggle, vertically centered
        {
            const float resetPadX = 9.0f;
            const float resetPadY = 4.0f;
            const ImVec2 resetTextSize = ImGui::CalcTextSize("Reset");
            const float resetW = resetTextSize.x + resetPadX * 2.0f;
            const float resetH = resetTextSize.y + resetPadY * 2.0f;
            const float resetGap = 8.0f;
            const ImVec2 resetMin(toggleMin.x - resetGap - resetW, headerCenterY - resetH * 0.5f);
            ImGui::SetCursorScreenPos(resetMin);
            const ImVec4 resetBase = enabled
                ? Mix(theme.accentSurface, theme.panelBackgroundAlt, 0.26f)
                : Mix(theme.panelBackgroundAlt, theme.frameBackgroundHover, 0.45f);
            const ImVec4 resetHover = enabled
                ? Mix(theme.accentSurface, theme.accentHover, 0.22f)
                : Mix(theme.frameBackgroundHover, theme.textMuted, 0.18f);
            const ImVec4 resetActive = enabled
                ? Mix(theme.accentSurface, theme.accent, 0.26f)
                : Mix(theme.frameBackgroundActive, theme.textMuted, 0.14f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(WithAlpha(resetBase, 0.92f)));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(WithAlpha(resetHover, 0.98f)));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(WithAlpha(resetActive, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(WithAlpha(enabled ? theme.accent : theme.textMuted, enabled ? 0.24f : 0.18f)));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(enabled ? ImVec4(0.92f, 0.94f, 0.99f, 0.96f) : ImVec4(0.74f, 0.76f, 0.82f, 0.92f)));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(resetPadX, resetPadY));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            headerState.resetPressed = ImGui::Button("Reset");
            markEffectControlRect(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(5);
        }

        if (headerLeftClicked) {
            ClearLayerSelections();
            if (effectsIo.KeyShift) {
                SelectEffectRange(stackIndex);
            } else if (effectsIo.KeyCtrl) {
                ToggleEffectSelection(stackIndex);
            } else {
                SelectSingleEffect(stackIndex);
            }
        } else if (togglePressed || headerState.resetPressed) {
            ClearLayerSelections();
            SelectSingleEffect(stackIndex);
        }
        if (headerRightClicked) {
            ClearLayerSelections();
            SelectSingleEffect(stackIndex);
            rightClickedEffectIndex_ = stackIndex;
            requestOpenEffectsContextMenu = true;
        }

        return headerState;
    };
    const auto endEffectHeader = [&](const bool contentOpen, const char* description) {
        if (contentOpen) {
            ImGui::Indent(10.0f);
            effectContentIndented = true;
        } else {
            effectContentIndented = false;
        }
        (void)contentOpen;
        (void)description;
    };
    const auto endEffectCard = [&]() {
        if (effectContentIndented) {
            ImGui::Unindent(10.0f);
            effectContentIndented = false;
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::PopID();
    };
    const auto effectOwnerIndexForStack = [&](const int stackIndex) {
        return static_cast<int>(scene_.effectStack[static_cast<std::size_t>(stackIndex)]);
    };
    const auto prepareEffectOwnerSelection = [&](const int stackIndex) {
        if (stackIndex < 0 || stackIndex >= static_cast<int>(scene_.effectStack.size())) {
            return;
        }
        if (selectedEffects_.size() != 1 || selectedEffect_ != stackIndex || !ContainsIndex(selectedEffects_, stackIndex)) {
            SelectSingleEffect(stackIndex);
        }
    };
    const auto syncEffectTimelineChange = [&](const int stackIndex, const bool createKeyframeFromExistingTrack) {
        if (stackIndex < 0 || stackIndex >= static_cast<int>(scene_.effectStack.size())) {
            return;
        }
        prepareEffectOwnerSelection(stackIndex);
        const int ownerIndex = effectOwnerIndexForStack(stackIndex);
        if (createKeyframeFromExistingTrack && HasKeyframesForOwner(KeyframeOwnerType::Effect, ownerIndex)) {
            AutoKeyCurrentFrame(KeyframeOwnerType::Effect, ownerIndex);
        }
        SyncCurrentKeyframeFromScene(KeyframeOwnerType::Effect, ownerIndex);
    };
    const auto captureEffectEdit = [&](const Scene& before, const bool changed, const int stackIndex) {
        if (changed) {
            syncEffectTimelineChange(stackIndex, true);
            MarkViewportDirty(DeterminePreviewResetReason(before, scene_));
        }
        CaptureWidgetUndo(before, changed);
    };
    const auto commitEffectAction = [&](const Scene& before, const int stackIndex, std::string label) {
        syncEffectTimelineChange(stackIndex, true);
        PushUndoState(before, std::move(label));
        MarkViewportDirty(DeterminePreviewResetReason(before, scene_));
    };
    const auto commitEffectReset = [&](const Scene& beforeReset, const char* effectName, const int stackIndex) {
        commitEffectAction(beforeReset, stackIndex, std::string("Reset ") + effectName);
    };

    const int effectCardColumns = 1;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 3.0f));
    if (ImGui::BeginTable("##effects_panel_cards", effectCardColumns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        for (std::size_t index = 0; index < scene_.effectStack.size(); ++index) {
            switch (scene_.effectStack[index]) {
            case EffectStackStage::Denoiser: {
        const EffectHeaderState denoiserHeader = beginEffectCard(
            "##effect_card_denoiser",
            "Denoiser",
            scene_.denoiser.enabled,
            scene_.denoiser.enabled,
            static_cast<int>(index));
        const Scene beforeDenoiserEnabled = scene_;
        if (denoiserHeader.enabledChanged) {
            scene_.denoiser.enabled = denoiserHeader.enabled;
        }
        bool denoiserEnabledChanged = denoiserHeader.enabledChanged;
        if (denoiserHeader.resetPressed) {
            const bool saved = scene_.denoiser.enabled;
            scene_.denoiser = defaultScene.denoiser;
            scene_.denoiser.enabled = saved;
        }
        endEffectHeader(denoiserHeader.contentOpen, nullptr);
        if (denoiserHeader.resetPressed) {
            commitEffectReset(beforeDenoiserEnabled, "Denoiser", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeDenoiserEnabled, denoiserEnabledChanged, static_cast<int>(index));
        }
        if (denoiserHeader.contentOpen && scene_.denoiser.enabled) {
            const double denoiserStrengthMin = 0.0;
            const double denoiserStrengthMax = 1.0;
            const bool denoiserControlsOpen = beginEffectSubsection("##effect_denoiser_section", "Controls");
            if (denoiserControlsOpen) {
                if (beginEffectFieldGrid("##effect_denoiser_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Strength");
                    setEffectItemWidth();
                    const Scene beforeDenoise = scene_;
                    bool denoiseChanged = SliderScalarWithInput("##effect_denoiser_strength", ImGuiDataType_Double, &scene_.denoiser.strength, &denoiserStrengthMin, &denoiserStrengthMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.denoiser.strength, defaultScene.denoiser.strength);
                    captureEffectEdit(beforeDenoise, denoiseChanged, static_cast<int>(index));
                    ImGui::EndTable();
                }
            }
            endEffectSubsection(denoiserControlsOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::DepthOfField: {
        const EffectHeaderState dofHeader = beginEffectCard(
            "##effect_card_dof",
            "Depth of Field",
            scene_.depthOfField.enabled,
            scene_.depthOfField.enabled,
            static_cast<int>(index));
        const Scene beforeDofEnabled = scene_;
        if (dofHeader.enabledChanged) {
            scene_.depthOfField.enabled = dofHeader.enabled;
        }
        bool dofEnabledChanged = dofHeader.enabledChanged;
        if (dofHeader.resetPressed) {
            const bool saved = scene_.depthOfField.enabled;
            scene_.depthOfField = defaultScene.depthOfField;
            scene_.depthOfField.enabled = saved;
        }
        endEffectHeader(dofHeader.contentOpen, nullptr);
        if (dofHeader.resetPressed) {
            commitEffectReset(beforeDofEnabled, "Depth of Field", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeDofEnabled, dofEnabledChanged, static_cast<int>(index));
        }
        if (dofHeader.contentOpen && scene_.depthOfField.enabled) {
            const double focusDepthMin = 0.0;
            const double focusDepthMax = 1.0;
            const double focusRangeMin = 0.01;
            const double focusRangeMax = 0.4;
            const double blurStrengthMin = 0.0;
            const double blurStrengthMax = 1.0;
            const bool dofFocusOpen = beginEffectSubsection("##effect_dof_section", "Focus");
            if (dofFocusOpen) {
                if (beginEffectFieldGrid("##effect_dof_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Focus Depth");
                    setEffectItemWidth();
                    Scene beforeDof = scene_;
                    bool dofChanged = SliderScalarWithInput("##effect_dof_focus_depth", ImGuiDataType_Double, &scene_.depthOfField.focusDepth, &focusDepthMin, &focusDepthMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.depthOfField.focusDepth, defaultScene.depthOfField.focusDepth);
                    captureEffectEdit(beforeDof, dofChanged, static_cast<int>(index));

                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Focus Range");
                    setEffectItemWidth();
                    beforeDof = scene_;
                    dofChanged = SliderScalarWithInput("##effect_dof_focus_range", ImGuiDataType_Double, &scene_.depthOfField.focusRange, &focusRangeMin, &focusRangeMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.depthOfField.focusRange, defaultScene.depthOfField.focusRange);
                    captureEffectEdit(beforeDof, dofChanged, static_cast<int>(index));

                    if (ImGui::TableGetColumnCount() > 1) {
                        ImGui::TableNextRow();
                    }
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Blur Strength");
                    setEffectItemWidth();
                    beforeDof = scene_;
                    dofChanged = SliderScalarWithInput("##effect_dof_blur_strength", ImGuiDataType_Double, &scene_.depthOfField.blurStrength, &blurStrengthMin, &blurStrengthMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.depthOfField.blurStrength, defaultScene.depthOfField.blurStrength);
                    captureEffectEdit(beforeDof, dofChanged, static_cast<int>(index));
                    ImGui::EndTable();
                }
            }
            endEffectSubsection(dofFocusOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::PostProcess: {
        const EffectHeaderState glowHeader = beginEffectCard(
            "##effect_card_post_process",
            "Glow",
            scene_.postProcess.enabled,
            scene_.postProcess.enabled,
            static_cast<int>(index));
        const Scene beforePostProcessEnabled = scene_;
        if (glowHeader.enabledChanged) {
            scene_.postProcess.enabled = glowHeader.enabled;
        }
        bool postProcessEnabledChanged = glowHeader.enabledChanged;
        if (glowHeader.resetPressed) {
            scene_.postProcess.bloomIntensity = defaultScene.postProcess.bloomIntensity;
            scene_.postProcess.bloomThreshold = defaultScene.postProcess.bloomThreshold;
        }
        endEffectHeader(glowHeader.contentOpen, nullptr);
        if (glowHeader.resetPressed) {
            commitEffectReset(beforePostProcessEnabled, "Glow", static_cast<int>(index));
        } else {
            captureEffectEdit(beforePostProcessEnabled, postProcessEnabledChanged, static_cast<int>(index));
        }
        if (glowHeader.contentOpen && scene_.postProcess.enabled) {
            const double bloomIntensityMin = 0.0;
            const double bloomIntensityMax = 2.0;
            const double bloomThresholdMin = 0.0;
            const double bloomThresholdMax = 2.0;
            const bool glowBloomOpen = beginEffectSubsection("##effect_glow_section", "Bloom");
            if (glowBloomOpen) {
                if (beginEffectFieldGrid("##effect_post_process_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Intensity");
                    setEffectItemWidth();
                    Scene beforePP = scene_;
                    bool ppChanged = SliderScalarWithInput("##effect_pp_bloom_intensity", ImGuiDataType_Double, &scene_.postProcess.bloomIntensity, &bloomIntensityMin, &bloomIntensityMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.bloomIntensity, defaultScene.postProcess.bloomIntensity);
                    captureEffectEdit(beforePP, ppChanged, static_cast<int>(index));

                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Threshold");
                    setEffectItemWidth();
                    beforePP = scene_;
                    ppChanged = SliderScalarWithInput("##effect_pp_bloom_threshold", ImGuiDataType_Double, &scene_.postProcess.bloomThreshold, &bloomThresholdMin, &bloomThresholdMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.bloomThreshold, defaultScene.postProcess.bloomThreshold);
                    captureEffectEdit(beforePP, ppChanged, static_cast<int>(index));

                    ImGui::EndTable();
                }
            }
            endEffectSubsection(glowBloomOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::ChromaticAberration: {
        const EffectHeaderState chromaticHeader = beginEffectCard(
            "##effect_card_chromatic",
            "Chromatic Aberration",
            scene_.postProcess.chromaticAberrationEnabled,
            scene_.postProcess.chromaticAberrationEnabled,
            static_cast<int>(index));
        const Scene beforeChromaticEnabled = scene_;
        if (chromaticHeader.enabledChanged) {
            scene_.postProcess.chromaticAberrationEnabled = chromaticHeader.enabled;
        }
        bool chromaticEnabledChanged = chromaticHeader.enabledChanged;
        if (chromaticHeader.resetPressed) {
            scene_.postProcess.chromaticAberration = defaultScene.postProcess.chromaticAberration;
        }
        endEffectHeader(chromaticHeader.contentOpen, nullptr);
        if (chromaticHeader.resetPressed) {
            commitEffectReset(beforeChromaticEnabled, "Chromatic Aberration", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeChromaticEnabled, chromaticEnabledChanged, static_cast<int>(index));
        }
        if (chromaticHeader.contentOpen && scene_.postProcess.chromaticAberrationEnabled) {
            const double chromaticMin = 0.0;
            const double chromaticMax = 5.0;
            const bool chromaticLensOpen = beginEffectSubsection("##effect_chromatic_section", "Lens");
            if (chromaticLensOpen) {
                if (beginEffectFieldGrid("##effect_chromatic_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Amount");
                    setEffectItemWidth();
                    Scene beforeChromatic = scene_;
                    bool chromaticChanged = SliderScalarWithInput("##effect_chromatic_amount", ImGuiDataType_Double, &scene_.postProcess.chromaticAberration, &chromaticMin, &chromaticMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.chromaticAberration, defaultScene.postProcess.chromaticAberration);
                    captureEffectEdit(beforeChromatic, chromaticChanged, static_cast<int>(index));
                    ImGui::EndTable();
                }
            }
            endEffectSubsection(chromaticLensOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::ColorTemperature: {
        const EffectHeaderState temperatureHeader = beginEffectCard(
            "##effect_card_temperature",
            "Temperature",
            scene_.postProcess.colorTemperatureEnabled,
            scene_.postProcess.colorTemperatureEnabled,
            static_cast<int>(index));
        const Scene beforeTemperatureEnabled = scene_;
        if (temperatureHeader.enabledChanged) {
            scene_.postProcess.colorTemperatureEnabled = temperatureHeader.enabled;
        }
        bool temperatureEnabledChanged = temperatureHeader.enabledChanged;
        if (temperatureHeader.resetPressed) {
            scene_.postProcess.colorTemperature = defaultScene.postProcess.colorTemperature;
        }
        endEffectHeader(temperatureHeader.contentOpen, nullptr);
        if (temperatureHeader.resetPressed) {
            commitEffectReset(beforeTemperatureEnabled, "Temperature", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeTemperatureEnabled, temperatureEnabledChanged, static_cast<int>(index));
        }
        if (temperatureHeader.contentOpen && scene_.postProcess.colorTemperatureEnabled) {
            const double colorTempMin = 2000.0;
            const double colorTempMax = 12000.0;
            const bool temperatureBalanceOpen = beginEffectSubsection("##effect_temperature_section", "Balance");
            if (temperatureBalanceOpen) {
                if (beginEffectFieldGrid("##effect_temperature_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Kelvin");
                    setEffectItemWidth();
                    Scene beforeTemperature = scene_;
                    bool temperatureChanged = SliderScalarWithInput("##effect_temperature_value", ImGuiDataType_Double, &scene_.postProcess.colorTemperature, &colorTempMin, &colorTempMax, "%.0f K")
                        || ResetValueOnDoubleClick(scene_.postProcess.colorTemperature, defaultScene.postProcess.colorTemperature);
                    captureEffectEdit(beforeTemperature, temperatureChanged, static_cast<int>(index));
                    ImGui::EndTable();
                }
            }
            endEffectSubsection(temperatureBalanceOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::Saturation: {
        const EffectHeaderState saturationHeader = beginEffectCard(
            "##effect_card_saturation",
            "Saturation",
            scene_.postProcess.saturationEnabled,
            scene_.postProcess.saturationEnabled,
            static_cast<int>(index));
        const Scene beforeSaturationEnabled = scene_;
        if (saturationHeader.enabledChanged) {
            scene_.postProcess.saturationEnabled = saturationHeader.enabled;
        }
        bool saturationEnabledChanged = saturationHeader.enabledChanged;
        if (saturationHeader.resetPressed) {
            scene_.postProcess.saturationBoost = defaultScene.postProcess.saturationBoost;
            scene_.postProcess.saturationVibrance = defaultScene.postProcess.saturationVibrance;
        }
        endEffectHeader(saturationHeader.contentOpen, nullptr);
        if (saturationHeader.resetPressed) {
            commitEffectReset(beforeSaturationEnabled, "Saturation", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeSaturationEnabled, saturationEnabledChanged, static_cast<int>(index));
        }
        if (saturationHeader.contentOpen && scene_.postProcess.saturationEnabled) {
            const double saturationMin = -1.0;
            const double saturationMax = 1.0;
            const double vibranceMin = -1.0;
            const double vibranceMax = 1.0;
            const bool saturationColorOpen = beginEffectSubsection("##effect_saturation_section", "Color");
            if (saturationColorOpen) {
                if (beginEffectFieldGrid("##effect_saturation_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Boost");
                    setEffectItemWidth();
                    Scene beforeSaturation = scene_;
                    bool saturationChanged = SliderScalarWithInput("##effect_saturation_amount", ImGuiDataType_Double, &scene_.postProcess.saturationBoost, &saturationMin, &saturationMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.saturationBoost, defaultScene.postProcess.saturationBoost);
                    captureEffectEdit(beforeSaturation, saturationChanged, static_cast<int>(index));

                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Vibrance");
                    setEffectItemWidth();
                    beforeSaturation = scene_;
                    saturationChanged = SliderScalarWithInput("##effect_saturation_vibrance", ImGuiDataType_Double, &scene_.postProcess.saturationVibrance, &vibranceMin, &vibranceMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.saturationVibrance, defaultScene.postProcess.saturationVibrance);
                    captureEffectEdit(beforeSaturation, saturationChanged, static_cast<int>(index));

                    ImGui::EndTable();
                }
            }
            endEffectSubsection(saturationColorOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::ToneMapping: {
        const EffectHeaderState toneMappingHeader = beginEffectCard(
            "##effect_card_tonemap",
            "Tone Mapping",
            scene_.postProcess.toneMappingEnabled,
            scene_.postProcess.toneMappingEnabled,
            static_cast<int>(index));
        const Scene beforeToneMappingEnabled = scene_;
        if (toneMappingHeader.enabledChanged) {
            scene_.postProcess.toneMappingEnabled = toneMappingHeader.enabled;
        }
        bool toneMappingEnabledChanged = toneMappingHeader.enabledChanged;
        if (toneMappingHeader.resetPressed) {
            scene_.postProcess.acesToneMap = defaultScene.postProcess.acesToneMap;
        }
        endEffectHeader(toneMappingHeader.contentOpen, nullptr);
        if (toneMappingHeader.resetPressed) {
            commitEffectReset(beforeToneMappingEnabled, "Tone Mapping", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeToneMappingEnabled, toneMappingEnabledChanged, static_cast<int>(index));
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::FilmGrain: {
        const EffectHeaderState filmGrainHeader = beginEffectCard(
            "##effect_card_film_grain",
            "Film Grain",
            scene_.postProcess.filmGrainEnabled,
            scene_.postProcess.filmGrainEnabled,
            static_cast<int>(index));
        const Scene beforeFilmGrainEnabled = scene_;
        if (filmGrainHeader.enabledChanged) {
            scene_.postProcess.filmGrainEnabled = filmGrainHeader.enabled;
        }
        bool filmGrainEnabledChanged = filmGrainHeader.enabledChanged;
        if (filmGrainHeader.resetPressed) {
            scene_.postProcess.filmGrain = defaultScene.postProcess.filmGrain;
            scene_.postProcess.filmGrainScale = defaultScene.postProcess.filmGrainScale;
        }
        endEffectHeader(filmGrainHeader.contentOpen, nullptr);
        if (filmGrainHeader.resetPressed) {
            commitEffectReset(beforeFilmGrainEnabled, "Film Grain", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeFilmGrainEnabled, filmGrainEnabledChanged, static_cast<int>(index));
        }
        if (filmGrainHeader.contentOpen && scene_.postProcess.filmGrainEnabled) {
            const double filmGrainMin = 0.0;
            const double filmGrainMax = 1.0;
            const double filmGrainScaleMin = 0.1;
            const double filmGrainScaleMax = 4.0;
            const bool filmTextureOpen = beginEffectSubsection("##effect_film_grain_section", "Texture");
            if (filmTextureOpen) {
                if (beginEffectFieldGrid("##effect_film_grain_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Amount");
                    setEffectItemWidth();
                    Scene beforeFilmGrain = scene_;
                    bool filmGrainChanged = SliderScalarWithInput("##effect_film_grain_amount", ImGuiDataType_Double, &scene_.postProcess.filmGrain, &filmGrainMin, &filmGrainMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.filmGrain, defaultScene.postProcess.filmGrain);
                    captureEffectEdit(beforeFilmGrain, filmGrainChanged, static_cast<int>(index));

                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Scale");
                    setEffectItemWidth();
                    beforeFilmGrain = scene_;
                    filmGrainChanged = SliderScalarWithInput("##effect_film_grain_scale", ImGuiDataType_Double, &scene_.postProcess.filmGrainScale, &filmGrainScaleMin, &filmGrainScaleMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.filmGrainScale, defaultScene.postProcess.filmGrainScale);
                    captureEffectEdit(beforeFilmGrain, filmGrainChanged, static_cast<int>(index));

                    ImGui::EndTable();
                }
            }
            endEffectSubsection(filmTextureOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::Vignette: {
        const EffectHeaderState vignetteHeader = beginEffectCard(
            "##effect_card_vignette",
            "Vignette",
            scene_.postProcess.vignetteEnabled,
            scene_.postProcess.vignetteEnabled,
            static_cast<int>(index));
        const Scene beforeVignetteEnabled = scene_;
        if (vignetteHeader.enabledChanged) {
            scene_.postProcess.vignetteEnabled = vignetteHeader.enabled;
        }
        bool vignetteEnabledChanged = vignetteHeader.enabledChanged;
        if (vignetteHeader.resetPressed) {
            scene_.postProcess.vignetteIntensity = defaultScene.postProcess.vignetteIntensity;
            scene_.postProcess.vignetteRoundness = defaultScene.postProcess.vignetteRoundness;
        }
        endEffectHeader(vignetteHeader.contentOpen, nullptr);
        if (vignetteHeader.resetPressed) {
            commitEffectReset(beforeVignetteEnabled, "Vignette", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeVignetteEnabled, vignetteEnabledChanged, static_cast<int>(index));
        }
        if (vignetteHeader.contentOpen && scene_.postProcess.vignetteEnabled) {
            const double vignetteIntensityMin = 0.0;
            const double vignetteIntensityMax = 1.5;
            const double vignetteRoundnessMin = 0.0;
            const double vignetteRoundnessMax = 1.0;
            const bool vignetteShapeOpen = beginEffectSubsection("##effect_vignette_section", "Shape");
            if (vignetteShapeOpen) {
                if (beginEffectFieldGrid("##effect_vignette_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Intensity");
                    setEffectItemWidth();
                    Scene beforeVignette = scene_;
                    bool vignetteChanged = SliderScalarWithInput("##effect_vignette_intensity", ImGuiDataType_Double, &scene_.postProcess.vignetteIntensity, &vignetteIntensityMin, &vignetteIntensityMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.vignetteIntensity, defaultScene.postProcess.vignetteIntensity);
                    captureEffectEdit(beforeVignette, vignetteChanged, static_cast<int>(index));

                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Roundness");
                    setEffectItemWidth();
                    beforeVignette = scene_;
                    vignetteChanged = SliderScalarWithInput("##effect_vignette_roundness", ImGuiDataType_Double, &scene_.postProcess.vignetteRoundness, &vignetteRoundnessMin, &vignetteRoundnessMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.vignetteRoundness, defaultScene.postProcess.vignetteRoundness);
                    captureEffectEdit(beforeVignette, vignetteChanged, static_cast<int>(index));
                    ImGui::EndTable();
                }
            }
            endEffectSubsection(vignetteShapeOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::Curves: {
        const EffectHeaderState curvesHeader = beginEffectCard(
            "##effect_card_curves",
            "Curves",
            scene_.postProcess.curvesEnabled,
            scene_.postProcess.curvesEnabled,
            static_cast<int>(index));
        const Scene beforeCurvesEnabled = scene_;
        if (curvesHeader.enabledChanged) {
            scene_.postProcess.curvesEnabled = curvesHeader.enabled;
        }
        bool curvesEnabledChanged = curvesHeader.enabledChanged;
        if (curvesHeader.resetPressed) {
            scene_.postProcess.curveBlackPoint = defaultScene.postProcess.curveBlackPoint;
            scene_.postProcess.curveWhitePoint = defaultScene.postProcess.curveWhitePoint;
            scene_.postProcess.curveGamma = defaultScene.postProcess.curveGamma;
            scene_.postProcess.curveUseCustom = defaultScene.postProcess.curveUseCustom;
            scene_.postProcess.curveControlPoints.clear();
        }
        endEffectHeader(curvesHeader.contentOpen, nullptr);
        if (curvesHeader.resetPressed) {
            commitEffectReset(beforeCurvesEnabled, "Curves", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeCurvesEnabled, curvesEnabledChanged, static_cast<int>(index));
        }
        if (curvesHeader.contentOpen && scene_.postProcess.curvesEnabled) {
            const double blackPointMin = 0.0;
            const double blackPointMax = 0.8;
            const double whitePointMin = 0.2;
            const double whitePointMax = 1.0;
            const double curveGammaMin = 0.3;
            const double curveGammaMax = 3.0;
            const bool curvesTypeOpen = beginEffectSubsection("##effect_curves_type_section", "Type");
            if (curvesTypeOpen) {
                if (ImGui::RadioButton("Levels", !scene_.postProcess.curveUseCustom)) {
                    const Scene beforeCurveMode = scene_;
                    scene_.postProcess.curveUseCustom = false;
                    commitEffectAction(beforeCurveMode, static_cast<int>(index), "Switch to Levels");
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Custom Curve", scene_.postProcess.curveUseCustom)) {
                    const Scene beforeCurveMode = scene_;
                    scene_.postProcess.curveUseCustom = true;
                    if (scene_.postProcess.curveControlPoints.empty()) {
                        scene_.postProcess.curveControlPoints = {{0.0, 0.0}, {0.25, 0.25}, {0.5, 0.5}, {0.75, 0.75}, {1.0, 1.0}};
                    }
                    commitEffectAction(beforeCurveMode, static_cast<int>(index), "Switch to Custom Curve");
                }
            }
            endEffectSubsection(curvesTypeOpen);

            if (scene_.postProcess.curveUseCustom) {
                const bool curvesEditorOpen = beginEffectSubsection("##effect_curves_editor_section", "Curve Editor");
                if (curvesEditorOpen) {
                    auto& controlPoints = scene_.postProcess.curveControlPoints;
                    if (controlPoints.size() < 2) {
                        controlPoints = {{0.0, 0.0}, {0.25, 0.25}, {0.5, 0.5}, {0.75, 0.75}, {1.0, 1.0}};
                    }

                    for (Vec2& point : controlPoints) {
                        point.x = std::clamp(point.x, 0.0, 1.0);
                        point.y = std::clamp(point.y, 0.0, 1.0);
                    }
                    std::sort(controlPoints.begin(), controlPoints.end(), [](const Vec2& left, const Vec2& right) {
                        return left.x < right.x;
                    });
                    controlPoints.front().x = 0.0;
                    controlPoints.back().x = 1.0;

                    const float availableWidth = ImGui::GetContentRegionAvail().x;
                    const float canvasSize = std::clamp(std::min(availableWidth - 2.0f, 320.0f), 220.0f, 320.0f);
                    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
                    const ImRect canvasRect(canvasOrigin, ImVec2(canvasOrigin.x + canvasSize, canvasOrigin.y + canvasSize));
                    ImGui::InvisibleButton("##effect_curves_editor_canvas", ImVec2(canvasSize, canvasSize));

                    const Scene beforeCurveCanvas = scene_;
                    ImGuiStorage* storage = ImGui::GetStateStorage();
                    const ImGuiID storageId = ImGui::GetItemID();
                    int selectedPoint = storage->GetInt(storageId, -1);
                    int draggingPoint = storage->GetInt(storageId + 1, -1);

                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const ImU32 backgroundColor = ImGui::GetColorU32(theme.panelBackgroundInset);
                    const ImU32 gridMajorColor = ImGui::GetColorU32(WithAlpha(theme.borderSubtle, 0.48f));
                    const ImU32 gridMinorColor = ImGui::GetColorU32(WithAlpha(theme.borderSubtle, 0.20f));
                    const ImU32 curveColor = ImGui::GetColorU32(ImVec4(0.90f, 0.92f, 0.97f, 0.97f));
                    const ImU32 fillColor = ImGui::GetColorU32(WithAlpha(theme.accent, 0.06f));
                    const ImU32 identityColor = ImGui::GetColorU32(ImVec4(0.75f, 0.42f, 0.18f, 0.72f));

                    const auto curveToScreen = [&](const double x, const double y) -> ImVec2 {
                        return ImVec2(
                            canvasRect.Min.x + static_cast<float>(x) * canvasRect.GetWidth(),
                            canvasRect.Max.y - static_cast<float>(y) * canvasRect.GetHeight());
                    };
                    const auto screenToCurve = [&](const ImVec2& point) -> Vec2 {
                        return {
                            std::clamp(static_cast<double>((point.x - canvasRect.Min.x) / canvasRect.GetWidth()), 0.0, 1.0),
                            std::clamp(static_cast<double>((canvasRect.Max.y - point.y) / canvasRect.GetHeight()), 0.0, 1.0)
                        };
                    };
                    const auto evalCurve = [&](const float t) -> float {
                        if (controlPoints.size() < 2) {
                            return t;
                        }
                        if (t <= static_cast<float>(controlPoints.front().x)) {
                            return static_cast<float>(controlPoints.front().y);
                        }
                        if (t >= static_cast<float>(controlPoints.back().x)) {
                            return static_cast<float>(controlPoints.back().y);
                        }
                        for (std::size_t pointIndex = 0; pointIndex + 1 < controlPoints.size(); ++pointIndex) {
                            const float x0 = static_cast<float>(controlPoints[pointIndex].x);
                            const float x1 = static_cast<float>(controlPoints[pointIndex + 1].x);
                            if (t <= x1) {
                                const float y0 = static_cast<float>(controlPoints[pointIndex].y);
                                const float y1 = static_cast<float>(controlPoints[pointIndex + 1].y);
                                const float localT = (t - x0) / std::max(1.0e-6f, x1 - x0);
                                return y0 + (y1 - y0) * localT;
                            }
                        }
                        return t;
                    };

                    drawList->AddRectFilled(canvasRect.Min, canvasRect.Max, backgroundColor, theme.roundingSmall);

                    constexpr float kDashLength = 4.0f;
                    constexpr float kDashGap = 4.0f;
                    for (int gridIndex = 1; gridIndex < 4; ++gridIndex) {
                        const float t = static_cast<float>(gridIndex) / 4.0f;
                        const float x = canvasRect.Min.x + t * canvasRect.GetWidth();
                        const float y = canvasRect.Min.y + t * canvasRect.GetHeight();
                        const ImU32 color = gridIndex == 2 ? gridMajorColor : gridMinorColor;
                        for (float pos = canvasRect.Min.y; pos < canvasRect.Max.y; pos += kDashLength + kDashGap) {
                            drawList->AddLine(
                                ImVec2(x, pos),
                                ImVec2(x, std::min(pos + kDashLength, canvasRect.Max.y)),
                                color);
                        }
                        for (float pos = canvasRect.Min.x; pos < canvasRect.Max.x; pos += kDashLength + kDashGap) {
                            drawList->AddLine(
                                ImVec2(pos, y),
                                ImVec2(std::min(pos + kDashLength, canvasRect.Max.x), y),
                                color);
                        }
                    }

                    drawList->AddLine(
                        ImVec2(canvasRect.Min.x, canvasRect.Max.y),
                        ImVec2(canvasRect.Max.x, canvasRect.Min.y),
                        identityColor,
                        1.4f);

                    constexpr int kCurveSampleCount = 192;
                    std::vector<ImVec2> sampledCurve;
                    sampledCurve.reserve(kCurveSampleCount + 1);
                    for (int sampleIndex = 0; sampleIndex <= kCurveSampleCount; ++sampleIndex) {
                        const float t = static_cast<float>(sampleIndex) / static_cast<float>(kCurveSampleCount);
                        sampledCurve.push_back(curveToScreen(t, evalCurve(t)));
                    }
                    const float fillWidth = canvasRect.GetWidth() / static_cast<float>(kCurveSampleCount) + 1.6f;
                    for (const ImVec2& point : sampledCurve) {
                        drawList->AddLine(point, ImVec2(point.x, canvasRect.Max.y), fillColor, fillWidth);
                    }
                    drawList->AddPolyline(sampledCurve.data(), static_cast<int>(sampledCurve.size()), curveColor, 0, 2.0f);

                    const ImVec2 mousePosition = ImGui::GetIO().MousePos;
                    int hoveredPoint = -1;
                    if (canvasRect.Contains(mousePosition)) {
                        for (int pointIndex = 0; pointIndex < static_cast<int>(controlPoints.size()); ++pointIndex) {
                            const ImVec2 pointScreen = curveToScreen(controlPoints[pointIndex].x, controlPoints[pointIndex].y);
                            const float dx = mousePosition.x - pointScreen.x;
                            const float dy = mousePosition.y - pointScreen.y;
                            if (dx * dx + dy * dy <= 56.0f) {
                                hoveredPoint = pointIndex;
                                break;
                            }
                        }
                    }

                    bool curveCanvasChanged = false;
                    const bool canvasHovered = ImGui::IsItemHovered();
                    const bool doubleClicked = canvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    if (doubleClicked && hoveredPoint > 0 && hoveredPoint < static_cast<int>(controlPoints.size()) - 1) {
                        controlPoints.erase(controlPoints.begin() + hoveredPoint);
                        selectedPoint = -1;
                        draggingPoint = -1;
                        hoveredPoint = -1;
                        curveCanvasChanged = true;
                    } else if (!doubleClicked && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        if (hoveredPoint >= 0) {
                            selectedPoint = hoveredPoint;
                            draggingPoint = hoveredPoint;
                        } else if (canvasRect.Contains(mousePosition)) {
                            const Vec2 newPoint = screenToCurve(mousePosition);
                            bool tooClose = false;
                            for (const Vec2& existingPoint : controlPoints) {
                                if (std::abs(existingPoint.x - newPoint.x) < 0.03) {
                                    tooClose = true;
                                    break;
                                }
                            }
                            if (!tooClose) {
                                controlPoints.push_back(newPoint);
                                std::sort(controlPoints.begin(), controlPoints.end(), [](const Vec2& left, const Vec2& right) {
                                    return left.x < right.x;
                                });
                                controlPoints.front().x = 0.0;
                                controlPoints.back().x = 1.0;
                                for (int pointIndex = 0; pointIndex < static_cast<int>(controlPoints.size()); ++pointIndex) {
                                    if (std::abs(controlPoints[pointIndex].x - newPoint.x) < 1.0e-4
                                        && std::abs(controlPoints[pointIndex].y - newPoint.y) < 1.0e-4) {
                                        selectedPoint = pointIndex;
                                        draggingPoint = pointIndex;
                                        break;
                                    }
                                }
                                curveCanvasChanged = true;
                            }
                        }
                    }

                    if (draggingPoint >= 0
                        && draggingPoint < static_cast<int>(controlPoints.size())
                        && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        const Vec2 mouseCurve = screenToCurve(mousePosition);
                        const bool isFirstPoint = draggingPoint == 0;
                        const bool isLastPoint = draggingPoint == static_cast<int>(controlPoints.size()) - 1;
                        const double newX = isFirstPoint
                            ? 0.0
                            : isLastPoint
                                ? 1.0
                                : std::clamp(
                                    mouseCurve.x,
                                    controlPoints[draggingPoint - 1].x + 0.02,
                                    controlPoints[draggingPoint + 1].x - 0.02);
                        const double newY = std::clamp(mouseCurve.y, 0.0, 1.0);
                        if (std::abs(controlPoints[draggingPoint].x - newX) > 1.0e-6
                            || std::abs(controlPoints[draggingPoint].y - newY) > 1.0e-6) {
                            controlPoints[draggingPoint] = {newX, newY};
                            curveCanvasChanged = true;
                        }
                    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        draggingPoint = -1;
                    }

                    storage->SetInt(storageId, selectedPoint);
                    storage->SetInt(storageId + 1, draggingPoint);

                    for (int pointIndex = 0; pointIndex < static_cast<int>(controlPoints.size()); ++pointIndex) {
                        const ImVec2 pointScreen = curveToScreen(controlPoints[pointIndex].x, controlPoints[pointIndex].y);
                        const bool isDragging = pointIndex == draggingPoint;
                        const bool isHovered = pointIndex == hoveredPoint;
                        const bool isSelected = pointIndex == selectedPoint;
                        const float halfSize = isDragging ? 6.0f : (isHovered ? 5.5f : 4.5f);
                        const ImU32 handleFill = isDragging
                            ? ImGui::GetColorU32(theme.accentHover)
                            : isHovered
                                ? ImGui::GetColorU32(theme.accent)
                                : isSelected
                                    ? ImGui::GetColorU32(ImVec4(0.94f, 0.95f, 0.99f, 1.0f))
                                    : ImGui::GetColorU32(ImVec4(0.78f, 0.82f, 0.90f, 0.92f));
                        drawList->AddRectFilled(
                            ImVec2(pointScreen.x - halfSize, pointScreen.y - halfSize),
                            ImVec2(pointScreen.x + halfSize, pointScreen.y + halfSize),
                            handleFill,
                            2.0f);
                        drawList->AddRect(
                            ImVec2(pointScreen.x - halfSize, pointScreen.y - halfSize),
                            ImVec2(pointScreen.x + halfSize, pointScreen.y + halfSize),
                            ImGui::GetColorU32(WithAlpha(theme.panelBackgroundInset, 0.80f)),
                            2.0f,
                            0,
                            1.5f);
                    }

                    if (canvasHovered && canvasRect.Contains(mousePosition)) {
                        if (hoveredPoint < 0) {
                            const float inputValue = static_cast<float>(screenToCurve(mousePosition).x);
                            const float outputValue = evalCurve(inputValue);
                            const ImVec2 hitPoint = curveToScreen(inputValue, outputValue);
                            const ImU32 crosshairColor = ImGui::GetColorU32(WithAlpha(theme.textDim, 0.32f));
                            drawList->AddLine(ImVec2(mousePosition.x, canvasRect.Min.y), ImVec2(mousePosition.x, canvasRect.Max.y), crosshairColor);
                            drawList->AddLine(ImVec2(canvasRect.Min.x, hitPoint.y), ImVec2(canvasRect.Max.x, hitPoint.y), crosshairColor);
                            drawList->AddCircleFilled(hitPoint, 3.5f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.88f)));
                            char tooltip[64];
                            std::snprintf(tooltip, sizeof(tooltip), "In %.2f  ->  Out %.2f", inputValue, outputValue);
                            ImGui::SetTooltip("%s", tooltip);
                        } else if (hoveredPoint > 0 && hoveredPoint < static_cast<int>(controlPoints.size()) - 1) {
                            char tooltip[96];
                            std::snprintf(
                                tooltip,
                                sizeof(tooltip),
                                "%.2f, %.2f  -  Drag to move  -  Dbl-click to delete",
                                static_cast<float>(controlPoints[hoveredPoint].x),
                                static_cast<float>(controlPoints[hoveredPoint].y));
                            ImGui::SetTooltip("%s", tooltip);
                        } else {
                            ImGui::SetTooltip("Endpoint - drag vertically to adjust output");
                        }
                    }

                    drawList->AddRect(
                        canvasRect.Min,
                        canvasRect.Max,
                        ImGui::GetColorU32(WithAlpha(theme.borderStrong, 0.55f)),
                        theme.roundingSmall,
                        0,
                        1.0f);
                    ImGui::SetCursorScreenPos(ImVec2(canvasOrigin.x, canvasOrigin.y + canvasSize + 6.0f));

                    if (curveCanvasChanged) {
                        controlPoints.front().x = 0.0;
                        controlPoints.back().x = 1.0;
                    }
                    captureEffectEdit(beforeCurveCanvas, curveCanvasChanged, static_cast<int>(index));

                    const float actionButtonWidth = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;

                    const Scene beforeIdentity = scene_;
                    if (DrawActionButton(
                            "##effect_curves_identity",
                            "Identity",
                            IconGlyph::Remove,
                            ActionTone::Slate,
                            false,
                            true,
                            actionButtonWidth,
                            "Reset to a straight identity curve")) {
                        controlPoints = {{0.0, 0.0}, {0.25, 0.25}, {0.5, 0.5}, {0.75, 0.75}, {1.0, 1.0}};
                        commitEffectAction(beforeIdentity, static_cast<int>(index), "Curves: Identity");
                    }
                    ImGui::SameLine(0.0f, 4.0f);

                    const Scene beforeSmooth = scene_;
                    if (DrawActionButton(
                            "##effect_curves_smooth",
                            "Smooth",
                            IconGlyph::Settings,
                            ActionTone::Slate,
                            false,
                            static_cast<int>(controlPoints.size()) > 2,
                            actionButtonWidth,
                            "Smooth interior control points")) {
                        for (int iteration = 0; iteration < 2; ++iteration) {
                            std::vector<Vec2> smoothedPoints = controlPoints;
                            for (int pointIndex = 1; pointIndex < static_cast<int>(controlPoints.size()) - 1; ++pointIndex) {
                                smoothedPoints[pointIndex].y =
                                    (controlPoints[pointIndex - 1].y + controlPoints[pointIndex].y + controlPoints[pointIndex + 1].y) / 3.0;
                            }
                            controlPoints = std::move(smoothedPoints);
                        }
                        commitEffectAction(beforeSmooth, static_cast<int>(index), "Curves: Smooth");
                    }
                    ImGui::SameLine(0.0f, 4.0f);

                    const Scene beforeSCurve = scene_;
                    if (DrawActionButton(
                            "##effect_curves_s_curve",
                            "S-Curve",
                            IconGlyph::Randomize,
                            ActionTone::Accent,
                            false,
                            true,
                            actionButtonWidth,
                            "Apply a classic S-curve")) {
                        controlPoints = {{0.0, 0.0}, {0.25, 0.18}, {0.5, 0.5}, {0.75, 0.82}, {1.0, 1.0}};
                        commitEffectAction(beforeSCurve, static_cast<int>(index), "Curves: S-Curve");
                    }
                }
                endEffectSubsection(curvesEditorOpen);
            } else {
                const bool curvesLevelsOpen = beginEffectSubsection("##effect_curves_section", "Levels");
                if (curvesLevelsOpen) {
                    if (beginEffectFieldGrid("##effect_curves_grid")) {
                        ImGui::TableNextColumn();
                        drawEffectFieldLabel("Black Point");
                        setEffectItemWidth();
                        Scene beforeCurves = scene_;
                        bool curvesChanged = SliderScalarWithInput("##effect_curves_black", ImGuiDataType_Double, &scene_.postProcess.curveBlackPoint, &blackPointMin, &blackPointMax, "%.2f")
                            || ResetValueOnDoubleClick(scene_.postProcess.curveBlackPoint, defaultScene.postProcess.curveBlackPoint);
                        scene_.postProcess.curveBlackPoint = std::min(scene_.postProcess.curveBlackPoint, scene_.postProcess.curveWhitePoint - 0.01);
                        captureEffectEdit(beforeCurves, curvesChanged, static_cast<int>(index));

                        ImGui::TableNextColumn();
                        drawEffectFieldLabel("White Point");
                        setEffectItemWidth();
                        beforeCurves = scene_;
                        curvesChanged = SliderScalarWithInput("##effect_curves_white", ImGuiDataType_Double, &scene_.postProcess.curveWhitePoint, &whitePointMin, &whitePointMax, "%.2f")
                            || ResetValueOnDoubleClick(scene_.postProcess.curveWhitePoint, defaultScene.postProcess.curveWhitePoint);
                        scene_.postProcess.curveWhitePoint = std::max(scene_.postProcess.curveWhitePoint, scene_.postProcess.curveBlackPoint + 0.01);
                        captureEffectEdit(beforeCurves, curvesChanged, static_cast<int>(index));

                        if (ImGui::TableGetColumnCount() > 1) {
                            ImGui::TableNextRow();
                        }
                        ImGui::TableNextColumn();
                        drawEffectFieldLabel("Gamma");
                        setEffectItemWidth();
                        beforeCurves = scene_;
                        curvesChanged = SliderScalarWithInput("##effect_curves_gamma", ImGuiDataType_Double, &scene_.postProcess.curveGamma, &curveGammaMin, &curveGammaMax, "%.2f")
                            || ResetValueOnDoubleClick(scene_.postProcess.curveGamma, defaultScene.postProcess.curveGamma);
                        captureEffectEdit(beforeCurves, curvesChanged, static_cast<int>(index));

                        ImGui::EndTable();
                    }
                }
                endEffectSubsection(curvesLevelsOpen);
            }
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::Sharpen: {
        const EffectHeaderState sharpenHeader = beginEffectCard(
            "##effect_card_sharpen",
            "Sharpen",
            scene_.postProcess.sharpenEnabled,
            scene_.postProcess.sharpenEnabled,
            static_cast<int>(index));
        const Scene beforeSharpenEnabled = scene_;
        if (sharpenHeader.enabledChanged) {
            scene_.postProcess.sharpenEnabled = sharpenHeader.enabled;
        }
        bool sharpenEnabledChanged = sharpenHeader.enabledChanged;
        if (sharpenHeader.resetPressed) {
            scene_.postProcess.sharpenAmount = defaultScene.postProcess.sharpenAmount;
        }
        endEffectHeader(sharpenHeader.contentOpen, nullptr);
        if (sharpenHeader.resetPressed) {
            commitEffectReset(beforeSharpenEnabled, "Sharpen", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeSharpenEnabled, sharpenEnabledChanged, static_cast<int>(index));
        }
        if (sharpenHeader.contentOpen && scene_.postProcess.sharpenEnabled) {
            const double sharpenMin = 0.0;
            const double sharpenMax = 1.0;
            const bool sharpenDetailOpen = beginEffectSubsection("##effect_sharpen_section", "Detail");
            if (sharpenDetailOpen) {
                if (beginEffectFieldGrid("##effect_sharpen_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Amount");
                    setEffectItemWidth();
                    Scene beforeSharpen = scene_;
                    bool sharpenChanged = SliderScalarWithInput("##effect_sharpen_amount", ImGuiDataType_Double, &scene_.postProcess.sharpenAmount, &sharpenMin, &sharpenMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.sharpenAmount, defaultScene.postProcess.sharpenAmount);
                    captureEffectEdit(beforeSharpen, sharpenChanged, static_cast<int>(index));
                    ImGui::EndTable();
                }
            }
            endEffectSubsection(sharpenDetailOpen);
        }
        endEffectCard();
                break;
            }

            case EffectStackStage::HueShift: {
        const EffectHeaderState hueShiftHeader = beginEffectCard(
            "##effect_card_hue_shift",
            "Hue Shift",
            scene_.postProcess.hueShiftEnabled,
            scene_.postProcess.hueShiftEnabled,
            static_cast<int>(index));
        const Scene beforeHueShiftEnabled = scene_;
        if (hueShiftHeader.enabledChanged) {
            scene_.postProcess.hueShiftEnabled = hueShiftHeader.enabled;
        }
        bool hueShiftEnabledChanged = hueShiftHeader.enabledChanged;
        if (hueShiftHeader.resetPressed) {
            scene_.postProcess.hueShiftDegrees = defaultScene.postProcess.hueShiftDegrees;
            scene_.postProcess.hueShiftSaturation = defaultScene.postProcess.hueShiftSaturation;
        }
        endEffectHeader(hueShiftHeader.contentOpen, nullptr);
        if (hueShiftHeader.resetPressed) {
            commitEffectReset(beforeHueShiftEnabled, "Hue Shift", static_cast<int>(index));
        } else {
            captureEffectEdit(beforeHueShiftEnabled, hueShiftEnabledChanged, static_cast<int>(index));
        }
        if (hueShiftHeader.contentOpen && scene_.postProcess.hueShiftEnabled) {
            const double hueShiftMin = -180.0;
            const double hueShiftMax = 180.0;
            const double saturationMin = 0.0;
            const double saturationMax = 2.0;
            const bool hueColorOpen = beginEffectSubsection("##effect_hue_shift_section", "Color");
            if (hueColorOpen) {
                if (beginEffectFieldGrid("##effect_hue_shift_grid")) {
                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Hue");
                    setEffectItemWidth();
                    Scene beforeHueShift = scene_;
                    bool hueShiftChanged = SliderScalarWithInput("##effect_hue_shift_degrees", ImGuiDataType_Double, &scene_.postProcess.hueShiftDegrees, &hueShiftMin, &hueShiftMax, "%.0f deg")
                        || ResetValueOnDoubleClick(scene_.postProcess.hueShiftDegrees, defaultScene.postProcess.hueShiftDegrees);
                    captureEffectEdit(beforeHueShift, hueShiftChanged, static_cast<int>(index));

                    ImGui::TableNextColumn();
                    drawEffectFieldLabel("Saturation");
                    setEffectItemWidth();
                    beforeHueShift = scene_;
                    hueShiftChanged = SliderScalarWithInput("##effect_hue_shift_saturation", ImGuiDataType_Double, &scene_.postProcess.hueShiftSaturation, &saturationMin, &saturationMax, "%.2f")
                        || ResetValueOnDoubleClick(scene_.postProcess.hueShiftSaturation, defaultScene.postProcess.hueShiftSaturation);
                    captureEffectEdit(beforeHueShift, hueShiftChanged, static_cast<int>(index));

                    ImGui::EndTable();
                }
            }
            endEffectSubsection(hueColorOpen);
        }
        endEffectCard();
                break;
            }

            default:
                break;
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    if (requestedMoveFrom >= 0
        && requestedMoveTo >= 0
        && requestedMoveFrom < static_cast<int>(scene_.effectStack.size())
        && requestedMoveTo < static_cast<int>(scene_.effectStack.size())
        && requestedMoveFrom != requestedMoveTo) {
        const Scene beforeReorder = scene_;
        std::vector<EffectStackStage> selectedStages;
        selectedStages.reserve(selectedEffects_.size());
        for (const int selectedIndex : selectedEffects_) {
            if (selectedIndex >= 0 && selectedIndex < static_cast<int>(scene_.effectStack.size())) {
                selectedStages.push_back(scene_.effectStack[static_cast<std::size_t>(selectedIndex)]);
            }
        }
        const bool hadPrimarySelection = selectedEffect_ >= 0 && selectedEffect_ < static_cast<int>(scene_.effectStack.size());
        const EffectStackStage primarySelectedStage = hadPrimarySelection
            ? scene_.effectStack[static_cast<std::size_t>(selectedEffect_)]
            : EffectStackStage::Denoiser;
        const bool hadRightClickedSelection = rightClickedEffectIndex_ >= 0 && rightClickedEffectIndex_ < static_cast<int>(scene_.effectStack.size());
        const EffectStackStage rightClickedStage = hadRightClickedSelection
            ? scene_.effectStack[static_cast<std::size_t>(rightClickedEffectIndex_)]
            : EffectStackStage::Denoiser;
        auto begin = scene_.effectStack.begin();
        if (requestedMoveFrom < requestedMoveTo) {
            std::rotate(begin + requestedMoveFrom, begin + requestedMoveFrom + 1, begin + requestedMoveTo + 1);
        } else {
            std::rotate(begin + requestedMoveTo, begin + requestedMoveFrom, begin + requestedMoveFrom + 1);
        }
        selectedEffects_.clear();
        selectedEffect_ = -1;
        rightClickedEffectIndex_ = -1;
        for (std::size_t index = 0; index < scene_.effectStack.size(); ++index) {
            if (std::find(selectedStages.begin(), selectedStages.end(), scene_.effectStack[index]) != selectedStages.end()) {
                selectedEffects_.push_back(static_cast<int>(index));
            }
            if (hadPrimarySelection && scene_.effectStack[index] == primarySelectedStage) {
                selectedEffect_ = static_cast<int>(index);
            }
            if (hadRightClickedSelection && scene_.effectStack[index] == rightClickedStage) {
                rightClickedEffectIndex_ = static_cast<int>(index);
            }
        }
        NormalizeEffectSelections();
        SyncCurrentKeyframeFromScene();
        PushUndoState(beforeReorder, "Reorder Effects");
        MarkViewportDirty(DeterminePreviewResetReason(beforeReorder, scene_));
    }

    NormalizeEffectSelections();

    const bool addPopupOpen = ImGui::IsPopupOpen("##add_effect_popup", ImGuiPopupFlags_None);
    const bool panelTabCtxOpen = ImGui::IsPopupOpen("##panel_tab_context_menu", ImGuiPopupFlags_None);
    ImGuiWindow* effectsWindow = ImGui::GetCurrentWindow();
    const ImRect effectsRect = effectsWindow != nullptr ? effectsWindow->InnerRect : ImRect();
    const bool effectsTabVisible = effectsWindow != nullptr
        && effectsWindow->DockIsActive
        && effectsWindow->DockTabIsVisible;
    if (requestOpenEffectsContextMenu) {
        ImGui::OpenPopup("##effects_context_menu");
    } else if (effectsTabVisible
        && !rightClickStartedOnEffectControl
        && !addPopupOpen
        && !panelTabCtxOpen
        && !ImGui::IsPopupOpen("##effects_context_menu", ImGuiPopupFlags_None)
        && effectsRect.Contains(effectsIo.MouseClickedPos[ImGuiMouseButton_Right])
        && effectsRect.Contains(effectsIo.MousePos)
        && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("##effects_context_menu");
    }
    if (ImGui::BeginPopup("##effects_context_menu")) {
        const bool hasSelection = rightClickedEffectIndex_ >= 0 && rightClickedEffectIndex_ < static_cast<int>(scene_.effectStack.size());
        if (hasSelection) {
            const bool affectsSelection = IsEffectSelected(rightClickedEffectIndex_) && HasMultipleEffectsSelected();
            const char* effectName = EffectStageDisplayName(scene_.effectStack[rightClickedEffectIndex_]);
            const std::string deleteLabel = affectsSelection
                ? "Delete Selected Effects"
                : std::string("Delete ") + effectName;
            if (ImGui::MenuItem(deleteLabel.c_str(), "Delete")) {
                if (affectsSelection) {
                    RemoveSelectedEffect();
                } else {
                    RemoveEffectAtIndex(rightClickedEffectIndex_, deleteLabel);
                }
            }
            ImGui::Separator();
        }

        if (ImGui::BeginMenu("Add Effect")) {
            std::array<bool, kEffectStackStageCount> ctxAdded {};
            for (const EffectStackStage stage : scene_.effectStack) {
                const std::size_t idx = static_cast<std::size_t>(stage);
                if (idx < kEffectStackStageCount) ctxAdded[idx] = true;
            }
            for (const EffectStackStage stage : kAllEffectStages) {
                const char* displayName = EffectStageDisplayName(stage);
                const bool alreadyInStack = ctxAdded[static_cast<std::size_t>(stage)];
                if (ImGui::MenuItem(displayName, nullptr, false, !alreadyInStack)) {
                    const Scene beforeAdd = scene_;
                    scene_.effectStack.push_back(stage);
                    EnableEffectStage(scene_, stage, true);
                    ClearLayerSelections();
                    SelectSingleEffect(static_cast<int>(scene_.effectStack.size()) - 1);
                    SyncCurrentKeyframeFromScene();
                    PushUndoState(beforeAdd, std::string("Add ") + displayName);
                    MarkViewportDirty(DeterminePreviewResetReason(beforeAdd, scene_));
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    } else {
        rightClickedEffectIndex_ = -1;
    }

    ImGui::End();
}

bool AppWindow::CaptureCurrentPreviewPixels(std::vector<std::uint32_t>& pixels, int& width, int& height) {
    width = std::max(1, UploadedViewportWidth());
    height = std::max(1, UploadedViewportHeight());
    pixels.clear();
    if (width <= 0 || height <= 0) {
        return false;
    }

    const PreviewPresentationState presentation = previewProgress_.presentation;
    if (presentation.device == PreviewRenderDevice::Cpu) {
        if (viewportPixels_.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
            return false;
        }
        pixels = viewportPixels_;
        return true;
    }

    if (renderBackend_ == nullptr) {
        return false;
    }

    const auto readTexture = [&](ID3D11Texture2D* texture) {
        return texture != nullptr && renderBackend_->ReadbackColorTexture(texture, pixels);
    };

    switch (presentation.stage) {
    case PreviewRenderStage::PostProcessed:
        return readTexture(gpuPostProcess_.OutputTexture());
    case PreviewRenderStage::DepthOfField:
        return readTexture(gpuDofRenderer_.OutputTexture());
    case PreviewRenderStage::Denoised:
        return readTexture(gpuDenoiser_.OutputTexture());
    case PreviewRenderStage::Base:
    case PreviewRenderStage::Composited:
    default:
        break;
    }

    auto compositeTexture = [&](ID3D11Texture2D* texture) {
        if (texture == nullptr) {
            return true;
        }
        std::vector<std::uint32_t> layerPixels;
        if (!renderBackend_->ReadbackColorTexture(texture, layerPixels)) {
            return false;
        }
        if (pixels.empty()) {
            pixels = std::move(layerPixels);
        } else {
            CompositePixelsOver(pixels, layerPixels);
        }
        return true;
    };

    if (presentation.content != PreviewRenderContent::Path && !compositeTexture(gpuFlameRenderer_.OutputTexture())) {
        return false;
    }
    if (presentation.content != PreviewRenderContent::Flame && !compositeTexture(gpuPathRenderer_.OutputTexture())) {
        return false;
    }
    return !pixels.empty();
}

void AppWindow::DrawHistogramPanel() {
    if (!histogramPanelOpen_) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Histogram", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    DrawDockPanelTabContextMenu("Histogram", histogramPanelOpen_);

    const int previewWidth = std::max(1, UploadedViewportWidth());
    const int previewHeight = std::max(1, UploadedViewportHeight());
    const std::size_t previewPixelCount = static_cast<std::size_t>(previewWidth) * static_cast<std::size_t>(previewHeight);
    const auto histogramRefreshInterval =
        previewPixelCount > 1800000 ? std::chrono::milliseconds(1600)
        : previewPixelCount > 900000 ? std::chrono::milliseconds(1100)
        : previewPixelCount > 400000 ? std::chrono::milliseconds(650)
        : std::chrono::milliseconds(250);
    const auto now = std::chrono::steady_clock::now();
    const bool histogramShapeChanged =
        !histogramCache_.valid
        || histogramCache_.width != previewWidth
        || histogramCache_.height != previewHeight
        || histogramCache_.presentation != previewProgress_.presentation;
    const bool previewChangedSinceHistogram = histogramCache_.sourceTimestamp != lastPreviewUpdate_;
    const bool previewSettled = previewProgress_.phase == PreviewProgressPhase::Complete;
    const bool dueForRefresh =
        histogramShapeChanged
        || (!histogramCache_.valid)
        || (previewChangedSinceHistogram && previewSettled)
        || (previewChangedSinceHistogram && (now - histogramCache_.lastRefreshAt >= histogramRefreshInterval));

    if (dueForRefresh) {
        std::vector<std::uint32_t> previewPixels;
        int capturedWidth = 0;
        int capturedHeight = 0;
        if (!CaptureCurrentPreviewPixels(previewPixels, capturedWidth, capturedHeight)) {
            if (!histogramCache_.valid) {
                ImGui::TextDisabled("No preview image available yet.");
                ImGui::End();
                return;
            }
        } else {
            histogramCache_ = {};
            histogramCache_.valid = true;
            histogramCache_.width = capturedWidth;
            histogramCache_.height = capturedHeight;
            histogramCache_.presentation = previewProgress_.presentation;
            histogramCache_.sourceTimestamp = lastPreviewUpdate_;
            histogramCache_.lastRefreshAt = now;

            double meanLuminance = 0.0;
            int shadowClipped = 0;
            int highlightClipped = 0;
            int midtones = 0;
            std::array<std::uint32_t, 256> luminanceCounts {};

            const std::size_t maxHistogramSamples =
                previewPixels.size() > 1800000 ? 12000
                : previewPixels.size() > 900000 ? 18000
                : previewPixels.size() > 400000 ? 28000
                : 48000;
            const std::size_t sampleStride = std::max<std::size_t>(1, (previewPixels.size() + maxHistogramSamples - 1) / maxHistogramSamples);
            std::size_t sampleCount = 0;
            for (std::size_t pixelIndex = 0; pixelIndex < previewPixels.size(); pixelIndex += sampleStride) {
                const std::uint32_t pixel = previewPixels[pixelIndex];
                const int red = static_cast<int>((pixel >> 16U) & 0xFFU);
                const int green = static_cast<int>((pixel >> 8U) & 0xFFU);
                const int blue = static_cast<int>(pixel & 0xFFU);
                const int lum = static_cast<int>(std::clamp(
                    std::round((red * 0.2126 + green * 0.7152 + blue * 0.0722)),
                    0.0,
                    255.0));
                histogramCache_.red[static_cast<std::size_t>(red)] += 1.0f;
                histogramCache_.green[static_cast<std::size_t>(green)] += 1.0f;
                histogramCache_.blue[static_cast<std::size_t>(blue)] += 1.0f;
                histogramCache_.luminance[static_cast<std::size_t>(lum)] += 1.0f;
                luminanceCounts[static_cast<std::size_t>(lum)] += 1U;
                meanLuminance += (red * 0.2126 + green * 0.7152 + blue * 0.0722) / 255.0;
                if (red <= 2 && green <= 2 && blue <= 2) {
                    ++shadowClipped;
                }
                if (red >= 253 && green >= 253 && blue >= 253) {
                    ++highlightClipped;
                }
                if (lum >= 64 && lum <= 191) {
                    ++midtones;
                }
                ++sampleCount;
            }

            const auto normalizeBins = [](std::array<float, 256>& bins) {
                const float maxBin = *std::max_element(bins.begin(), bins.end());
                if (maxBin <= 0.0f) {
                    return;
                }
                for (float& value : bins) {
                    value /= maxBin;
                }
            };
            normalizeBins(histogramCache_.red);
            normalizeBins(histogramCache_.green);
            normalizeBins(histogramCache_.blue);
            normalizeBins(histogramCache_.luminance);

            const double sampleCountDouble = static_cast<double>(std::max<std::size_t>(1, sampleCount));
            const auto percentileLuminance = [&](const double percentile) {
                const std::uint64_t threshold = static_cast<std::uint64_t>(std::ceil(percentile * sampleCountDouble));
                std::uint64_t cumulative = 0;
                for (std::size_t index = 0; index < luminanceCounts.size(); ++index) {
                    cumulative += luminanceCounts[index];
                    if (cumulative >= threshold) {
                        return static_cast<double>(index) / 255.0;
                    }
                }
                return 1.0;
            };

            histogramCache_.sampleCount = sampleCount;
            histogramCache_.meanLuminance = meanLuminance / sampleCountDouble;
            histogramCache_.medianLuminance = percentileLuminance(0.50);
            histogramCache_.p95Luminance = percentileLuminance(0.95);
            histogramCache_.shadowClipPercent = (static_cast<double>(shadowClipped) / sampleCountDouble) * 100.0;
            histogramCache_.midtonePercent = (static_cast<double>(midtones) / sampleCountDouble) * 100.0;
            histogramCache_.highlightClipPercent = (static_cast<double>(highlightClipped) / sampleCountDouble) * 100.0;
        }
    }

    if (!histogramCache_.valid) {
        ImGui::TextDisabled("No histogram data yet.");
        ImGui::End();
        return;
    }

    const UiTheme& theme = GetUiTheme();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float availableHeight = ImGui::GetContentRegionAvail().y;
    const float legendHeight = ImGui::GetFrameHeight() + 2.0f;
    const float statsBlockHeight = ImGui::GetTextLineHeight() * 3.2f + style.ItemSpacing.y * 3.0f;
    const float bottomReserve = legendHeight + statsBlockHeight + style.ItemSpacing.y * 3.0f;
    const float targetChartHeight = availableHeight - bottomReserve;
    const float chartHeight = std::clamp(targetChartHeight, 140.0f, 320.0f);
    ImGui::BeginChild(
        "##histogram_chart_region",
        ImVec2(0.0f, chartHeight),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 chartAvail = ImGui::GetContentRegionAvail();
    const ImVec2 chartSize(std::max(1.0f, chartAvail.x), std::max(1.0f, chartAvail.y));
    const ImVec2 chartPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##histogram_canvas", chartSize);
    const ImRect chartRect(chartPos, ImVec2(chartPos.x + chartSize.x, chartPos.y + chartSize.y));
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 chartFillTop = ImGui::GetColorU32(Mix(theme.panelBackgroundInset, theme.frameBackground, 0.55f));
    const ImU32 chartFillBottom = ImGui::GetColorU32(theme.panelBackgroundInset);
    const ImU32 chartBorder = ImGui::GetColorU32(theme.border);
    const ImU32 gridColor = ImGui::GetColorU32(WithAlpha(theme.textDim, 0.16f));
    const ImU32 lumaFill = ImGui::GetColorU32(ImVec4(0.92f, 0.94f, 0.98f, 0.18f));
    const ImU32 lumaStroke = ImGui::GetColorU32(ImVec4(0.92f, 0.94f, 0.98f, 0.72f));
    const ImU32 redFill = ImGui::GetColorU32(ImVec4(0.96f, 0.34f, 0.36f, 0.16f));
    const ImU32 redStroke = ImGui::GetColorU32(ImVec4(0.98f, 0.40f, 0.42f, 0.92f));
    const ImU32 greenFill = ImGui::GetColorU32(ImVec4(0.34f, 0.84f, 0.52f, 0.16f));
    const ImU32 greenStroke = ImGui::GetColorU32(ImVec4(0.42f, 0.90f, 0.58f, 0.92f));
    const ImU32 blueFill = ImGui::GetColorU32(ImVec4(0.30f, 0.58f, 0.94f, 0.16f));
    const ImU32 blueStroke = ImGui::GetColorU32(ImVec4(0.38f, 0.64f, 0.98f, 0.94f));
    const ImU32 shadowTint = ImGui::GetColorU32(ImVec4(0.10f, 0.16f, 0.30f, 0.22f));
    const ImU32 highlightTint = ImGui::GetColorU32(ImVec4(0.78f, 0.74f, 0.48f, 0.12f));
    const ImU32 axisLabelColor = ImGui::GetColorU32(WithAlpha(theme.textDim, 0.85f));

    drawList->AddRectFilledMultiColor(
        chartRect.Min,
        chartRect.Max,
        chartFillTop,
        chartFillTop,
        chartFillBottom,
        chartFillBottom);
    drawList->AddRectFilled(
        chartRect.Min,
        ImVec2(chartRect.Min.x + chartRect.GetWidth() * 0.06f, chartRect.Max.y),
        shadowTint,
        theme.roundingMedium,
        ImDrawFlags_RoundCornersLeft);
    drawList->AddRectFilled(
        ImVec2(chartRect.Max.x - chartRect.GetWidth() * 0.06f, chartRect.Min.y),
        chartRect.Max,
        highlightTint,
        theme.roundingMedium,
        ImDrawFlags_RoundCornersRight);

    constexpr int gridDivisionsX = 4;
    constexpr int gridDivisionsY = 4;
    for (int gridIndex = 1; gridIndex < gridDivisionsX; ++gridIndex) {
        const float t = static_cast<float>(gridIndex) / static_cast<float>(gridDivisionsX);
        const float x = ImLerp(chartRect.Min.x, chartRect.Max.x, t);
        drawList->AddLine(ImVec2(x, chartRect.Min.y), ImVec2(x, chartRect.Max.y), gridColor, 1.0f);
    }
    for (int gridIndex = 1; gridIndex < gridDivisionsY; ++gridIndex) {
        const float t = static_cast<float>(gridIndex) / static_cast<float>(gridDivisionsY);
        const float y = ImLerp(chartRect.Min.y, chartRect.Max.y, t);
        drawList->AddLine(ImVec2(chartRect.Min.x, y), ImVec2(chartRect.Max.x, y), gridColor, 1.0f);
    }

    const auto drawHistogramLayer = [&](const std::array<float, 256>& bins, const ImU32 fillColor, const ImU32 strokeColor) {
        std::array<ImVec2, 256> linePoints {};
        for (int binIndex = 0; binIndex < 256; ++binIndex) {
            const float x = ImLerp(chartRect.Min.x, chartRect.Max.x, static_cast<float>(binIndex) / 255.0f);
            const float y = ImLerp(chartRect.Max.y - 1.0f, chartRect.Min.y + 10.0f, bins[static_cast<std::size_t>(binIndex)]);
            linePoints[static_cast<std::size_t>(binIndex)] = ImVec2(x, y);
            drawList->AddLine(ImVec2(x, chartRect.Max.y - 1.0f), ImVec2(x, y), fillColor, 1.0f);
        }
        drawList->AddPolyline(linePoints.data(), static_cast<int>(linePoints.size()), strokeColor, ImDrawFlags_None, 2.0f);
    };

    drawHistogramLayer(histogramCache_.luminance, lumaFill, lumaStroke);
    drawHistogramLayer(histogramCache_.red, redFill, redStroke);
    drawHistogramLayer(histogramCache_.green, greenFill, greenStroke);
    drawHistogramLayer(histogramCache_.blue, blueFill, blueStroke);
    drawList->AddRect(chartRect.Min, chartRect.Max, chartBorder, theme.roundingMedium, 0, 1.0f);

    const char* xAxisLabels[] = {"0", "64", "128", "192", "255"};
    for (int index = 0; index < 5; ++index) {
        const float t = static_cast<float>(index) / 4.0f;
        const float x = ImLerp(chartRect.Min.x, chartRect.Max.x, t);
        const ImVec2 textSize = ImGui::CalcTextSize(xAxisLabels[index]);
        const float labelMinX = chartRect.Min.x + 6.0f;
        const float labelMaxX = std::max(labelMinX, chartRect.Max.x - textSize.x - 6.0f);
        const float textX = std::clamp(
            x - textSize.x * 0.5f,
            labelMinX,
            labelMaxX);
        drawList->AddText(
            ImVec2(textX, chartRect.Max.y - textSize.y - 5.0f),
            axisLabelColor,
            xAxisLabels[index]);
    }
    ImGui::EndChild();

    ImGui::Spacing();
    PushMonospaceFont();
    const auto drawStatValue = [&](const char* label, const std::string& value) {
        ImGui::TextDisabled("%s", label);
        ImGui::Text("%s", value.c_str());
    };
    if (ImGui::BeginTable("##histogram_stats", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableNextColumn();
        drawStatValue("SIZE", std::format("{}x{}", histogramCache_.width, histogramCache_.height));
        ImGui::TableNextColumn();
        drawStatValue("SAMPLES", std::format("{:L}", histogramCache_.sampleCount));
        ImGui::TableNextColumn();
        drawStatValue("MEAN", std::format("{:.2f}", histogramCache_.meanLuminance));
        ImGui::TableNextColumn();
        drawStatValue("MEDIAN", std::format("{:.2f}", histogramCache_.medianLuminance));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        drawStatValue("P95", std::format("{:.2f}", histogramCache_.p95Luminance));
        ImGui::TableNextColumn();
        drawStatValue("SHADOWS", std::format("{:.1f}%", histogramCache_.shadowClipPercent));
        ImGui::TableNextColumn();
        drawStatValue("MIDTONES", std::format("{:.1f}%", histogramCache_.midtonePercent));
        ImGui::TableNextColumn();
        drawStatValue("HIGHLIGHTS", std::format("{:.1f}%", histogramCache_.highlightClipPercent));
        ImGui::EndTable();
    }
    PopMonospaceFont();

    const auto drawLegendItem = [&](const char* id, const char* label, const ImVec4& color) {
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const float itemWidth = textSize.x + 34.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, Mix(theme.panelBackgroundAlt, color, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Mix(theme.frameBackgroundHover, color, 0.26f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Mix(theme.frameBackgroundActive, color, 0.32f));
        ImGui::Button(id, ImVec2(itemWidth, 0.0f));
        const ImRect chipRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        drawList->AddCircleFilled(
            ImVec2(chipRect.Min.x + 12.0f, (chipRect.Min.y + chipRect.Max.y) * 0.5f),
            4.0f,
            ImGui::GetColorU32(color));
        drawList->AddText(
            ImVec2(chipRect.Min.x + 22.0f, chipRect.Min.y + 4.0f),
            ImGui::GetColorU32(theme.text),
            label);
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    };

    drawLegendItem("##histogram_luma_chip", "Luma", ImVec4(0.92f, 0.94f, 0.98f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f);
    drawLegendItem("##histogram_red_chip", "Red", ImVec4(0.96f, 0.34f, 0.36f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f);
    drawLegendItem("##histogram_green_chip", "Green", ImVec4(0.34f, 0.84f, 0.52f, 1.0f));
    ImGui::SameLine(0.0f, 8.0f);
    drawLegendItem("##histogram_blue_chip", "Blue", ImVec4(0.30f, 0.58f, 0.94f, 1.0f));

    ImGui::End();
}

void AppWindow::DrawCameraPanel() {
    if (!cameraPanelOpen_) {
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    DrawDockPanelTabContextMenu("Camera", cameraPanelOpen_);

    const UiTheme& theme = GetUiTheme();
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
    const bool useWideCameraGrid = ImGui::GetContentRegionAvail().x >= ScaleUi(kWideCameraGridMinWidth);
    const auto captureCameraEdit = [&](const Scene& before, const bool changed) {
        if (changed) {
            AutoKeyCurrentFrame();
            MarkViewportDirty(PreviewResetReason::CameraChanged);
        }
        CaptureWidgetUndo(before, changed);
    };
    const auto beginFieldGrid = [&](const char* id) {
        if (useWideCameraGrid) {
            const float contentWidth = ImGui::GetContentRegionAvail().x;
            const float labelWidth = std::clamp(contentWidth * 0.10f, ScaleUi(72.0f), ScaleUi(92.0f));
            const float inputWidth = std::max(ScaleUi(120.0f), (contentWidth - labelWidth * 2.0f) * 0.5f);
            ImGui::Columns(4, id, false);
            ImGui::SetColumnWidth(0, labelWidth);
            ImGui::SetColumnWidth(1, inputWidth);
            ImGui::SetColumnWidth(2, labelWidth);
        } else {
            ImGui::Columns(1, id, false);
        }
    };
    const auto endFieldGrid = [&]() {
        ImGui::Columns(1);
    };
    const auto fieldLabel = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        if (useWideCameraGrid) {
            ImGui::NextColumn();
        }
    };
    const auto fieldWidget = [&](const auto& drawWidget) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool changed = drawWidget();
        if (useWideCameraGrid) {
            ImGui::NextColumn();
        }
        return changed;
    };
    const auto captureLockedAspectRatio = [&]() {
        cameraAspectLockedRatio_ = std::max(1.0e-6, scene_.camera.frameWidth) / std::max(1.0e-6, scene_.camera.frameHeight);
    };

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
    captureCameraEdit(beforeAspectPreset, aspectPresetChanged);
    if (aspectPresetChanged && cameraAspectLocked_) {
        captureLockedAspectRatio();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, theme.textDim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Gate: %s", CameraAspectSummary(scene_.camera).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();

    Scene beforeFraming = scene_;
    const double lockedAspectRatio = std::max(1.0e-6, cameraAspectLockedRatio_);
    fieldLabel("Width");
    bool framingChanged = fieldWidget([&]() {
        bool changed = DragScalarWithInput("##camera_frame_width", ImGuiDataType_Double, &scene_.camera.frameWidth, 0.05f, &frameMin, &frameMax, "%.2f");
        if (changed && cameraAspectLocked_) {
            scene_.camera.frameHeight = scene_.camera.frameWidth / lockedAspectRatio;
        }
        return ResetValueOnDoubleClick(scene_.camera.frameWidth, defaultCamera.frameWidth) || changed;
    });
    fieldLabel("Height");
    framingChanged = fieldWidget([&]() {
        bool changed = DragScalarWithInput("##camera_frame_height", ImGuiDataType_Double, &scene_.camera.frameHeight, 0.05f, &frameMin, &frameMax, "%.2f");
        if (changed && cameraAspectLocked_) {
            scene_.camera.frameWidth = scene_.camera.frameHeight * lockedAspectRatio;
        }
        return ResetValueOnDoubleClick(scene_.camera.frameHeight, defaultCamera.frameHeight) || changed;
    }) || framingChanged;
    scene_.camera.frameWidth = std::clamp(scene_.camera.frameWidth, frameMin, frameMax);
    scene_.camera.frameHeight = std::clamp(scene_.camera.frameHeight, frameMin, frameMax);
    endFieldGrid();

    ImGui::Spacing();
    const float framingActionsWidth = ImGui::GetContentRegionAvail().x;
    const float lockButtonWidth = ScaleUi(148.0f);
    const float swapButtonWidth = ScaleUi(132.0f);
    const float framingActionGap = ScaleUi(8.0f);
    const bool stackFramingActions = framingActionsWidth < (lockButtonWidth + swapButtonWidth + framingActionGap);
    if (DrawActionButton(
            "##camera_aspect_lock",
            cameraAspectLocked_ ? "Aspect Locked" : "Lock Aspect",
            cameraAspectLocked_ ? IconGlyph::Lock : IconGlyph::LockOpen,
            ActionTone::Accent,
            cameraAspectLocked_,
            true,
            stackFramingActions ? std::max(0.0f, framingActionsWidth) : lockButtonWidth,
            cameraAspectLocked_ ? "Unlock aspect ratio editing" : "Lock width and height to the current aspect ratio")) {
        cameraAspectLocked_ = !cameraAspectLocked_;
        if (cameraAspectLocked_) {
            captureLockedAspectRatio();
        }
    }
    if (!stackFramingActions) {
        ImGui::SameLine(0.0f, framingActionGap);
    }
    if (DrawActionButton(
            "##camera_swap_aspect",
            "Swap W/H",
            IconGlyph::SwapHoriz,
            ActionTone::Accent,
            false,
            true,
            stackFramingActions ? std::max(0.0f, framingActionsWidth) : swapButtonWidth,
            "Swap the camera gate between landscape and portrait")) {
        std::swap(scene_.camera.frameWidth, scene_.camera.frameHeight);
        if (cameraAspectLocked_) {
            captureLockedAspectRatio();
        }
        framingChanged = true;
    }
    captureCameraEdit(beforeFraming, framingChanged);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Viewport matte and export both follow this gate.");
    ImGui::PopTextWrapPos();

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
    const bool stackCameraActions = ImGui::GetContentRegionAvail().x < ScaleUi(kWideCameraActionsMinWidth);
    const float resetButtonWidth = stackCameraActions ? std::max(0.0f, ImGui::GetContentRegionAvail().x) : ScaleUi(150.0f);
    if (DrawActionButton("##camera_panel_reset_camera", "Reset Camera", IconGlyph::ResetCamera, ActionTone::Accent, false, true, resetButtonWidth)) {
        PushUndoState(scene_);
        scene_.camera = CameraState {};
        if (cameraAspectLocked_) {
            captureLockedAspectRatio();
        }
        AutoKeyCurrentFrame();
        MarkViewportDirty(PreviewResetReason::CameraChanged);
    }
    if (!stackCameraActions) {
        ImGui::SameLine();
    }
    const float exportButtonWidth = stackCameraActions ? std::max(0.0f, ImGui::GetContentRegionAvail().x) : ScaleUi(146.0f);
    if (DrawActionButton("##camera_panel_export", "Open Export", IconGlyph::ExportImage, ActionTone::Accent, exportPanelOpen_, true, exportButtonWidth)) {
        exportPanelOpen_ = !exportPanelOpen_;
        if (exportPanelOpen_) {
            OpenExportPanel();
        }
    }

    ImGui::End();
}

void AppWindow::DrawViewportPanel() {
    if (!viewportPanelOpen_) {
        return;
    }

    const UiTheme& theme = GetUiTheme();
    const std::string statusLine = WideToUtf8(statusText_)
        + " | Mode "
        + ToString(scene_.mode)
        + " | Preview "
        + std::to_string(std::max(1, UploadedViewportWidth()))
        + "x"
        + std::to_string(std::max(1, UploadedViewportHeight()));
    const float statusPaddingX = ScaleUi(kStatusPanelPaddingX);
    const float statusPaddingY = ScaleUi(kStatusPanelPaddingY);
    const float statusHeaderPaddingY = ScaleUi(kStatusPanelHeaderPaddingY);
    const float statusSeparatorHeight = ScaleUi(kStatusPanelSeparatorHeight);
    const float overlayMarginX = ScaleUi(kOverlayPanelMarginX);
    const float overlayMarginY = ScaleUi(kOverlayPanelMarginY);
    const StatusOverlayLayout minStatusLayout = BuildStatusOverlayLayout(
        statusLine,
        ScaleUi(kStatusPanelMinWrapWidth) + statusPaddingX * 2.0f,
        statusPaddingX,
        statusPaddingY,
        statusHeaderPaddingY,
        statusSeparatorHeight);
    const ImVec2 viewportMinSize(
        minStatusLayout.size.x + overlayMarginX * 2.0f,
        minStatusLayout.size.y + overlayMarginY * 2.0f);
    ImGui::SetNextWindowSizeConstraints(viewportMinSize, ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.panelBackgroundInset);
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    DrawDockPanelTabContextMenu("Viewport", viewportPanelOpen_);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const int width = std::max(1, static_cast<int>(available.x));
    const int height = std::max(1, static_cast<int>(available.y));
    const bool gpuPreviewRequested = gpuFlamePreviewEnabled_;
    const bool allowViewportRender = !bootstrapUiFramePending_;
    bool attemptedViewportRender = false;

    if (allowViewportRender
        && (gpuPreviewRequested
            || (renderBackend_ == nullptr || renderBackend_->CpuPreviewShaderResourceView() == nullptr)
            || UploadedViewportWidth() != width
            || UploadedViewportHeight() != height)) {
        attemptedViewportRender = true;
        RenderViewportIfNeeded(width, height);
    }

    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    const ImVec2 imageMax(imageMin.x + available.x, imageMin.y + available.y);
    bool hovered = false;

    const auto drawViewportPlaceholder = [&]() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 fillColor = ImGui::GetColorU32(theme.panelBackgroundInset);
        const ImU32 borderColor = ImGui::GetColorU32(theme.border);
        drawList->AddRectFilled(imageMin, imageMax, fillColor, theme.roundingSmall);
        drawList->AddRect(imageMin, imageMax, borderColor, theme.roundingSmall, 0, 1.0f);
        const char* label = bootstrapUiFramePending_ ? "Loading UI..." : "Loading preview...";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const float iconExtent = std::clamp(std::min(available.x, available.y) * 0.16f, 34.0f, 64.0f);
        const float blockHeight = iconExtent + 12.0f + textSize.y;
        const ImVec2 iconCenter(
            imageMin.x + available.x * 0.5f,
            imageMin.y + std::max(0.0f, (available.y - blockHeight) * 0.5f) + iconExtent * 0.5f);
        DrawAnimatedLoadingIcon(
            drawList,
            iconCenter,
            iconExtent,
            ImGui::GetColorU32(WithAlpha(theme.accentHover, 0.95f)));
        const ImVec2 textPos(
            imageMin.x + std::max(0.0f, (available.x - textSize.x) * 0.5f),
            iconCenter.y + iconExtent * 0.5f + 12.0f);
        drawList->AddText(textPos, ImGui::GetColorU32(theme.textMuted), label);
    };

    const BackendPreviewImage previewImage = renderBackend_ != nullptr
        ? renderBackend_->ResolvePreviewImage(BackendPreviewLookupRequest {
            previewProgress_.presentation,
            scene_.mode,
            scene_.gridVisible,
            CollectPreviewSurfaces()
        })
        : BackendPreviewImage {};
    if (previewImage.HasLayers()) {
        ImGui::Image(reinterpret_cast<ImTextureID>(previewImage.layers[0].textureId), available);
        hovered = ImGui::IsItemHovered();
        for (std::size_t layerIndex = 1; layerIndex < previewImage.layerCount; ++layerIndex) {
            ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(previewImage.layers[layerIndex].textureId), imageMin, imageMax);
        }
        if (previewImage.layerCount > 1) {
            hovered = hovered || ImGui::IsMouseHoveringRect(imageMin, imageMax);
        }
    } else {
        drawViewportPlaceholder();
    }

    if (available.x > 1.0f && available.y > 1.0f) {
        const ImRect imageRect(imageMin, imageMax);
        const ImRect cameraRect = CameraFrameRectInBounds(scene_.camera, imageRect);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 matteColor = ImGui::GetColorU32(theme.matte);
        const ImU32 outlineColor = ImGui::GetColorU32(theme.cameraFrame);
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
        const std::uint32_t targetIterations = previewProgress_.targetIterations;
        const std::uint32_t accumulated = previewProgress_.displayedIterations;
        if (targetIterations > 0 && accumulated < targetIterations) {
            const float progress = static_cast<float>(accumulated) / static_cast<float>(targetIterations);
            constexpr float barHeight = 2.0f;
            const ImVec2 barMin(imageMin.x, imageMax.y - barHeight);
            const ImVec2 barMax(imageMin.x + available.x * progress, imageMax.y);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(barMin, barMax, ImGui::GetColorU32(WithAlpha(theme.accent, 0.55f)));
        }
    }

    if (showStatusOverlay_ && imageMax.x > imageMin.x && imageMax.y > imageMin.y) {
        DrawStatusBar(imageMin, imageMax);
    }

    const bool viewportDirtyBeforeInteraction = IsViewportDirty();
    HandleViewportInteraction(allowViewportRender && hovered, width, height);
    const bool viewportDirtiedByInteraction = IsViewportDirty() && !viewportDirtyBeforeInteraction;

    if (allowViewportRender && IsViewportDirty() && (!attemptedViewportRender || viewportDirtiedByInteraction)) {
        RenderViewportIfNeeded(width, height);
    }

    ImGui::End();
}

void AppWindow::DrawStatusBar(const ImVec2& viewportMin, const ImVec2& viewportMax) {
    const ImRect viewportRect(viewportMin, viewportMax);
    if (viewportRect.GetWidth() <= 1.0f || viewportRect.GetHeight() <= 1.0f) {
        return;
    }

    const UiTheme& theme = GetUiTheme();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (drawList == nullptr) {
        return;
    }

    const std::string bodyText = WideToUtf8(statusText_)
        + " | Mode "
        + ToString(scene_.mode)
        + " | Preview "
        + std::to_string(std::max(1, UploadedViewportWidth()))
        + "x"
        + std::to_string(std::max(1, UploadedViewportHeight()));
    const float overlayMarginX = ScaleUi(kOverlayPanelMarginX);
    const float overlayMarginY = ScaleUi(kOverlayPanelMarginY);
    const float statusPaddingX = ScaleUi(kStatusPanelPaddingX);
    const float statusPaddingY = ScaleUi(kStatusPanelPaddingY);
    const float statusHeaderPaddingY = ScaleUi(kStatusPanelHeaderPaddingY);
    const float statusSeparatorHeight = ScaleUi(kStatusPanelSeparatorHeight);
    const float maxPanelWidth = std::max(1.0f, viewportRect.GetWidth() - overlayMarginX * 2.0f);
    const StatusOverlayLayout layout = BuildStatusOverlayLayout(
        bodyText,
        maxPanelWidth,
        statusPaddingX,
        statusPaddingY,
        statusHeaderPaddingY,
        statusSeparatorHeight);

    const ImVec2 panelMin(
        std::max(viewportRect.Min.x + overlayMarginX, viewportRect.Max.x - overlayMarginX - layout.size.x),
        viewportRect.Min.y + overlayMarginY);
    const ImVec2 panelMax(panelMin.x + layout.size.x, panelMin.y + layout.size.y);
    const ImVec2 headerMax(panelMax.x, panelMin.y + layout.headerHeight);
    const float separatorY = headerMax.y;

    drawList->PushClipRect(viewportRect.Min, viewportRect.Max, true);
    drawList->AddRectFilled(panelMin, panelMax, ImGui::GetColorU32(theme.panelBackgroundElevated), theme.roundingLarge);
    drawList->AddRectFilled(panelMin, headerMax, ImGui::GetColorU32(theme.panelBackgroundAlt), theme.roundingLarge, ImDrawFlags_RoundCornersTop);
    drawList->AddLine(
        ImVec2(panelMin.x, separatorY),
        ImVec2(panelMax.x, separatorY),
        ImGui::GetColorU32(theme.border),
        statusSeparatorHeight);
    drawList->AddRect(panelMin, panelMax, ImGui::GetColorU32(theme.border), theme.roundingLarge, 0, 1.0f);

    const ImVec2 titlePos(panelMin.x + statusPaddingX, panelMin.y + statusHeaderPaddingY);
    const ImVec2 bodyPos(panelMin.x + statusPaddingX, separatorY + statusPaddingY);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.text);
    ImGui::RenderTextEllipsis(
        drawList,
        titlePos,
        ImVec2(panelMax.x - statusPaddingX, headerMax.y),
        panelMax.x - statusPaddingX,
        "Status",
        nullptr,
        &layout.titleSize);
    ImGui::RenderTextEllipsis(
        drawList,
        bodyPos,
        ImVec2(panelMax.x - statusPaddingX, panelMax.y - statusPaddingY),
        panelMax.x - statusPaddingX,
        bodyText.c_str(),
        nullptr,
        &layout.bodySize);
    ImGui::PopStyleColor();
    drawList->PopClipRect();
}

}  // namespace radiary
