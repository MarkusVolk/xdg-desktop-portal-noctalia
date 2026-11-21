#include "print/cups_printers.h"

#include <cups/cups.h>

#include <cstring>
#include <fstream>
#include <vector>

namespace xdpn::print {

  namespace {

    constexpr int kDiscoveryMs = 3000;

    struct Collected {
      std::vector<cups_dest_t*> dests;
    };

    int collect(void* data, unsigned flags, cups_dest_t* dest) {
      if ((flags & CUPS_DEST_FLAGS_REMOVED) != 0) {
        return 1;
      }
      cups_dest_t* copy = nullptr;
      cupsCopyDest(dest, 0, &copy);
      static_cast<Collected*>(data)->dests.push_back(copy);
      return 1;
    }

    Media mediaFrom(http_t* http, cups_dest_t* dest, cups_dinfo_t* info, cups_size_t& size) {
      const char* label = cupsLocalizeDestMedia(http, dest, info, CUPS_MEDIA_FLAGS_DEFAULT, &size);
      return {.name = size.media,
              .label = label != nullptr ? label : size.media,
              .widthMm = size.width / 100.0,
              .heightMm = size.length / 100.0,
              .topMm = size.top / 100.0,
              .bottomMm = size.bottom / 100.0,
              .leftMm = size.left / 100.0,
              .rightMm = size.right / 100.0};
    }

    Printer describe(cups_dest_t* dest) {
      Printer printer;
      printer.name = dest->name;
      printer.isDefault = dest->is_default != 0;
      const char* info = cupsGetOption("printer-info", dest->num_options, dest->options);
      printer.label = info != nullptr && info[0] != '\0' ? info : dest->name;

      cups_dinfo_t* details = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, dest);
      if (details == nullptr) {
        return printer;
      }
      const int count = cupsGetDestMediaCount(CUPS_HTTP_DEFAULT, dest, details, CUPS_MEDIA_FLAGS_DEFAULT);
      for (int i = 0; i < count; ++i) {
        cups_size_t size{};
        if (cupsGetDestMediaByIndex(CUPS_HTTP_DEFAULT, dest, details, i, CUPS_MEDIA_FLAGS_DEFAULT, &size) != 0) {
          printer.media.push_back(mediaFrom(CUPS_HTTP_DEFAULT, dest, details, size));
        }
      }
      cups_size_t defaultSize{};
      if (cupsGetDestMediaDefault(CUPS_HTTP_DEFAULT, dest, details, CUPS_MEDIA_FLAGS_DEFAULT, &defaultSize) != 0) {
        printer.defaultMedia = defaultSize.media;
      }
      printer.duplex = cupsCheckDestSupported(CUPS_HTTP_DEFAULT, dest, details, CUPS_SIDES, CUPS_SIDES_TWO_SIDED_PORTRAIT) != 0;
      printer.color = cupsCheckDestSupported(CUPS_HTTP_DEFAULT, dest, details, "print-color-mode", "color") != 0;
      cupsFreeDestInfo(details);
      return printer;
    }

  } // namespace

  std::vector<Printer> enumeratePrinters() {
    Collected collected;
    cupsEnumDests(CUPS_DEST_FLAGS_NONE, kDiscoveryMs, nullptr, 0, 0, &collect, &collected);
    std::vector<Printer> printers;
    for (cups_dest_t* dest : collected.dests) {
      printers.push_back(describe(dest));
      cupsFreeDests(1, dest);
    }
    return printers;
  }

  std::string submit(const std::string& printer, const std::string& title, const std::filesystem::path& pdf,
                     const dialog::PrintJob& job) {
    // The enumerated destination carries the URI of a discovered printer; a named lookup of a
    // printer without a queue yields one that cupsCopyDestInfo cannot use.
    cups_dest_t* dest = nullptr;
    Collected collected;
    cupsEnumDests(CUPS_DEST_FLAGS_NONE, kDiscoveryMs, nullptr, 0, 0, &collect, &collected);
    for (cups_dest_t* candidate : collected.dests) {
      if (dest == nullptr && printer == candidate->name) {
        dest = candidate;
      } else {
        cupsFreeDests(1, candidate);
      }
    }
    if (dest == nullptr) {
      dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, printer.c_str(), nullptr);
    }
    if (dest == nullptr) {
      return "printer " + printer + " not found";
    }
    cups_dinfo_t* info = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, dest);
    if (info == nullptr) {
      std::string error = std::string("no printer information: ") + cupsLastErrorString();
      cupsFreeDests(1, dest);
      return error;
    }
    int numOptions = 0;
    cups_option_t* options = nullptr;
    for (const auto& [name, value] : cupsOptions(job)) {
      numOptions = cupsAddOption(name.c_str(), value.c_str(), numOptions, &options);
    }

    std::string error;
    int jobId = 0;
    if (cupsCreateDestJob(CUPS_HTTP_DEFAULT, dest, info, &jobId, title.c_str(), numOptions, options) > IPP_STATUS_OK_IGNORED_OR_SUBSTITUTED) {
      error = cupsLastErrorString();
    } else if (cupsStartDestDocument(CUPS_HTTP_DEFAULT, dest, info, jobId, title.c_str(), CUPS_FORMAT_PDF, 0, nullptr, 1) !=
               HTTP_STATUS_CONTINUE) {
      error = cupsLastErrorString();
    } else {
      std::ifstream input(pdf, std::ios::binary);
      std::vector<char> buffer(65536);
      while (error.empty() && input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && cupsWriteRequestData(CUPS_HTTP_DEFAULT, buffer.data(), static_cast<size_t>(count)) != HTTP_STATUS_CONTINUE) {
          error = cupsLastErrorString();
        }
      }
      if (cupsFinishDestDocument(CUPS_HTTP_DEFAULT, dest, info) > IPP_STATUS_OK_IGNORED_OR_SUBSTITUTED && error.empty()) {
        error = cupsLastErrorString();
      }
    }
    cupsFreeOptions(numOptions, options);
    cupsFreeDestInfo(info);
    cupsFreeDests(1, dest);
    return error;
  }

} // namespace xdpn::print
