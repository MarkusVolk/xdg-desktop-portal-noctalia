#include "icons/icon_lookup.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <unistd.h>
#include <vector>

int main() {
  namespace fs = std::filesystem;
  int failures = 0;
  auto check = [&failures](bool condition, const char* what) {
    if (!condition) {
      std::println(stderr, "FAIL: {}", what);
      ++failures;
    }
  };
  const fs::path root = fs::temp_directory_path() / ("xdpn-icons-" + std::to_string(::getpid()));
  auto touch = [](const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "x";
  };
  touch(root / "a/icons/hicolor/scalable/apps/editor.svg");
  touch(root / "a/icons/hicolor/48x48/apps/editor.png");
  touch(root / "a/icons/Breeze/apps/48/viewer.svg");
  touch(root / "b/icons/hicolor/256x256/apps/viewer.png");
  touch(root / "b/pixmaps/legacy.png");
  const std::vector<fs::path> dirs{root / "a", root / "b"};

  using xdpn::icons::lookupName;
  check(lookupName("editor", "", dirs).ends_with("48x48/apps/editor.png"), "48 px before scalable");
  check(lookupName("viewer", "Breeze", dirs).ends_with("Breeze/apps/48/viewer.svg"), "theme layout context/size first");
  check(lookupName("viewer", "", dirs).ends_with("256x256/apps/viewer.png"), "hicolor in a later data dir");
  check(lookupName("legacy", "", dirs).ends_with("pixmaps/legacy.png"), "pixmaps fallback");
  check(lookupName("missing", "", dirs).empty(), "missing icon");
  check(lookupName((root / "b/pixmaps/legacy.png").string(), "", dirs) == (root / "b/pixmaps/legacy.png").string(),
        "absolute path");
  fs::remove_all(root);
  return failures == 0 ? 0 : 1;
}
