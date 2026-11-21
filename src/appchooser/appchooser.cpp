#include "appchooser/appchooser.h"

#include "dbus/introspection.h"
#include "dbus/options.h"
#include "icons/icon_lookup.h"

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
        list.push_back({.id = id, .name = name != nullptr ? name : id, .icon = icons::lookup(g_app_info_get_icon(G_APP_INFO(info)))});
        g_object_unref(info);
      }
      return list;
    }

  } // namespace appchooser

  AppChooser::AppChooser(GDBusConnection* connection) : m_calls(connection) {
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

  AppChooser::~AppChooser() { m_interface.reset(); }

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

    m_calls.start(handle, invocation, request, [](GDBusMethodInvocation* call, const dialog::Answer& answer) {
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
      g_dbus_method_invocation_return_value(call, g_variant_new("(ua{sv})", response, &results));
    });
  }

  void AppChooser::updateChoices(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    GVariant* choices = nullptr;
    g_variant_get(parameters, "(&o@as)", &handle, &choices);
    if (DialogProcess* process = m_calls.dialog(handle); process != nullptr) {
      process->send(dialog::formatApplications(appchooser::applications(stringArray(choices))));
    }
    g_variant_unref(choices);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  }


} // namespace xdpn
