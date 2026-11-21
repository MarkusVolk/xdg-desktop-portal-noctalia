#include "dialog/dialog_window.h"

#include "core/input/key_symbols.h"
#include "core/ui_phase.h"
#include "render/animation/animation_manager.h"
#include "render/render_context.h"
#include "ui/builders.h"
#include "ui/controls/image.h"
#include "ui/style.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <linux/input-event-codes.h>
#include <utility>

namespace xdpn::dialog {

  namespace {

    constexpr std::uint32_t kAccessWidth = 440;
    constexpr std::uint32_t kChooserWidth = 420;
    constexpr std::uint32_t kChooserHeight = 480;
    constexpr float kTextWidth = 440.0F - 2.0F * Style::spaceLg;

    // The window keeps one size so that tiling compositors float it like a dialog.
    std::uint32_t accessHeight(const Request& request) {
      const auto lines = [](const std::string& text, std::size_t perLine) {
        return text.empty() ? 0U : static_cast<std::uint32_t>(1 + text.size() / perLine);
      };
      std::uint32_t height = 150;
      height += 18 * lines(request.subtitle, 60);
      height += 20 * lines(request.body, 56) + (request.body.empty() ? 0 : 8);
      for (const auto& choice : request.choices) {
        height += choice.options.empty() ? 36 : 28 + 32 * static_cast<std::uint32_t>(choice.options.size());
      }
      return std::clamp<std::uint32_t>(height, 180, 640);
    }

    void literal(Label& label) { label.setUseMarkup(false); }

  } // namespace

  DialogWindow::DialogWindow(Request request, WaylandConnection& connection, RenderContext& renderer,
                             AnimationManager& animations, AnswerCallback onAnswer)
      : m_request(std::move(request)), m_connection(connection), m_renderer(renderer), m_animations(animations),
        m_onAnswer(std::move(onAnswer)), m_surface(connection) {
    for (const auto& choice : m_request.choices) {
      ChoiceState state{.choice = choice, .value = choice.initial, .checkbox = nullptr, .radios = {}};
      if (state.value.empty()) {
        state.value = choice.options.empty() ? "false" : choice.options.front().id;
      }
      m_choices.push_back(std::move(state));
    }
    if (m_request.kind == Kind::AppChooser) {
      const auto& apps = m_request.applications;
      const bool known = std::ranges::any_of(apps, [this](const Application& app) { return app.id == m_request.lastChoice; });
      m_selected = known ? m_request.lastChoice : (apps.empty() ? std::string() : apps.front().id);
    }

    const bool chooser = m_request.kind == Kind::AppChooser;
    const std::uint32_t width = chooser ? kChooserWidth : kAccessWidth;
    const std::uint32_t height = chooser ? kChooserHeight : accessHeight(m_request);

    m_surface.setRenderContext(&m_renderer);
    m_surface.setAnimationManager(&m_animations);
    m_surface.setPrepareFrameCallback([this](bool, bool) { prepare(); });
    m_surface.setUpdateCallback([] {});
    m_surface.setConfigureCallback([this](std::uint32_t, std::uint32_t) { m_surface.requestLayout(); });
    m_surface.setClosedCallback([this] { cancel(); });
    m_surface.setRoleLifecycleCallbacks(
        [width, height](xdg_toplevel* toplevel) {
          xdg_toplevel_set_min_size(toplevel, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
          xdg_toplevel_set_max_size(toplevel, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
        },
        [] {});
    const std::string title = !m_request.title.empty() ? m_request.title : (chooser ? "Open With" : "Allow Access");
    if (!m_surface.initialize(nullptr, {.width = width,
                                        .height = height,
                                        .minWidth = width,
                                        .minHeight = height,
                                        .title = title,
                                        .appId = "dev.noctalia.PortalDialog"})) {
      throw std::runtime_error("cannot create the dialog window");
    }
  }

  DialogWindow::~DialogWindow() = default;

  void DialogWindow::requestUpdate() { m_surface.requestLayout(); }

  void DialogWindow::prepare() {
    if (m_surface.width() == 0 || m_surface.height() == 0 || !m_renderer.makeCurrent(m_surface.renderTarget())) {
      return;
    }
    UiPhaseScope phase(UiPhase::Layout);
    auto& renderer = m_surface.renderTarget().renderer();
    if (m_rebuild) {
      build(renderer);
      m_rebuild = false;
    }
    m_root->arrange(renderer, {0, 0, static_cast<float>(m_surface.width()), static_cast<float>(m_surface.height())});
  }

  std::unique_ptr<Node> DialogWindow::icon(Renderer& renderer, const std::string& path, float size, const char* glyph) {
    if (!path.empty()) {
      auto image = ui::image({.fit = ImageFit::Contain, .width = size, .height = size});
      ImageSourceOptions options;
      options.targetSize = static_cast<int>(size);
      image->setAsyncReadyCallback([this] { requestUpdate(); });
      image->setSourceFile(renderer, path, options);
      return image;
    }
    return ui::glyph({.glyph = glyph, .glyphSize = size, .color = colorSpecFromRole(ColorRole::Primary)});
  }

  void DialogWindow::build(Renderer& renderer) {
    m_applicationButtons.clear();
    m_accept = nullptr;

    auto root = ui::column({.align = FlexAlign::Stretch,
                            .gap = Style::spaceMd,
                            .padding = Style::spaceLg,
                            .fill = colorSpecFromRole(ColorRole::Surface)});
    root->setAnimationManager(&m_animations);

    const bool chooser = m_request.kind == Kind::AppChooser;
    auto header = ui::row({.align = FlexAlign::Start, .gap = Style::spaceMd});
    header->addChild(icon(renderer, m_request.icon, 28.0F, chooser ? "apps" : "shield-lock"));
    auto titles = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs, .flexGrow = 1.0F});
    titles->addChild(ui::label({.text = !m_request.title.empty() ? m_request.title : (chooser ? "Open With" : "Allow Access"),
                                .fontSize = Style::fontSizeTitle,
                                .fontWeight = FontWeight::SemiBold,
                                .color = colorSpecFromRole(ColorRole::OnSurface),
                                .maxWidth = kTextWidth - 40.0F,
                                .maxLines = 3,
                                .configure = literal}));
    if (!m_request.subtitle.empty()) {
      titles->addChild(ui::label({.text = m_request.subtitle,
                                  .fontSize = Style::fontSizeCaption,
                                  .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
                                  .maxWidth = kTextWidth - 40.0F,
                                  .maxLines = 4,
                                  .configure = literal}));
    }
    header->addChild(std::move(titles));
    root->addChild(std::move(header));

    if (chooser) {
      buildChooser(*root, renderer);
    } else {
      buildAccess(*root);
    }

    auto buttons = ui::row({.align = FlexAlign::Center, .justify = FlexJustify::End, .gap = Style::spaceSm});
    buttons->addChild(ui::button({.text = chooser ? std::string("Cancel")
                                                  : (!m_request.denyLabel.empty() ? m_request.denyLabel : "Deny"),
                                  .controlHeight = Style::controlHeightSm,
                                  .variant = ButtonVariant::Outline,
                                  .minWidth = 88.0F,
                                  .onClick = [this] { cancel(); }}));
    buttons->addChild(ui::button({.out = &m_accept,
                                  .text = chooser ? std::string("Open")
                                                  : (!m_request.grantLabel.empty() ? m_request.grantLabel : "Allow"),
                                  .controlHeight = Style::controlHeightSm,
                                  .variant = ButtonVariant::Primary,
                                  .minWidth = 88.0F,
                                  .onClick = [this] { accept(); }}));
    if (chooser) {
      m_accept->setEnabled(!m_selected.empty());
    }
    root->addChild(std::move(buttons));

    m_root = std::move(root);
    m_surface.setSceneRoot(m_root.get());
    m_dispatcher.setSceneRoot(m_root.get());
    m_dispatcher.setFocus(m_accept->inputArea());
  }

