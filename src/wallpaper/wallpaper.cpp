#include "wallpaper/wallpaper.h"

#include "dbus/introspection.h"
#include "dbus/options.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <print>

namespace xdpn {

  namespace wallpaper {

    std::filesystem::path storeDirectory() {
      std::filesystem::path base;
      if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && data[0] == '/') {
        base = data;
      } else if (const char* home = std::getenv("HOME"); home != nullptr) {
        base = std::filesystem::path(home) / ".local" / "share";
      }
      return base / "xdg-desktop-portal-noctalia" / "wallpapers";
    }

    bool apply(const std::filesystem::path& picture) {
      std::error_code error;
      const auto store = storeDirectory();
      std::filesystem::create_directories(store, error);
      const auto target = store / picture.filename();
      if (picture != target) {
        std::filesystem::copy_file(picture, target, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
          std::println(stderr, "wallpaper: cannot copy {}: {}", picture.string(), error.message());
          return false;
        }
      }
      const std::string path = target.string();
      const char* argv[] = {"noctalia", "msg", "wallpaper-set", path.c_str(), nullptr};
      GError* spawnError = nullptr;
      gint status = 0;
      if (!g_spawn_sync(nullptr, const_cast<char**>(argv), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr,
                        nullptr, &status, &spawnError)) {
        std::println(stderr, "wallpaper: cannot run noctalia: {}", spawnError->message);
        g_clear_error(&spawnError);
        return false;
      }
      if (!g_spawn_check_wait_status(status, &spawnError)) {
        std::println(stderr, "wallpaper: noctalia msg wallpaper-set failed: {}", spawnError->message);
        g_clear_error(&spawnError);
        return false;
      }
      return true;
    }

  } // namespace wallpaper

  Wallpaper::Wallpaper(GDBusConnection* connection) : m_calls(connection) {
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kWallpaper,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          if (method == "SetWallpaperURI") {
            setWallpaperUri(parameters, invocation);
            return;
          }
          g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                "Unknown method %s", method.c_str());
        });
  }

  Wallpaper::~Wallpaper() { m_interface.reset(); }

  void Wallpaper::setWallpaperUri(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* parentWindow = nullptr;
    const char* uri = nullptr;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&s&s@a{sv})", &handle, &appId, &parentWindow, &uri, &options);
    const bool preview = lookupBool(options, "show-preview", false);
    g_variant_unref(options);

    gchar* local = g_filename_from_uri(uri, nullptr, nullptr);
    const std::filesystem::path picture = local != nullptr ? local : "";
    g_free(local);
    std::error_code error;
    if (picture.empty() || !std::filesystem::is_regular_file(picture, error)) {
      std::println(stderr, "wallpaper: {} is no local file", uri);
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", dbus::Other));
      return;
    }
    if (!preview) {
      const bool applied = wallpaper::apply(picture);
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", applied ? dbus::Success : dbus::Other));
      return;
    }

    dialog::Request request;
    request.kind = dialog::Kind::Wallpaper;
    request.title = "Set Background?";
    request.subtitle = std::format("{} · {}", applicationName(appId, "An application"), picture.filename().string());
    request.image = picture.string();
    m_calls.start(handle, invocation, request, [picture](GDBusMethodInvocation* call, const dialog::Answer& answer) {
      guint32 response = answer.response;
      if (response == dbus::Success && !wallpaper::apply(picture)) {
        response = dbus::Other;
      }
      g_dbus_method_invocation_return_value(call, g_variant_new("(u)", response));
    });
  }

} // namespace xdpn
