#include "dialog/request.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>

namespace xdpn::dialog {

  namespace {

    using json = nlohmann::json;

    std::string text(const json& object, const char* key) {
      const auto it = object.find(key);
      return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    json applicationsJson(const std::vector<Application>& applications) {
      json list = json::array();
      for (const auto& app : applications) {
        list.push_back({{"id", app.id}, {"name", app.name}, {"icon", app.icon}});
      }
      return list;
    }

    std::vector<Application> applicationsFrom(const json& list) {
      std::vector<Application> applications;
      if (!list.is_array()) {
        return applications;
      }
      for (const auto& item : list) {
        if (!item.is_object()) {
          continue;
        }
        Application app{.id = text(item, "id"), .name = text(item, "name"), .icon = text(item, "icon")};
        if (!app.id.empty()) {
          if (app.name.empty()) {
            app.name = app.id;
          }
          applications.push_back(std::move(app));
        }
      }
      return applications;
    }

    json jobJson(const PrintJob& job) {
      return {{"printer", job.printer},   {"copies", job.copies},        {"collate", job.collate},
              {"allPages", job.allPages}, {"ranges", job.ranges},        {"media", job.media},
              {"landscape", job.landscape}, {"sides", job.sides},        {"color", job.color},
              {"outputPath", job.outputPath}};
    }

    bool flag(const json& object, const char* key, bool fallback) {
      const auto it = object.find(key);
      return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
    }

    PrintJob jobFrom(const json& object) {
      PrintJob job;
      if (!object.is_object()) {
        return job;
      }
      job.printer = text(object, "printer");
      if (const auto it = object.find("copies"); it != object.end() && it->is_number_integer()) {
        job.copies = std::clamp(it->get<int>(), 1, 999);
      }
      job.collate = flag(object, "collate", true);
      job.allPages = flag(object, "allPages", true);
      job.ranges = text(object, "ranges");
      job.media = text(object, "media");
      job.landscape = flag(object, "landscape", false);
      if (auto sides = text(object, "sides"); !sides.empty()) {
        job.sides = sides;
      }
      job.color = flag(object, "color", true);
      job.outputPath = text(object, "outputPath");
      return job;
    }

    std::vector<std::string_view> splitList(std::string_view text, char separator) {
      std::vector<std::string_view> parts;
      while (true) {
        const auto at = text.find(separator);
        parts.push_back(text.substr(0, at));
        if (at == std::string_view::npos) {
          return parts;
        }
        text.remove_prefix(at + 1);
      }
    }

    std::optional<int> pageNumber(std::string_view text) {
      const auto start = text.find_first_not_of(" \t");
      if (start == std::string_view::npos) {
        return std::nullopt;
      }
      text = text.substr(start, text.find_last_not_of(" \t") - start + 1);
      int value = 0;
      const auto [rest, error] = std::from_chars(text.data(), text.data() + text.size(), value);
      if (error != std::errc() || rest != text.data() + text.size()) {
        return std::nullopt;
      }
      return value;
    }

  } // namespace

  bool validPageRanges(const std::string& ranges) {
    if (ranges.find_first_not_of(" \t") == std::string::npos) {
      return false;
    }
    for (const auto part : splitList(ranges, ',')) {
      const auto bounds = splitList(part, '-');
      if (bounds.size() > 2) {
        return false;
      }
      const auto first = pageNumber(bounds[0]);
      if (!first || *first < 1) {
        return false;
      }
      if (bounds.size() == 2) {
        const auto last = pageNumber(bounds[1]);
        if (!last || *last < *first) {
          return false;
        }
      }
    }
    return true;
  }

  std::string formatRequest(const Request& request) {
    json object;
    switch (request.kind) {
    case Kind::Access:
      object["kind"] = "access";
      break;
    case Kind::AppChooser:
      object["kind"] = "appchooser";
      break;
    case Kind::Account:
      object["kind"] = "account";
      break;
    case Kind::Wallpaper:
      object["kind"] = "wallpaper";
      break;
    case Kind::Launcher:
      object["kind"] = "launcher";
      break;
    case Kind::Print:
      object["kind"] = "print";
      break;
    case Kind::Invalid:
      object["kind"] = "invalid";
      break;
    }
    object["title"] = request.title;
    object["subtitle"] = request.subtitle;
    object["body"] = request.body;
    object["icon"] = request.icon;
    object["denyLabel"] = request.denyLabel;
    object["grantLabel"] = request.grantLabel;
    json choices = json::array();
    for (const auto& choice : request.choices) {
      json options = json::array();
      for (const auto& option : choice.options) {
        options.push_back({{"id", option.id}, {"label", option.label}});
      }
      choices.push_back({{"id", choice.id}, {"label", choice.label}, {"options", options}, {"initial", choice.initial}});
    }
    object["choices"] = choices;
    object["applications"] = applicationsJson(request.applications);
    object["lastChoice"] = request.lastChoice;
    object["image"] = request.image;
    object["detail"] = request.detail;
    object["text"] = request.text;
    object["editable"] = request.editable;
    json printers = json::array();
    for (const auto& printer : request.printers) {
      json media = json::array();
      for (const auto& entry : printer.media) {
        media.push_back({{"id", entry.id}, {"label", entry.label}});
      }
      printers.push_back({{"id", printer.id},
                          {"label", printer.label},
                          {"media", media},
                          {"duplex", printer.duplex},
                          {"color", printer.color},
                          {"toFile", printer.toFile}});
    }
    object["printers"] = printers;
    object["printJob"] = jobJson(request.printJob);
    return object.dump();
  }

