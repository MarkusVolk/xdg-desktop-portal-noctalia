#include "print/print_settings.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <string_view>

namespace xdpn::print {

  namespace {

    std::string settingString(GVariant* dict, const char* key) {
      if (dict == nullptr) {
        return {};
      }
      const char* value = nullptr;
      return g_variant_lookup(dict, key, "&s", &value) ? std::string(value) : std::string();
    }

    std::optional<int> parseInt(std::string_view text) {
      int value = 0;
      const auto [rest, error] = std::from_chars(text.data(), text.data() + text.size(), value);
      if (error != std::errc() || rest != text.data() + text.size()) {
        return std::nullopt;
      }
      return value;
    }

    std::vector<std::string_view> split(std::string_view text, char separator) {
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

    std::string trim(std::string_view text) {
      const auto start = text.find_first_not_of(" \t");
      if (start == std::string_view::npos) {
        return {};
      }
      const auto end = text.find_last_not_of(" \t");
      return std::string(text.substr(start, end - start + 1));
    }

    // Shifts every number of a range list by `delta`; empty when a number is out of range.
    std::string shiftRanges(const std::string& ranges, int delta, int minimum) {
      std::string out;
      for (const auto part : split(ranges, ',')) {
        const auto bounds = split(part, '-');
        if (bounds.size() > 2) {
          return {};
        }
        std::string piece;
        for (std::size_t i = 0; i < bounds.size(); ++i) {
          const auto number = parseInt(trim(bounds[i]));
          if (!number || *number + delta < minimum) {
            return {};
          }
          piece += (i == 0 ? "" : "-") + std::to_string(*number + delta);
        }
        out += (out.empty() ? "" : ",") + piece;
      }
      return out;
    }

  } // namespace

  std::vector<Media> standardMedia() {
    return {
        {"iso_a4_210x297mm", "A4", 210, 297, 6.35, 6.35, 6.35, 6.35},
        {"na_letter_8.5x11in", "US Letter", 215.9, 279.4, 6.35, 6.35, 6.35, 6.35},
        {"iso_a5_148x210mm", "A5", 148, 210, 6.35, 6.35, 6.35, 6.35},
        {"iso_a3_297x420mm", "A3", 297, 420, 6.35, 6.35, 6.35, 6.35},
        {"na_legal_8.5x14in", "US Legal", 215.9, 355.6, 6.35, 6.35, 6.35, 6.35},
    };
  }

  std::optional<std::pair<double, double>> pwgSizeMm(const std::string& name) {
    const auto underscore = name.rfind('_');
    if (underscore == std::string::npos) {
      return std::nullopt;
    }
    std::string_view size = std::string_view(name).substr(underscore + 1);
    double factor = 0;
    if (size.ends_with("mm")) {
      factor = 1.0;
    } else if (size.ends_with("in")) {
      factor = 25.4;
    } else {
      return std::nullopt;
    }
    size.remove_suffix(2);
    const auto x = size.find('x');
    if (x == std::string_view::npos) {
      return std::nullopt;
    }
    double width = 0;
    double height = 0;
    const auto w = size.substr(0, x);
    const auto h = size.substr(x + 1);
    if (std::from_chars(w.data(), w.data() + w.size(), width).ec != std::errc() ||
        std::from_chars(h.data(), h.data() + h.size(), height).ec != std::errc() || width <= 0 || height <= 0) {
      return std::nullopt;
    }
    return std::pair{width * factor, height * factor};
  }

  bool validRanges(const std::string& ranges) { return dialog::validPageRanges(ranges); }

  std::string rangesFromGtk(const std::string& ranges) { return shiftRanges(ranges, 1, 1); }

  std::string rangesToGtk(const std::string& ranges) { return shiftRanges(ranges, -1, 0); }

