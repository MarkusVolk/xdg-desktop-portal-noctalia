#include "settings/settings.h"

#include "dbus/introspection.h"

#include <cstdio>
#include <print>

namespace xdpn {

  namespace {

    constexpr const char* kSchemas[] = {
        "org.gnome.desktop.a11y",
        "org.gnome.desktop.a11y.interface",
        "org.gnome.desktop.calendar",
        "org.gnome.desktop.input-sources",
        "org.gnome.desktop.interface",
        "org.gnome.desktop.peripherals.mouse",
        "org.gnome.desktop.privacy",
        "org.gnome.desktop.sound",
        "org.gnome.desktop.wm.preferences",
        "org.gnome.settings-daemon.plugins.xsettings",
    };

    constexpr char kInterface[] = "org.gnome.desktop.interface";
    constexpr char kA11yInterface[] = "org.gnome.desktop.a11y.interface";
    constexpr char kAppearance[] = "org.freedesktop.appearance";
    constexpr char kFontconfig[] = "org.gnome.fontconfig";

    struct Accent {
      std::string_view name;
      std::array<double, 3> rgb;
    };

    constexpr Accent kAccents[] = {
        {"blue", {0x35 / 255.0, 0x84 / 255.0, 0xe4 / 255.0}},   {"teal", {0x21 / 255.0, 0x90 / 255.0, 0xa4 / 255.0}},
        {"green", {0x3a / 255.0, 0x94 / 255.0, 0x4a / 255.0}},  {"yellow", {0xc8 / 255.0, 0x88 / 255.0, 0x00 / 255.0}},
        {"orange", {0xed / 255.0, 0x5b / 255.0, 0x00 / 255.0}}, {"red", {0xe6 / 255.0, 0x2d / 255.0, 0x42 / 255.0}},
        {"pink", {0xd5 / 255.0, 0x61 / 255.0, 0x99 / 255.0}},   {"purple", {0x91 / 255.0, 0x41 / 255.0, 0xac / 255.0}},
        {"slate", {0x6f / 255.0, 0x83 / 255.0, 0x96 / 255.0}},
    };

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

  } // namespace

  namespace settings {

    bool namespaceMatches(std::string_view name, std::span<const std::string> patterns) {
      if (patterns.empty()) {
        return true;
      }
      for (const auto& pattern : patterns) {
        if (pattern.empty() || pattern == name) {
          return true;
        }
        if (pattern.back() == '*' && name.starts_with(std::string_view(pattern).substr(0, pattern.size() - 1))) {
          return true;
        }
      }
      return false;
    }

    std::optional<std::array<double, 3>> accentRgb(std::string_view name) {
      for (const auto& accent : kAccents) {
        if (accent.name == name) {
          return accent.rgb;
        }
      }
      return std::nullopt;
    }

  } // namespace settings

