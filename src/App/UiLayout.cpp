#include "UiLayout.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gx
{
namespace
{
float scaled(const float value, const float scale)
{
    return value * scale;
}

float logicalWidthFor(const AppState &state, const float scale)
{
    return static_cast<float>(std::max(1, state.framebufferWidth)) / scale;
}

float logicalHeightFor(const AppState &state, const float scale)
{
    return static_cast<float>(std::max(1, state.framebufferHeight)) / scale;
}

float clampedMargin(const int width, const int height, const float scale)
{
    const auto logicalMin = static_cast<float>(std::min(width, height)) / scale;
    return std::clamp(logicalMin * 0.018f, 8.0f, 16.0f) * scale;
}

float buttonWidthFor(const std::string_view text, const float fontPx, const float minimumWidth)
{
    return std::max(minimumWidth, textAdvancePx(text.size(), fontPx) + fontPx * 1.15f);
}

std::vector<UiButtonLayout> rightAlignedButtons(const UiPixelRect &panel,
                                                const float fontPx,
                                                const float paddingPx,
                                                const float gapPx,
                                                const float buttonHeightPx,
                                                const float buttonTopPx,
                                                std::vector<UiButtonLayout> buttons)
{
    auto right = panel.right - paddingPx;
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
    {
        const auto minWidth = it->text.size() <= 1 ? buttonHeightPx : buttonHeightPx + fontPx * 0.85f;
        const auto width = buttonWidthFor(it->text, fontPx, minWidth);
        it->bounds = UiPixelRect{
            right - width,
            buttonTopPx,
            right,
            buttonTopPx + buttonHeightPx
        };
        right -= width + gapPx;
    }
    return buttons;
}
}

float UiPixelRect::width() const
{
    return right - left;
}

float UiPixelRect::height() const
{
    return bottom - top;
}

bool UiPixelRect::contains(const double x, const double y) const
{
    return x >= static_cast<double>(left)
        && x <= static_cast<double>(right)
        && y >= static_cast<double>(top)
        && y <= static_cast<double>(bottom);
}

float normalizedPixelX(const float pixelX, const int framebufferWidth)
{
    return framebufferWidth <= 0 ? -1.0f : pixelX / static_cast<float>(framebufferWidth) * 2.0f - 1.0f;
}

float normalizedPixelYFromTop(const float pixelY, const int framebufferHeight)
{
    return framebufferHeight <= 0 ? 1.0f : 1.0f - pixelY / static_cast<float>(framebufferHeight) * 2.0f;
}

float textAdvancePx(const size_t characterCount, const float pixelHeight)
{
    constexpr auto monoAdvanceRatio = 0.60f;
    return static_cast<float>(characterCount) * pixelHeight * monoAdvanceRatio;
}

float textAdvanceNdc(const size_t characterCount, const float pixelHeight, const int framebufferWidth)
{
    if (framebufferWidth <= 0)
    {
        return 0.0f;
    }

    const auto pixelWidth = textAdvancePx(characterCount, pixelHeight);
    return pixelWidth / static_cast<float>(framebufferWidth) * 2.0f;
}

float textHeightNdc(const float pixelHeight, const int framebufferHeight)
{
    if (framebufferHeight <= 0)
    {
        return 0.0f;
    }
    return pixelHeight / static_cast<float>(framebufferHeight) * 2.0f;
}

float uiScaleFor(const AppState &state)
{
    return std::clamp(static_cast<float>(state.devicePixelRatio), 1.0f, 3.0f);
}

UiNdcRect toNdcRect(const UiPixelRect &rect, const int framebufferWidth, const int framebufferHeight)
{
    return {
        .xMin = normalizedPixelX(rect.left, framebufferWidth),
        .xMax = normalizedPixelX(rect.right, framebufferWidth),
        .yMin = normalizedPixelYFromTop(rect.bottom, framebufferHeight),
        .yMax = normalizedPixelYFromTop(rect.top, framebufferHeight)
    };
}

size_t maxVisibleCharacters(const float xMin,
                            const float xMax,
                            const float pixelHeight,
                            const int framebufferWidth)
{
    const auto characterWidth = textAdvanceNdc(1, pixelHeight, framebufferWidth);
    if (characterWidth <= 0.0f)
    {
        return 0;
    }

    const auto availableWidth = std::max(0.0f, xMax - xMin);
    return static_cast<size_t>(std::floor(availableWidth / characterWidth));
}

VisibleFormulaText visibleFormulaText(const std::string_view text,
                                      const size_t requestedCursor,
                                      const float xMin,
                                      const float xMax,
                                      const float pixelHeight,
                                      const int framebufferWidth)
{
    const auto cursor = std::min(requestedCursor, text.size());
    const auto maxChars = maxVisibleCharacters(xMin, xMax, pixelHeight, framebufferWidth);
    if (maxChars == 0)
    {
        return {};
    }
    if (text.size() <= maxChars)
    {
        return {std::string{text}, cursor, 0};
    }

    if (maxChars <= 3)
    {
        const auto start = cursor > maxChars ? cursor - maxChars : 0;
        return {std::string{text.substr(start, maxChars)}, cursor - start, start};
    }

    if (cursor >= text.size())
    {
        const auto payloadChars = maxChars - 3;
        const auto start = text.size() - payloadChars;
        return {"..." + std::string{text.substr(start, payloadChars)}, maxChars, start};
    }

    if (cursor <= maxChars - 3)
    {
        const auto payloadChars = maxChars - 3;
        return {std::string{text.substr(0, payloadChars)} + "...", cursor, 0};
    }

    const auto payloadChars = maxChars > 6 ? maxChars - 6 : maxChars - 3;
    auto start = cursor - std::min(cursor, payloadChars / 2);
    if (start + payloadChars > text.size())
    {
        start = text.size() - payloadChars;
    }
    const auto visibleCursor = 3 + (cursor - start);
    return {"..." + std::string{text.substr(start, payloadChars)} + "...", visibleCursor, start};
}

FormulaOverlayLayout formulaOverlayLayoutFor(const AppState &state)
{
    const auto width = std::max(1, state.framebufferWidth);
    const auto height = std::max(1, state.framebufferHeight);
    const auto scale = uiScaleFor(state);
    const auto logicalWidth = logicalWidthFor(state, scale);
    const auto logicalHeight = logicalHeightFor(state, scale);
    const auto compact = logicalWidth < 420.0f || logicalHeight < 340.0f;
    const auto tiny = logicalWidth < 320.0f;
    const auto marginPx = std::clamp(logicalWidth * 0.025f, 8.0f, 18.0f) * scale;
    const auto topPx = std::clamp(logicalHeight * 0.018f, 8.0f, 16.0f) * scale;
    const auto fontPx = scaled(compact ? 16.0f : 20.0f, scale);
    const auto messageFontPx = scaled(compact ? 11.0f : 12.0f, scale);
    const auto paddingPx = std::clamp(fontPx * 0.70f, scaled(10.0f, scale), scaled(16.0f, scale));
    const auto hasError = !state.formulaInput.error.empty();
    const auto basePanelHeightPx = std::clamp(
        fontPx * 2.85f,
        scaled(48.0f, scale),
        std::min(scaled(70.0f, scale), static_cast<float>(height) * 0.28f));
    const auto messageHeightPx = hasError ? messageFontPx * 1.65f + scaled(6.0f, scale) : 0.0f;
    const auto panelHeightPx = std::min(static_cast<float>(height) - topPx - scaled(4.0f, scale),
                                        basePanelHeightPx + messageHeightPx);

    const auto panel = UiPixelRect{
        marginPx,
        topPx,
        std::max(marginPx + scaled(80.0f, scale), static_cast<float>(width) - marginPx),
        std::min(static_cast<float>(height) - scaled(4.0f, scale), topPx + panelHeightPx)
    };

    const auto lineBoxPx = fontPx * 1.28f;
    const auto textTopPx = hasError
        ? panel.top + std::max(scaled(7.0f, scale), paddingPx * 0.55f)
        : panel.top + std::max(scaled(6.0f, scale), (panel.height() - lineBoxPx) * 0.5f);
    const auto buttonHeightPx = std::clamp(fontPx * 1.45f, scaled(26.0f, scale), scaled(32.0f, scale));
    const auto buttonTopPx = textTopPx + (lineBoxPx - buttonHeightPx) * 0.5f;
    const auto gapPx = scaled(compact ? 5.0f : 7.0f, scale);

    std::vector<UiButtonLayout> buttons{
        {
            .action = UiAction::SubmitFormulaInput,
            .text = tiny ? "OK" : "Apply",
            .primary = true
        },
        {
            .action = UiAction::CancelFormulaInput,
            .text = tiny ? "X" : "Cancel"
        }
    };
    buttons = rightAlignedButtons(panel,
                                  scaled(compact ? 13.0f : 14.0f, scale),
                                  paddingPx,
                                  gapPx,
                                  buttonHeightPx,
                                  buttonTopPx,
                                  std::move(buttons));

    auto labelText = logicalWidth < 460.0f ? std::string{"f="} : std::string{"f(x,y)="};
    const auto labelXPx = panel.left + paddingPx;
    auto labelWidthPx = textAdvancePx(labelText.size(), fontPx);
    auto inputXPx = labelXPx + labelWidthPx + paddingPx;
    const auto buttonLeftPx = buttons.empty() ? panel.right - paddingPx : buttons.front().bounds.left;
    auto inputRightPx = buttonLeftPx - gapPx;
    const auto minimumInputPx = std::clamp(panel.width() * 0.28f, scaled(42.0f, scale), scaled(116.0f, scale));

    if (inputRightPx - inputXPx < minimumInputPx)
    {
        labelText = "f=";
        labelWidthPx = textAdvancePx(labelText.size(), fontPx);
        inputXPx = labelXPx + labelWidthPx + paddingPx * 0.75f;
    }
    if (inputRightPx - inputXPx < minimumInputPx)
    {
        labelText.clear();
        inputXPx = labelXPx;
    }
    if (inputRightPx <= inputXPx)
    {
        inputRightPx = std::min(panel.right - paddingPx, inputXPx + scaled(1.0f, scale));
    }

    return {
        .panel = panel,
        .input = UiPixelRect{inputXPx, textTopPx - scaled(3.0f, scale), inputRightPx, textTopPx + lineBoxPx},
        .labelX = labelXPx,
        .textY = textTopPx,
        .fontPx = fontPx,
        .messageY = textTopPx + lineBoxPx + scaled(4.0f, scale),
        .messageFontPx = messageFontPx,
        .labelText = std::move(labelText),
        .buttons = std::move(buttons)
    };
}

StatusOverlayLayout statusOverlayLayoutFor(const AppState &state)
{
    const auto width = std::max(1, state.framebufferWidth);
    const auto height = std::max(1, state.framebufferHeight);
    const auto scale = uiScaleFor(state);
    const auto logicalWidth = logicalWidthFor(state, scale);
    const auto compact = logicalWidth < 430.0f;
    const auto tiny = logicalWidth < 300.0f;
    const auto marginPx = clampedMargin(width, height, scale);
    const auto fontPx = scaled(compact ? 12.0f : 13.0f, scale);
    const auto paddingPx = scaled(compact ? 8.0f : 10.0f, scale);
    const auto panelHeightPx = scaled(compact ? 34.0f : 38.0f, scale);
    const auto panel = UiPixelRect{
        marginPx,
        marginPx,
        std::max(marginPx + scaled(120.0f, scale), static_cast<float>(width) - marginPx),
        std::min(static_cast<float>(height) - scaled(4.0f, scale), marginPx + panelHeightPx)
    };
    const auto buttonHeightPx = scaled(compact ? 24.0f : 26.0f, scale);
    const auto buttonTopPx = panel.top + (panel.height() - buttonHeightPx) * 0.5f;
    const auto gapPx = scaled(compact ? 5.0f : 6.0f, scale);

    std::vector<UiButtonLayout> buttons{
        {
            .action = UiAction::BeginFormulaInput,
            .text = tiny ? "E" : "Edit",
            .primary = true
        },
        {
            .action = UiAction::ResetViewport,
            .text = tiny ? "H" : "Home"
        },
        {
            .action = UiAction::ToggleDebug,
            .text = tiny ? "D" : "Debug",
            .active = state.debug
        }
    };
    buttons = rightAlignedButtons(panel, fontPx, paddingPx, gapPx, buttonHeightPx, buttonTopPx, std::move(buttons));

    auto formulaLabel = compact ? std::string{"f="} : std::string{"f(x,y)="};
    auto labelX = panel.left + paddingPx;
    auto formulaTextX = labelX + textAdvancePx(formulaLabel.size(), fontPx) + scaled(6.0f, scale);
    const auto formulaRight = buttons.empty()
        ? panel.right - paddingPx
        : std::max(formulaTextX + scaled(1.0f, scale), buttons.front().bounds.left - gapPx);
    if (formulaRight - formulaTextX < scaled(28.0f, scale))
    {
        formulaLabel.clear();
        formulaTextX = labelX;
    }

    const auto lineBoxPx = fontPx * 1.28f;
    const auto textTopPx = panel.top + std::max(scaled(4.0f, scale), (panel.height() - lineBoxPx) * 0.5f);

    return {
        .panel = panel,
        .formula = UiPixelRect{formulaTextX, textTopPx - scaled(3.0f, scale), formulaRight, textTopPx + lineBoxPx},
        .formulaLabelX = labelX,
        .formulaTextX = formulaTextX,
        .textY = textTopPx,
        .fontPx = fontPx,
        .formulaLabel = std::move(formulaLabel),
        .buttons = std::move(buttons)
    };
}

std::optional<UiAction> hitTestUiAction(const double pixelX, const double pixelY, const AppState &state)
{
    const auto buttons = state.formulaInput.active
        ? formulaOverlayLayoutFor(state).buttons
        : statusOverlayLayoutFor(state).buttons;

    for (const auto &button : buttons)
    {
        if (button.bounds.contains(pixelX, pixelY))
        {
            return button.action;
        }
    }
    return std::nullopt;
}
}
