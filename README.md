# xdg-desktop-portal-noctalia

An [xdg-desktop-portal](https://github.com/flatpak/xdg-desktop-portal) backend for desktops
running the [Noctalia](https://github.com/noctalia-dev/noctalia) shell, without GTK or Qt.

Implemented interfaces:

- `org.freedesktop.impl.portal.Settings`: the GNOME desktop GSettings schemas (Noctalia writes
  the colour scheme there) and `org.freedesktop.appearance` (`color-scheme`, `contrast`,
  `accent-color`).

Printing and file choosing are left to other backends (xdg-desktop-portal-gtk, Luciole).

## Building

```sh
meson setup build
ninja -C build
meson test -C build
```

Dependencies: GLib/GIO ≥ 2.76.

## Using it

List `noctalia` in the desktop's `portals.conf`, for example
`/usr/share/xdg-desktop-portal/umbriel-portals.conf`:

```ini
[preferred]
default=umbriel;noctalia;gtk
```
