#include "email/email.h"

#include <cstdio>
#include <print>

int main() {
  using xdpn::email::mailtoUri;
  int failures = 0;
  auto check = [&failures](const std::string& got, const std::string& want) {
    if (got != want) {
      std::println(stderr, "FAIL: got {} want {}", got, want);
      ++failures;
    }
  };
  check(mailtoUri({}), "mailto:");
  check(mailtoUri({.to = {"a@example.org", "b@example.org"}, .cc = {}, .bcc = {}, .subject = {}, .body = {}, .attachments = {}}),
        "mailto:a%40example.org,b%40example.org");
  check(mailtoUri({.to = {"a@example.org"}, .cc = {"c@example.org"}, .bcc = {"d@example.org"}, .subject = "Hi & bye",
                   .body = "Line 1\nLine 2", .attachments = {"/tmp/a b.txt"}}),
        "mailto:a%40example.org?cc=c%40example.org&bcc=d%40example.org&subject=Hi%20%26%20bye&body=Line%201%0ALine%202"
        "&attach=%2Ftmp%2Fa%20b.txt");
  return failures == 0 ? 0 : 1;
}
