#include "app/AppWidgets.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "misc/cpp/imgui_stdlib.h"

namespace radiary {

void PushMonospaceFont();
void PopMonospaceFont();

namespace {

constexpr float kComboArrowScale = 0.66f;

ImFont* gActionIconFont = nullptr;
ImFont* gMonospaceFont = nullptr;

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

const UiTheme kUiTheme {
    .appBackgroundTop = ImVec4(0.04f, 0.04f, 0.05f, 1.0f),
    .appBackgroundBottom = ImVec4(0.04f, 0.04f, 0.05f, 1.0f),
    .panelBackground = ImVec4(0.07f, 0.07f, 0.08f, 1.0f),
    .panelBackgroundAlt = ImVec4(0.10f, 0.10f, 0.12f, 1.0f),
    .panelBackgroundElevated = ImVec4(0.10f, 0.10f, 0.12f, 0.98f),
    .panelBackgroundInset = ImVec4(0.09f, 0.09f, 0.10f, 1.0f),
    .frameBackground = ImVec4(0.13f, 0.13f, 0.15f, 1.0f),
    .frameBackgroundHover = ImVec4(0.18f, 0.18f, 0.21f, 1.0f),
    .frameBackgroundActive = ImVec4(0.22f, 0.22f, 0.26f, 1.0f),
    .accent = ImVec4(0.44f, 0.58f, 0.92f, 1.0f),
    .accentHover = ImVec4(0.48f, 0.58f, 0.92f, 1.0f),
    .accentActive = ImVec4(0.36f, 0.47f, 0.78f, 1.0f),
    .accentSoft = ImVec4(0.44f, 0.58f, 0.92f, 0.12f),
    .accentSurface = ImVec4(0.13f, 0.19f, 0.32f, 1.0f),
    .border = ImVec4(0.30f, 0.29f, 0.28f, 0.42f),
    .borderStrong = ImVec4(0.34f, 0.33f, 0.31f, 0.52f),
    .borderSubtle = ImVec4(0.34f, 0.33f, 0.31f, 0.34f),
    .text = ImVec4(0.93f, 0.92f, 0.90f, 1.0f),
    .textMuted = ImVec4(0.72f, 0.73f, 0.79f, 1.0f),
    .textDim = ImVec4(0.62f, 0.60f, 0.57f, 1.0f),
    .textOnAccent = ImVec4(0.97f, 0.98f, 1.0f, 1.0f),
    .overlayScrim = ImVec4(0.03f, 0.03f, 0.04f, 0.78f),
    .matte = ImVec4(0.02f, 0.02f, 0.03f, 0.26f),
    .cameraFrame = ImVec4(0.86f, 0.87f, 0.90f, 0.42f),
    .roundingSmall = 6.0f,
    .roundingMedium = 10.0f,
    .roundingLarge = 14.0f,
};

struct ActionPalette {
    ImVec4 base;
    ImVec4 hover;
    ImVec4 active;
    ImVec4 border;
    ImVec4 text;
};

ActionPalette GetActionPalette(const ActionTone tone, const bool toggled) {
    switch (tone) {
    case ActionTone::Accent:
        return toggled
            ? ActionPalette {{0.23f, 0.27f, 0.34f, 1.0f}, {0.29f, 0.34f, 0.42f, 1.0f}, {0.19f, 0.23f, 0.29f, 1.0f}, {0.53f, 0.57f, 0.66f, 0.42f}, {0.95f, 0.95f, 0.94f, 1.0f}}
            : ActionPalette {{0.14f, 0.17f, 0.21f, 1.0f}, {0.19f, 0.22f, 0.27f, 1.0f}, {0.11f, 0.14f, 0.18f, 1.0f}, {0.35f, 0.39f, 0.46f, 0.34f}, {0.90f, 0.91f, 0.92f, 1.0f}};
    case ActionTone::Slate:
    default:
        return toggled
            ? ActionPalette {{0.27f, 0.29f, 0.33f, 1.0f}, {0.33f, 0.35f, 0.40f, 1.0f}, {0.21f, 0.23f, 0.27f, 1.0f}, {0.47f, 0.49f, 0.56f, 0.38f}, {0.93f, 0.93f, 0.92f, 1.0f}}
            : ActionPalette {{0.13f, 0.14f, 0.17f, 1.0f}, {0.18f, 0.19f, 0.23f, 1.0f}, {0.10f, 0.11f, 0.15f, 1.0f}, {0.29f, 0.31f, 0.37f, 0.30f}, {0.88f, 0.89f, 0.90f, 1.0f}};
    }
}

int ScalarFormatPrecision(const char* format) {
    if (format == nullptr) {
        return 0;
    }
    const char* dot = std::strchr(format, '.');
    if (dot == nullptr) {
        return 0;
    }
    int precision = 0;
    for (const char* current = dot + 1; *current >= '0' && *current <= '9'; ++current) {
        precision = precision * 10 + (*current - '0');
    }
    return precision;
}

double ScalarKeyboardStep(const ImGuiDataType dataType, const char* format) {
    switch (dataType) {
    case ImGuiDataType_Float:
    case ImGuiDataType_Double:
        return std::pow(10.0, -std::max(0, ScalarFormatPrecision(format)));
    case ImGuiDataType_U32:
    case ImGuiDataType_S32:
    default:
        return 1.0;
    }
}

template <typename T>
bool ApplyScalarDelta(T& value, const double delta, const T* minimum, const T* maximum) {
    const double current = static_cast<double>(value);
    double next = current + delta;
    if (minimum != nullptr) {
        next = std::max(next, static_cast<double>(*minimum));
    }
    if (maximum != nullptr) {
        next = std::min(next, static_cast<double>(*maximum));
    }
    const T castValue = static_cast<T>(next);
    if (castValue == value) {
        return false;
    }
    value = castValue;
    return true;
}

bool ApplyScalarKeyboardDelta(const ImGuiDataType dataType, void* value, const double delta, const void* minimum, const void* maximum) {
    switch (dataType) {
    case ImGuiDataType_S32:
        return ApplyScalarDelta(*static_cast<int*>(value), delta, static_cast<const int*>(minimum), static_cast<const int*>(maximum));
    case ImGuiDataType_U32:
        return ApplyScalarDelta(*static_cast<std::uint32_t*>(value), delta, static_cast<const std::uint32_t*>(minimum), static_cast<const std::uint32_t*>(maximum));
    case ImGuiDataType_Float:
        return ApplyScalarDelta(*static_cast<float*>(value), delta, static_cast<const float*>(minimum), static_cast<const float*>(maximum));
    case ImGuiDataType_Double:
        return ApplyScalarDelta(*static_cast<double*>(value), delta, static_cast<const double*>(minimum), static_cast<const double*>(maximum));
    default:
        return false;
    }
}

void ClampScalarValue(const ImGuiDataType dataType, void* value, const void* minimum, const void* maximum) {
    switch (dataType) {
    case ImGuiDataType_S32: {
        int& current = *static_cast<int*>(value);
        if (minimum != nullptr) {
            current = std::max(current, *static_cast<const int*>(minimum));
        }
        if (maximum != nullptr) {
            current = std::min(current, *static_cast<const int*>(maximum));
        }
        break;
    }
    case ImGuiDataType_U32: {
        std::uint32_t& current = *static_cast<std::uint32_t*>(value);
        if (minimum != nullptr) {
            current = std::max(current, *static_cast<const std::uint32_t*>(minimum));
        }
        if (maximum != nullptr) {
            current = std::min(current, *static_cast<const std::uint32_t*>(maximum));
        }
        break;
    }
    case ImGuiDataType_Float: {
        float& current = *static_cast<float*>(value);
        if (minimum != nullptr) {
            current = std::max(current, *static_cast<const float*>(minimum));
        }
        if (maximum != nullptr) {
            current = std::min(current, *static_cast<const float*>(maximum));
        }
        break;
    }
    case ImGuiDataType_Double: {
        double& current = *static_cast<double*>(value);
        if (minimum != nullptr) {
            current = std::max(current, *static_cast<const double*>(minimum));
        }
        if (maximum != nullptr) {
            current = std::min(current, *static_cast<const double*>(maximum));
        }
        break;
    }
    default:
        break;
    }
}

struct InlineScalarEditState {
    ImGuiID itemId = 0;
    bool focusPending = false;
    std::string buffer;
};

InlineScalarEditState& GetInlineScalarEditState() {
    static InlineScalarEditState state;
    return state;
}

class MathExpressionParser {
public:
    explicit MathExpressionParser(const char* text)
        : cursor_(text != nullptr ? text : "") {}

