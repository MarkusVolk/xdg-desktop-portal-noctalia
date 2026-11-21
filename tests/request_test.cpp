#include "dialog/request.h"

#include <cstdio>
#include <print>

int main() {
  using namespace xdpn::dialog;

  int failures = 0;
  auto check = [&failures](bool condition, const char* what) {
    if (!condition) {
      std::println(stderr, "FAIL: {}", what);
      ++failures;
    }
  };

  Request request;
  request.kind = Kind::Access;
  request.title = "Allow \"foot\" to take screenshots?";
  request.grantLabel = "Allow";
  request.choices.push_back({.id = "remember", .label = "Remember", .options = {}, .initial = "true"});
  request.choices.push_back(
      {.id = "quality", .label = "Quality", .options = {{"low", "Low"}, {"high", "High"}}, .initial = ""});
  const auto parsed = parseRequest(formatRequest(request));
  check(parsed.kind == Kind::Access, "kind round trip");
  check(parsed.title == request.title, "title with quotes");
  check(parsed.choices.size() == 2 && parsed.choices[1].options.size() == 2, "choices round trip");
  check(parsed.choices[0].initial == "true", "initial value");

  check(parseRequest("not json").kind == Kind::Invalid, "garbage is invalid");
  check(parseRequest(R"({"kind":"other"})").kind == Kind::Invalid, "unknown kind is invalid");

  Request chooser;
  chooser.kind = Kind::AppChooser;
  chooser.applications = {{"org.gnome.Loupe", "Image Viewer", "/usr/share/icons/loupe.svg"}, {"", "skipped", ""}, {"x", "", ""}};
  const auto apps = parseRequest(formatRequest(chooser)).applications;
  check(apps.size() == 2 && apps[1].name == "x", "applications: empty id skipped, empty name falls back to id");
  check(apps[0].icon == "/usr/share/icons/loupe.svg", "icon path round trip");

  const auto update = parseApplications(formatApplications({{"a", "A", ""}}));
  check(update && update->size() == 1, "application update");
  check(!parseApplications(R"({"kind":"access"})"), "no update without applications");

  const auto answer = parseAnswer(formatAnswer({.response = 0, .choices = {{"remember", "false"}}, .application = {}}));
  check(answer.response == 0 && answer.choices.at("remember") == "false", "answer round trip");
  check(parseAnswer("").response == 2, "empty answer is response 2");
  check(parseAnswer(R"({"response":7})").response == 2, "response clamped");
  return failures == 0 ? 0 : 1;
}
