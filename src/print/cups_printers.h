#pragma once

#include "print/print_settings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xdpn::print {

  struct Printer {
    std::string name;
    std::string label;
    bool isDefault = false;
    std::vector<Media> media;
    std::string defaultMedia;
    bool duplex = false;
    bool color = false;
  };

  // The printers CUPS knows, queues and discovered IPP printers; blocks for a few seconds at most.
  [[nodiscard]] std::vector<Printer> enumeratePrinters();

  // Sends a PDF to a printer with the job's options. Empty on success, else the error.
  [[nodiscard]] std::string submit(const std::string& printer, const std::string& title,
                                   const std::filesystem::path& pdf, const dialog::PrintJob& job);

} // namespace xdpn::print
