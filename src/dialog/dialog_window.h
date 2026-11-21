#pragma once

#include "dialog/request.h"
#include "render/scene/input_dispatcher.h"
#include "wayland/toplevel_surface.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class AnimationManager;
class Button;
class Checkbox;
class Flex;
class Node;
class RadioButton;
class RenderContext;
class Renderer;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;

namespace xdpn::dialog {

  // The toplevel of one portal dialog: an Access question with its choices, or an application
  // chooser. The answer is handed to the callback exactly once; the main loop stops after it.
  class DialogWindow {
  public:
    using AnswerCallback = std::function<void(const Answer&)>;

    DialogWindow(Request request, WaylandConnection& connection, RenderContext& renderer, AnimationManager& animations,
                 AnswerCallback onAnswer);
    ~DialogWindow();
    DialogWindow(const DialogWindow&) = delete;
    DialogWindow& operator=(const DialogWindow&) = delete;

    [[nodiscard]] bool done() const noexcept { return m_answered; }
    void cancel();
    void setApplications(std::vector<Application> applications);
    void pointer(const PointerEvent& event);
    void keyboard(const KeyboardEvent& event);
    void closeSurface();

  private:
    struct ChoiceState {
      Choice choice;
      std::string value;
      Checkbox* checkbox = nullptr;
      std::vector<RadioButton*> radios;
    };

    void prepare();
    void build(Renderer& renderer);
    void buildAccess(Flex& content);
    void buildChooser(Flex& content, Renderer& renderer);
    std::unique_ptr<Node> icon(Renderer& renderer, const std::string& path, float size, const char* glyph);
    void select(const std::string& application);
    void accept();
    void answer(Answer answer);
    void requestUpdate();

    Request m_request;
    WaylandConnection& m_connection;
    RenderContext& m_renderer;
    AnimationManager& m_animations;
    AnswerCallback m_onAnswer;
    ToplevelSurface m_surface;
    InputDispatcher m_dispatcher;
    std::unique_ptr<Node> m_root;
    std::vector<ChoiceState> m_choices;
    std::vector<std::pair<std::string, Button*>> m_applicationButtons;
    std::string m_selected;
    Button* m_accept = nullptr;
    bool m_rebuild = true;
    bool m_answered = false;
  };

} // namespace xdpn::dialog