    bool Parse(double& result) {
        SkipWhitespace();
        if (*cursor_ == '\0') {
            return false;
        }
        const double value = ParseExpression();
        SkipWhitespace();
        if (!ok_ || *cursor_ != '\0' || !std::isfinite(value)) {
            return false;
        }
        result = value;
        return true;
    }

private:
    double ParseExpression() {
        double value = ParseTerm();
        while (ok_) {
            SkipWhitespace();
            if (Consume('+')) {
                value += ParseTerm();
            } else if (Consume('-')) {
                value -= ParseTerm();
            } else {
                break;
            }
        }
        return value;
    }

    double ParseTerm() {
        double value = ParseFactor();
        while (ok_) {
            SkipWhitespace();
            if (Consume('*')) {
                value *= ParseFactor();
            } else if (Consume('/')) {
                const double divisor = ParseFactor();
                if (std::abs(divisor) <= 1.0e-12) {
                    ok_ = false;
                    return 0.0;
                }
                value /= divisor;
            } else if (Consume('%')) {
                const double divisor = ParseFactor();
                if (std::abs(divisor) <= 1.0e-12) {
                    ok_ = false;
                    return 0.0;
                }
                value = std::fmod(value, divisor);
            } else {
                break;
            }
        }
        return value;
    }

    double ParseFactor() {
        SkipWhitespace();
        if (Consume('+')) {
            return ParseFactor();
        }
        if (Consume('-')) {
            return -ParseFactor();
        }
        if (Consume('(')) {
            const double value = ParseExpression();
            SkipWhitespace();
            if (!Consume(')')) {
                ok_ = false;
                return 0.0;
            }
            return value;
        }
        return ParseNumber();
    }

    double ParseNumber() {
        SkipWhitespace();
        errno = 0;
        char* end = nullptr;
        const double value = std::strtod(cursor_, &end);
        if (end == cursor_ || errno == ERANGE) {
            ok_ = false;
            return 0.0;
        }
        cursor_ = end;
        return value;
    }

    bool Consume(const char ch) {
        if (*cursor_ != ch) {
            return false;
        }
        ++cursor_;
        return true;
    }

    void SkipWhitespace() {
        while (*cursor_ != '\0' && std::isspace(static_cast<unsigned char>(*cursor_))) {
            ++cursor_;
        }
    }

    const char* cursor_ = "";
    bool ok_ = true;
};

std::string FormatScalarValue(const ImGuiDataType dataType, const void* value, const char* format) {
    char buffer[128] = {};
    ImGui::DataTypeFormatString(buffer, IM_ARRAYSIZE(buffer), dataType, value, format);
    return buffer;
}

bool AssignScalarExpressionValue(const ImGuiDataType dataType, void* value, const double evaluatedValue) {
    if (!std::isfinite(evaluatedValue)) {
        return false;
    }

    switch (dataType) {
    case ImGuiDataType_S32: {
        const double clamped = std::clamp(
            std::trunc(evaluatedValue),
            static_cast<double>(std::numeric_limits<int>::lowest()),
            static_cast<double>(std::numeric_limits<int>::max()));
        const int castValue = static_cast<int>(clamped);
        int& current = *static_cast<int*>(value);
        if (current == castValue) {
            return false;
        }
        current = castValue;
        return true;
    }
    case ImGuiDataType_U32: {
        const double clamped = std::clamp(
            std::trunc(evaluatedValue),
            0.0,
            static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
        const std::uint32_t castValue = static_cast<std::uint32_t>(clamped);
        std::uint32_t& current = *static_cast<std::uint32_t*>(value);
        if (current == castValue) {
            return false;
        }
        current = castValue;
        return true;
    }
    case ImGuiDataType_Float: {
        const float castValue = static_cast<float>(evaluatedValue);
        float& current = *static_cast<float*>(value);
        if (current == castValue) {
            return false;
        }
        current = castValue;
        return true;
    }
    case ImGuiDataType_Double: {
        double& current = *static_cast<double*>(value);
        if (current == evaluatedValue) {
            return false;
        }
        current = evaluatedValue;
        return true;
    }
    default:
        return false;
    }
}

bool CommitInlineScalarExpression(
    InlineScalarEditState& inlineEdit,
    const ImGuiDataType dataType,
    void* value,
    const void* minimum,
    const void* maximum,
    const char* format) {
    MathExpressionParser parser(inlineEdit.buffer.c_str());
    double evaluatedValue = 0.0;
    if (!parser.Parse(evaluatedValue)) {
        return false;
    }

    const bool changed = AssignScalarExpressionValue(dataType, value, evaluatedValue);
    ClampScalarValue(dataType, value, minimum, maximum);
    inlineEdit.buffer = FormatScalarValue(dataType, value, format);
    return changed;
}

bool HandleScalarInputAffordances(const ImRect& frameBounds, const ImGuiDataType dataType, void* value, const void* minimum, const void* maximum, const char* format) {
    ImGuiContext& g = *GImGui;
    const ImGuiID itemId = g.LastItemData.ID;
    if (itemId == 0) {
        return false;
    }

    bool valueChanged = false;
    InlineScalarEditState& inlineEdit = GetInlineScalarEditState();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool focused = ImGui::IsItemFocused();
    const bool active = ImGui::GetActiveID() == itemId;
    const bool editingThisItem = inlineEdit.itemId == itemId;

    if (!editingThisItem && (focused || active)) {
        ImGui::SetItemKeyOwner(ImGuiKey_LeftArrow, ImGuiInputFlags_None);
        ImGui::SetItemKeyOwner(ImGuiKey_RightArrow, ImGuiInputFlags_None);
        ImGui::SetItemKeyOwner(ImGuiKey_Enter, ImGuiInputFlags_None);
        ImGui::SetItemKeyOwner(ImGuiKey_KeypadEnter, ImGuiInputFlags_None);

        const double baseStep = ScalarKeyboardStep(dataType, format);
        const double stepScale = g.IO.KeyShift ? 10.0 : 1.0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
            valueChanged = ApplyScalarKeyboardDelta(dataType, value, -baseStep * stepScale, minimum, maximum) || valueChanged;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
            valueChanged = ApplyScalarKeyboardDelta(dataType, value, baseStep * stepScale, minimum, maximum) || valueChanged;
        }
    }

    if (!editingThisItem && hovered) {
        ImGui::SetItemKeyOwner(ImGuiKey_Enter, ImGuiInputFlags_None);
        ImGui::SetItemKeyOwner(ImGuiKey_KeypadEnter, ImGuiInputFlags_None);
    }

    const bool requestInlineInput = !editingThisItem && ((hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        || (focused && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))));

