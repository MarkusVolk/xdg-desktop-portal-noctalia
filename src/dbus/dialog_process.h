#pragma once

#include "dialog/request.h"

#include <gio/gio.h>

#include <functional>
#include <memory>
#include <string>

namespace xdpn {

  // One running noctalia-portal-dialog: the request goes to its stdin, the one answer line comes
  // back from its stdout. A crash, an empty answer or cancel() yield response 2 or 1.
  class DialogProcess {
  public:
    using AnswerCallback = std::function<void(const dialog::Answer&)>;

    DialogProcess(const dialog::Request& request, AnswerCallback onAnswer);
    ~DialogProcess();

    DialogProcess(const DialogProcess&) = delete;
    DialogProcess& operator=(const DialogProcess&) = delete;
    DialogProcess(DialogProcess&&) = delete;
    DialogProcess& operator=(DialogProcess&&) = delete;

    [[nodiscard]] bool started() const noexcept { return m_process != nullptr; }
    // Sends one more protocol line (an application list update).
    void send(const std::string& line);
    // Terminates the dialog; the callback then reports a cancellation.
    void cancel();

  private:
    static void onLine(GObject* source, GAsyncResult* result, gpointer data);
    void finish(const dialog::Answer& answer);

    GSubprocess* m_process = nullptr;
    GOutputStream* m_stdin = nullptr;
    GDataInputStream* m_stdout = nullptr;
    GCancellable* m_cancellable = nullptr;
    AnswerCallback m_onAnswer;
    bool m_finished = false;
  };

  // The dialog executable: $NOCTALIA_PORTAL_DIALOG or the installed helper.
  std::string dialogExecutable();

} // namespace xdpn
