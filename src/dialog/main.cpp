#include "core/application_identity.h"
#include "core/files/resource_paths.h"
#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/input/keybind_matcher.h"
#include "core/log.h"
#include "core/ui_strings.h"
#include "dialog/dialog_window.h"
#include "dialog/glib_poll_source.h"
#include "dialog/main_loop.h"
#include "dialog/palette_file.h"
#include "dialog/poll_sources.h"
#include "dialog/request.h"
#include "render/animation/animation_manager.h"
#include "render/codecs/codecs.h"
#include "render/core/async_texture_cache.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "wayland/wayland_connection.h"
#include "xdpn_build_config.h"

#include <glib-unix.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {

  constexpr auto kLog = Logger("dialog");

  volatile std::sig_atomic_t g_stopRequested = 0;
  void stopRequested(int) { g_stopRequested = 1; }

  // Reads stdin up to the first newline, byte by byte, so that later lines stay in the pipe.
  std::optional<std::string> readLine(int fd) {
    std::string line;
    char byte = 0;
    while (true) {
      const ssize_t count = ::read(fd, &byte, 1);
      if (count == 1) {
        if (byte == '\n') {
          return line;
        }
        line.push_back(byte);
        continue;
      }
      if (count < 0 && errno == EINTR) {
        continue;
      }
      return line.empty() ? std::nullopt : std::optional<std::string>(line);
    }
  }

  // Later lines from the daemon: updated application lists.
  class StdinReader {
  public:
    explicit StdinReader(std::function<void(const std::string&)> onLine) : m_onLine(std::move(onLine)) {
      m_source = g_unix_fd_add(STDIN_FILENO, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), &onReadable, this);
    }
    ~StdinReader() {
      if (m_source != 0) {
        g_source_remove(m_source);
      }
    }
    StdinReader(const StdinReader&) = delete;
    StdinReader& operator=(const StdinReader&) = delete;

  private:
    static gboolean onReadable(gint fd, GIOCondition /*condition*/, gpointer data) {
      auto* self = static_cast<StdinReader*>(data);
      std::array<char, 4096> buffer{};
      const ssize_t count = ::read(fd, buffer.data(), buffer.size());
      if (count <= 0) {
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
          return G_SOURCE_CONTINUE;
        }
        self->m_source = 0;
        return G_SOURCE_REMOVE;
      }
      self->m_pending.append(buffer.data(), static_cast<std::size_t>(count));
      std::size_t newline = 0;
      while ((newline = self->m_pending.find('\n')) != std::string::npos) {
        const std::string line = self->m_pending.substr(0, newline);
        self->m_pending.erase(0, newline + 1);
        self->m_onLine(line);
      }
      return G_SOURCE_CONTINUE;
    }

    std::function<void(const std::string&)> m_onLine;
    std::string m_pending;
    guint m_source = 0;
  };

  void initializeKeys() {
    const std::array<std::pair<KeybindAction, std::uint32_t>, 5> keys{{
        {KeybindAction::Cancel, XKB_KEY_Escape},
        {KeybindAction::Left, XKB_KEY_Left},
        {KeybindAction::Right, XKB_KEY_Right},
        {KeybindAction::Up, XKB_KEY_Up},
        {KeybindAction::Down, XKB_KEY_Down},
    }};
    for (const auto& [action, sym] : keys) {
      KeybindMatcher::setMatcher(action, [sym](std::uint32_t key, std::uint32_t modifiers) {
        return key == sym && modifiers == 0;
      });
    }
    KeybindMatcher::setMatcher(KeybindAction::TabNext, [](std::uint32_t sym, std::uint32_t modifiers) {
      return sym == XKB_KEY_Tab && modifiers == 0;
    });
    KeybindMatcher::setMatcher(KeybindAction::TabPrevious, [](std::uint32_t sym, std::uint32_t modifiers) {
      return KeySymbol::isTab(sym) && modifiers == KeyMod::Shift;
    });
    KeybindMatcher::setMatcher(KeybindAction::Validate, [](std::uint32_t sym, std::uint32_t modifiers) {
      return modifiers == 0 && KeySymbol::isEnterOrSpace(sym);
    });
  }

  void initializeLibrary() {
    setApplicationIdentity({.name = "noctalia-portal-dialog", .envPrefix = "NOCTALIA_PORTAL"});
    initLogLevelFromEnvironment();
    const char* overridePath = std::getenv("NOCTALIA_PORTAL_ASSETS_DIR");
    const std::filesystem::path assets = overridePath != nullptr ? overridePath : XDPN_ASSETS_DIR;
    paths::setAssetRoots({.application = assets, .library = assets});
    setUiStrings({.keyName = {}, .selectPlaceholder = [] { return std::string("Select…"); }});
    auto palette = xdpn::dialog::loadPalette(xdpn::dialog::palettePath(), xdpn::dialog::defaultPalette());
    if (!palette) {
      kLog.warn("{}", palette.error());
      setPalette(xdpn::dialog::defaultPalette());
    } else {
      setPalette(*palette);
    }
    initializeKeys();
    codecs::installImageDecoder();
  }

  void writeAnswer(const xdpn::dialog::Answer& answer) {
    std::println("{}", xdpn::dialog::formatAnswer(answer));
    std::fflush(stdout);
  }

  void run(const xdpn::dialog::Request& request) {
    initializeLibrary();
    GlibPollSource glib;
    WaylandConnection connection;
    if (!connection.connect()) {
      throw std::runtime_error("could not connect to Wayland: " + connection.describeDisplayError());
    }
    GlSharedContext graphics;
    graphics.initialize(connection.display());
    RenderContext renderer;
    renderer.initialize(graphics);
    AsyncTextureCache textures;
    textures.initialize(&graphics);
    textures.setMakeCurrentCallback([&graphics] { graphics.makeCurrentSurfaceless(); });
    renderer.setAsyncTextureCache(&textures);
    AnimationManager animations;

    std::optional<xdpn::dialog::DialogWindow> window;
    window.emplace(request, connection, renderer, animations, writeAnswer);
    connection.setPointerEventCallback([&window](const PointerEvent& event) { window->pointer(event); });
    connection.setKeyboardEventCallback([&window](const KeyboardEvent& event) { window->keyboard(event); });
    StdinReader updates([&window](const std::string& line) {
      if (auto applications = xdpn::dialog::parseApplications(line)) {
        window->setApplications(std::move(*applications));
      }
    });

    DeferredCallPollSource deferred;
    TimerPollSource timers;
    KeyRepeatPollSource repeat(connection);
    MainLoop loop(connection, {&deferred, &timers, &repeat, &glib, &textures}, [&window] {
      if (g_stopRequested != 0) {
        window->cancel();
      }
      return !window->done();
    });
    loop.run();

    window->closeSurface();
    wl_display_flush(connection.display());
    connection.setPointerEventCallback({});
    connection.setKeyboardEventCallback({});
    window.reset();
    renderer.setAsyncTextureCache(nullptr);
    renderer.cleanup();
    graphics.cleanup();
  }

} // namespace

int main() {
  const auto line = readLine(STDIN_FILENO);
  const auto request = line ? xdpn::dialog::parseRequest(*line) : xdpn::dialog::Request{};
  if (request.kind == xdpn::dialog::Kind::Invalid) {
    std::println(stderr, "noctalia-portal-dialog: no valid request on stdin");
    writeAnswer({.response = 2, .choices = {}, .application = {}, .text = {}, .printJob = {}});
    return EXIT_FAILURE;
  }

  const auto previousTerm = std::signal(SIGTERM, stopRequested);
  const auto previousInt = std::signal(SIGINT, stopRequested);
  int status = EXIT_SUCCESS;
  try {
    run(request);
  } catch (const std::exception& error) {
    std::println(stderr, "noctalia-portal-dialog: {}", error.what());
    writeAnswer({.response = 2, .choices = {}, .application = {}, .text = {}, .printJob = {}});
    status = EXIT_FAILURE;
  }
  std::signal(SIGTERM, previousTerm);
  std::signal(SIGINT, previousInt);
  return status;
}
