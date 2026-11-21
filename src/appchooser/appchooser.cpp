#include "appchooser/appchooser.h"

#include "dbus/introspection.h"

#include <gio/gdesktopappinfo.h>

#include <utility>

namespace xdpn {

  namespace {

    std::vector<std::string> stringArray(GVariant* value) {
      std::vector<std::string> out;
      gsize count = 0;
      const char** items = g_variant_get_strv(value, &count);
      for (gsize i = 0; i < count; ++i) {
        out.emplace_back(items[i]);
      }
      g_free(items);
      return out;
    }

    std::string lookupString(GVariant* options, const char* key) {
      const char* value = nullptr;
      return g_variant_lookup(options, key, "&s", &value) ? std::string(value) : std::string();
    }

    // What is being opened, for the subtitle: the file name, else the URI, else the content type.
    std::string subject(GVariant* options) {
      if (auto filename = lookupString(options, "filename"); !filename.empty()) {
        return filename;
      }
      if (auto uri = lookupString(options, "uri"); !uri.empty()) {
        if (gchar* path = g_filename_from_uri(uri.c_str(), nullptr, nullptr); path != nullptr) {
          gchar* base = g_path_get_basename(path);
          std::string name = base;
          g_free(base);
          g_free(path);
          return name;
        }
        return uri;
      }
      if (auto type = lookupString(options, "content_type"); !type.empty()) {
        gchar* description = g_content_type_get_description(type.c_str());
        std::string text = description != nullptr ? description : type;
        g_free(description);
        return text;
      }
      return {};
    }

  } // namespace

  namespace appchooser {

    std::vector<dialog::Application> applications(const std::vector<std::string>& ids) {
      std::vector<dialog::Application> list;
      for (const auto& id : ids) {
        GDesktopAppInfo* info = g_desktop_app_info_new((id + ".desktop").c_str());
        if (info == nullptr) {
          continue;
        }
        const char* name = g_app_info_get_display_name(G_APP_INFO(info));
        list.push_back({.id = id, .name = name != nullptr ? name : id});
        g_object_unref(info);
      }
      return list;
    }

  } // namespace appchooser

  AppChooser::AppChooser(GDBusConnection* connection) : m_connection(G_DBUS_CONNECTION(g_object_ref(connection))) {
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kAppChooser,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          if (method == "ChooseApplication") {
            chooseApplication(parameters, invocation);
          } else if (method == "UpdateChoices") {
            updateChoices(parameters, invocation);
          } else {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                  "Unknown method %s", method.c_str());
          }
        });
  }

  AppChooser::~AppChooser() {
    m_interface.reset();
    for (auto& [handle, call] : m_calls) {
      (void)handle;
      g_dbus_method_invocation_return_value(call->invocation, g_variant_new("(u@a{sv})", dbus::Other,
                                                                            g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)));
    }
    m_calls.clear();
    g_object_unref(m_connection);
  }

  void AppChooser::chooseApplication(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* parentWindow = nullptr;
    GVariant* choices = nullptr;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&s@as@a{sv})", &handle, &appId, &parentWindow, &choices, &options);

    dialog::Request request;
    request.kind = dialog::Kind::AppChooser;
    request.title = "Open With";
    request.subtitle = subject(options);
    request.lastChoice = lookupString(options, "last_choice");
    request.applications = appchooser::applications(stringArray(choices));
    g_variant_unref(choices);
    g_variant_unref(options);

    const std::string key = handle;
    auto call = std::make_unique<Call>();
    call->invocation = invocation;
    call->request = std::make_unique<dbus::Request>(m_connection, key, [this, key] {
      if (auto it = m_calls.find(key); it != m_calls.end() && it->second->dialog) {
        it->second->dialog->cancel();
      }
    });
    Call* raw = call.get();
    m_calls[key] = std::move(call);
    raw->dialog = std::make_unique<DialogProcess>(request, [this, key](const dialog::Answer& answer) { complete(key, answer); });
    if (!raw->dialog->started()) {
      complete(key, {.response = dbus::Other, .choices = {}, .application = {}});
    }
  }

  void AppChooser::updateChoices(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    GVariant* choices = nullptr;
    g_variant_get(parameters, "(&o@as)", &handle, &choices);
    if (auto it = m_calls.find(handle); it != m_calls.end() && it->second->dialog) {
      it->second->dialog->send(dialog::formatApplications(appchooser::applications(stringArray(choices))));
    }
    g_variant_unref(choices);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  }

  void AppChooser::complete(const std::string& handle, const dialog::Answer& answer) {
    auto it = m_calls.find(handle);
    if (it == m_calls.end()) {
      return;
    }
    GVariantBuilder results;
    g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
    guint32 response = answer.response;
    if (response == dbus::Success) {
      if (answer.application.empty()) {
        response = dbus::Other;
      } else {
        g_variant_builder_add(&results, "{sv}", "choice", g_variant_new_string(answer.application.c_str()));
      }
    }
    g_dbus_method_invocation_return_value(it->second->invocation, g_variant_new("(ua{sv})", response, &results));
    auto call = std::move(it->second);
    m_calls.erase(it);
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE, +[](gpointer) -> gboolean { return G_SOURCE_REMOVE; }, call.release(),
        +[](gpointer data) { delete static_cast<Call*>(data); });
  }

} // namespace xdpn