    if (requestInlineInput) {
        inlineEdit.itemId = itemId;
        inlineEdit.focusPending = true;
        inlineEdit.buffer = FormatScalarValue(dataType, value, format);
    }

    if (inlineEdit.itemId == itemId) {
        ImGuiWindow* window = g.CurrentWindow;
        const ImVec2 restoreCursor = window->DC.CursorPos;
        const ImVec2 restoreCursorPrevLine = window->DC.CursorPosPrevLine;
        const ImVec2 restoreCursorMax = window->DC.CursorMaxPos;
        const ImVec2 restoreIdealMax = window->DC.IdealMaxPos;
        const ImVec2 restoreCurrLineSize = window->DC.CurrLineSize;
        const ImVec2 restorePrevLineSize = window->DC.PrevLineSize;
        const float restoreCurrLineTextBaseOffset = window->DC.CurrLineTextBaseOffset;
        const float restorePrevLineTextBaseOffset = window->DC.PrevLineTextBaseOffset;
        const bool restoreIsSameLine = window->DC.IsSameLine;
        const bool openingThisFrame = requestInlineInput || inlineEdit.focusPending;
        ImGui::SetCursorScreenPos(frameBounds.Min);
        ImGui::SetNextItemWidth(frameBounds.GetWidth());
        ImGui::PushID(static_cast<int>(itemId));
        if (inlineEdit.focusPending) {
            ImGui::SetKeyboardFocusHere();
            inlineEdit.focusPending = false;
        }
        PushMonospaceFont();
        const bool submitted = ImGui::InputText(
            "##inline_scalar",
            &inlineEdit.buffer,
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
        PopMonospaceFont();
        const bool enterPressed = submitted;
        const bool deactivated = ImGui::IsItemDeactivated();
        const bool cancelRequested = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool commitRequested = !cancelRequested && (enterPressed || deactivated);
        if (commitRequested) {
            valueChanged = CommitInlineScalarExpression(inlineEdit, dataType, value, minimum, maximum, format) || valueChanged;
        }
        if (deactivated) {
            ClampScalarValue(dataType, value, minimum, maximum);
        }
        const bool closeRequested = cancelRequested || enterPressed || deactivated;
        const bool keepEditing = !closeRequested && (openingThisFrame || ImGui::IsItemActive() || ImGui::IsItemFocused());
        ImGui::PopID();
        // This editor is drawn as an overlay on top of the slider/drag widget, so restore layout state
        // directly instead of issuing another SetCursorScreenPos() with no following item.
        window->DC.CursorPos = restoreCursor;
        window->DC.CursorPosPrevLine = restoreCursorPrevLine;
        window->DC.CursorMaxPos = restoreCursorMax;
        window->DC.IdealMaxPos = restoreIdealMax;
        window->DC.CurrLineSize = restoreCurrLineSize;
        window->DC.PrevLineSize = restorePrevLineSize;
        window->DC.CurrLineTextBaseOffset = restoreCurrLineTextBaseOffset;
        window->DC.PrevLineTextBaseOffset = restorePrevLineTextBaseOffset;
        window->DC.IsSameLine = restoreIsSameLine;
        window->DC.IsSetPos = false;
        if (!keepEditing) {
            inlineEdit.itemId = 0;
            inlineEdit.focusPending = false;
            inlineEdit.buffer.clear();
        }
    }

    return valueChanged;
}

}  // namespace

void SetActionIconFont(ImFont* font) {
    gActionIconFont = font;
}

void SetMonospaceFont(ImFont* font) {
    gMonospaceFont = font;
}

void PushMonospaceFont() {
    if (gMonospaceFont != nullptr) {
        ImGui::PushFont(gMonospaceFont);
    }
}

void PopMonospaceFont() {
    if (gMonospaceFont != nullptr) {
        ImGui::PopFont();
    }
}

const UiTheme& GetUiTheme() {
    return kUiTheme;
}

void ApplyRadiaryStyle(ImGuiStyle& style) {
    const UiTheme& theme = GetUiTheme();
    style = ImGuiStyle {};
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowPadding = ImVec2(11.0f, 9.0f);
    style.FramePadding = ImVec2(9.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(6.0f, 4.0f);

    style.Colors[ImGuiCol_WindowBg] = theme.panelBackground;
    style.Colors[ImGuiCol_ChildBg] = theme.panelBackgroundInset;
    style.Colors[ImGuiCol_PopupBg] = theme.panelBackgroundElevated;
    style.Colors[ImGuiCol_Border] = theme.border;
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_FrameBg] = theme.frameBackground;
    style.Colors[ImGuiCol_FrameBgHovered] = theme.frameBackgroundHover;
    style.Colors[ImGuiCol_FrameBgActive] = theme.frameBackgroundActive;
    style.Colors[ImGuiCol_TitleBg] = theme.panelBackgroundAlt;
    style.Colors[ImGuiCol_TitleBgActive] = theme.frameBackground;
    style.Colors[ImGuiCol_TitleBgCollapsed] = theme.panelBackground;
    style.Colors[ImGuiCol_MenuBarBg] = theme.panelBackgroundAlt;
    style.Colors[ImGuiCol_ScrollbarBg] = theme.panelBackgroundInset;
    style.Colors[ImGuiCol_ScrollbarGrab] = Mix(theme.frameBackground, theme.borderStrong, 0.55f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = theme.frameBackgroundHover;
    style.Colors[ImGuiCol_ScrollbarGrabActive] = theme.frameBackgroundActive;
    style.Colors[ImGuiCol_CheckMark] = theme.accentHover;
    style.Colors[ImGuiCol_SliderGrab] = theme.accent;
    style.Colors[ImGuiCol_SliderGrabActive] = theme.accentHover;
    style.Colors[ImGuiCol_Button] = theme.panelBackgroundAlt;
    style.Colors[ImGuiCol_ButtonHovered] = theme.frameBackgroundHover;
    style.Colors[ImGuiCol_ButtonActive] = theme.frameBackgroundActive;
    style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.19f, 0.96f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.21f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.27f, 0.31f, 1.0f);
    style.Colors[ImGuiCol_Separator] = theme.borderSubtle;
    style.Colors[ImGuiCol_SeparatorHovered] = WithAlpha(theme.accent, 0.62f);
    style.Colors[ImGuiCol_SeparatorActive] = WithAlpha(theme.accentHover, 0.86f);
    style.Colors[ImGuiCol_ResizeGrip] = WithAlpha(theme.accent, 0.18f);
    style.Colors[ImGuiCol_ResizeGripHovered] = WithAlpha(theme.accent, 0.46f);
    style.Colors[ImGuiCol_ResizeGripActive] = WithAlpha(theme.accentHover, 0.68f);
    style.Colors[ImGuiCol_Tab] = theme.panelBackgroundAlt;
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.21f, 0.20f, 0.23f, 0.98f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.17f, 0.17f, 0.20f, 0.98f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.09f, 0.09f, 0.11f, 0.86f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.17f, 0.92f);
    style.Colors[ImGuiCol_DockingPreview] = WithAlpha(theme.accent, 0.24f);
    style.Colors[ImGuiCol_DockingEmptyBg] = theme.appBackgroundBottom;
    style.Colors[ImGuiCol_Text] = theme.text;
    style.Colors[ImGuiCol_TextDisabled] = theme.textDim;
    style.Colors[ImGuiCol_TextSelectedBg] = WithAlpha(theme.accent, 0.28f);
    style.Colors[ImGuiCol_PlotHistogram] = theme.accent;
    style.Colors[ImGuiCol_PlotHistogramHovered] = theme.accentHover;
    style.Colors[ImGuiCol_TableHeaderBg] = theme.panelBackgroundAlt;
    style.Colors[ImGuiCol_TableBorderStrong] = theme.borderStrong;
    style.Colors[ImGuiCol_TableBorderLight] = theme.borderSubtle;
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = WithAlpha(theme.panelBackgroundAlt, 0.34f);
    style.Colors[ImGuiCol_NavCursor] = WithAlpha(theme.accentHover, 0.90f);
}

