#ifndef INTERACTIONCONTROLLER_H
#define INTERACTIONCONTROLLER_H

#include <vector>

#define GLFW_INCLUDE_NONE
#include <glfwpp/glfwpp.h>

#include "AppState.h"

namespace gx
{
class InteractionController
{
public:
    [[nodiscard]] std::vector<InputEvent> handleKey(glfw::KeyCode key,
                                                    glfw::KeyState action,
                                                    const AppState &state) const;
    [[nodiscard]] std::vector<InputEvent> handleText(unsigned int codepoint,
                                                     const AppState &state) const;
    [[nodiscard]] std::vector<InputEvent> handleDrag(double dx,
                                                     double dy,
                                                     const AppState &state) const;
    [[nodiscard]] std::vector<InputEvent> handleScroll(double offset,
                                                       const AppState &state) const;
    [[nodiscard]] std::vector<InputEvent> handleClick(double x,
                                                      double y,
                                                      const AppState &state) const;
    [[nodiscard]] std::vector<InputEvent> handleResize(int width,
                                                       int height,
                                                       const AppState &state,
                                                       double devicePixelRatio = 1.0) const;

private:
    [[nodiscard]] static ViewportChangedEvent resizeViewportEvent(int width,
                                                                  int height,
                                                                  const AppState &state,
                                                                  double devicePixelRatio);
    [[nodiscard]] static ViewportChangedEvent resetViewportEvent(const AppState &state);
    [[nodiscard]] static bool printableAscii(unsigned int codepoint);
};
}

#endif // INTERACTIONCONTROLLER_H
