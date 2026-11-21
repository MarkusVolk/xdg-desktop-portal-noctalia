#pragma once

#include "dbus/interface.h"

#include <gio/gio.h>

#include <map>
#include <memory>
#include <string>

namespace xdpn {

  // org.freedesktop.impl.portal.Inhibit: suspend and logout through logind inhibitor locks,
  // idle through org.freedesktop.ScreenSaver (served by Noctalia).
  class Inhibit {
  public:
    explicit Inhibit(GDBusConnection* connection);
    ~Inhibit();

    Inhibit(const Inhibit&) = delete;
    Inhibit& operator=(const Inhibit&) = delete;
    Inhibit(Inhibit&&) = delete;
    Inhibit& operator=(Inhibit&&) = delete;

    enum Flags : guint32 { Logout = 1, UserSwitch = 2, Suspend = 4, Idle = 8 };

  private:
    struct Inhibition;
    struct Monitor;

    void handle(const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation);
    void inhibit(GVariant* parameters, GDBusMethodInvocation* invocation);
    void createMonitor(GVariant* parameters, GDBusMethodInvocation* invocation);
    void release(const std::string& handle);
    void closeMonitor(const std::string& handle);

    GDBusConnection* m_connection = nullptr;
    GDBusConnection* m_system = nullptr;
    std::map<std::string, std::unique_ptr<Inhibition>> m_inhibitions;
    std::map<std::string, std::unique_ptr<Monitor>> m_monitors;
    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace inhibit {

    // The logind "what" list for portal flags, empty when no lock is needed.
    [[nodiscard]] std::string logindWhat(guint32 flags);

  } // namespace inhibit

} // namespace xdpn
