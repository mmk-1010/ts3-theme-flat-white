# Flat White · Flat Dark — TeamSpeak 3 Theme Pack

[简体中文](README.md) | **English**

![Flat White and Flat Dark interfaces](assets/theme-preview.png)

## Style

A light theme and a dark theme built on the same flat language: no gradients, no highlights,
no shadows. Depth comes only from 1px hairlines, rounded corners and a single teal accent.
Controls use a 6px radius, panels 8px, rows are 22px tall, and everything is set in
Microsoft YaHei UI 9pt so mixed Chinese and English text keeps a steady line height.

- **Flat White (light)**: window background `#F7F8FA`, panels pure white; body text `#1F2328`,
  accent `#0F766E`.
- **Flat Dark (dark)**: window background `#141519`, panels `#1B1D22`; body text `#E7E9EC`,
  accent `#2DD4BF`. With the companion plugin installed, the Windows title bar, window borders
  and dialogs turn dark as well — not just the client area.

The pack also ships two sets of linear icons and a plugin that rearranges the interface into
a Discord-style, content-first layout. A separate pack of 78 synthesized sound effects is
available as an optional install. Every asset was drawn or synthesized for this project;
there are no third-party images or audio files.

## Download

Choose the matching ZIP archive on the [latest release](https://github.com/mmk-1010/ts3-theme-flat-white/releases/latest) page:

| Archive | Contents |
| --- | --- |
| [Flat White](https://github.com/mmk-1010/ts3-theme-flat-white/releases/latest) | Light theme + light icons + plugin |
| [Flat Dark](https://github.com/mmk-1010/ts3-theme-flat-white/releases/latest) | Dark theme + dark icons + plugin |

Both archives contain the same plugin features; every file inside the dark archive uses a
Flat Dark name. The plugin provides the Discord-style layout, bubble chat, message and poke
alerts, the floating user card, the dark title bar and free window resizing.
System events and error logs support selecting and copying text, and your away, microphone
mute and headset mute states stay in sync across the user panel, member rows and profile page.
The theme, icon pack and plugin can also be downloaded individually.

**Optional sounds**: download and install the 78 synthesized sound effects separately as
[Flat White Sounds](https://github.com/mmk-1010/ts3-theme-flat-white/releases/latest/download/Flat-White-Sounds.ts3_soundpack) or
[Flat Dark Sounds](https://github.com/mmk-1010/ts3-theme-flat-white/releases/latest/download/Flat-Dark-Sounds.ts3_soundpack); both contain the same sounds.

Notification sounds prefer the installed Flat White Sounds / Flat Dark Sounds pack and fall
back to the client's default pack; the away / microphone / headset buttons do not depend on it.

## Installation

Unzip and double-click each installer in order, restarting the client after every step:

1. **Theme**: double-click `Flat White.ts3_style` or `Flat Dark.ts3_style` (installing both is
   fine too), then restart and choose `flat_white` (light) or `flat_dark` (dark) under
   **Tools → Options → Design → Theme**.
2. **Icon pack**: double-click the matching `.ts3_iconpack`, then restart and choose
   *Flat White Icons* (light) or *Flat Dark Icons* (dark) under
   **Tools → Options → Design → Icon Packs**.
3. **Plugin**: quit TS3 completely first, then double-click the `.ts3_plugin` (the dark
   bundle names it `Flat Dark UI.ts3_plugin`). On the next start, check that *Flat White UI*
   is ticked under **Tools → Options → Addons** — both bundles install the same plugin, so
   they never load twice.
4. **Sounds (optional, separate install)**: double-click the separately downloaded
   `.ts3_soundpack` and follow the prompts (running as administrator is needed if the client
   lives under Program Files), then restart and choose
   *Flat White Sounds* (light) or *Flat Dark Sounds* (dark) under
   **Tools → Options → Notifications → Sound Pack**.

The theme, icon pack, plugin and sounds are all optional — installing only the theme already
reskins the client. Without the plugin, the dark theme only darkens the client area and the
title bar is still drawn by the system; with it, the title bar, borders and custom-drawn
controls go dark together.

## Uninstall

- Theme and icon pack: switch back to the defaults under **Options → Design**, or delete the
  corresponding entries from the client directory (theme) and `%APPDATA%\TS3Client\gfx`
  (icon pack).
- Plugin: delete `%APPDATA%\TS3Client\plugins\flatwhite_ui_win64.dll`.
- Sounds: delete `<client directory>\sound\flatwhite` and the installer-created
  `sound\Flat White Sounds` / `sound\Flat Dark Sounds` folders.

## Compatibility

Tested on TeamSpeak 3 **3.6.2** (Windows x64, Qt 5.15.2). The plugin is win64 only; the theme
and icon pack are pure resources and should work on any platform, but have not been tested on
macOS or Linux.
