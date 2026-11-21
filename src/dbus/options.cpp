#include "dbus/options.h"

#include <gio/gdesktopappinfo.h>

namespace xdpn {

  std::string lookupString(GVariant* options, const char* key) {
    const char* value = nullptr;
    return g_variant_lookup(options, key, "&s", &value) ? std::string(value) : std::string();
  }

  bool lookupBool(GVariant* options, const char* key, bool fallback) {
    gboolean value = FALSE;
    return g_variant_lookup(options, key, "b", &value) ? value != FALSE : fallback;
  }

  std::string applicationName(const std::string& appId, const std::string& fallback) {
    if (appId.empty()) {
      return fallback;
    }
    GDesktopAppInfo* info = g_desktop_app_info_new((appId + ".desktop").c_str());
    if (info == nullptr) {
      return appId;
    }
    std::string name = g_app_info_get_display_name(G_APP_INFO(info));
    g_object_unref(info);
    return name;
  }

} // namespace xdpn
