#include "access/access.h"

#include "dbus/introspection.h"
#include "dbus/options.h"
#include "icons/icon_lookup.h"

#include <utility>
#include <vector>

namespace xdpn {

  namespace {


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

  Access::Access(GDBusConnection* connection) : m_calls(connection) {
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

  Access::~Access() { m_interface.reset(); }

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

    m_calls.start(handle, invocation, request, [](GDBusMethodInvocation* call, const dialog::Answer& answer) {
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
      g_dbus_method_invocation_return_value(call, g_variant_new("(ua{sv})", answer.response, &results));
    });
  }

} // namespace xdpn
