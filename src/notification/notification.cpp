#include "notification/notification.h"

#include "dbus/introspection.h"
#include "icons/icon_lookup.h"

#include <gio/gdesktopappinfo.h>

#include <charconv>
#include <cstdio>
#include <print>

namespace xdpn {

  namespace {

    constexpr char kServer[] = "org.freedesktop.Notifications";
    constexpr char kServerPath[] = "/org/freedesktop/Notifications";
    constexpr char kButtonPrefix[] = "xdpn-button-";

    std::string key(const std::string& appId, const std::string& id) { return appId + '\n' + id; }

    std::string lookupString(GVariant* dict, const char* name) {
      const char* value = nullptr;
      return g_variant_lookup(dict, name, "&s", &value) ? std::string(value) : std::string();
    }

    std::string applicationName(const std::string& appId) {
      if (appId.empty()) {
        return {};
      }
      GDesktopAppInfo* info = g_desktop_app_info_new((appId + ".desktop").c_str());
      if (info == nullptr) {
        return appId;
      }
      std::string name = g_app_info_get_display_name(G_APP_INFO(info));
      g_object_unref(info);
      return name;
    }

    // The serialized portal icon to a name or path the server understands.
    std::string iconFor(GVariant* notification, const std::string& appId) {
      GVariant* serialized = g_variant_lookup_value(notification, "icon", nullptr);
      std::string result;
      if (serialized != nullptr) {
        GIcon* icon = g_icon_deserialize(serialized);
        if (icon != nullptr && G_IS_THEMED_ICON(icon)) {
          const gchar* const* names = g_themed_icon_get_names(G_THEMED_ICON(icon));
          result = names != nullptr && names[0] != nullptr ? names[0] : "";
        } else if (icon != nullptr) {
          result = icons::lookup(icon);
        }
        g_clear_object(&icon);
        g_variant_unref(serialized);
      }
      return result.empty() ? appId : result;
    }

  } // namespace

  namespace notification {

    std::uint8_t urgency(const std::string& priority) {
      if (priority == "low") {
        return 0;
      }
      if (priority == "urgent") {
        return 2;
      }
      return 1;
    }

    std::string applicationPath(const std::string& appId) {
      std::string path = "/" + appId;
      for (char& c : path) {
        if (c == '.') {
          c = '/';
        } else if (c == '-') {
          c = '_';
        }
      }
      return path;
    }

    std::string buttonKey(std::size_t index) { return kButtonPrefix + std::to_string(index); }

    std::optional<std::size_t> buttonIndex(const std::string& key) {
      const std::string_view prefix = kButtonPrefix;
      if (!key.starts_with(prefix)) {
        return std::nullopt;
      }
      std::size_t index = 0;
      const auto* begin = key.data() + prefix.size();
      const auto* end = key.data() + key.size();
      const auto [rest, error] = std::from_chars(begin, end, index);
      if (error != std::errc() || rest != end || begin == end) {
        return std::nullopt;
      }
      return index;
    }

  } // namespace notification

  Notification::Entry::~Entry() {
    if (defaultAction && defaultAction->target != nullptr) {
      g_variant_unref(defaultAction->target);
    }
    for (auto& button : buttons) {
      if (button.target != nullptr) {
        g_variant_unref(button.target);
      }
    }
  }

