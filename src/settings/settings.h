#pragma once

#include "dbus/interface.h"

#include <gio/gio.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xdpn {

  // org.freedesktop.impl.portal.Settings: the desktop GSettings schemas, plus the
  // org.freedesktop.appearance keys derived from them.
  class Settings {
  public:
    explicit Settings(GDBusConnection* connection);
    ~Settings();

    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;
    Settings(Settings&&) = delete;
    Settings& operator=(Settings&&) = delete;

  private:
    struct Schema {
      std::string name;
      GSettingsSchema* schema = nullptr;
      GSettings* settings = nullptr;
      gulong changedHandler = 0;
    };

    void handle(const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation);
    void onChanged(const Schema& schema, const char* key);
    [[nodiscard]] const Schema* find(std::string_view name) const;
    [[nodiscard]] GVariant* appearance(std::string_view key) const;

    std::vector<std::unique_ptr<Schema>> m_schemas;
    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace settings {

    // An empty pattern list or an empty pattern matches all; a trailing '*' matches a prefix.
    [[nodiscard]] bool namespaceMatches(std::string_view name, std::span<const std::string> patterns);

    // org.gnome.desktop.interface accent-color names to sRGB, the libadwaita palette.
    [[nodiscard]] std::optional<std::array<double, 3>> accentRgb(std::string_view name);

  } // namespace settings

} // namespace xdpn
