#pragma once

#include <gio/gio.h>

#include <string>

namespace xdpn {

  // A string option of an a{sv}, empty when missing.
  [[nodiscard]] std::string lookupString(GVariant* options, const char* key);
  [[nodiscard]] bool lookupBool(GVariant* options, const char* key, bool fallback);
  // The display name of an application ID's desktop file; the ID itself, or `fallback` for none.
  [[nodiscard]] std::string applicationName(const std::string& appId, const std::string& fallback);

} // namespace xdpn