  Notification::Notification(GDBusConnection* connection)
      : m_connection(G_DBUS_CONNECTION(g_object_ref(connection))) {
    auto subscribe = [this](const char* signal) {
      return g_dbus_connection_signal_subscribe(
          m_connection, kServer, kServer, signal, kServerPath, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
          +[](GDBusConnection*, const char*, const char*, const char*, const char* name, GVariant* parameters,
              gpointer data) { static_cast<Notification*>(data)->onServerSignal(name, parameters); },
          this, nullptr);
    };
    m_actionSubscription = subscribe("ActionInvoked");
    m_closedSubscription = subscribe("NotificationClosed");
    m_tokenSubscription = subscribe("ActivationToken");
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kNotification,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          if (method == "AddNotification") {
            add(parameters, invocation);
          } else if (method == "RemoveNotification") {
            remove(parameters, invocation);
          } else {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                  "Unknown method %s", method.c_str());
          }
        },
        [](const std::string& property) -> GVariant* {
          if (property == "version") {
            return g_variant_new_uint32(2);
          }
          if (property == "SupportedOptions") {
            return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0);
          }
          return nullptr;
        });
  }

  Notification::~Notification() {
    m_interface.reset();
    for (guint id : {m_actionSubscription, m_closedSubscription, m_tokenSubscription}) {
      if (id != 0) {
        g_dbus_connection_signal_unsubscribe(m_connection, id);
      }
    }
    m_entries.clear();
    g_object_unref(m_connection);
  }

  void Notification::add(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* appId = nullptr;
    const char* id = nullptr;
    GVariant* notification = nullptr;
    g_variant_get(parameters, "(&s&s@a{sv})", &appId, &id, &notification);

    auto entry = std::make_unique<Entry>();
    entry->appId = appId;
    entry->id = id;
    const std::string mapKey = key(appId, id);
    if (auto it = m_entries.find(mapKey); it != m_entries.end()) {
      entry->serverId = it->second->serverId;
    }

    GVariantBuilder actions;
    g_variant_builder_init(&actions, G_VARIANT_TYPE_STRING_ARRAY);
    if (auto name = lookupString(notification, "default-action"); !name.empty()) {
      entry->defaultAction = Action{.name = name, .target = g_variant_lookup_value(notification, "default-action-target", nullptr)};
      g_variant_builder_add(&actions, "s", "default");
      g_variant_builder_add(&actions, "s", "");
    }
    if (GVariant* buttons = g_variant_lookup_value(notification, "buttons", G_VARIANT_TYPE("aa{sv}")); buttons != nullptr) {
      GVariantIter iter;
      g_variant_iter_init(&iter, buttons);
      GVariant* button = nullptr;
      while ((button = g_variant_iter_next_value(&iter)) != nullptr) {
        const auto label = lookupString(button, "label");
        const auto action = lookupString(button, "action");
        if (!action.empty()) {
          g_variant_builder_add(&actions, "s", notification::buttonKey(entry->buttons.size()).c_str());
          g_variant_builder_add(&actions, "s", label.c_str());
          entry->buttons.push_back({.name = action, .target = g_variant_lookup_value(button, "target", nullptr)});
        }
        g_variant_unref(button);
      }
      g_variant_unref(buttons);
    }

    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&hints, "{sv}", "urgency",
                          g_variant_new_byte(notification::urgency(lookupString(notification, "priority"))));
    if (!entry->appId.empty()) {
      g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(entry->appId.c_str()));
    }

    const auto appName = applicationName(entry->appId);
    const auto icon = iconFor(notification, entry->appId);
    const auto title = lookupString(notification, "title");
    const auto body = lookupString(notification, "body");
    g_variant_unref(notification);

    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        m_connection, kServer, kServerPath, kServer, "Notify",
        g_variant_new("(susssasa{sv}i)", appName.c_str(), entry->serverId, icon.c_str(), title.c_str(), body.c_str(),
                      &actions, &hints, -1),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (reply == nullptr) {
      g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.portal.Error.Failed", error->message);
      g_clear_error(&error);
      return;
    }
    g_variant_get(reply, "(u)", &entry->serverId);
    g_variant_unref(reply);
    m_entries[mapKey] = std::move(entry);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  }

  void Notification::remove(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* appId = nullptr;
    const char* id = nullptr;
    g_variant_get(parameters, "(&s&s)", &appId, &id);
    if (auto it = m_entries.find(key(appId, id)); it != m_entries.end()) {
      g_dbus_connection_call(m_connection, kServer, kServerPath, kServer, "CloseNotification",
                             g_variant_new("(u)", it->second->serverId), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
                             nullptr, nullptr);
      m_entries.erase(it);
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
  }

  Notification::Entry* Notification::findByServerId(guint32 serverId) {
    for (auto& [mapKey, entry] : m_entries) {
      (void)mapKey;
      if (entry->serverId == serverId) {
        return entry.get();
      }
    }
    return nullptr;
  }

  void Notification::onServerSignal(const char* signal, GVariant* parameters) {
    const std::string_view name = signal;
    if (name == "ActivationToken") {
      guint32 serverId = 0;
      const char* token = nullptr;
      g_variant_get(parameters, "(u&s)", &serverId, &token);
      m_tokens[serverId] = token;
      return;
    }
    if (name == "NotificationClosed") {
      guint32 serverId = 0;
      guint32 reason = 0;
      g_variant_get(parameters, "(uu)", &serverId, &reason);
      m_tokens.erase(serverId);
      if (Entry* entry = findByServerId(serverId); entry != nullptr) {
        m_entries.erase(key(entry->appId, entry->id));
      }
      return;
    }
    if (name == "ActionInvoked") {
      guint32 serverId = 0;
      const char* actionKey = nullptr;
      g_variant_get(parameters, "(u&s)", &serverId, &actionKey);
      Entry* entry = findByServerId(serverId);
      if (entry == nullptr) {
        return;
      }
      std::string token;
      if (auto it = m_tokens.find(serverId); it != m_tokens.end()) {
        token = it->second;
        m_tokens.erase(it);
      }
      if (std::string_view(actionKey) == "default") {
        if (entry->defaultAction) {
          activate(*entry, *entry->defaultAction, token);
        }
      } else if (auto index = notification::buttonIndex(actionKey); index && *index < entry->buttons.size()) {
        activate(*entry, entry->buttons[*index], token);
      }
    }
  }

  void Notification::activate(const Entry& entry, const Action& action, const std::string& token) {
    GVariantBuilder parameter;
    g_variant_builder_init(&parameter, G_VARIANT_TYPE("av"));
    if (action.target != nullptr) {
      g_variant_builder_add(&parameter, "v", action.target);
    }
    GVariantBuilder platform;
    g_variant_builder_init(&platform, G_VARIANT_TYPE_VARDICT);
    if (!token.empty()) {
      g_variant_builder_add(&platform, "{sv}", "desktop-startup-id", g_variant_new_string(token.c_str()));
      g_variant_builder_add(&platform, "{sv}", "activation-token", g_variant_new_string(token.c_str()));
    }
    const std::string path = notification::applicationPath(entry.appId);
    if (action.name.starts_with("app.")) {
      g_dbus_connection_call(m_connection, entry.appId.c_str(), path.c_str(), "org.freedesktop.Application",
                             "ActivateAction",
                             g_variant_new("(sava{sv})", action.name.c_str() + 4, &parameter, &platform), nullptr,
                             G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
      return;
    }
    g_dbus_connection_call(m_connection, entry.appId.c_str(), path.c_str(), "org.freedesktop.Application", "Activate",
                           g_variant_new("(a{sv})", &platform), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr,
                           nullptr);
    GVariantBuilder signalParameter;
    g_variant_builder_init(&signalParameter, G_VARIANT_TYPE("av"));
    if (action.target != nullptr) {
      g_variant_builder_add(&signalParameter, "v", action.target);
    }
    m_interface->emitSignal("ActionInvoked", g_variant_new("(sssav)", entry.appId.c_str(), entry.id.c_str(),
                                                           action.name.c_str(), &signalParameter));
  }

} // namespace xdpn
