#include "lockdown/lockdown.h"

#include "dbus/introspection.h"

namespace xdpn {

  namespace {

    struct Source {
      const char* property;
      const char* schema;
      const char* key;
      bool inverted;
    };

    constexpr Source kSources[] = {
        {"disable-printing", "org.gnome.desktop.lockdown", "disable-printing", false},
        {"disable-save-to-disk", "org.gnome.desktop.lockdown", "disable-save-to-disk", false},
        {"disable-application-handlers", "org.gnome.desktop.lockdown", "disable-application-handlers", false},
        {"disable-location", "org.gnome.system.location", "enabled", true},
        {"disable-camera", "org.gnome.desktop.privacy", "disable-camera", false},
        {"disable-microphone", "org.gnome.desktop.privacy", "disable-microphone", false},
        {"disable-sound-output", "org.gnome.desktop.privacy", "disable-sound-output", false},
    };

  } // namespace

  Lockdown::Lockdown(GDBusConnection* connection) {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    for (const auto& entry : kSources) {
      GSettingsSchema* schema = source != nullptr ? g_settings_schema_source_lookup(source, entry.schema, TRUE) : nullptr;
      if (schema == nullptr) {
        continue;
      }
      const bool hasKey = g_settings_schema_has_key(schema, entry.key);
      GSettings* settings = nullptr;
      if (hasKey) {
        for (GSettings* existing : m_settings) {
          gchar* id = nullptr;
          g_object_get(existing, "schema-id", &id, nullptr);
          const bool same = id != nullptr && std::string(id) == entry.schema;
          g_free(id);
          if (same) {
            settings = existing;
            break;
          }
        }
        if (settings == nullptr) {
          settings = g_settings_new_full(schema, nullptr, nullptr);
          m_settings.push_back(settings);
        }
      }
      g_settings_schema_unref(schema);
      if (settings == nullptr) {
        continue;
      }
      auto binding = std::make_unique<Binding>();
      binding->property = entry.property;
      binding->settings = settings;
      binding->key = entry.key;
      binding->inverted = entry.inverted;
      // GSettings only emits "changed" for keys that have been read once.
      (void)g_settings_get_boolean(settings, entry.key);
      binding->handler = g_signal_connect(
          settings, ("changed::" + binding->key).c_str(),
          G_CALLBACK(+[](GSettings* settings, const char* key, gpointer data) {
            auto* bound = static_cast<Binding*>(data);
            auto* self = static_cast<Lockdown*>(g_object_get_data(G_OBJECT(settings), "xdpn-lockdown"));
            if (self != nullptr && self->m_interface) {
              const bool value = g_settings_get_boolean(settings, key) != bound->inverted;
              self->m_interface->emitPropertyChanged(bound->property.c_str(), g_variant_new_boolean(value));
            }
          }),
          binding.get());
      g_object_set_data(G_OBJECT(settings), "xdpn-lockdown", this);
      m_bindings.push_back(std::move(binding));
    }

    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kLockdown,
        [](const std::string& method, GVariant* /*parameters*/, GDBusMethodInvocation* invocation) {
          g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                "Unknown method %s", method.c_str());
        },
        [this](const std::string& property) -> GVariant* {
          const Binding* binding = find(property);
          const bool value = binding != nullptr &&
                             (g_settings_get_boolean(binding->settings, binding->key.c_str()) != binding->inverted);
          return g_variant_new_boolean(value);
        },
        [this](const std::string& property, GVariant* value) -> bool {
          const Binding* binding = find(property);
          if (binding == nullptr || !g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
            return false;
          }
          return g_settings_set_boolean(binding->settings, binding->key.c_str(),
                                        (g_variant_get_boolean(value) != FALSE) != binding->inverted) != FALSE;
        });
  }

  Lockdown::~Lockdown() {
    m_interface.reset();
    for (auto& binding : m_bindings) {
      g_signal_handler_disconnect(binding->settings, binding->handler);
    }
    m_bindings.clear();
    for (GSettings* settings : m_settings) {
      g_object_unref(settings);
    }
  }

  const Lockdown::Binding* Lockdown::find(const std::string& property) const {
    for (const auto& binding : m_bindings) {
      if (binding->property == property) {
        return binding.get();
      }
    }
    return nullptr;
  }

} // namespace xdpn