void PushFloatingPanelStyle(const bool compact) {
    const UiTheme& theme = GetUiTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, compact ? theme.roundingMedium : theme.roundingLarge);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, compact ? ImVec2(8.0f, 8.0f) : ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, compact ? ImVec2(8.0f, 7.0f) : ImVec2(10.0f, 9.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.panelBackgroundElevated);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.border);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, theme.panelBackgroundAlt);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, theme.frameBackground);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, theme.panelBackgroundAlt);
}

void PopFloatingPanelStyle() {
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);
}

ImWchar IconGlyphCodepoint(const IconGlyph glyph) {
    switch (glyph) {
    case IconGlyph::Undo:
        return 0xE166;
    case IconGlyph::Redo:
        return 0xE15A;
    case IconGlyph::NewScene:
        return 0xE89C;
    case IconGlyph::OpenScene:
        return 0xE2C8;
    case IconGlyph::SaveScene:
        return 0xE161;
    case IconGlyph::SaveSceneAs:
        return 0xEB60;
    case IconGlyph::ExportImage:
        return 0xF09B;
    case IconGlyph::Randomize:
        return 0xE043;
    case IconGlyph::Play:
        return 0xE037;
    case IconGlyph::Pause:
        return 0xE034;
    case IconGlyph::Grid:
        return 0xF015;
    case IconGlyph::ArrowDropDown:
        return 0xE5C5;
    case IconGlyph::ArrowDropUp:
        return 0xE5C7;
    case IconGlyph::Settings:
        return 0xE8B8;
    case IconGlyph::ChevronLeft:
        return 0xE5CB;
    case IconGlyph::ChevronRight:
        return 0xE5CC;
    case IconGlyph::PreviousKeyframe:
        return 0xE045;
    case IconGlyph::NextKeyframe:
        return 0xE044;
    case IconGlyph::Add:
        return 0xE145;
    case IconGlyph::Remove:
        return 0xE15B;
    case IconGlyph::ResetCamera:
        return 0xE412;
    case IconGlyph::VisibilityOn:
        return 0xE8F4;
    case IconGlyph::VisibilityOff:
        return 0xE8F5;
    }
    return 0;
}

bool DrawMaterialFontIcon(ImDrawList* drawList, const ImRect& rect, const IconGlyph glyph, const ImU32 color, const float scale) {
    if (gActionIconFont == nullptr) {
        return false;
    }
    const ImWchar codepoint = IconGlyphCodepoint(glyph);
    if (codepoint == 0) {
        return false;
    }

    char iconUtf8[8] = {};
    const int iconLength = ImTextCharToUtf8(iconUtf8, codepoint);
    if (iconLength <= 0) {
        return false;
    }
    iconUtf8[iconLength] = '\0';
    const float iconFontSize = rect.GetHeight() * scale;
    const ImVec2 glyphSize = gActionIconFont->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, iconUtf8);
    const ImVec2 glyphPos(
        rect.Min.x + (rect.GetWidth() - glyphSize.x) * 0.5f,
        rect.Min.y + (rect.GetHeight() - glyphSize.y) * 0.5f + 0.5f);
    drawList->AddText(gActionIconFont, iconFontSize, glyphPos, color, iconUtf8);
    return true;
}