  dialog::PrintJob jobFromSettings(GVariant* settings, GVariant* pageSetup) {
    dialog::PrintJob job;
    job.printer = settingString(settings, "printer");
    if (const auto copies = parseInt(settingString(settings, "n-copies")); copies && *copies > 0) {
      job.copies = std::min(*copies, 999);
    }
    if (const auto collate = settingString(settings, "collate"); !collate.empty()) {
      job.collate = collate == "true";
    }
    if (settingString(settings, "print-pages") == "ranges") {
      if (auto ranges = rangesFromGtk(settingString(settings, "page-ranges")); validRanges(ranges)) {
        job.allPages = false;
        job.ranges = ranges;
      }
    }
    std::string orientation = settingString(settings, "orientation");
    if (orientation.empty()) {
      orientation = settingString(pageSetup, "Orientation");
    }
    job.landscape = orientation == "landscape" || orientation == "reverse_landscape";
    const auto duplex = settingString(settings, "duplex");
    job.sides = duplex == "vertical" ? "two-sided-long-edge" : duplex == "horizontal" ? "two-sided-short-edge" : "one-sided";
    if (const auto color = settingString(settings, "use-color"); !color.empty()) {
      job.color = color == "true";
    }
    job.media = settingString(pageSetup, "Name");
    if (job.media.empty()) {
      job.media = settingString(settings, "paper-format");
    }
    if (auto uri = settingString(settings, "output-uri"); !uri.empty()) {
      if (gchar* path = g_filename_from_uri(uri.c_str(), nullptr, nullptr); path != nullptr) {
        job.outputPath = path;
        g_free(path);
      }
    }
    return job;
  }

  GVariant* settingsFromJob(const dialog::PrintJob& job, const Media& media) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    auto add = [&builder](const char* key, const std::string& value) {
      g_variant_builder_add(&builder, "{sv}", key, g_variant_new_string(value.c_str()));
    };
    add("printer", job.printer);
    add("n-copies", std::to_string(job.copies));
    add("collate", job.collate ? "true" : "false");
    add("print-pages", job.allPages ? "all" : "ranges");
    if (!job.allPages) {
      add("page-ranges", rangesToGtk(job.ranges));
    }
    add("orientation", job.landscape ? "landscape" : "portrait");
    add("duplex", job.sides == "two-sided-long-edge"    ? "vertical"
                  : job.sides == "two-sided-short-edge" ? "horizontal"
                                                        : "simplex");
    add("use-color", job.color ? "true" : "false");
    add("paper-format", media.name);
    add("paper-width", std::format("{:.2f}", media.widthMm));
    add("paper-height", std::format("{:.2f}", media.heightMm));
    if (!job.outputPath.empty()) {
      gchar* uri = g_filename_to_uri(job.outputPath.c_str(), nullptr, nullptr);
      if (uri != nullptr) {
        add("output-uri", uri);
        add("output-file-format", "pdf");
        g_free(uri);
      }
    }
    return g_variant_builder_end(&builder);
  }

  GVariant* pageSetupFromJob(const dialog::PrintJob& job, const Media& media) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&builder, "{sv}", "Name", g_variant_new_string(media.name.c_str()));
    g_variant_builder_add(&builder, "{sv}", "DisplayName", g_variant_new_string(media.label.c_str()));
    g_variant_builder_add(&builder, "{sv}", "Width", g_variant_new_double(media.widthMm));
    g_variant_builder_add(&builder, "{sv}", "Height", g_variant_new_double(media.heightMm));
    g_variant_builder_add(&builder, "{sv}", "MarginTop", g_variant_new_double(media.topMm));
    g_variant_builder_add(&builder, "{sv}", "MarginBottom", g_variant_new_double(media.bottomMm));
    g_variant_builder_add(&builder, "{sv}", "MarginLeft", g_variant_new_double(media.leftMm));
    g_variant_builder_add(&builder, "{sv}", "MarginRight", g_variant_new_double(media.rightMm));
    g_variant_builder_add(&builder, "{sv}", "Orientation", g_variant_new_string(job.landscape ? "landscape" : "portrait"));
    return g_variant_builder_end(&builder);
  }

  std::vector<std::pair<std::string, std::string>> cupsOptions(const dialog::PrintJob& job) {
    // Colour and duplex only when they differ from what every printer can, so that a
    // monochrome or one-sided printer does not reject the job.
    std::vector<std::pair<std::string, std::string>> options{
        {"copies", std::to_string(job.copies)},
        {"multiple-document-handling",
         job.collate ? "separate-documents-collated-copies" : "separate-documents-uncollated-copies"},
    };
    if (job.sides != "one-sided") {
      options.emplace_back("sides", job.sides);
    }
    if (!job.color) {
      options.emplace_back("print-color-mode", "monochrome");
    }
    if (!job.media.empty()) {
      options.emplace_back("media", job.media);
    }
    if (!job.allPages && validRanges(job.ranges)) {
      options.emplace_back("page-ranges", job.ranges);
    }
    return options;
  }

} // namespace xdpn::print
