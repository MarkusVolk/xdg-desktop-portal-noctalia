#include "access/access.h"
#include "account/account.h"
#include "appchooser/appchooser.h"
#include "email/email.h"
#include "inhibit/inhibit.h"
#include "launcher/launcher.h"
#include "lockdown/lockdown.h"
#include "notification/notification.h"
#include "settings/settings.h"
#include "wallpaper/wallpaper.h"

#include <gio/gio.h>
#include <glib-unix.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <print>

namespace {

  constexpr char kBusName[] = "org.freedesktop.impl.portal.desktop.noctalia";

  struct Daemon {
    GMainLoop* loop = nullptr;
    std::unique_ptr<xdpn::Settings> settings;
    std::unique_ptr<xdpn::Inhibit> inhibit;
    std::unique_ptr<xdpn::Access> access;
    std::unique_ptr<xdpn::AppChooser> appChooser;
    std::unique_ptr<xdpn::Lockdown> lockdown;
    std::unique_ptr<xdpn::Email> email;
    std::unique_ptr<xdpn::Notification> notification;
    std::unique_ptr<xdpn::Account> account;
    std::unique_ptr<xdpn::Wallpaper> wallpaper;
    std::unique_ptr<xdpn::DynamicLauncher> launcher;
    int status = 0;
  };

  gboolean quitLoop(gpointer data) {
    g_main_loop_quit(static_cast<Daemon*>(data)->loop);
    return G_SOURCE_REMOVE;
  }

} // namespace

int main(int argc, char** argv) {
  bool replace = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-r") == 0 || std::strcmp(argv[i], "--replace") == 0) {
      replace = true;
    } else {
      std::println(stderr, "usage: {} [--replace]", argv[0]);
      return 2;
    }
  }

  Daemon daemon;
  daemon.loop = g_main_loop_new(nullptr, FALSE);
  g_unix_signal_add(SIGINT, quitLoop, &daemon);
  g_unix_signal_add(SIGTERM, quitLoop, &daemon);

  auto flags = static_cast<GBusNameOwnerFlags>(G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                               (replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0));
  const guint owner = g_bus_own_name(
      G_BUS_TYPE_SESSION, kBusName, flags,
      +[](GDBusConnection* connection, const char* /*name*/, gpointer data) {
        auto* self = static_cast<Daemon*>(data);
        self->settings = std::make_unique<xdpn::Settings>(connection);
        self->inhibit = std::make_unique<xdpn::Inhibit>(connection);
        self->access = std::make_unique<xdpn::Access>(connection);
        self->appChooser = std::make_unique<xdpn::AppChooser>(connection);
        self->lockdown = std::make_unique<xdpn::Lockdown>(connection);
        self->email = std::make_unique<xdpn::Email>(connection);
        self->notification = std::make_unique<xdpn::Notification>(connection);
        self->account = std::make_unique<xdpn::Account>(connection);
        self->wallpaper = std::make_unique<xdpn::Wallpaper>(connection);
        self->launcher = std::make_unique<xdpn::DynamicLauncher>(connection);
      },
      nullptr,
      +[](GDBusConnection* connection, const char* name, gpointer data) {
        auto* self = static_cast<Daemon*>(data);
        // Another instance owning or taking over the name is no failure; only a missing bus is.
        if (connection == nullptr) {
          std::println(stderr, "xdg-desktop-portal-noctalia: no session bus");
          self->status = 1;
        } else {
          std::println(stderr, "xdg-desktop-portal-noctalia: {} is owned by another instance", name);
        }
        g_main_loop_quit(self->loop);
      },
      &daemon, nullptr);

  g_main_loop_run(daemon.loop);

  daemon.launcher.reset();
  daemon.wallpaper.reset();
  daemon.account.reset();
  daemon.notification.reset();
  daemon.email.reset();
  daemon.lockdown.reset();
  daemon.appChooser.reset();
  daemon.access.reset();
  daemon.inhibit.reset();
  daemon.settings.reset();
  g_bus_unown_name(owner);
  g_main_loop_unref(daemon.loop);
  return daemon.status;
}
