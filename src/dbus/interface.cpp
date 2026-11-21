#include "dbus/interface.h"

#include "dbus/introspection.h"

#include <cstdio>
#include <print>
#include <utility>

namespace xdpn::dbus {

  Interface::Interface(GDBusConnection* connection, std::string path, const char* introspectionXml,
                       MethodHandler methods, PropertyGetter properties, PropertySetter setter)
      : m_connection(G_DBUS_CONNECTION(g_object_ref(connection))), m_path(std::move(path)),
        m_methods(std::move(methods)), m_properties(std::move(properties)), m_setter(std::move(setter)) {
    GError* error = nullptr;
    m_node = g_dbus_node_info_new_for_xml(introspectionXml, &error);
    if (m_node == nullptr || m_node->interfaces == nullptr || m_node->interfaces[0] == nullptr) {
      std::println(stderr, "dbus: bad introspection data: {}", error != nullptr ? error->message : "no interface");
      g_clear_error(&error);
      return;
    }

    GDBusInterfaceVTable vtable{};
    vtable.method_call = &Interface::onMethodCall;
    vtable.get_property = &Interface::onGetProperty;
    vtable.set_property = &Interface::onSetProperty;
    m_registrationId = g_dbus_connection_register_object(m_connection, m_path.c_str(), m_node->interfaces[0], &vtable,
                                                         this, nullptr, &error);
    if (m_registrationId == 0) {
      std::println(stderr, "dbus: cannot export {} at {}: {}", name(), m_path, error->message);
      g_clear_error(&error);
    }
  }

  Interface::~Interface() {
    if (m_registrationId != 0) {
      g_dbus_connection_unregister_object(m_connection, m_registrationId);
    }
    if (m_node != nullptr) {
      g_dbus_node_info_unref(m_node);
    }
    g_object_unref(m_connection);
  }

  const char* Interface::name() const {
    return m_node != nullptr && m_node->interfaces != nullptr && m_node->interfaces[0] != nullptr
               ? m_node->interfaces[0]->name
               : "";
  }

  void Interface::emitSignal(const char* signal, GVariant* parameters) const {
    GError* error = nullptr;
    if (!g_dbus_connection_emit_signal(m_connection, nullptr, m_path.c_str(), name(), signal, parameters, &error)) {
      std::println(stderr, "dbus: cannot emit {}.{}: {}", name(), signal, error->message);
      g_clear_error(&error);
    }
  }

  void Interface::emitPropertyChanged(const char* property, GVariant* value) const {
    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&changed, "{sv}", property, value);
    GError* error = nullptr;
    if (!g_dbus_connection_emit_signal(m_connection, nullptr, m_path.c_str(), "org.freedesktop.DBus.Properties",
                                       "PropertiesChanged",
                                       g_variant_new("(sa{sv}as)", name(), &changed, nullptr), &error)) {
      std::println(stderr, "dbus: cannot emit PropertiesChanged for {}: {}", property, error->message);
      g_clear_error(&error);
    }
  }

  void Interface::onMethodCall(GDBusConnection* /*connection*/, const char* /*sender*/, const char* /*path*/,
                               const char* /*interface*/, const char* method, GVariant* parameters,
                               GDBusMethodInvocation* invocation, gpointer data) {
    auto* self = static_cast<Interface*>(data);
    if (!self->m_methods) {
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                            "Method %s is not implemented", method);
      return;
    }
    self->m_methods(method, parameters, invocation);
  }

  GVariant* Interface::onGetProperty(GDBusConnection* /*connection*/, const char* /*sender*/, const char* /*path*/,
                                     const char* /*interface*/, const char* property, GError** error, gpointer data) {
    auto* self = static_cast<Interface*>(data);
    GVariant* value = self->m_properties ? self->m_properties(property) : nullptr;
    if (value == nullptr) {
      g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property);
    }
    return value;
  }

  gboolean Interface::onSetProperty(GDBusConnection* /*connection*/, const char* /*sender*/, const char* /*path*/,
                                    const char* /*interface*/, const char* property, GVariant* value, GError** error,
                                    gpointer data) {
    auto* self = static_cast<Interface*>(data);
    if (self->m_setter && self->m_setter(property, value)) {
      return TRUE;
    }
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_PROPERTY_READ_ONLY, "Property %s cannot be set", property);
    return FALSE;
  }

  Request::Request(GDBusConnection* connection, const std::string& handle, CloseHandler onClose)
      : m_onClose(std::move(onClose)),
        m_interface(connection, handle, introspection::kRequest,
                    [this](const std::string& method, GVariant* /*parameters*/, GDBusMethodInvocation* invocation) {
                      if (method == "Close") {
                        g_dbus_method_invocation_return_value(invocation, nullptr);
                        if (!m_closed) {
                          m_closed = true;
                          if (m_onClose) {
                            m_onClose();
                          }
                        }
                        return;
                      }
                      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                            "Unknown method %s", method.c_str());
                    }) {}

  void returnError(GDBusMethodInvocation* invocation, const char* message) {
    g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.portal.Error.Failed", message);
  }

} // namespace xdpn::dbus
