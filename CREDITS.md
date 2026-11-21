# Credits

xdg-desktop-portal-noctalia is built on the UI library of
[Noctalia](https://github.com/noctalia-dev/noctalia) and follows the structure of
[xdg-desktop-portal-umbriel](https://github.com/noctalia-dev/xdg-desktop-portal-umbriel). Both are
MIT licensed, Copyright (c) 2026 noctalia-dev.

- `lib/` is an unchanged copy of Noctalia's `lib/` (without `third_party/`), refreshed with
  `tools/sync-lib.sh`. Changes to it belong upstream.
- From [Plume](https://github.com/MarkusVolk/plume), which adapted them from
  [Luciole](https://github.com/noctalia-dev/luciole): `src/dialog/main_loop.*`,
  `src/dialog/glib_poll_source.*`, `src/dialog/poll_sources.h`, `src/dialog/palette_file.*`.
- The Settings and Inhibit semantics follow
  [xdg-desktop-portal-gtk](https://github.com/flatpak/xdg-desktop-portal-gtk) (LGPL-2.1+); no code
  is copied from it.
- `lib/assets/fonts/` carries the Tabler icon font; its license is
  `lib/assets/fonts/tabler-icons-license.txt`.
