#include "launcher/launcher.h"

#include "dbus/introspection.h"
#include "dbus/options.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>
#include <print>
#include <string_view>
#include <unistd.h>

namespace xdpn {

  namespace launcher {

    const char* iconExtension(std::span<const std::uint8_t> data) {
      std::string_view text(reinterpret_cast<const char*>(data.data()), std::min<std::size_t>(data.size(), 512));
      const auto start = text.find_first_not_of(" \t\r\n\xef\xbb\xbf");
      if (start != std::string_view::npos && text.substr(start).starts_with("<")) {
        return ".svg";
      }
      return ".png";
    }

  } // namespace launcher

  namespace {

    // The icon of a launcher request, written to a file the dialog can load; empty without one.
    std::filesystem::path writeIcon(GVariant* serialized, unsigned counter) {
      GIcon* icon = g_icon_deserialize(serialized);
      if (icon == nullptr || !G_IS_BYTES_ICON(icon)) {
        g_clear_object(&icon);
        return {};
      }
      GBytes* bytes = g_bytes_icon_get_bytes(G_BYTES_ICON(icon));
      gsize size = 0;
      const auto* data = static_cast<const std::uint8_t*>(g_bytes_get_data(bytes, &size));
      std::filesystem::path path = std::filesystem::path(g_get_user_runtime_dir()) / "xdg-desktop-portal-noctalia";
      std::error_code error;
      std::filesystem::create_directories(path, error);
      path /= std::format("launcher-icon-{}-{}{}", getpid(), counter, launcher::iconExtension({data, size}));
      std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
      g_object_unref(icon);
      return path;
    }

  } // namespace

  DynamicLauncher::DynamicLauncher(GDBusConnection* connection) : m_calls(connection) {
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kDynamicLauncher,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          if (method == "PrepareInstall") {
            prepareInstall(parameters, invocation);
          } else if (method == "RequestInstallToken") {
            // No application store is trusted to install launchers without asking.
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", dbus::Other));
          } else {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                  "Unknown method %s", method.c_str());
          }
        },
        [](const std::string& property) -> GVariant* {
          if (property == "SupportedLauncherTypes") {
            return g_variant_new_uint32(Application | WebApplication);
          }
          if (property == "version") {
            return g_variant_new_uint32(1);
          }
          return nullptr;
        });
  }

  DynamicLauncher::~DynamicLauncher() { m_interface.reset(); }

  void DynamicLauncher::prepareInstall(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* parentWindow = nullptr;
    const char* name = nullptr;
    GVariant* iconVariant = nullptr;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&s&s@v@a{sv})", &handle, &appId, &parentWindow, &name, &iconVariant, &options);
    guint32 type = Application;
    g_variant_lookup(options, "launcher_type", "u", &type);
    const auto target = lookupString(options, "target");
    const bool editable = lookupBool(options, "editable_name", true);
    g_variant_unref(options);

    GVariant* serialized = g_variant_get_variant(iconVariant);
    const auto iconPath = writeIcon(serialized, ++m_iconCounter);
    g_variant_unref(serialized);

    dialog::Request request;
    request.kind = dialog::Kind::Launcher;
    request.title = type == WebApplication ? "Create Web App Launcher" : "Create Launcher";
    request.subtitle = std::format("{} wants to add a launcher", applicationName(appId, "An application"));
    request.icon = iconPath.string();
    request.text = name;
    request.editable = editable;
    request.detail = type == WebApplication ? target : std::string();

    m_calls.start(handle, invocation, request,
                  [iconVariant, iconPath](GDBusMethodInvocation* call, const dialog::Answer& answer) {
                    std::error_code error;
                    if (!iconPath.empty()) {
                      std::filesystem::remove(iconPath, error);
                    }
                    GVariantBuilder results;
                    g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
                    guint32 response = answer.response;
                    if (response == dbus::Success) {
                      if (answer.text.empty()) {
                        response = dbus::Other;
                      } else {
                        g_variant_builder_add(&results, "{sv}", "name", g_variant_new_string(answer.text.c_str()));
                        g_variant_builder_add(&results, "{sv}", "icon", iconVariant);
                      }
                    }
                    g_variant_unref(iconVariant);
                    g_dbus_method_invocation_return_value(call, g_variant_new("(ua{sv})", response, &results));
                  });
  }

} // namespace xdpn