  Request parseRequest(const std::string& line) {
    Request request;
    const json object = json::parse(line, nullptr, false);
    if (!object.is_object()) {
      return request;
    }
    const auto kind = text(object, "kind");
    if (kind == "access") {
      request.kind = Kind::Access;
    } else if (kind == "appchooser") {
      request.kind = Kind::AppChooser;
    } else if (kind == "account") {
      request.kind = Kind::Account;
    } else if (kind == "wallpaper") {
      request.kind = Kind::Wallpaper;
    } else if (kind == "launcher") {
      request.kind = Kind::Launcher;
    } else if (kind == "print") {
      request.kind = Kind::Print;
    } else {
      return request;
    }
    request.title = text(object, "title");
    request.subtitle = text(object, "subtitle");
    request.body = text(object, "body");
    request.icon = text(object, "icon");
    request.denyLabel = text(object, "denyLabel");
    request.grantLabel = text(object, "grantLabel");
    request.lastChoice = text(object, "lastChoice");
    request.image = text(object, "image");
    request.detail = text(object, "detail");
    request.text = text(object, "text");
    request.editable = flag(object, "editable", false);
    if (const auto it = object.find("printers"); it != object.end() && it->is_array()) {
      for (const auto& item : *it) {
        if (!item.is_object() || text(item, "id").empty()) {
          continue;
        }
        PrintPrinter printer{.id = text(item, "id"),
                             .label = text(item, "label"),
                             .media = {},
                             .duplex = flag(item, "duplex", false),
                             .color = flag(item, "color", false),
                             .toFile = flag(item, "toFile", false)};
        if (const auto media = item.find("media"); media != item.end() && media->is_array()) {
          for (const auto& entry : *media) {
            if (entry.is_object() && !text(entry, "id").empty()) {
              printer.media.push_back({.id = text(entry, "id"), .label = text(entry, "label")});
            }
          }
        }
        request.printers.push_back(std::move(printer));
      }
    }
    if (const auto it = object.find("printJob"); it != object.end()) {
      request.printJob = jobFrom(*it);
    }
    if (const auto it = object.find("choices"); it != object.end() && it->is_array()) {
      for (const auto& item : *it) {
        if (!item.is_object()) {
          continue;
        }
        Choice choice{.id = text(item, "id"), .label = text(item, "label"), .options = {}, .initial = text(item, "initial")};
        if (const auto options = item.find("options"); options != item.end() && options->is_array()) {
          for (const auto& option : *options) {
            if (option.is_object()) {
              choice.options.push_back({.id = text(option, "id"), .label = text(option, "label")});
            }
          }
        }
        if (!choice.id.empty()) {
          request.choices.push_back(std::move(choice));
        }
      }
    }
    if (const auto it = object.find("applications"); it != object.end()) {
      request.applications = applicationsFrom(*it);
    }
    return request;
  }

  std::string formatApplications(const std::vector<Application>& applications) {
    return json{{"applications", applicationsJson(applications)}}.dump();
  }

  std::optional<std::vector<Application>> parseApplications(const std::string& line) {
    const json object = json::parse(line, nullptr, false);
    if (!object.is_object() || !object.contains("applications")) {
      return std::nullopt;
    }
    return applicationsFrom(object["applications"]);
  }

  std::string formatAnswer(const Answer& answer) {
    json object{{"response", answer.response}};
    if (!answer.choices.empty()) {
      object["choices"] = answer.choices;
    }
    if (!answer.application.empty()) {
      object["application"] = answer.application;
    }
    if (!answer.text.empty()) {
      object["text"] = answer.text;
    }
    if (!answer.printJob.printer.empty()) {
      object["printJob"] = jobJson(answer.printJob);
    }
    return object.dump();
  }

  Answer parseAnswer(const std::string& line) {
    Answer answer{.response = 2, .choices = {}, .application = {}, .text = {}, .printJob = {}};
    const json object = json::parse(line, nullptr, false);
    if (!object.is_object()) {
      return answer;
    }
    if (const auto it = object.find("response"); it != object.end() && it->is_number_unsigned()) {
      answer.response = std::min(it->get<unsigned>(), 2U);
    }
    if (const auto it = object.find("choices"); it != object.end() && it->is_object()) {
      for (const auto& [key, value] : it->items()) {
        if (value.is_string()) {
          answer.choices[key] = value.get<std::string>();
        }
      }
    }
    answer.application = text(object, "application");
    answer.text = text(object, "text");
    if (const auto it = object.find("printJob"); it != object.end()) {
      answer.printJob = jobFrom(*it);
    }
    return answer;
  }

} // namespace xdpn::dialog
