#include "dialog/request.h"

#include <nlohmann/json.hpp>

#include <algorithm>

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

  } // namespace

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
    if (const auto it = object.find("editable"); it != object.end() && it->is_boolean()) {
      request.editable = it->get<bool>();
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
    return object.dump();
  }

  Answer parseAnswer(const std::string& line) {
    Answer answer{.response = 2, .choices = {}, .application = {}, .text = {}};
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
    return answer;
  }

} // namespace xdpn::dialog
