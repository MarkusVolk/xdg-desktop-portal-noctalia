#include "inhibit/inhibit.h"

#include <cstdio>
#include <print>

int main() {
  using xdpn::Inhibit;
  using xdpn::inhibit::logindWhat;

  int failures = 0;
  auto check = [&failures](bool condition, const char* what) {
    if (!condition) {
      std::println(stderr, "FAIL: {}", what);
      ++failures;
    }
  };

  check(logindWhat(0).empty(), "no flags, no lock");
  check(logindWhat(Inhibit::Idle).empty(), "idle needs no logind lock");
  check(logindWhat(Inhibit::UserSwitch).empty(), "user switch needs no logind lock");
  check(logindWhat(Inhibit::Suspend) == "sleep", "suspend");
  check(logindWhat(Inhibit::Logout) == "shutdown", "logout");
  check(logindWhat(Inhibit::Logout | Inhibit::Suspend | Inhibit::Idle) == "shutdown:sleep", "combined");
  return failures == 0 ? 0 : 1;
}
