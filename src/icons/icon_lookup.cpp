#include "icons/icon_lookup.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>

namespace xdpn::icons {

  namespace {

    constexpr std::array<std::string_view, 8> kSizes = {"48x48", "64x64", "scalable", "96x96",
                                                        "128x128", "256x256", "32x32", "24x24"};
    constexpr std::array<std::string_view, 7> kContexts = {"apps", "status", "actions", "devices",
                                                           "places", "mimetypes", "categories"};
    constexpr std::array<std::string_view, 2> kExtensions = {".png", ".svg"};

    std::string firstExisting(const std::filesystem::path& base, const std::string& name) {
      std::error_code error;
      for (const auto extension : kExtensions) {
        auto candidate = base / (name + std::string(extension));
        if (std::filesystem::is_regular_file(candidate, error)) {
          return candidate.string();
        }
      }
      return {};
    }

    std::string themeIconTheme() {
      GSettingsSchemaSource* source = g_settings_schema_source_get_default();
      GSettingsSchema* schema =
          source != nullptr ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : nullptr;
      if (schema == nullptr) {
        return {};
      }
      std::string theme;
      if (g_settings_schema_has_key(schema, "icon-theme")) {
        GSettings* settings = g_settings_new_full(schema, nullptr, nullptr);
        gchar* value = g_settings_get_string(settings, "icon-theme");
        theme = value != nullptr ? value : "";
        g_free(value);
        g_object_unref(settings);
      }
      g_settings_schema_unref(schema);
      return theme;
    }

  } // namespace

  std::vector<std::filesystem::path> dataDirs() {
    std::vector<std::filesystem::path> dirs;
    if (const char* home = std::getenv("XDG_DATA_HOME"); home != nullptr && home[0] == '/') {
      dirs.emplace_back(home);
    } else if (const char* user = std::getenv("HOME"); user != nullptr) {
      dirs.emplace_back(std::filesystem::path(user) / ".local" / "share");
    }
    const char* system = std::getenv("XDG_DATA_DIRS");
    std::string_view list = system != nullptr && system[0] != '\0' ? system : "/usr/local/share:/usr/share";
    while (!list.empty()) {
      const auto colon = list.find(':');
      const auto entry = list.substr(0, colon);
      if (!entry.empty()) {
        dirs.emplace_back(std::string(entry));
      }
      if (colon == std::string_view::npos) {
        break;
      }
      list.remove_prefix(colon + 1);
    }
    return dirs;
  }

  std::string lookupName(const std::string& name, const std::string& theme, const std::vector<std::filesystem::path>& dirs) {
    if (name.empty()) {
      return {};
    }
    std::error_code error;
    if (name.front() == '/') {
      return std::filesystem::is_regular_file(name, error) ? name : std::string();
    }
    std::vector<std::string> themes;
    for (const auto& candidate : {theme, std::string("hicolor"), std::string("Adwaita")}) {
      if (!candidate.empty() && std::find(themes.begin(), themes.end(), candidate) == themes.end()) {
        themes.push_back(candidate);
      }
    }
    for (const auto& themeName : themes) {
      for (const auto& dir : dirs) {
        const auto root = dir / "icons" / themeName;
        if (!std::filesystem::is_directory(root, error)) {
          continue;
        }
        for (const auto size : kSizes) {
          for (const auto context : kContexts) {
            // Both layouts: <size>/<context> (hicolor) and <context>/<size> (Breeze).
            if (auto found = firstExisting(root / size / context, name); !found.empty()) {
              return found;
            }
            const std::string_view number = size.substr(0, size.find('x'));
            if (auto found = firstExisting(root / context / std::string(number), name); !found.empty()) {
              return found;
            }
          }
        }
      }
    }
    for (const auto& dir : dirs) {
      if (auto found = firstExisting(dir / "pixmaps", name); !found.empty()) {
        return found;
      }
    }
    return {};
  }

  std::string lookupName(const std::string& name) { return lookupName(name, themeIconTheme(), dataDirs()); }

  std::string lookup(GIcon* icon) {
    if (icon == nullptr) {
      return {};
    }
    if (G_IS_FILE_ICON(icon)) {
      gchar* path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(icon)));
      std::string result = path != nullptr ? path : "";
      g_free(path);
      return result;
    }
    if (G_IS_THEMED_ICON(icon)) {
      const auto theme = themeIconTheme();
      const auto dirs = dataDirs();
      for (const gchar* const* name = g_themed_icon_get_names(G_THEMED_ICON(icon)); name != nullptr && *name != nullptr;
           ++name) {
        if (auto found = lookupName(*name, theme, dirs); !found.empty()) {
          return found;
        }
      }
    }
    return {};
  }

} // namespace xdpn::icons
