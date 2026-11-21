#pragma once

#include "dbus/dialog_calls.h"
#include "dbus/interface.h"

#include <gio/gio.h>

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
    void accessDialog(GVariant* parameters, GDBusMethodInvocation* invocation);

    DialogCalls m_calls;
    std::unique_ptr<dbus::Interface> m_interface;
  };

} // namespace xdpn