bool BeginComboWithMaterialArrow(const char* label, const char* previewValue, ImGuiComboFlags flags) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    const ImGuiNextWindowDataFlags backupNextWindowDataFlags = g.NextWindowData.HasFlags;
    g.NextWindowData.ClearFlags();
    if (window->SkipItems) {
        return false;
    }

    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    IM_ASSERT((flags & (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)) != (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview));
    if (flags & ImGuiComboFlags_WidthFitPreview) {
        IM_ASSERT((flags & (ImGuiComboFlags_NoPreview | static_cast<ImGuiComboFlags>(ImGuiComboFlags_CustomPreview))) == 0);
    }

    const float arrowSize = (flags & ImGuiComboFlags_NoArrowButton) ? 0.0f : ImGui::GetFrameHeight();
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float previewWidth =
        ((flags & ImGuiComboFlags_WidthFitPreview) && (previewValue != nullptr))
        ? ImGui::CalcTextSize(previewValue, nullptr, true).x
        : 0.0f;
    const float width =
        (flags & ImGuiComboFlags_NoPreview)
        ? arrowSize
        : ((flags & ImGuiComboFlags_WidthFitPreview)
            ? (arrowSize + previewWidth + style.FramePadding.x * 2.0f)
            : ImGui::CalcItemWidth());
    const ImRect bb(
        window->DC.CursorPos,
        ImVec2(window->DC.CursorPos.x + width, window->DC.CursorPos.y + labelSize.y + style.FramePadding.y * 2.0f));
    const ImRect totalBb(
        bb.Min,
        ImVec2(bb.Max.x + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f), bb.Max.y));
    ImGui::ItemSize(totalBb, style.FramePadding.y);
    if (!ImGui::ItemAdd(totalBb, id, &bb)) {
        return false;
    }

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const ImGuiID popupId = ImHashStr("##ComboPopup", 0, id);
    bool popupOpen = ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_None);
    if (pressed && !popupOpen) {
        ImGui::OpenPopupEx(popupId, ImGuiPopupFlags_None);
        popupOpen = true;
    }

    const ImU32 frameColor = ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    const float valueX2 = ImMax(bb.Min.x, bb.Max.x - arrowSize);
    ImGui::RenderNavCursor(bb, id);
    if (!(flags & ImGuiComboFlags_NoPreview)) {
        window->DrawList->AddRectFilled(
            bb.Min,
            ImVec2(valueX2, bb.Max.y),
            frameColor,
            style.FrameRounding,
            (flags & ImGuiComboFlags_NoArrowButton) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
    }
    if (!(flags & ImGuiComboFlags_NoArrowButton)) {
        const ImU32 buttonColor = ImGui::GetColorU32((popupOpen || hovered) ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        const ImVec2 arrowButtonMin(valueX2, bb.Min.y);
        const ImVec2 arrowButtonMax(bb.Max.x, bb.Max.y);
        window->DrawList->AddRectFilled(
            arrowButtonMin,
            arrowButtonMax,
            buttonColor,
            style.FrameRounding,
            (width <= arrowSize) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersRight);
        if (valueX2 + arrowSize - style.FramePadding.x <= bb.Max.x) {
            const float arrowFontSize = ImGui::GetFontSize();
            const ImVec2 arrowCenter(
                (arrowButtonMin.x + arrowButtonMax.x) * 0.5f,
                (arrowButtonMin.y + arrowButtonMax.y) * 0.5f);
            const ImVec2 arrowPos(
                arrowCenter.x - arrowFontSize * 0.5f,
                arrowCenter.y - arrowFontSize * 0.5f * kComboArrowScale);
            ImGui::RenderArrow(
                window->DrawList,
                arrowPos,
                textColor,
                ImGuiDir_Down,
                kComboArrowScale);
        }
    }
    ImGui::RenderFrameBorder(bb.Min, bb.Max, style.FrameRounding);

    if (flags & static_cast<ImGuiComboFlags>(ImGuiComboFlags_CustomPreview)) {
        g.ComboPreviewData.PreviewRect = ImRect(bb.Min.x, bb.Min.y, valueX2, bb.Max.y);
        IM_ASSERT(previewValue == nullptr || previewValue[0] == 0);
        previewValue = nullptr;
    }

    if (previewValue != nullptr && !(flags & ImGuiComboFlags_NoPreview)) {
        if (g.LogEnabled) {
            ImGui::LogSetNextTextDecoration("{", "}");
        }
        ImGui::RenderTextClipped(
            ImVec2(bb.Min.x + style.FramePadding.x, bb.Min.y + style.FramePadding.y),
            ImVec2(valueX2, bb.Max.y),
            previewValue,
            nullptr,
            nullptr);
    }
    if (labelSize.x > 0.0f) {
        ImGui::RenderText(ImVec2(bb.Max.x + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y), label);
    }

    if (!popupOpen) {
        return false;
    }

    g.NextWindowData.HasFlags = backupNextWindowDataFlags;
    return ImGui::BeginComboPopup(popupId, bb, flags);
}

bool ComboWithMaterialArrow(const char* label, int* currentItem, const char* const items[], const int itemsCount) {
    const char* previewValue =
        (currentItem != nullptr && *currentItem >= 0 && *currentItem < itemsCount)
        ? items[*currentItem]
        : "";
    bool changed = false;
    if (BeginComboWithMaterialArrow(label, previewValue, 0)) {
        for (int index = 0; index < itemsCount; ++index) {
            const bool selected = currentItem != nullptr && *currentItem == index;
            if (ImGui::Selectable(items[index], selected)) {
                if (currentItem != nullptr) {
                    *currentItem = index;
                }
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool SliderScalarWithInput(const char* label, const ImGuiDataType dataType, void* value, const void* minimum, const void* maximum, const char* format, const ImGuiSliderFlags flags) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 frameMin = ImGui::GetCursorScreenPos();
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float frameWidth = ImGui::CalcItemWidth();
    const ImRect frameBounds(frameMin, ImVec2(frameMin.x + frameWidth, frameMin.y + labelSize.y + style.FramePadding.y * 2.0f));
    PushMonospaceFont();
    const bool changed = ImGui::SliderScalar(label, dataType, value, minimum, maximum, format, flags | ImGuiSliderFlags_AlwaysClamp);
    PopMonospaceFont();
    const bool affordanceChanged = HandleScalarInputAffordances(frameBounds, dataType, value, minimum, maximum, format);
    return changed || affordanceChanged;
}

bool DragScalarWithInput(const char* label, const ImGuiDataType dataType, void* value, const float speed, const void* minimum, const void* maximum, const char* format, const ImGuiSliderFlags flags) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 frameMin = ImGui::GetCursorScreenPos();
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float frameWidth = ImGui::CalcItemWidth();
    const ImRect frameBounds(frameMin, ImVec2(frameMin.x + frameWidth, frameMin.y + labelSize.y + style.FramePadding.y * 2.0f));
    PushMonospaceFont();
    const bool changed = ImGui::DragScalar(label, dataType, value, speed, minimum, maximum, format, flags);
    PopMonospaceFont();
    const bool affordanceChanged = HandleScalarInputAffordances(frameBounds, dataType, value, minimum, maximum, format);
    return changed || affordanceChanged;
}

bool SliderIntWithInput(const char* label, int* value, const int minimum, const int maximum) {
    return SliderScalarWithInput(label, ImGuiDataType_S32, value, &minimum, &maximum, "%d");
}

bool DragIntWithInput(const char* label, int* value, const float speed, const int minimum, const int maximum) {
    return DragScalarWithInput(label, ImGuiDataType_S32, value, speed, &minimum, &maximum, "%d");
}

bool ResetColorOnDoubleClick(float color[3], const Color& defaultColor) {
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        color[0] = defaultColor.r / 255.0f;
        color[1] = defaultColor.g / 255.0f;
        color[2] = defaultColor.b / 255.0f;
        return true;
    }
    return false;
}

void DrawIconGlyph(ImDrawList* drawList, const ImRect& rect, const IconGlyph glyph, const ImU32 color) {
    const ImRect shiftedRect(
        ImVec2(rect.Min.x, rect.Min.y + 1.0f),
        ImVec2(rect.Max.x, rect.Max.y + 1.0f));
    const float width = rect.GetWidth();
    const float height = rect.GetHeight();
    const float left = shiftedRect.Min.x;
    const float top = shiftedRect.Min.y;
    const float right = shiftedRect.Max.x;
    const float stroke = std::max(1.45f, width * 0.078f);
    const auto point = [&](const float x, const float y) {
        return ImVec2(left + width * x, top + height * y);
    };
    const auto line = [&](const float x1, const float y1, const float x2, const float y2) {
        const ImVec2 a = point(x1, y1);
        const ImVec2 b = point(x2, y2);
        drawList->AddLine(a, b, color, stroke);
        drawList->AddCircleFilled(a, stroke * 0.34f, color);
        drawList->AddCircleFilled(b, stroke * 0.34f, color);
    };
    const auto circle = [&](const float x, const float y, const float radius) {
        drawList->AddCircle(point(x, y), width * radius, color, 0, stroke);
    };
    const auto polyline = [&](std::initializer_list<ImVec2> pts, const bool closed = false) {
        std::vector<ImVec2> transformed;
        transformed.reserve(pts.size());
        for (const ImVec2& p : pts) {
            transformed.push_back(point(p.x, p.y));
        }
        drawList->AddPolyline(transformed.data(), static_cast<int>(transformed.size()), color, closed ? ImDrawFlags_Closed : 0, stroke);
    };
    const auto bezier = [&](const float x1, const float y1, const float cx1, const float cy1, const float cx2, const float cy2, const float x2, const float y2) {
        drawList->AddBezierCubic(point(x1, y1), point(cx1, cy1), point(cx2, cy2), point(x2, y2), color, stroke);
    };

    (void)right;

    switch (glyph) {
    case IconGlyph::NewScene:
        polyline({ImVec2(0.26f, 0.16f), ImVec2(0.62f, 0.16f), ImVec2(0.78f, 0.32f), ImVec2(0.78f, 0.84f), ImVec2(0.26f, 0.84f)}, true);
        line(0.60f, 0.16f, 0.60f, 0.34f);
        line(0.60f, 0.34f, 0.78f, 0.34f);
        line(0.50f, 0.42f, 0.50f, 0.70f);
        line(0.36f, 0.56f, 0.64f, 0.56f);
        break;
    case IconGlyph::OpenScene:
        polyline({ImVec2(0.16f, 0.38f), ImVec2(0.30f, 0.22f), ImVec2(0.50f, 0.22f), ImVec2(0.58f, 0.32f), ImVec2(0.84f, 0.32f), ImVec2(0.74f, 0.80f), ImVec2(0.18f, 0.80f)}, true);
        line(0.58f, 0.54f, 0.36f, 0.54f);
        line(0.36f, 0.54f, 0.46f, 0.44f);
        line(0.36f, 0.54f, 0.46f, 0.64f);
        break;
    case IconGlyph::SaveScene:
        drawList->AddRect(point(0.22f, 0.16f), point(0.78f, 0.84f), color, 4.0f, 0, stroke);
        drawList->AddRect(point(0.32f, 0.20f), point(0.62f, 0.38f), color, 3.0f, 0, stroke);
        drawList->AddRect(point(0.32f, 0.54f), point(0.68f, 0.74f), color, 3.0f, 0, stroke);
        line(0.66f, 0.20f, 0.66f, 0.40f);
        break;
    case IconGlyph::ExportImage:
        line(0.50f, 0.18f, 0.50f, 0.58f);
        line(0.50f, 0.18f, 0.38f, 0.30f);
        line(0.50f, 0.18f, 0.62f, 0.30f);
        line(0.22f, 0.70f, 0.78f, 0.70f);
        line(0.22f, 0.70f, 0.22f, 0.54f);
        line(0.78f, 0.70f, 0.78f, 0.54f);
        break;
    case IconGlyph::Randomize:
        line(0.50f, 0.16f, 0.50f, 0.36f);
        line(0.40f, 0.26f, 0.60f, 0.26f);
        line(0.22f, 0.56f, 0.34f, 0.56f);
        line(0.28f, 0.50f, 0.28f, 0.62f);
        line(0.66f, 0.62f, 0.82f, 0.62f);
        line(0.74f, 0.54f, 0.74f, 0.70f);
        break;
    case IconGlyph::Play:
        polyline({ImVec2(0.34f, 0.24f), ImVec2(0.74f, 0.50f), ImVec2(0.34f, 0.76f)}, true);
        break;
    case IconGlyph::Pause:
        line(0.38f, 0.24f, 0.38f, 0.76f);
        line(0.62f, 0.24f, 0.62f, 0.76f);
        break;
    case IconGlyph::Grid:
        drawList->AddRect(point(0.20f, 0.20f), point(0.80f, 0.80f), color, 4.0f, 0, stroke);
        line(0.40f, 0.20f, 0.40f, 0.80f);
        line(0.60f, 0.20f, 0.60f, 0.80f);
        line(0.20f, 0.40f, 0.80f, 0.40f);
        line(0.20f, 0.60f, 0.80f, 0.60f);
        break;
    case IconGlyph::Settings:
        for (int index = 0; index < 3; ++index) {
            const float y = top + height * (0.28f + static_cast<float>(index) * 0.22f);
            drawList->AddLine(ImVec2(left + width * 0.22f, y), ImVec2(left + width * 0.78f, y), color, stroke);
        }
        circle(0.38f, 0.28f, 0.07f);
        circle(0.66f, 0.50f, 0.07f);
        circle(0.44f, 0.72f, 0.07f);
        break;
    case IconGlyph::Undo:
        bezier(0.74f, 0.34f, 0.56f, 0.18f, 0.34f, 0.24f, 0.34f, 0.48f);
        bezier(0.34f, 0.48f, 0.34f, 0.68f, 0.54f, 0.74f, 0.72f, 0.60f);
        line(0.34f, 0.48f, 0.20f, 0.34f);
        line(0.34f, 0.48f, 0.18f, 0.50f);
        break;
    case IconGlyph::Redo:
        bezier(0.26f, 0.34f, 0.44f, 0.18f, 0.66f, 0.24f, 0.66f, 0.48f);
        bezier(0.66f, 0.48f, 0.66f, 0.68f, 0.46f, 0.74f, 0.28f, 0.60f);
        line(0.66f, 0.48f, 0.80f, 0.34f);
        line(0.66f, 0.48f, 0.82f, 0.50f);
        break;
    case IconGlyph::ChevronLeft:
        polyline({ImVec2(0.62f, 0.24f), ImVec2(0.38f, 0.50f), ImVec2(0.62f, 0.76f)});
        break;
    case IconGlyph::ChevronRight:
        polyline({ImVec2(0.38f, 0.24f), ImVec2(0.62f, 0.50f), ImVec2(0.38f, 0.76f)});
        break;
    case IconGlyph::PreviousKeyframe:
        line(0.26f, 0.24f, 0.26f, 0.76f);
        polyline({ImVec2(0.64f, 0.24f), ImVec2(0.40f, 0.50f), ImVec2(0.64f, 0.76f)});
        break;
    case IconGlyph::NextKeyframe:
        line(0.74f, 0.24f, 0.74f, 0.76f);
        polyline({ImVec2(0.36f, 0.24f), ImVec2(0.60f, 0.50f), ImVec2(0.36f, 0.76f)});
        break;
    case IconGlyph::Add:
        line(0.50f, 0.24f, 0.50f, 0.76f);
        line(0.24f, 0.50f, 0.76f, 0.50f);
        break;
    case IconGlyph::Remove:
        line(0.24f, 0.50f, 0.76f, 0.50f);
        break;
    case IconGlyph::ResetCamera:
        drawList->AddRect(point(0.22f, 0.34f), point(0.78f, 0.78f), color, 4.0f, 0, stroke);
        drawList->AddRect(point(0.32f, 0.24f), point(0.46f, 0.34f), color, 3.0f, 0, stroke);
        circle(0.50f, 0.56f, 0.12f);
        bezier(0.64f, 0.28f, 0.72f, 0.18f, 0.84f, 0.20f, 0.84f, 0.34f);
        line(0.84f, 0.34f, 0.74f, 0.28f);
        line(0.84f, 0.34f, 0.74f, 0.40f);
        break;
    case IconGlyph::VisibilityOn:
        bezier(0.14f, 0.50f, 0.28f, 0.22f, 0.72f, 0.22f, 0.86f, 0.50f);
        bezier(0.14f, 0.50f, 0.28f, 0.78f, 0.72f, 0.78f, 0.86f, 0.50f);
        circle(0.50f, 0.50f, 0.12f);
        break;
    case IconGlyph::VisibilityOff:
        bezier(0.14f, 0.50f, 0.28f, 0.22f, 0.72f, 0.22f, 0.86f, 0.50f);
        bezier(0.14f, 0.50f, 0.28f, 0.78f, 0.72f, 0.78f, 0.86f, 0.50f);
        circle(0.50f, 0.50f, 0.12f);
        line(0.24f, 0.78f, 0.76f, 0.22f);
        break;
    }
}

bool DrawActionButton(
    const char* id,
    const char* label,
    const IconGlyph glyph,
    const ActionTone tone,
    const bool toggled,
    const bool enabled,
    const float minWidth,
    const char* tooltip) {
    const bool iconOnly = label == nullptr || label[0] == '\0';
    const float height = ImGui::GetFrameHeight();
    const ImVec2 textSize = iconOnly ? ImVec2(0.0f, 0.0f) : ImGui::CalcTextSize(label);
    const float horizontalPadding = 10.0f;
    const float iconBox = height - 8.0f;
    const float labelGap = 6.0f;
    const float contentWidth = iconOnly ? iconBox : iconBox + labelGap + textSize.x;
    const float buttonWidth = iconOnly ? height : std::max(minWidth, contentWidth + horizontalPadding * 2.0f);
    const ImVec2 size(buttonWidth, height);
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    if (!enabled) {
        ImGui::BeginDisabled();
    }
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (!enabled) {
        ImGui::EndDisabled();
    }

    const ActionPalette palette = GetActionPalette(tone, toggled);
    const float alpha = enabled ? 1.0f : ImGui::GetStyle().DisabledAlpha;
    const ImVec4 fill = held ? palette.active : hovered ? palette.hover : palette.base;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImRect rect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ImVec4(fill.x, fill.y, fill.z, fill.w * alpha)), 8.0f);
    drawList->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(ImVec4(palette.border.x, palette.border.y, palette.border.z, palette.border.w * alpha)), 8.0f, 0, 1.0f);

    const float iconLeft = iconOnly ? rect.Min.x + (buttonWidth - iconBox) * 0.5f : rect.Min.x + horizontalPadding;
    const float iconTop = rect.Min.y + (height - iconBox) * 0.5f;
    const ImU32 iconColor = ImGui::GetColorU32(ImVec4(palette.text.x, palette.text.y, palette.text.z, palette.text.w * alpha));
    const bool drewFontIcon = DrawMaterialFontIcon(
        drawList,
        ImRect(ImVec2(iconLeft, iconTop), ImVec2(iconLeft + iconBox, iconTop + iconBox)),
        glyph,
        iconColor);
    if (!drewFontIcon) {
        DrawIconGlyph(drawList, ImRect(ImVec2(iconLeft, iconTop), ImVec2(iconLeft + iconBox, iconTop + iconBox)), glyph, iconColor);
    }

    if (!iconOnly) {
        drawList->AddText(ImVec2(iconLeft + iconBox + labelGap, rect.Min.y + (height - textSize.y) * 0.5f), ImGui::GetColorU32(ImVec4(palette.text.x, palette.text.y, palette.text.z, palette.text.w * alpha)), label);
    }

    if (hovered && tooltip != nullptr && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
    }

    return enabled && pressed;
}

