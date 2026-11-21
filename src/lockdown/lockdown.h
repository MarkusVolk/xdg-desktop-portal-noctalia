#pragma once

#include "dbus/interface.h"

#include <gio/gio.h>

#include <memory>
#include <string>
#include <vector>

namespace xdpn {

  // org.freedesktop.impl.portal.Lockdown: the properties follow the GNOME lockdown, location and
  // privacy GSettings, in both directions.
  class Lockdown {
  public:
    explicit Lockdown(GDBusConnection* connection);
    ~Lockdown();

    Lockdown(const Lockdown&) = delete;
    Lockdown& operator=(const Lockdown&) = delete;
    Lockdown(Lockdown&&) = delete;
    Lockdown& operator=(Lockdown&&) = delete;

  private:
    struct Binding {
      std::string property;
      GSettings* settings = nullptr;
      std::string key;
      bool inverted = false;
      gulong handler = 0;
    };

    [[nodiscard]] const Binding* find(const std::string& property) const;

    std::vector<std::unique_ptr<Binding>> m_bindings;
    std::vector<GSettings*> m_settings;
    std::unique_ptr<dbus::Interface> m_interface;
  };

} // namespace xdpn