  void DialogWindow::buildAccess(Flex& content) {
    if (!m_request.body.empty()) {
      content.addChild(ui::label({.text = m_request.body,
                                  .fontSize = Style::fontSizeBody,
                                  .color = colorSpecFromRole(ColorRole::OnSurface),
                                  .maxWidth = kTextWidth,
                                  .maxLines = 8,
                                  .configure = literal}));
    }
    for (auto& state : m_choices) {
      state.checkbox = nullptr;
      state.radios.clear();
      if (state.choice.options.empty()) {
        auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm});
        row->addChild(ui::checkbox({.out = &state.checkbox,
                                    .checked = state.value == "true",
                                    .onChange = [&state](bool checked) { state.value = checked ? "true" : "false"; }}));
        row->addChild(ui::label({.text = state.choice.label,
                                 .fontSize = Style::fontSizeBody,
                                 .color = colorSpecFromRole(ColorRole::OnSurface),
                                 .maxWidth = kTextWidth - 32.0F,
                                 .configure = literal}));
        content.addChild(std::move(row));
        continue;
      }
      auto group = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs});
      group->addChild(ui::label({.text = state.choice.label,
                                 .fontSize = Style::fontSizeBody,
                                 .fontWeight = FontWeight::SemiBold,
                                 .color = colorSpecFromRole(ColorRole::OnSurface),
                                 .maxWidth = kTextWidth,
                                 .configure = literal}));
      for (const auto& option : state.choice.options) {
        auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm});
        RadioButton* radio = nullptr;
        row->addChild(ui::radioButton({.out = &radio,
                                       .checked = state.value == option.id,
                                       .onChange = [this, &state, id = option.id](bool checked) {
                                         if (!checked) {
                                           return;
                                         }
                                         state.value = id;
                                         for (std::size_t i = 0; i < state.radios.size(); ++i) {
                                           state.radios[i]->setChecked(state.choice.options[i].id == id);
                                         }
                                         requestUpdate();
                                       }}));
        state.radios.push_back(radio);
        row->addChild(ui::label({.text = option.label,
                                 .fontSize = Style::fontSizeBody,
                                 .color = colorSpecFromRole(ColorRole::OnSurface),
                                 .maxWidth = kTextWidth - 32.0F,
                                 .configure = literal}));
        group->addChild(std::move(row));
      }
      content.addChild(std::move(group));
    }
    content.addChild(ui::spacer());
  }

  void DialogWindow::buildChooser(Flex& content, Renderer& renderer) {
    ScrollView* scroll = nullptr;
    auto list = ui::scrollView({.out = &scroll,
                                .scrollbarVisible = true,
                                .fill = colorSpecFromRole(ColorRole::SurfaceContainerLow),
                                .styleRadius = Style::radiusLg,
                                .fillWidth = true,
                                .flexGrow = 1.0F});
    auto* items = scroll->content();
    items->setDirection(FlexDirection::Vertical);
    items->setAlign(FlexAlign::Stretch);
    items->setGap(Style::spaceXs);
    items->setPadding(Style::spaceXs);
    if (m_request.applications.empty()) {
      items->addChild(ui::label({.text = "No application can open this.",
                                 .fontSize = Style::fontSizeBody,
                                 .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
                                 .configure = literal}));
    }
    for (const auto& app : m_request.applications) {
      Button* button = nullptr;
      items->addChild(ui::button({.out = &button,
                                  .text = app.name,
                                  .controlHeight = Style::controlHeight,
                                  .selected = app.id == m_selected,
                                  .contentAlign = ButtonContentAlign::Start,
                                  .variant = ButtonVariant::Ghost,
                                  .gap = Style::spaceSm,
                                  .onClick = [this, id = app.id] { select(id); }}));
      button->insertChildAt(0, icon(renderer, app.icon, 24.0F, "app-window"));
      m_applicationButtons.emplace_back(app.id, button);
    }
    content.addChild(std::move(list));
  }

  void DialogWindow::select(const std::string& application) {
    if (m_selected == application) {
      accept();
      return;
    }
    m_selected = application;
    for (auto& [id, button] : m_applicationButtons) {
      button->setSelected(id == m_selected);
    }
    if (m_accept != nullptr) {
      m_accept->setEnabled(!m_selected.empty());
    }
    requestUpdate();
  }

  void DialogWindow::setApplications(std::vector<Application> applications) {
    m_request.applications = std::move(applications);
    const bool known =
        std::ranges::any_of(m_request.applications, [this](const Application& app) { return app.id == m_selected; });
    if (!known) {
      m_selected = m_request.applications.empty() ? std::string() : m_request.applications.front().id;
    }
    m_rebuild = true;
    requestUpdate();
  }

  void DialogWindow::accept() {
    Answer result{.response = 0, .choices = {}, .application = {}};
    if (m_request.kind == Kind::AppChooser) {
      if (m_selected.empty()) {
        return;
      }
      result.application = m_selected;
    } else {
      for (const auto& state : m_choices) {
        result.choices[state.choice.id] = state.value;
      }
    }
    answer(std::move(result));
  }

  void DialogWindow::cancel() { answer({.response = 1, .choices = {}, .application = {}}); }

  void DialogWindow::answer(Answer result) {
    if (m_answered) {
      return;
    }
    m_answered = true;
    if (m_onAnswer) {
      m_onAnswer(result);
    }
  }

  void DialogWindow::closeSurface() {
    m_dispatcher.setSceneRoot(nullptr);
    m_surface.setSceneRoot(nullptr);
    m_surface.setClosedCallback({});
    m_surface.close();
  }

  void DialogWindow::keyboard(const KeyboardEvent& event) {
    if (event.pressed && !event.preedit && KeySymbol::isEscape(event.sym)) {
      cancel();
      return;
    }
    m_dispatcher.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
    requestUpdate();
  }

  void DialogWindow::pointer(const PointerEvent& event) {
    if (event.surface != m_surface.wlSurface()) {
      return;
    }
    const auto x = static_cast<float>(event.sx);
    const auto y = static_cast<float>(event.sy);
    switch (event.type) {
    case PointerEvent::Type::Enter:
      m_dispatcher.pointerEnter(x, y, event.serial);
      break;
    case PointerEvent::Type::Leave:
      m_dispatcher.pointerLeave();
      break;
    case PointerEvent::Type::Motion:
      m_dispatcher.pointerMotion(x, y, event.serial);
      break;
    case PointerEvent::Type::Button: {
      const bool taken =
          m_dispatcher.pointerButton(x, y, event.button, event.pressed, event.serial, event.time, event.touch);
      if (!taken && event.pressed && event.button == BTN_LEFT) {
        m_surface.beginMove(event.serial);
      }
      break;
    }
    case PointerEvent::Type::Axis:
      m_dispatcher.pointerAxis(x, y, event.axis, event.axisSource, event.axisValue, event.axisDiscrete,
                               event.axisValue120, event.axisLines, event.axisGestureSerial);
      break;
    }
    requestUpdate();
  }

} // namespace xdpn::dialog
