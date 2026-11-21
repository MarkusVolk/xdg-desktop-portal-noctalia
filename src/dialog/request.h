#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

// The dialog protocol between the portal daemon and noctalia-portal-dialog. The daemon writes one
// JSON object per line to the dialog's stdin: first the request, then for an app chooser any
// updated application lists. The dialog writes exactly one JSON line to stdout: the answer.
namespace xdpn::dialog {

  struct ChoiceOption {
    std::string id;
    std::string label;
  };

  // An Access dialog choice: a checkbox without options ("true"/"false"), a radio group with them.
  struct Choice {
    std::string id;
    std::string label;
    std::vector<ChoiceOption> options;
    std::string initial;
  };

  struct Application {
    std::string id;
    std::string name;
  };

  enum class Kind { Invalid, Access, AppChooser };

  struct Request {
    Kind kind = Kind::Invalid;
    std::string title;
    std::string subtitle;
    std::string body;
    std::string icon;
    std::string denyLabel;
    std::string grantLabel;
    std::vector<Choice> choices;
    std::vector<Application> applications;
    std::string lastChoice;
  };

  struct Answer {
    // A portal response code: 0 success, 1 cancelled, 2 other.
    unsigned response = 1;
    std::map<std::string, std::string> choices;
    std::string application;
  };

  std::string formatRequest(const Request& request);
  // A line the dialog cannot read yields a request of Kind::Invalid.
  Request parseRequest(const std::string& line);

  std::string formatApplications(const std::vector<Application>& applications);
  std::optional<std::vector<Application>> parseApplications(const std::string& line);

  std::string formatAnswer(const Answer& answer);
  // An unreadable answer is a cancellation with response 2.
  Answer parseAnswer(const std::string& line);

} // namespace xdpn::dialog
