#include "dbus/dialog_calls.h"

#include <utility>

namespace xdpn {

  GVariant* emptyResults() { return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0); }

  DialogCalls::DialogCalls(GDBusConnection* connection) : m_connection(G_DBUS_CONNECTION(g_object_ref(connection))) {}

  DialogCalls::~DialogCalls() {
    for (auto& [handle, call] : m_calls) {
      (void)handle;
      call->finish(call->invocation, {.response = dbus::Other, .choices = {}, .application = {}, .text = {}});
    }
    m_calls.clear();
    g_object_unref(m_connection);
  }

  void DialogCalls::start(const std::string& handle, GDBusMethodInvocation* invocation, const dialog::Request& request,
                          Finish finish) {
    auto call = std::make_unique<Call>();
    call->invocation = invocation;
    call->finish = std::move(finish);
    call->request = std::make_unique<dbus::Request>(m_connection, handle, [this, handle] {
      if (DialogProcess* process = dialog(handle); process != nullptr) {
        process->cancel();
      }
    });
    Call* raw = call.get();
    m_calls[handle] = std::move(call);
    raw->dialog = std::make_unique<DialogProcess>(request, [this, handle](const dialog::Answer& answer) {
      complete(handle, answer);
    });
    if (!raw->dialog->started()) {
      complete(handle, {.response = dbus::Other, .choices = {}, .application = {}, .text = {}});
    }
  }

  DialogProcess* DialogCalls::dialog(const std::string& handle) {
    auto it = m_calls.find(handle);
    return it != m_calls.end() ? it->second->dialog.get() : nullptr;
  }

  void DialogCalls::complete(const std::string& handle, const dialog::Answer& answer) {
    auto it = m_calls.find(handle);
    if (it == m_calls.end()) {
      return;
    }
    auto call = std::move(it->second);
    m_calls.erase(it);
    call->finish(call->invocation, answer);
    // The dialog's answer callback is still on the stack; release the call once it has returned.
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE, +[](gpointer) -> gboolean { return G_SOURCE_REMOVE; }, call.release(),
        +[](gpointer data) { delete static_cast<Call*>(data); });
  }

} // namespace xdpn
