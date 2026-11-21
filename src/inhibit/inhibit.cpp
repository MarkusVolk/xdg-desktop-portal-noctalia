#include "inhibit/inhibit.h"

#include "dbus/introspection.h"

#include <gio/gunixfdlist.h>
#include <unistd.h>

#include <cstdio>
#include <optional>
#include <print>
#include <utility>

namespace xdpn {

  namespace {

    constexpr int kCallTimeoutMs = 5000;

    struct Deferred {
      Inhibit* self;
      std::string handle;
      void (*action)(Inhibit*, const std::string&);
    };

    void defer(Inhibit* self, std::string handle, void (*action)(Inhibit*, const std::string&)) {
      g_idle_add_full(
          G_PRIORITY_DEFAULT_IDLE,
          +[](gpointer data) -> gboolean {
            auto* deferred = static_cast<Deferred*>(data);
            deferred->action(deferred->self, deferred->handle);
            return G_SOURCE_REMOVE;
          },
          new Deferred{self, std::move(handle), action}, +[](gpointer data) { delete static_cast<Deferred*>(data); });
    }

    int takeLogindLock(GDBusConnection* system, const std::string& what, const char* who, const char* why) {
      if (system == nullptr) {
        return -1;
      }
      GError* error = nullptr;
      GUnixFDList* fds = nullptr;
      GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(
          system, "org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Inhibit",
          g_variant_new("(ssss)", what.c_str(), who, why, "block"), G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE,
          kCallTimeoutMs, nullptr, &fds, nullptr, &error);
      if (reply == nullptr) {
        std::println(stderr, "inhibit: logind Inhibit({}) failed: {}", what, error->message);
        g_clear_error(&error);
        return -1;
      }
      gint32 index = -1;
      g_variant_get(reply, "(h)", &index);
      g_variant_unref(reply);
      const int fd = fds != nullptr ? g_unix_fd_list_get(fds, index, &error) : -1;
      if (fd < 0 && error != nullptr) {
        std::println(stderr, "inhibit: no logind lock fd: {}", error->message);
        g_clear_error(&error);
      }
      g_clear_object(&fds);
      return fd;
    }

  } // namespace

  namespace inhibit {

    std::string logindWhat(guint32 flags) {
      std::string what;
      auto add = [&what](const char* item) {
        if (!what.empty()) {
          what += ':';
        }
        what += item;
      };
      if ((flags & Inhibit::Logout) != 0) {
        add("shutdown");
      }
      if ((flags & Inhibit::Suspend) != 0) {
        add("sleep");
      }
      return what;
    }

  } // namespace inhibit

  struct Inhibit::Inhibition {
    std::unique_ptr<dbus::Request> request;
    int logindFd = -1;
    std::optional<guint32> cookie;
  };

  struct Inhibit::Monitor {
    std::unique_ptr<dbus::Interface> session;
  };

  Inhibit::Inhibit(GDBusConnection* connection) : m_connection(G_DBUS_CONNECTION(g_object_ref(connection))) {
    GError* error = nullptr;
    m_system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (m_system == nullptr) {
      std::println(stderr, "inhibit: no system bus: {}", error->message);
      g_clear_error(&error);
    }
    m_interface = std::make_unique<dbus::Interface>(
        connection, dbus::kPortalPath, introspection::kInhibit,
        [this](const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          handle(method, parameters, invocation);
        });
  }

  Inhibit::~Inhibit() {
    m_interface.reset();
    m_monitors.clear();
    for (auto& [handle, inhibition] : m_inhibitions) {
      (void)handle;
      if (inhibition->logindFd >= 0) {
        close(inhibition->logindFd);
      }
    }
    m_inhibitions.clear();
    g_clear_object(&m_system);
    g_object_unref(m_connection);
  }

