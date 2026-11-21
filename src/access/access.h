#pragma once

#include "dbus/dialog_process.h"
#include "dbus/interface.h"

#include <gio/gio.h>

#include <map>
#include <memory>
#include <string>

namespace xdpn {

  // org.freedesktop.impl.portal.Access: the permission question with optional choices, asked in
  // a noctalia-portal-dialog.
  class Access {
  public:
    explicit Access(GDBusConnection* connection);
    ~Access();

    Access(const Access&) = delete;
    Access& operator=(const Access&) = delete;
    Access(Access&&) = delete;
    Access& operator=(Access&&) = delete;

  private:
    struct Call {
      GDBusMethodInvocation* invocation = nullptr;
      std::unique_ptr<dbus::Request> request;
      std::unique_ptr<DialogProcess> dialog;
    };

    void accessDialog(GVariant* parameters, GDBusMethodInvocation* invocation);
    void complete(const std::string& handle, const dialog::Answer& answer);

    GDBusConnection* m_connection = nullptr;
    std::map<std::string, std::unique_ptr<Call>> m_calls;
    std::unique_ptr<dbus::Interface> m_interface;
  };

} // namespace xdpn
