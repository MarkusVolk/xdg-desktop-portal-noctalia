#pragma once

#include "dbus/dialog_calls.h"
#include "dbus/interface.h"

#include <gio/gio.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace xdpn {

  // org.freedesktop.impl.portal.DynamicLauncher: confirms, and lets the user rename, a launcher an
  // application wants to install; xdg-desktop-portal does the installing.
  class DynamicLauncher {
  public:
    explicit DynamicLauncher(GDBusConnection* connection);
    ~DynamicLauncher();

    DynamicLauncher(const DynamicLauncher&) = delete;
    DynamicLauncher& operator=(const DynamicLauncher&) = delete;
    DynamicLauncher(DynamicLauncher&&) = delete;
    DynamicLauncher& operator=(DynamicLauncher&&) = delete;

    enum LauncherType : guint32 { Application = 1, WebApplication = 2 };

  private:
    void prepareInstall(GVariant* parameters, GDBusMethodInvocation* invocation);

    DialogCalls m_calls;
    unsigned m_iconCounter = 0;
    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace launcher {

    // The file extension that lets the image loader recognise icon bytes: ".svg" for SVG markup.
    [[nodiscard]] const char* iconExtension(std::span<const std::uint8_t> data);

  } // namespace launcher

} // namespace xdpn