void DrawSectionChip(const char* label, const ImVec4& fillColor) {
    (void)fillColor;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.58f, 0.56f, 0.52f, 0.82f), "%s", label);
}

void DrawSubtleToolbarDivider(const float spacing) {
    const UiTheme& theme = GetUiTheme();
    ImGui::SameLine(0.0f, spacing);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight() - 8.0f;
    ImGui::Dummy(ImVec2(6.0f, ImGui::GetFrameHeight()));
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(pos.x + 3.0f, pos.y + 4.0f),
        ImVec2(pos.x + 3.0f, pos.y + 4.0f + height),
        ImGui::GetColorU32(theme.borderSubtle),
        1.0f);
    ImGui::SameLine(0.0f, spacing);
}

float CurveEditorToScreenY(const ImRect& rect, const double value) {
    const double normalized = Clamp((1.2 - value) / 1.4, 0.0, 1.0);
    return rect.Min.y + static_cast<float>(normalized) * rect.GetHeight();
}

double ScreenToCurveEditorY(const ImRect& rect, const float y) {
    const double normalized = Clamp((y - rect.Min.y) / std::max(1.0f, rect.GetHeight()), 0.0, 1.0);
    return 1.2 - normalized * 1.4;
}

bool DrawBezierCurveEditor(const char* id, SceneKeyframe& keyframe) {
    const UiTheme& theme = GetUiTheme();
    const ImVec2 size(220.0f, 126.0f);
    ImGui::InvisibleButton(id, size);
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 border = ImGui::GetColorU32(WithAlpha(theme.borderStrong, 0.72f));
    const ImU32 fill = ImGui::GetColorU32(theme.panelBackgroundInset);
    const ImU32 grid = ImGui::GetColorU32(WithAlpha(theme.borderSubtle, 0.70f));
    const ImU32 curve = ImGui::GetColorU32(theme.text);
    const ImU32 handle = ImGui::GetColorU32(theme.accentHover);
    drawList->AddRectFilled(rect.Min, rect.Max, fill, theme.roundingMedium);
    drawList->AddRect(rect.Min, rect.Max, border, theme.roundingMedium);
    for (int step = 1; step < 4; ++step) {
        const float x = rect.Min.x + rect.GetWidth() * (static_cast<float>(step) / 4.0f);
        const float y = rect.Min.y + rect.GetHeight() * (static_cast<float>(step) / 4.0f);
        drawList->AddLine(ImVec2(x, rect.Min.y), ImVec2(x, rect.Max.y), grid);
        drawList->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), grid);
    }

    if (keyframe.easing == KeyframeEasing::Hold) {
        const float midY = CurveEditorToScreenY(rect, 0.0);
        drawList->AddLine(ImVec2(rect.Min.x, midY), ImVec2(rect.Max.x, midY), curve, 2.0f);
        drawList->AddLine(ImVec2(rect.Max.x, midY), ImVec2(rect.Max.x, CurveEditorToScreenY(rect, 1.0)), curve, 2.0f);
        return false;
    }

    const auto handlePos = [&](const double hx, const double hy) {
        return ImVec2(
            rect.Min.x + static_cast<float>(Clamp(hx, 0.0, 1.0)) * rect.GetWidth(),
            CurveEditorToScreenY(rect, hy));
    };
    ImVec2 p0 = handlePos(0.0, 0.0);
    ImVec2 p1 = handlePos(keyframe.easeX1, keyframe.easeY1);
    ImVec2 p2 = handlePos(keyframe.easeX2, keyframe.easeY2);
    ImVec2 p3 = handlePos(1.0, 1.0);

    drawList->AddLine(p0, p1, grid, 1.0f);
    drawList->AddLine(p2, p3, grid, 1.0f);
    ImVec2 previous = p0;
    for (int step = 1; step <= 48; ++step) {
        const float t = static_cast<float>(step) / 48.0f;
        const float u = 1.0f - t;
        ImVec2 curvePoint(
            u * u * u * p0.x + 3.0f * u * u * t * p1.x + 3.0f * u * t * t * p2.x + t * t * t * p3.x,
            u * u * u * p0.y + 3.0f * u * u * t * p1.y + 3.0f * u * t * t * p2.y + t * t * t * p3.y);
        drawList->AddLine(previous, curvePoint, curve, 2.0f);
        previous = curvePoint;
    }
    drawList->AddCircleFilled(p1, 5.0f, handle);
    drawList->AddCircleFilled(p2, 5.0f, handle);

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID baseId = ImGui::GetItemID();
    const ImGuiID activeHandleId = baseId + 1;
    bool changed = false;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float dx1 = mouse.x - p1.x;
        const float dy1 = mouse.y - p1.y;
        const float dx2 = mouse.x - p2.x;
        const float dy2 = mouse.y - p2.y;
        const float distance1 = dx1 * dx1 + dy1 * dy1;
        const float distance2 = dx2 * dx2 + dy2 * dy2;
        storage->SetInt(activeHandleId, distance1 <= distance2 ? 1 : 2);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        storage->SetInt(activeHandleId, 0);
    }
    const int activeHandle = storage->GetInt(activeHandleId, 0);
    if (activeHandle != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const double mx = Clamp((mouse.x - rect.Min.x) / std::max(1.0f, rect.GetWidth()), 0.0, 1.0);
        const double my = Clamp(ScreenToCurveEditorY(rect, mouse.y), -0.4, 1.4);
        if (activeHandle == 1) {
            keyframe.easeX1 = mx;
            keyframe.easeY1 = my;
        } else {
            keyframe.easeX2 = mx;
            keyframe.easeY2 = my;
        }
        keyframe.easing = KeyframeEasing::Custom;
        changed = true;
    }
    return changed;
}

bool ContainsIndex(const std::vector<int>& indices, const int index) {
    return std::find(indices.begin(), indices.end(), index) != indices.end();
}

void NormalizeIndices(std::vector<int>& indices, const int itemCount, int& primaryIndex) {
    std::vector<int> normalized;
    normalized.reserve(indices.size());
    for (const int index : indices) {
        if (index < 0 || index >= itemCount || ContainsIndex(normalized, index)) {
            continue;
        }
        normalized.push_back(index);
    }
    std::sort(normalized.begin(), normalized.end());
    indices = std::move(normalized);

    if (itemCount <= 0) {
        indices.clear();
        primaryIndex = 0;
        return;
    }

    primaryIndex = std::clamp(primaryIndex, 0, itemCount - 1);
    if (indices.empty()) {
        return;
    }

    if (!ContainsIndex(indices, primaryIndex)) {
        primaryIndex = indices.back();
    }
}

}  // namespace radiary
