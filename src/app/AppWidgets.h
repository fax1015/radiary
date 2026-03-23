#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "core/Scene.h"

namespace radiary {

enum class ActionTone {
    Slate,
    Accent
};

struct UiTheme {
    ImVec4 appBackgroundTop;
    ImVec4 appBackgroundBottom;
    ImVec4 panelBackground;
    ImVec4 panelBackgroundAlt;
    ImVec4 panelBackgroundElevated;
    ImVec4 panelBackgroundInset;
    ImVec4 frameBackground;
    ImVec4 frameBackgroundHover;
    ImVec4 frameBackgroundActive;
    ImVec4 accent;
    ImVec4 accentHover;
    ImVec4 accentActive;
    ImVec4 accentSoft;
    ImVec4 accentSurface;
    ImVec4 border;
    ImVec4 borderStrong;
    ImVec4 borderSubtle;
    ImVec4 text;
    ImVec4 textMuted;
    ImVec4 textDim;
    ImVec4 textOnAccent;
    ImVec4 overlayScrim;
    ImVec4 matte;
    ImVec4 cameraFrame;
    float roundingSmall = 6.0f;
    float roundingMedium = 10.0f;
    float roundingLarge = 14.0f;
};

enum class IconGlyph {
    NewScene,
    OpenScene,
    SaveScene,
    SaveSceneAs,
    ExportImage,
    Randomize,
    Play,
    Pause,
    Grid,
    ArrowDropDown,
    ArrowDropUp,
    Settings,
    Undo,
    Redo,
    ChevronLeft,
    ChevronRight,
    PreviousKeyframe,
    NextKeyframe,
    Add,
    Remove,
    ResetCamera,
    VisibilityOn,
    VisibilityOff
};

void SetActionIconFont(ImFont* font);
const UiTheme& GetUiTheme();
void ApplyRadiaryStyle(ImGuiStyle& style);
void PushFloatingPanelStyle(bool compact = false);
void PopFloatingPanelStyle();

ImWchar IconGlyphCodepoint(IconGlyph glyph);
bool DrawMaterialFontIcon(ImDrawList* drawList, const ImRect& rect, IconGlyph glyph, ImU32 color, float scale = 0.98f);
void DrawIconGlyph(ImDrawList* drawList, const ImRect& rect, IconGlyph glyph, ImU32 color);

bool BeginComboWithMaterialArrow(const char* label, const char* previewValue, ImGuiComboFlags flags = 0);
bool ComboWithMaterialArrow(const char* label, int* currentItem, const char* const items[], int itemsCount);

template <typename T>
bool ResetValueOnDoubleClick(T& value, const T& defaultValue) {
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        value = defaultValue;
        return true;
    }
    return false;
}

bool SliderScalarWithInput(const char* label, ImGuiDataType dataType, void* value, const void* minimum, const void* maximum, const char* format, ImGuiSliderFlags flags = 0);
bool DragScalarWithInput(const char* label, ImGuiDataType dataType, void* value, float speed, const void* minimum, const void* maximum, const char* format, ImGuiSliderFlags flags = 0);
bool SliderIntWithInput(const char* label, int* value, int minimum, int maximum);
bool DragIntWithInput(const char* label, int* value, float speed, int minimum, int maximum);
bool ResetColorOnDoubleClick(float color[3], const Color& defaultColor);

bool DrawActionButton(
    const char* id,
    const char* label,
    IconGlyph glyph,
    ActionTone tone,
    bool toggled = false,
    bool enabled = true,
    float minWidth = 0.0f,
    const char* tooltip = nullptr);

void DrawSectionChip(const char* label, const ImVec4& fillColor);
void DrawSubtleToolbarDivider(float spacing = 10.0f);

float CurveEditorToScreenY(const ImRect& rect, double value);
double ScreenToCurveEditorY(const ImRect& rect, float y);
bool DrawBezierCurveEditor(const char* id, SceneKeyframe& keyframe);

template <typename Items>
std::string MakeUniqueCopyName(const std::string& sourceName, const std::string& fallbackName, const Items& items) {
    const std::string baseName = sourceName.empty() ? fallbackName : sourceName;
    std::string candidate = baseName + " Copy";
    int suffix = 2;
    const auto exists = [&](const std::string& name) {
        return std::any_of(items.begin(), items.end(), [&](const auto& item) {
            return item.name == name;
        });
    };
    while (exists(candidate)) {
        candidate = baseName + " Copy " + std::to_string(suffix++);
    }
    return candidate;
}

template <typename Layer>
int InsertLayerCopy(std::vector<Layer>& layers, int selectedIndex, Layer layer) {
    const int insertIndex = std::clamp(selectedIndex + 1, 0, static_cast<int>(layers.size()));
    layers.insert(layers.begin() + insertIndex, std::move(layer));
    return insertIndex;
}

bool ContainsIndex(const std::vector<int>& indices, int index);
void NormalizeIndices(std::vector<int>& indices, int itemCount, int& primaryIndex);

}  // namespace radiary
