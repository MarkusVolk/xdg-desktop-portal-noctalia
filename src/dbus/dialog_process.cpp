#include "dbus/dialog_process.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <print>

namespace xdpn {

  std::string dialogExecutable() {
    if (const char* configured = std::getenv("NOCTALIA_PORTAL_DIALOG"); configured != nullptr && configured[0] != '\0') {
      return configured;
    }
    return XDPN_DIALOG_PATH;
  }

  DialogProcess::DialogProcess(const dialog::Request& request, AnswerCallback onAnswer)
      : m_cancellable(g_cancellable_new()), m_onAnswer(std::move(onAnswer)) {
    const std::string executable = dialogExecutable();
    GError* error = nullptr;
    const char* argv[] = {executable.c_str(), nullptr};
    m_process = g_subprocess_newv(argv, static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE),
                                  &error);
    if (m_process == nullptr) {
      std::println(stderr, "dialog: cannot start {}: {}", executable, error->message);
      g_clear_error(&error);
      return;
    }
    m_stdin = G_OUTPUT_STREAM(g_object_ref(g_subprocess_get_stdin_pipe(m_process)));
    m_stdout = g_data_input_stream_new(g_subprocess_get_stdout_pipe(m_process));
    send(dialog::formatRequest(request));
    g_data_input_stream_read_line_async(m_stdout, G_PRIORITY_DEFAULT, m_cancellable, &DialogProcess::onLine, this);
  }

  DialogProcess::~DialogProcess() {
    g_cancellable_cancel(m_cancellable);
    if (m_process != nullptr && !m_finished) {
      g_subprocess_send_signal(m_process, SIGTERM);
    }
    g_clear_object(&m_stdout);
    if (m_stdin != nullptr) {
      g_output_stream_close(m_stdin, nullptr, nullptr);
      g_clear_object(&m_stdin);
    }
    g_clear_object(&m_process);
    g_clear_object(&m_cancellable);
  }

  void DialogProcess::send(const std::string& line) {
    if (m_stdin == nullptr) {
      return;
    }
    const std::string data = line + "\n";
    GError* error = nullptr;
    if (!g_output_stream_write_all(m_stdin, data.data(), data.size(), nullptr, nullptr, &error) ||
        !g_output_stream_flush(m_stdin, nullptr, &error)) {
      std::println(stderr, "dialog: cannot write the request: {}", error->message);
      g_clear_error(&error);
    }
  }

  void DialogProcess::cancel() {
    if (m_process != nullptr && !m_finished) {
      g_subprocess_send_signal(m_process, SIGTERM);
    }
  }

  void DialogProcess::onLine(GObject* source, GAsyncResult* result, gpointer data) {
    GError* error = nullptr;
    gsize length = 0;
    char* line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), result, &length, &error);
    if (error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_error_free(error);
      return;
    }
    g_clear_error(&error);
    auto* self = static_cast<DialogProcess*>(data);
    const dialog::Answer answer = dialog::parseAnswer(line != nullptr ? std::string(line, length) : std::string());
    g_free(line);
    self->finish(answer);
  }

  void DialogProcess::finish(const dialog::Answer& answer) {
    if (m_finished) {
      return;
    }
    m_finished = true;
    auto callback = std::move(m_onAnswer);
    if (callback) {
      callback(answer);
    }
  }

} // namespace xdpn
