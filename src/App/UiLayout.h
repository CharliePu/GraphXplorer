#ifndef UILAYOUT_H
#define UILAYOUT_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "AppState.h"

namespace gx
{
struct UiPixelRect
{
    float left{0.0f};
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};

    [[nodiscard]] float width() const;
    [[nodiscard]] float height() const;
    [[nodiscard]] bool contains(double x, double y) const;
};

struct UiNdcRect
{
    float xMin{0.0f};
    float xMax{0.0f};
    float yMin{0.0f};
    float yMax{0.0f};
};

enum class UiAction
{
    BeginFormulaInput,
    SubmitFormulaInput,
    CancelFormulaInput,
    ResetViewport,
    ToggleDebug
};

struct UiButtonLayout
{
    UiAction action{UiAction::BeginFormulaInput};
    UiPixelRect bounds{};
    std::string text{};
    bool active{false};
    bool primary{false};
};

struct VisibleFormulaText
{
    std::string text{};
    size_t cursor{0};
    size_t sourceOffset{0};
};

struct FormulaOverlayLayout
{
    UiPixelRect panel{};
    UiPixelRect input{};
    float labelX{0.0f};
    float textY{0.0f};
    float fontPx{18.0f};
    float messageY{0.0f};
    float messageFontPx{12.0f};
    std::string labelText{"f(x,y)="};
    std::vector<UiButtonLayout> buttons{};
};

struct StatusOverlayLayout
{
    UiPixelRect panel{};
    UiPixelRect formula{};
    float formulaLabelX{0.0f};
    float formulaTextX{0.0f};
    float textY{0.0f};
    float fontPx{13.0f};
    std::string formulaLabel{"f="};
    std::vector<UiButtonLayout> buttons{};
};

[[nodiscard]] float normalizedPixelX(float pixelX, int framebufferWidth);
[[nodiscard]] float normalizedPixelYFromTop(float pixelY, int framebufferHeight);
[[nodiscard]] float textAdvancePx(size_t characterCount, float pixelHeight);
[[nodiscard]] float textAdvanceNdc(size_t characterCount, float pixelHeight, int framebufferWidth);
[[nodiscard]] float textHeightNdc(float pixelHeight, int framebufferHeight);
[[nodiscard]] float uiScaleFor(const AppState &state);
[[nodiscard]] UiNdcRect toNdcRect(const UiPixelRect &rect, int framebufferWidth, int framebufferHeight);

[[nodiscard]] VisibleFormulaText visibleFormulaText(std::string_view text,
                                                    size_t requestedCursor,
                                                    float xMinNdc,
                                                    float xMaxNdc,
                                                    float pixelHeight,
                                                    int framebufferWidth);
[[nodiscard]] FormulaOverlayLayout formulaOverlayLayoutFor(const AppState &state);
[[nodiscard]] StatusOverlayLayout statusOverlayLayoutFor(const AppState &state);
[[nodiscard]] std::optional<UiAction> hitTestUiAction(double pixelX, double pixelY, const AppState &state);
}

#endif // UILAYOUT_H
