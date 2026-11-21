#include "access/access.h"

#include "dbus/introspection.h"
#include "icons/icon_lookup.h"

#include <utility>
#include <vector>

namespace xdpn {

  namespace {

    std::string lookupString(GVariant* options, const char* key) {
      const char* value = nullptr;
      return g_variant_lookup(options, key, "&s", &value) ? std::string(value) : std::string();
    }

    std::vector<dialog::Choice> lookupChoices(GVariant* options) {
      std::vector<dialog::Choice> choices;
      GVariant* list = g_variant_lookup_value(options, "choices", G_VARIANT_TYPE("a(ssa(ss)s)"));
      if (list == nullptr) {
        return choices;
      }
      GVariantIter iter;
      g_variant_iter_init(&iter, list);
      const char* id = nullptr;
      const char* label = nullptr;
      GVariantIter* optionIter = nullptr;
      const char* initial = nullptr;
      while (g_variant_iter_next(&iter, "(&s&sa(ss)&s)", &id, &label, &optionIter, &initial)) {
        dialog::Choice choice{.id = id, .label = label, .options = {}, .initial = initial};
        const char* optionId = nullptr;
        const char* optionLabel = nullptr;
        while (g_variant_iter_next(optionIter, "(&s&s)", &optionId, &optionLabel)) {
          choice.options.push_back({.id = optionId, .label = optionLabel});
        }
        g_variant_iter_free(optionIter);
        choices.push_back(std::move(choice));
      }
      g_variant_unref(list);
      return choices;
    }

  } // namespace

  Access::Access(GDBusConnection* connection) : m_connection(G_DBUS_CONNECTION(g_object_ref(connection))) {
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kAccess,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          if (method == "AccessDialog") {
            accessDialog(parameters, invocation);
            return;
          }
          g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                "Unknown method %s", method.c_str());
        });
  }

  Access::~Access() {
    m_interface.reset();
    for (auto& [handle, call] : m_calls) {
      (void)handle;
      g_dbus_method_invocation_return_value(call->invocation, g_variant_new("(u@a{sv})", dbus::Other,
                                                                            g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)));
    }
    m_calls.clear();
    g_object_unref(m_connection);
  }

  void Access::accessDialog(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* parentWindow = nullptr;
    const char* title = nullptr;
    const char* subtitle = nullptr;
    const char* body = nullptr;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&s&s&s&s@a{sv})", &handle, &appId, &parentWindow, &title, &subtitle, &body, &options);

    dialog::Request request;
    request.kind = dialog::Kind::Access;
    request.title = title;
    request.subtitle = subtitle;
    request.body = body;
    request.icon = icons::lookupName(lookupString(options, "icon"));
    request.denyLabel = lookupString(options, "deny_label");
    request.grantLabel = lookupString(options, "grant_label");
    request.choices = lookupChoices(options);
    g_variant_unref(options);

    const std::string key = handle;
    auto call = std::make_unique<Call>();
    call->invocation = invocation;
    call->request = std::make_unique<dbus::Request>(m_connection, key, [this, key] {
      if (auto it = m_calls.find(key); it != m_calls.end() && it->second->dialog) {
        it->second->dialog->cancel();
      }
    });
    Call* raw = call.get();
    m_calls[key] = std::move(call);
    raw->dialog = std::make_unique<DialogProcess>(request, [this, key](const dialog::Answer& answer) { complete(key, answer); });
    if (!raw->dialog->started()) {
      complete(key, {.response = dbus::Other, .choices = {}, .application = {}});
    }
  }

  void Access::complete(const std::string& handle, const dialog::Answer& answer) {
    auto it = m_calls.find(handle);
    if (it == m_calls.end()) {
      return;
    }
    GVariantBuilder results;
    g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
    if (answer.response == dbus::Success && !answer.choices.empty()) {
      GVariantBuilder choices;
      g_variant_builder_init(&choices, G_VARIANT_TYPE("a(ss)"));
      for (const auto& [id, value] : answer.choices) {
        g_variant_builder_add(&choices, "(ss)", id.c_str(), value.c_str());
      }
      g_variant_builder_add(&results, "{sv}", "choices", g_variant_builder_end(&choices));
    }
    g_dbus_method_invocation_return_value(it->second->invocation, g_variant_new("(ua{sv})", answer.response, &results));
    auto call = std::move(it->second);
    m_calls.erase(it);
    // The dialog's answer callback is still on the stack; release the call once it has returned.
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE, +[](gpointer) -> gboolean { return G_SOURCE_REMOVE; }, call.release(),
        +[](gpointer data) { delete static_cast<Call*>(data); });
  }

} // namespace xdpn
