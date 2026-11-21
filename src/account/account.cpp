#include "account/account.h"

#include "dbus/introspection.h"
#include "dbus/options.h"

#include <pwd.h>
#include <unistd.h>

#include <filesystem>
#include <format>

namespace xdpn {

  namespace account {

    std::string gecosName(const std::string& gecos) { return gecos.substr(0, gecos.find(',')); }

  } // namespace account

  namespace {

    Account::User currentUser() {
      Account::User user;
      if (const passwd* entry = getpwuid(getuid()); entry != nullptr) {
        user.id = entry->pw_name != nullptr ? entry->pw_name : "";
        user.name = account::gecosName(entry->pw_gecos != nullptr ? entry->pw_gecos : "");
        std::error_code error;
        const auto face = std::filesystem::path(entry->pw_dir != nullptr ? entry->pw_dir : "") / ".face";
        if (std::filesystem::is_regular_file(face, error)) {
          user.image = face.string();
        }
      }

      GDBusConnection* system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
      if (system == nullptr) {
        return user;
      }
      GVariant* found = g_dbus_connection_call_sync(system, "org.freedesktop.Accounts", "/org/freedesktop/Accounts",
                                                    "org.freedesktop.Accounts", "FindUserById",
                                                    g_variant_new("(x)", static_cast<gint64>(getuid())),
                                                    G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
      if (found != nullptr) {
        const char* path = nullptr;
        g_variant_get(found, "(&o)", &path);
        GVariant* all = g_dbus_connection_call_sync(system, "org.freedesktop.Accounts", path,
                                                    "org.freedesktop.DBus.Properties", "GetAll",
                                                    g_variant_new("(s)", "org.freedesktop.Accounts.User"),
                                                    G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr,
                                                    nullptr);
        if (all != nullptr) {
          GVariant* properties = g_variant_get_child_value(all, 0);
          if (auto name = lookupString(properties, "RealName"); !name.empty()) {
            user.name = name;
          }
          std::error_code error;
          if (auto icon = lookupString(properties, "IconFile");
              !icon.empty() && std::filesystem::is_regular_file(icon, error)) {
            user.image = icon;
          }
          g_variant_unref(properties);
          g_variant_unref(all);
        }
        g_variant_unref(found);
      }
      g_object_unref(system);
      return user;
    }

  } // namespace

  Account::Account(GDBusConnection* connection) : m_calls(connection) {
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kAccount,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          if (method == "GetUserInformation") {
            getUserInformation(parameters, invocation);
            return;
          }
          g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                "Unknown method %s", method.c_str());
        });
  }

  Account::~Account() { m_interface.reset(); }

  void Account::getUserInformation(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* window = nullptr;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&s@a{sv})", &handle, &appId, &window, &options);
    const auto reason = lookupString(options, "reason");
    g_variant_unref(options);

    const User user = currentUser();
    const auto app = applicationName(appId, "An application");
    dialog::Request request;
    request.kind = dialog::Kind::Account;
    request.title = "Share Your Details?";
    request.subtitle = user.name.empty() ? user.id : std::format("{} ({})", user.name, user.id);
    request.body = std::format("{} wants to use your name and picture.", app);
    if (!reason.empty()) {
      request.body += "\n" + reason;
    }
    request.icon = user.image;

    m_calls.start(handle, invocation, request, [user](GDBusMethodInvocation* call, const dialog::Answer& answer) {
      GVariantBuilder results;
      g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
      if (answer.response == dbus::Success) {
        g_variant_builder_add(&results, "{sv}", "id", g_variant_new_string(user.id.c_str()));
        g_variant_builder_add(&results, "{sv}", "name", g_variant_new_string(user.name.c_str()));
        gchar* uri = user.image.empty() ? nullptr : g_filename_to_uri(user.image.c_str(), nullptr, nullptr);
        g_variant_builder_add(&results, "{sv}", "image", g_variant_new_string(uri != nullptr ? uri : ""));
        g_free(uri);
      }
      g_dbus_method_invocation_return_value(call, g_variant_new("(ua{sv})", answer.response, &results));
    });
  }

} // namespace xdpn