  Settings::Settings(GDBusConnection* connection) {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    for (const char* name : kSchemas) {
      GSettingsSchema* schema = source != nullptr ? g_settings_schema_source_lookup(source, name, TRUE) : nullptr;
      if (schema == nullptr) {
        continue;
      }
      auto entry = std::make_unique<Schema>();
      entry->name = name;
      entry->schema = schema;
      entry->settings = g_settings_new_full(schema, nullptr, nullptr);
      entry->changedHandler = g_signal_connect(
          entry->settings, "changed",
          G_CALLBACK(+[](GSettings* /*settings*/, const char* key, gpointer data) {
            auto* schemaEntry = static_cast<Schema*>(data);
            auto* self = static_cast<Settings*>(g_object_get_data(G_OBJECT(schemaEntry->settings), "xdpn-settings"));
            if (self != nullptr) {
              self->onChanged(*schemaEntry, key);
            }
          }),
          entry.get());
      g_object_set_data(G_OBJECT(entry->settings), "xdpn-settings", this);
      // GSettings only emits "changed" for keys that have been read once.
      gchar** keys = g_settings_schema_list_keys(schema);
      for (gchar** key = keys; *key != nullptr; ++key) {
        g_variant_unref(g_settings_get_value(entry->settings, *key));
      }
      g_strfreev(keys);
      m_schemas.push_back(std::move(entry));
    }

    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kSettings,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          handle(method, parameters, invocation);
        },
        [](const std::string& property) -> GVariant* {
          return property == "version" ? g_variant_new_uint32(2) : nullptr;
        });
  }

  Settings::~Settings() {
    m_interface.reset();
    for (auto& entry : m_schemas) {
      g_signal_handler_disconnect(entry->settings, entry->changedHandler);
      g_object_unref(entry->settings);
      g_settings_schema_unref(entry->schema);
    }
  }

  const Settings::Schema* Settings::find(std::string_view name) const {
    for (const auto& entry : m_schemas) {
      if (entry->name == name) {
        return entry.get();
      }
    }
    return nullptr;
  }

  GVariant* Settings::appearance(std::string_view key) const {
    if (key == "color-scheme") {
      const Schema* interface = find(kInterface);
      guint32 scheme = 0;
      if (interface != nullptr && g_settings_schema_has_key(interface->schema, "color-scheme")) {
        scheme = static_cast<guint32>(g_settings_get_enum(interface->settings, "color-scheme"));
      }
      return g_variant_new_uint32(scheme);
    }
    if (key == "contrast") {
      const Schema* a11y = find(kA11yInterface);
      bool high = a11y != nullptr && g_settings_schema_has_key(a11y->schema, "high-contrast") &&
                  g_settings_get_boolean(a11y->settings, "high-contrast");
      return g_variant_new_uint32(high ? 1 : 0);
    }
    if (key == "accent-color") {
      const Schema* interface = find(kInterface);
      if (interface == nullptr || !g_settings_schema_has_key(interface->schema, "accent-color")) {
        return nullptr;
      }
      gchar* name = g_settings_get_string(interface->settings, "accent-color");
      auto rgb = settings::accentRgb(name);
      g_free(name);
      if (!rgb) {
        return nullptr;
      }
      return g_variant_new("(ddd)", (*rgb)[0], (*rgb)[1], (*rgb)[2]);
    }
    return nullptr;
  }

  void Settings::handle(const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
    if (method == "ReadAll") {
      GVariant* patternsValue = g_variant_get_child_value(parameters, 0);
      const auto patterns = stringArray(patternsValue);
      g_variant_unref(patternsValue);

      GVariantBuilder all;
      g_variant_builder_init(&all, G_VARIANT_TYPE("a{sa{sv}}"));
      for (const auto& entry : m_schemas) {
        if (!settings::namespaceMatches(entry->name, patterns)) {
          continue;
        }
        GVariantBuilder keys;
        g_variant_builder_init(&keys, G_VARIANT_TYPE("a{sv}"));
        gchar** names = g_settings_schema_list_keys(entry->schema);
        for (gchar** key = names; *key != nullptr; ++key) {
          GVariant* value = g_settings_get_value(entry->settings, *key);
          g_variant_builder_add(&keys, "{sv}", *key, value);
          g_variant_unref(value);
        }
        g_strfreev(names);
        g_variant_builder_add(&all, "{sa{sv}}", entry->name.c_str(), &keys);
      }
      if (settings::namespaceMatches(kFontconfig, patterns)) {
        GVariantBuilder keys;
        g_variant_builder_init(&keys, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&keys, "{sv}", "serial", g_variant_new_int32(0));
        g_variant_builder_add(&all, "{sa{sv}}", kFontconfig, &keys);
      }
      if (settings::namespaceMatches(kAppearance, patterns)) {
        GVariantBuilder keys;
        g_variant_builder_init(&keys, G_VARIANT_TYPE("a{sv}"));
        for (const char* key : {"color-scheme", "contrast", "accent-color"}) {
          if (GVariant* value = appearance(key); value != nullptr) {
            g_variant_builder_add(&keys, "{sv}", key, value);
          }
        }
        g_variant_builder_add(&all, "{sa{sv}}", kAppearance, &keys);
      }
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sa{sv}})", &all));
      return;
    }

    if (method == "Read") {
      const char* name = nullptr;
      const char* key = nullptr;
      g_variant_get(parameters, "(&s&s)", &name, &key);
      GVariant* value = nullptr;
      if (std::string_view(name) == kAppearance) {
        value = appearance(key);
      } else if (std::string_view(name) == kFontconfig && std::string_view(key) == "serial") {
        value = g_variant_new_int32(0);
      } else if (const Schema* entry = find(name);
                 entry != nullptr && g_settings_schema_has_key(entry->schema, key)) {
        value = g_settings_get_value(entry->settings, key);
      }
      if (value == nullptr) {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.portal.Error.NotFound",
                                                   "Requested setting not found");
        return;
      }
      g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", g_variant_take_ref(value)));
      g_variant_unref(value);
      return;
    }

    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s",
                                          method.c_str());
  }

  void Settings::onChanged(const Schema& schema, const char* key) {
    const std::string_view keyView(key);
    GVariant* value = g_settings_get_value(schema.settings, key);
    m_interface->emitSignal("SettingChanged", g_variant_new("(ssv)", schema.name.c_str(), key, value));
    g_variant_unref(value);
    if (schema.name == kInterface && (keyView == "color-scheme" || keyView == "accent-color")) {
      if (GVariant* derived = appearance(keyView); derived != nullptr) {
        m_interface->emitSignal("SettingChanged", g_variant_new("(ssv)", kAppearance, key, derived));
      }
    }
    if (schema.name == kA11yInterface && keyView == "high-contrast") {
      m_interface->emitSignal("SettingChanged", g_variant_new("(ssv)", kAppearance, "contrast", appearance("contrast")));
    }
  }

} // namespace xdpn
