#pragma once

#include "dbus/dialog_calls.h"
#include "dbus/interface.h"

#include <gio/gio.h>

#include <memory>
#include <string>
#include <vector>

namespace xdpn {

  // org.freedesktop.impl.portal.AppChooser: picks the application that opens a file or URI from
  // the choices xdg-desktop-portal offers, in a noctalia-portal-dialog.
  class AppChooser {
  public:
    explicit AppChooser(GDBusConnection* connection);
    ~AppChooser();

    AppChooser(const AppChooser&) = delete;
    AppChooser& operator=(const AppChooser&) = delete;
    AppChooser(AppChooser&&) = delete;
    AppChooser& operator=(AppChooser&&) = delete;

  private:
    void chooseApplication(GVariant* parameters, GDBusMethodInvocation* invocation);
    void updateChoices(GVariant* parameters, GDBusMethodInvocation* invocation);

    DialogCalls m_calls;
    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace appchooser {

    // Desktop IDs as xdg-desktop-portal passes them, without ".desktop", to dialog entries;
    // IDs without an installed desktop file are left out.
    [[nodiscard]] std::vector<dialog::Application> applications(const std::vector<std::string>& ids);

  } // namespace appchooser

} // namespace xdpn
