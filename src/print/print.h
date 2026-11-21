#pragma once

#include "dbus/dialog_calls.h"
#include "dbus/interface.h"
#include "print/cups_printers.h"

#include <gio/gio.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xdpn {

  // org.freedesktop.impl.portal.Print: the print dialog over CUPS printers and a Print to File
  // printer, and the print jobs; the settings go back to the application in GtkPrintSettings form.
  class Print {
  public:
    explicit Print(GDBusConnection* connection);
    ~Print();

    Print(const Print&) = delete;
    Print& operator=(const Print&) = delete;
    Print(Print&&) = delete;
    Print& operator=(Print&&) = delete;

  private:
    struct Prepared {
      std::string appId;
      dialog::PrintJob job;
    };
    using Chosen = std::function<void(GDBusMethodInvocation* invocation, const dialog::Answer& answer)>;

    void preparePrint(GVariant* parameters, GDBusMethodInvocation* invocation);
    void print(GVariant* parameters, GDBusMethodInvocation* invocation);
    // Lists the printers in a worker thread, then asks in the dialog.
    void ask(const std::string& handle, GDBusMethodInvocation* invocation, const std::string& title,
             const dialog::PrintJob& preset, const std::string& acceptLabel, bool toFile, Chosen chosen);
    void printJob(GDBusMethodInvocation* invocation, const std::string& title, int fd, const dialog::PrintJob& job);
    [[nodiscard]] print::Media mediaFor(const dialog::PrintJob& job) const;
    [[nodiscard]] guint32 remember(const std::string& appId, const dialog::PrintJob& job);

    DialogCalls m_calls;
    std::vector<print::Printer> m_printers;
    std::map<guint32, Prepared> m_prepared;
    unsigned m_counter = 0;
    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace print {

    // The printer name GTK applications know for printing to a file.
    inline constexpr char kFilePrinter[] = "Print to File";

    // $HOME/Documents/<title>.pdf, or $HOME/<title>.pdf without a Documents folder.
    [[nodiscard]] std::string defaultOutputPath(const std::string& title);

  } // namespace print

} // namespace xdpn
