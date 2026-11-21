#include "account/account.h"
#include "dialog/request.h"
#include "launcher/launcher.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <print>
#include <string_view>

int main() {
  int failures = 0;
  auto check = [&failures](bool condition, const char* what) {
    if (!condition) {
      std::println(stderr, "FAIL: {}", what);
      ++failures;
    }
  };

  using xdpn::account::gecosName;
  check(gecosName("Markus Volk,,,") == "Markus Volk", "gecos with commas");
  check(gecosName("Plain") == "Plain" && gecosName("").empty(), "gecos without commas");

  using xdpn::launcher::iconExtension;
  auto bytes = [](std::string_view text) {
    return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  };
  check(std::string_view(iconExtension(bytes("<?xml version=\"1.0\"?><svg/>"))) == ".svg", "svg markup");
  check(std::string_view(iconExtension(bytes("\n  <svg xmlns=\"x\"/>"))) == ".svg", "svg after whitespace");
  check(std::string_view(iconExtension(bytes("\x89PNG\r\n"))) == ".png", "png");

  using namespace xdpn::dialog;
  Request launcher;
  launcher.kind = Kind::Launcher;
  launcher.text = "Example";
  launcher.editable = true;
  launcher.detail = "https://example.org";
  launcher.image = "/tmp/preview.png";
  const auto parsed = parseRequest(formatRequest(launcher));
  check(parsed.kind == Kind::Launcher && parsed.text == "Example" && parsed.editable && parsed.detail == launcher.detail &&
            parsed.image == launcher.image,
        "launcher request round trip");
  for (const Kind kind : {Kind::Account, Kind::Wallpaper}) {
    Request request;
    request.kind = kind;
    check(parseRequest(formatRequest(request)).kind == kind, "account and wallpaper kinds");
  }
  check(parseAnswer(formatAnswer({.response = 0, .choices = {}, .application = {}, .text = "Renamed"})).text == "Renamed",
        "answer text");
  return failures == 0 ? 0 : 1;
}
