#pragma once

#include "dialog/request.h"

#include <gio/gio.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

// Print settings in the formats around the portal: the GtkPrintSettings and GtkPageSetup
// dictionaries applications send and expect back, the job the dialog edits, and CUPS options.
namespace xdpn::print {

  struct Media {
    // PWG self-describing name, such as "iso_a4_210x297mm".
    std::string name;
    std::string label;
    double widthMm = 0;
    double heightMm = 0;
    double topMm = 0;
    double bottomMm = 0;
    double leftMm = 0;
    double rightMm = 0;
  };

  // The sizes offered when printing to a file.
  [[nodiscard]] std::vector<Media> standardMedia();
  // The size of a PWG self-describing name ("iso_a4_210x297mm", "na_letter_8.5x11in").
  [[nodiscard]] std::optional<std::pair<double, double>> pwgSizeMm(const std::string& name);

  // "1-3,5" with ascending, positive numbers.
  [[nodiscard]] bool validRanges(const std::string& ranges);
  // GtkPrintSettings stores page ranges 0-based ("0-2,4"); the dialog and CUPS count from 1.
  [[nodiscard]] std::string rangesFromGtk(const std::string& ranges);
  [[nodiscard]] std::string rangesToGtk(const std::string& ranges);

  // Presets from the application's settings and page setup; unknown keys are ignored.
  [[nodiscard]] dialog::PrintJob jobFromSettings(GVariant* settings, GVariant* pageSetup);
  // GtkPrintSettings and GtkPageSetup dictionaries (floating) for a job printed on `media`.
  [[nodiscard]] GVariant* settingsFromJob(const dialog::PrintJob& job, const Media& media);
  [[nodiscard]] GVariant* pageSetupFromJob(const dialog::PrintJob& job, const Media& media);

  // IPP job attributes for CUPS.
  [[nodiscard]] std::vector<std::pair<std::string, std::string>> cupsOptions(const dialog::PrintJob& job);

} // namespace xdpn::print
