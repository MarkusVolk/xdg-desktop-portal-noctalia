#pragma once

#include "dbus/dialog_process.h"
#include "dbus/interface.h"

#include <gio/gio.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace xdpn {

  // The pending portal calls of one interface that wait for a dialog: the request object at the
  // handle, the dialog process, and the invocation to answer. Closing the request cancels the dialog.
  class DialogCalls {
  public:
    // Answers the invocation; also called with response 2 for calls still open at destruction.
    using Finish = std::function<void(GDBusMethodInvocation* invocation, const dialog::Answer& answer)>;

    explicit DialogCalls(GDBusConnection* connection);
    ~DialogCalls();

    DialogCalls(const DialogCalls&) = delete;
    DialogCalls& operator=(const DialogCalls&) = delete;
    DialogCalls(DialogCalls&&) = delete;
    DialogCalls& operator=(DialogCalls&&) = delete;

    void start(const std::string& handle, GDBusMethodInvocation* invocation, const dialog::Request& request,
               Finish finish);
    // The running dialog of a call, or nullptr.
    [[nodiscard]] DialogProcess* dialog(const std::string& handle);

  private:
    struct Call {
      GDBusMethodInvocation* invocation = nullptr;
      Finish finish;
      std::unique_ptr<dbus::Request> request;
      std::unique_ptr<DialogProcess> dialog;
    };

    void complete(const std::string& handle, const dialog::Answer& answer);

    GDBusConnection* m_connection = nullptr;
    std::map<std::string, std::unique_ptr<Call>> m_calls;
  };

  // An empty a{sv}, for results without entries.
  [[nodiscard]] GVariant* emptyResults();

} // namespace xdpn
