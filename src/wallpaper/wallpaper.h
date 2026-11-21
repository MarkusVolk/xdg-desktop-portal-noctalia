#pragma once

#include "dbus/dialog_calls.h"
#include "dbus/interface.h"

#include <gio/gio.h>

#include <filesystem>
#include <memory>
#include <string>

namespace xdpn {

  // org.freedesktop.impl.portal.Wallpaper: sets Noctalia's wallpaper, after a preview when asked.
  class Wallpaper {
  public:
    explicit Wallpaper(GDBusConnection* connection);
    ~Wallpaper();

    Wallpaper(const Wallpaper&) = delete;
    Wallpaper& operator=(const Wallpaper&) = delete;
    Wallpaper(Wallpaper&&) = delete;
    Wallpaper& operator=(Wallpaper&&) = delete;

  private:
    void setWallpaperUri(GVariant* parameters, GDBusMethodInvocation* invocation);

    DialogCalls m_calls;
    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace wallpaper {

    // $XDG_DATA_HOME/xdg-desktop-portal-noctalia/wallpapers
    [[nodiscard]] std::filesystem::path storeDirectory();
    // Copies the picture into the store, since portal paths only live as long as the document
    // portal entry, and makes it Noctalia's wallpaper. False when either step fails.
    bool apply(const std::filesystem::path& picture);

  } // namespace wallpaper

} // namespace xdpn
