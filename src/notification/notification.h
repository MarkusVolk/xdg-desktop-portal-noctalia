#pragma once

#include "dbus/interface.h"

#include <gio/gio.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xdpn {

  // org.freedesktop.impl.portal.Notification: forwards to org.freedesktop.Notifications (Noctalia)
  // and turns the answers back into application actions, like xdg-desktop-portal-gtk does when
  // GNOME Shell is not there.
  class Notification {
  public:
    explicit Notification(GDBusConnection* connection);
    ~Notification();

    Notification(const Notification&) = delete;
    Notification& operator=(const Notification&) = delete;
    Notification(Notification&&) = delete;
    Notification& operator=(Notification&&) = delete;

    struct Action {
      std::string name;
      // A serialized GVariant target, empty for none.
      GVariant* target = nullptr;
    };

  private:
    struct Entry {
      std::string appId;
      std::string id;
      guint32 serverId = 0;
      std::optional<Action> defaultAction;
      std::vector<Action> buttons;
      ~Entry();
    };

    void add(GVariant* parameters, GDBusMethodInvocation* invocation);
    void remove(GVariant* parameters, GDBusMethodInvocation* invocation);
    void onServerSignal(const char* signal, GVariant* parameters);
    void activate(const Entry& entry, const Action& action, const std::string& token);
    Entry* findByServerId(guint32 serverId);

    GDBusConnection* m_connection = nullptr;
    guint m_actionSubscription = 0;
    guint m_closedSubscription = 0;
    guint m_tokenSubscription = 0;
    std::map<guint32, std::string> m_tokens;
    std::map<std::string, std::unique_ptr<Entry>> m_entries;
    std::unique_ptr<dbus::Interface> m_interface;
  };

  namespace notification {

    // Portal priority to the urgency hint: low 0, normal and high 1, urgent 2.
    [[nodiscard]] std::uint8_t urgency(const std::string& priority);
    // The D-Bus object path of a GApplication with this ID.
    [[nodiscard]] std::string applicationPath(const std::string& appId);
    // The action key of button `index` in the server's actions list.
    [[nodiscard]] std::string buttonKey(std::size_t index);
    [[nodiscard]] std::optional<std::size_t> buttonIndex(const std::string& key);

  } // namespace notification

} // namespace xdpn
