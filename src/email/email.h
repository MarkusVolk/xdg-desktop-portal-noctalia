#pragma once

#include "dbus/interface.h"

#include <gio/gio.h>

#include <memory>
#include <string>
#include <vector>

namespace xdpn {

  // org.freedesktop.impl.portal.Email: hands a mailto: URI to the default mail client.
  class Email {
  public:
    explicit Email(GDBusConnection* connection);
    ~Email();

    Email(const Email&) = delete;
    Email& operator=(const Email&) = delete;
    Email(Email&&) = delete;
    Email& operator=(Email&&) = delete;

  private:
    void composeEmail(GVariant* parameters, GDBusMethodInvocation* invocation);

    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace email {

    struct Message {
      std::vector<std::string> to;
      std::vector<std::string> cc;
      std::vector<std::string> bcc;
      std::string subject;
      std::string body;
      std::vector<std::string> attachments;
    };

    // RFC 6068 mailto: URI; attachments as the attach= extension most clients understand.
    [[nodiscard]] std::string mailtoUri(const Message& message);

  } // namespace email

} // namespace xdpn
