#pragma once

#include <gio/gio.h>

#include <functional>
#include <string>

namespace xdpn::dbus {

  inline constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";

  // One exported D-Bus interface: the methods go to a single handler, properties to a getter.
  class Interface {
  public:
    using MethodHandler = std::function<void(const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation)>;
    using PropertyGetter = std::function<GVariant*(const std::string& property)>;
    using PropertySetter = std::function<bool(const std::string& property, GVariant* value)>;

    Interface(GDBusConnection* connection, std::string path, const char* introspectionXml, MethodHandler methods,
              PropertyGetter properties = {}, PropertySetter setter = {});
    ~Interface();

    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;
    Interface(Interface&&) = delete;
    Interface& operator=(Interface&&) = delete;

    [[nodiscard]] bool registered() const { return m_registrationId != 0; }
    [[nodiscard]] const std::string& path() const { return m_path; }
    [[nodiscard]] const char* name() const;

    // Takes ownership of a floating parameters variant.
    void emitSignal(const char* signal, GVariant* parameters) const;
    // org.freedesktop.DBus.Properties.PropertiesChanged for one property; takes a floating value.
    void emitPropertyChanged(const char* property, GVariant* value) const;

  private:
    static void onMethodCall(GDBusConnection* connection, const char* sender, const char* path, const char* interface,
                             const char* method, GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data);
    static GVariant* onGetProperty(GDBusConnection* connection, const char* sender, const char* path,
                                   const char* interface, const char* property, GError** error, gpointer data);
    static gboolean onSetProperty(GDBusConnection* connection, const char* sender, const char* path,
                                  const char* interface, const char* property, GVariant* value, GError** error,
                                  gpointer data);

    GDBusConnection* m_connection = nullptr;
    GDBusNodeInfo* m_node = nullptr;
    std::string m_path;
    MethodHandler m_methods;
    PropertyGetter m_properties;
    PropertySetter m_setter;
    guint m_registrationId = 0;
  };

  // The impl.portal.Request object a backend exports at the handle path for the length of a call.
  class Request {
  public:
    using CloseHandler = std::function<void()>;

    Request(GDBusConnection* connection, const std::string& handle, CloseHandler onClose);

    [[nodiscard]] bool closed() const { return m_closed; }

  private:
    bool m_closed = false;
    CloseHandler m_onClose;
    Interface m_interface;
  };

  // Portal response codes.
  enum Response : guint32 { Success = 0, Cancelled = 1, Other = 2 };

  void returnError(GDBusMethodInvocation* invocation, const char* message);

} // namespace xdpn::dbus
