#include "notification/notification.h"

#include <cstdio>
#include <print>

int main() {
  using namespace xdpn::notification;
  int failures = 0;
  auto check = [&failures](bool condition, const char* what) {
    if (!condition) {
      std::println(stderr, "FAIL: {}", what);
      ++failures;
    }
  };
  check(urgency("low") == 0 && urgency("normal") == 1 && urgency("high") == 1 && urgency("urgent") == 2 &&
            urgency("") == 1,
        "urgency");
  check(applicationPath("org.gnome.Loupe") == "/org/gnome/Loupe", "app path");
  check(applicationPath("de.flk.my-app") == "/de/flk/my_app", "app path with dash");
  check(buttonIndex(buttonKey(3)) == 3, "button key round trip");
  check(!buttonIndex("default") && !buttonIndex("xdpn-button-") && !buttonIndex("xdpn-button-1x"), "bad keys");
  return failures == 0 ? 0 : 1;
}
