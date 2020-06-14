/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <terminal_view/ShaderConfig.h>
#include <terminal_view/BackgroundRenderer.h>
#include <terminal_view/TextRenderer.h>
#include <terminal_view/DecorationRenderer.h>
#include <terminal_view/GLCursor.h>
#include <terminal_view/ScreenCoordinates.h>
#include <terminal_view/RenderMetrics.h>

#include <terminal/Logger.h>
#include <terminal/Terminal.h>

#include <crispy/text/Font.h>

#include <QPoint>
#include <QMatrix2x4>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

#include <fmt/format.h>

#include <chrono>
#include <memory>
#include <vector>
#include <utility>

namespace terminal::view {

struct ShaderConfig;

/**
 * Renders a terminal's screen to the current OpenGL context.
 */
class GLRenderer : public QOpenGLFunctions {
  public:
    /** Constructs a GLRenderer instances.
     *
     * @p _logger the logging instance to use when logging is needed during rendering.
     * @p _fonts reference to the set of loaded fonts to be used for rendering text.
     * @p _colorProfile user-configurable color profile to use to map terminal colors to.
     * @p _projectionMatrix projection matrix to apply to the rendered scene when rendering the screen.
     */
    GLRenderer(Logger _logger,
               WindowSize const& _screenSize,
               FontConfig const& _fonts,
               ColorProfile _colorProfile,
               Opacity _backgroundOpacity,
               Decorator _hyperlinkNormal,
               Decorator _hyperlinkHover,
               ShaderConfig const& _backgroundShaderConfig,
               ShaderConfig const& _textShaderConfig,
               ShaderConfig const& _decoratorShaderConfig,
               ShaderConfig const& _cursorShaderConfig,
               QMatrix4x4 const& _projectionMatrix);

    size_t cellHeight() const noexcept { return fonts_.regular.first.get().lineHeight(); }
    size_t cellWidth() const noexcept { return fonts_.regular.first.get().maxAdvance(); }

    void setColorProfile(ColorProfile const& _colors);
    void setBackgroundOpacity(terminal::Opacity _opacity);
    void setFont(FontConfig const& _fonts);
    bool setFontSize(unsigned int _fontSize);
    void setProjection(QMatrix4x4 const& _projectionMatrix);

    void setHyperlinkDecoration(Decorator _normal, Decorator _hover)
    {
        decorationRenderer_.setHyperlinkDecoration(_normal, _hover);
    }

    constexpr void setScreenSize(WindowSize const& _screenSize) noexcept
    {
        screenCoordinates_.screenSize = _screenSize;
    }

    constexpr void setMargin(int _leftMargin, int _bottomMargin) noexcept
    {
        screenCoordinates_.leftMargin = _leftMargin;
        screenCoordinates_.bottomMargin = _bottomMargin;
    }

    /**
     * Renders the given @p _terminal to the current OpenGL context.
     *
     * @p _now The time hint to use when rendering the eventually blinking cursor.
     */
    uint64_t render(Terminal& _terminal,
                    std::chrono::steady_clock::time_point _now,
                    terminal::Coordinate const& _currentMousePosition);

    RenderMetrics const& metrics() const noexcept { return metrics_; }

    // Converts given RGBColor with its given opacity to a 4D-vector of values between 0.0 and 1.0
    static constexpr QVector4D canonicalColor(RGBColor const& _rgb, Opacity _opacity = Opacity::Opaque)
    {
        return QVector4D{
            static_cast<float>(_rgb.red) / 255.0f,
            static_cast<float>(_rgb.green) / 255.0f,
            static_cast<float>(_rgb.blue) / 255.0f,
            static_cast<float>(_opacity) / 255.0f
        };
    }

    void clearCache();

  private:
    void renderCell(cursor_pos_t _row, cursor_pos_t _col, ScreenBuffer::Cell const& _cell);
    void renderCursor(Terminal const& _terminal);
    void renderSelection(Terminal const& _terminal);

  private:
    RenderMetrics metrics_;

    ScreenCoordinates screenCoordinates_;
    Logger logger_;

    ColorProfile colorProfile_;
    Opacity backgroundOpacity_;

    FontConfig fonts_;

    QMatrix4x4 projectionMatrix_;
    BackgroundRenderer backgroundRenderer_;
    TextRenderer textRenderer_;
    DecorationRenderer decorationRenderer_;
    GLCursor cursor_;
};

} // end namespace
