#pragma once

#include "dbus/dialog_calls.h"
#include "dbus/interface.h"

#include <gio/gio.h>

#include <memory>
#include <string>

namespace xdpn {

  // org.freedesktop.impl.portal.Account: asks before an application gets the user's name and
  // picture, from AccountsService or the passwd entry and ~/.face.
  class Account {
  public:
    explicit Account(GDBusConnection* connection);
    ~Account();

    Account(const Account&) = delete;
    Account& operator=(const Account&) = delete;
    Account(Account&&) = delete;
    Account& operator=(Account&&) = delete;

    struct User {
      std::string id;
      std::string name;
      std::string image;
    };

  private:
    void getUserInformation(GVariant* parameters, GDBusMethodInvocation* invocation);

    DialogCalls m_calls;
    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace account {

    // The full name from a GECOS field: the part before the first comma.
    [[nodiscard]] std::string gecosName(const std::string& gecos);

  } // namespace account

} // namespace xdpn
