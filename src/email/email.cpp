#include "email/email.h"

#include "dbus/introspection.h"
#include "dbus/options.h"

#include <cstdio>
#include <print>

namespace xdpn {

  namespace email {

    namespace {

      std::string escape(const std::string& text) {
        gchar* escaped = g_uri_escape_string(text.c_str(), nullptr, FALSE);
        std::string result = escaped;
        g_free(escaped);
        return result;
      }

    } // namespace

    std::string mailtoUri(const Message& message) {
      std::string uri = "mailto:";
      for (std::size_t i = 0; i < message.to.size(); ++i) {
        uri += (i == 0 ? "" : ",") + escape(message.to[i]);
      }
      char separator = '?';
      auto add = [&uri, &separator](const char* field, const std::string& value) {
        uri += separator;
        uri += field;
        uri += '=';
        uri += escape(value);
        separator = '&';
      };
      for (const auto& address : message.cc) {
        add("cc", address);
      }
      for (const auto& address : message.bcc) {
        add("bcc", address);
      }
      if (!message.subject.empty()) {
        add("subject", message.subject);
      }
      if (!message.body.empty()) {
        add("body", message.body);
      }
      for (const auto& path : message.attachments) {
        add("attach", path);
      }
      return uri;
    }

  } // namespace email

  namespace {

    std::vector<std::string> lookupStrings(GVariant* options, const char* key) {
      std::vector<std::string> out;
      const char** values = nullptr;
      if (g_variant_lookup(options, key, "^a&s", &values)) {
        for (const char** value = values; *value != nullptr; ++value) {
          out.emplace_back(*value);
        }
        g_free(values);
      }
      return out;
    }


  } // namespace

  Email::Email(GDBusConnection* connection) {
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kEmail,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          if (method == "ComposeEmail") {
            composeEmail(parameters, invocation);
            return;
          }
          g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                "Unknown method %s", method.c_str());
        });
  }

  Email::~Email() = default;

  void Email::composeEmail(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* parentWindow = nullptr;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&s@a{sv})", &handle, &appId, &parentWindow, &options);

    email::Message message;
    if (auto address = lookupString(options, "address"); !address.empty()) {
      message.to.push_back(address);
    }
    for (auto& address : lookupStrings(options, "addresses")) {
      message.to.push_back(std::move(address));
    }
    message.cc = lookupStrings(options, "cc");
    message.bcc = lookupStrings(options, "bcc");
    message.subject = lookupString(options, "subject");
    message.body = lookupString(options, "body");
    message.attachments = lookupStrings(options, "attachments");
    const std::string token = lookupString(options, "activation_token");
    g_variant_unref(options);

    guint32 response = dbus::Success;
    GAppInfo* client = g_app_info_get_default_for_uri_scheme("mailto");
    if (client == nullptr) {
      std::println(stderr, "email: no mail client handles mailto:");
      response = dbus::Other;
    } else {
      GAppLaunchContext* context = g_app_launch_context_new();
      if (!token.empty()) {
        g_app_launch_context_setenv(context, "XDG_ACTIVATION_TOKEN", token.c_str());
      }
      const std::string uri = email::mailtoUri(message);
      GList* uris = g_list_append(nullptr, const_cast<char*>(uri.c_str()));
      GError* error = nullptr;
      if (!g_app_info_launch_uris(client, uris, context, &error)) {
        std::println(stderr, "email: cannot start the mail client: {}", error->message);
        g_clear_error(&error);
        response = dbus::Other;
      }
      g_list_free(uris);
      g_object_unref(context);
      g_object_unref(client);
    }
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@a{sv})", response,
                                                                    g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)));
  }

} // namespace xdpn
