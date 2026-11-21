#pragma once

#include <gio/gio.h>

#include <filesystem>
#include <string>
#include <vector>

// Finds the file of a themed icon, so that a dialog can load it with Noctalia's codec pack.
namespace xdpn::icons {

  // $XDG_DATA_HOME followed by $XDG_DATA_DIRS.
  [[nodiscard]] std::vector<std::filesystem::path> dataDirs();

  // The first match of `name` in `theme`, then hicolor and Adwaita, below <dir>/icons for each
  // data dir, preferring 48 px, then larger and scalable sizes; then <dir>/pixmaps. An absolute
  // existing path is returned as it is. Empty when nothing is found.
  [[nodiscard]] std::string lookupName(const std::string& name, const std::string& theme,
                                       const std::vector<std::filesystem::path>& dirs);

  // A GIcon (themed or file) with the icon theme from org.gnome.desktop.interface.
  [[nodiscard]] std::string lookup(GIcon* icon);
  [[nodiscard]] std::string lookupName(const std::string& name);

} // namespace xdpn::icons
