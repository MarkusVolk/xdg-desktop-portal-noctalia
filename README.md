# xdg-desktop-portal-noctalia

An [xdg-desktop-portal](https://github.com/flatpak/xdg-desktop-portal) backend for desktops
running the [Noctalia](https://github.com/noctalia-dev/noctalia) shell, without GTK or Qt.

Implemented interfaces:

- `org.freedesktop.impl.portal.Settings`: the GNOME desktop GSettings schemas (Noctalia writes
  the colour scheme there) and `org.freedesktop.appearance` (`color-scheme`, `contrast`,
  `accent-color`).
- `org.freedesktop.impl.portal.Inhibit`: logout and suspend through logind inhibitor locks,
  idle through `org.freedesktop.ScreenSaver` (served by Noctalia), and session monitors.
- `org.freedesktop.impl.portal.Access`: the permission dialog with its choices.
- `org.freedesktop.impl.portal.AppChooser`: the application that opens a file or URI, with
  updates of the offered list while the dialog is open.
- `org.freedesktop.impl.portal.Lockdown`: the GNOME lockdown, location and privacy settings.
- `org.freedesktop.impl.portal.Email`: a `mailto:` URI for the default mail client.
- `org.freedesktop.impl.portal.Notification`: forwarded to the notification server (Noctalia),
  with the default action and buttons sent back to the application.
- `org.freedesktop.impl.portal.Account`: the user's name and picture (AccountsService, else the
  passwd entry and `~/.face`), after asking.
- `org.freedesktop.impl.portal.Wallpaper`: sets Noctalia's wallpaper (`noctalia msg
  wallpaper-set`), with a preview when asked; the picture is kept in
  `$XDG_DATA_HOME/xdg-desktop-portal-noctalia/wallpapers`.
- `org.freedesktop.impl.portal.DynamicLauncher`: confirms and names launchers and web app
  launchers; no application gets install tokens without asking.
- `org.freedesktop.impl.portal.Print`: the print dialog over the CUPS printers and a Print to
  File printer; settings and page setup go back in the GtkPrintSettings format.

Dialogs run in `noctalia-portal-dialog`, one process per dialog, so the daemon itself has no
Wayland connection. Their colours come from a Noctalia template; add to
`~/.config/noctalia/config.toml`:

```toml
[theme.templates.user.xdg-desktop-portal-noctalia]
input_path = "/usr/share/xdg-desktop-portal-noctalia/templates/colors.toml"
output_path = "$XDG_CONFIG_HOME/xdg-desktop-portal-noctalia/colors.toml"
```

File choosing is left to another backend, such as Luciole's.

## Building

```sh
meson setup build
ninja -C build
meson test -C build
```

Dependencies: GLib/GIO ≥ 2.76, nlohmann-json; for the dialog helper (`-Ddialog=true`, the
default) also what Noctalia's lib/ needs: wayland, EGL/GLES 2, freetype, fontconfig, cairo, pango,
harfbuzz, xkbcommon, stb, tomlplusplus, and for the codec pack (application icons) librsvg,
libwebp and libjxl; for the Print portal (`-Dprint=true`, the default) CUPS.

## Using it

List `noctalia` in the desktop's `portals.conf`, for example
`/usr/share/xdg-desktop-portal/umbriel-portals.conf`:

```ini
[preferred]
default=umbriel;noctalia
org.freedesktop.impl.portal.FileChooser=luciole
```
