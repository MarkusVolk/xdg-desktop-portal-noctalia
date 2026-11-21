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
    // Path of the icon file, empty for none.
    std::string icon;
  };

  struct PrintMedia {
    std::string id;
    std::string label;
  };

  struct PrintPrinter {
    std::string id;
    std::string label;
    std::vector<PrintMedia> media;
    bool duplex = false;
    bool color = false;
    // The virtual printer that writes a PDF file.
    bool toFile = false;
  };

  // The settings of a print job; page ranges are 1-based as the user writes them ("1-3,5").
  struct PrintJob {
    std::string printer;
    int copies = 1;
    bool collate = true;
    bool allPages = true;
    std::string ranges;
    std::string media;
    bool landscape = false;
    // "one-sided", "two-sided-long-edge" or "two-sided-short-edge".
    std::string sides = "one-sided";
    bool color = true;
    std::string outputPath;
  };

  enum class Kind { Invalid, Access, AppChooser, Account, Wallpaper, Launcher, Print };

  struct Request {
    Kind kind = Kind::Invalid;
    std::string title;
    std::string subtitle;
    std::string body;
    // Path of an icon file, empty for the dialog's own glyph.
    std::string icon;
    std::string denyLabel;
    std::string grantLabel;
    std::vector<Choice> choices;
    std::vector<Application> applications;
    std::string lastChoice;
    // A large picture: the wallpaper preview, the account avatar.
    std::string image;
    // A line of detail below the body, such as a launcher's web address.
    std::string detail;
    // An editable value (a launcher's name) when `editable`, else shown as text.
    std::string text;
    bool editable = false;
    std::vector<PrintPrinter> printers;
    PrintJob printJob;
  };

  struct Answer {
    // A portal response code: 0 success, 1 cancelled, 2 other.
    unsigned response = 1;
    std::map<std::string, std::string> choices;
    std::string application;
    std::string text;
    PrintJob printJob;
  };

  // Page ranges as the user writes them: "1-3,5", ascending, from page 1.
  [[nodiscard]] bool validPageRanges(const std::string& ranges);

  std::string formatRequest(const Request& request);
  // A line the dialog cannot read yields a request of Kind::Invalid.
  Request parseRequest(const std::string& line);

  std::string formatApplications(const std::vector<Application>& applications);
  std::optional<std::vector<Application>> parseApplications(const std::string& line);

  std::string formatAnswer(const Answer& answer);
  // An unreadable answer is a cancellation with response 2.
  Answer parseAnswer(const std::string& line);

} // namespace xdpn::dialog