  void Inhibit::handle(const std::string& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
    if (method == "Inhibit") {
      inhibit(parameters, invocation);
    } else if (method == "CreateMonitor") {
      createMonitor(parameters, invocation);
    } else if (method == "QueryEndResponse") {
      g_dbus_method_invocation_return_value(invocation, nullptr);
    } else {
      g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                            "Unknown method %s", method.c_str());
    }
  }

  void Inhibit::inhibit(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* appId = nullptr;
    const char* window = nullptr;
    guint32 flags = 0;
    GVariant* options = nullptr;
    g_variant_get(parameters, "(&o&s&su@a{sv})", &handle, &appId, &window, &flags, &options);
    const char* reason = nullptr;
    if (!g_variant_lookup(options, "reason", "&s", &reason)) {
      reason = "";
    }
    const char* who = appId[0] != '\0' ? appId : "xdg-desktop-portal";
    const char* why = reason[0] != '\0' ? reason : "Inhibited by an application";

    auto inhibition = std::make_unique<Inhibition>();
    const std::string handleString = handle;
    inhibition->request = std::make_unique<dbus::Request>(m_connection, handleString, [this, handleString] {
      defer(this, handleString, +[](Inhibit* self, const std::string& key) { self->release(key); });
    });

    if (const auto what = inhibit::logindWhat(flags); !what.empty()) {
      inhibition->logindFd = takeLogindLock(m_system, what, who, why);
    }
    if ((flags & Idle) != 0) {
      GError* error = nullptr;
      GVariant* reply = g_dbus_connection_call_sync(
          m_connection, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver",
          "Inhibit", g_variant_new("(ss)", who, why), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
          nullptr, &error);
      if (reply != nullptr) {
        guint32 cookie = 0;
        g_variant_get(reply, "(u)", &cookie);
        inhibition->cookie = cookie;
        g_variant_unref(reply);
      } else {
        std::println(stderr, "inhibit: ScreenSaver.Inhibit failed: {}", error->message);
        g_clear_error(&error);
      }
    }
    g_variant_unref(options);

    m_inhibitions[handleString] = std::move(inhibition);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  }

  void Inhibit::release(const std::string& handle) {
    auto it = m_inhibitions.find(handle);
    if (it == m_inhibitions.end()) {
      return;
    }
    if (it->second->logindFd >= 0) {
      close(it->second->logindFd);
    }
    if (it->second->cookie) {
      GError* error = nullptr;
      GVariant* reply = g_dbus_connection_call_sync(
          m_connection, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver",
          "UnInhibit", g_variant_new("(u)", *it->second->cookie), nullptr, G_DBUS_CALL_FLAGS_NONE, kCallTimeoutMs,
          nullptr, &error);
      if (reply != nullptr) {
        g_variant_unref(reply);
      } else {
        std::println(stderr, "inhibit: ScreenSaver.UnInhibit failed: {}", error->message);
        g_clear_error(&error);
      }
    }
    m_inhibitions.erase(it);
  }

  void Inhibit::createMonitor(GVariant* parameters, GDBusMethodInvocation* invocation) {
    const char* handle = nullptr;
    const char* sessionHandle = nullptr;
    const char* appId = nullptr;
    const char* window = nullptr;
    g_variant_get(parameters, "(&o&o&s&s)", &handle, &sessionHandle, &appId, &window);

    const std::string key = sessionHandle;
    auto monitor = std::make_unique<Monitor>();
    monitor->session = std::make_unique<dbus::Interface>(
        m_connection, key, introspection::kSession,
        [this, key](const std::string& method, GVariant* /*parameters*/, GDBusMethodInvocation* call) {
          if (method != "Close") {
            g_dbus_method_invocation_return_error(call, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                                  "Unknown method %s", method.c_str());
            return;
          }
          g_dbus_method_invocation_return_value(call, nullptr);
          defer(this, key, +[](Inhibit* self, const std::string& session) { self->closeMonitor(session); });
        },
        [](const std::string& property) -> GVariant* {
          return property == "version" ? g_variant_new_uint32(1) : nullptr;
        });
    m_monitors[key] = std::move(monitor);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", dbus::Success));

    defer(this, key, +[](Inhibit* self, const std::string& session) {
      if (!self->m_monitors.contains(session)) {
        return;
      }
      GVariantBuilder state;
      g_variant_builder_init(&state, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add(&state, "{sv}", "screensaver-active", g_variant_new_boolean(FALSE));
      g_variant_builder_add(&state, "{sv}", "session-state", g_variant_new_uint32(1));
      self->m_interface->emitSignal("StateChanged", g_variant_new("(oa{sv})", session.c_str(), &state));
    });
  }

  void Inhibit::closeMonitor(const std::string& handle) { m_monitors.erase(handle); }

} // namespace xdpn
