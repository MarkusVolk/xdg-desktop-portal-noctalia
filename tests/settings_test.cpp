#include "settings/settings.h"

#include <cstdio>
#include <print>
#include <string>
#include <vector>

namespace {

  int failures = 0;

  void check(bool condition, const char* what) {
    if (!condition) {
      std::println(stderr, "FAIL: {}", what);
      ++failures;
    }
  }

} // namespace

int main() {
  using xdpn::settings::accentRgb;
  using xdpn::settings::namespaceMatches;

  const std::vector<std::string> none;
  check(namespaceMatches("org.gnome.desktop.interface", none), "empty list matches all");
  const std::vector<std::string> empty{""};
  check(namespaceMatches("org.freedesktop.appearance", empty), "empty pattern matches all");
  const std::vector<std::string> exact{"org.freedesktop.appearance"};
  check(namespaceMatches("org.freedesktop.appearance", exact), "exact match");
  check(!namespaceMatches("org.freedesktop.appearance.extra", exact), "no prefix without star");
  const std::vector<std::string> prefix{"org.gnome.*"};
  check(namespaceMatches("org.gnome.desktop.interface", prefix), "prefix match");
  check(!namespaceMatches("org.freedesktop.appearance", prefix), "prefix mismatch");

  const auto blue = accentRgb("blue");
  check(blue.has_value() && (*blue)[2] > 0.89 && (*blue)[2] < 0.9, "blue accent");
  check(!accentRgb("magenta").has_value(), "unknown accent");

  return failures == 0 ? 0 : 1;
}
