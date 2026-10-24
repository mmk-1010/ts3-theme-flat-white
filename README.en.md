# Flat White · Flat Dark — TeamSpeak 3 Theme Pack

[简体中文](README.md) | **English**

Author: **3ku-nanodesu**　Version: 1.3

![Flat White and Flat Dark interfaces](assets/theme-preview.png)

## New in 1.3

- **Complete message alerts**: a card appears in the top-right corner for messages in
  other channels, private chats and the server. It is paired with a notification sound,
  a flashing taskbar entry and a red dot on the matching chat tab.
- **Alerts follow the conversation**: the card no longer disappears on its own. Clicking
  it opens the conversation; switching to that channel or private-chat tab manually also
  dismisses it, so it never covers a chat you are already reading.
- **No card for the chat you are reading**: messages in the channel or private chat already
  on screen only play the notification sound; the taskbar still flashes when the client is
  in the background.
- **Floating user card**: the bottom user strip is now a rounded card floating over the
  channel list, and the away / microphone / headset buttons share one 1.5px outline style
  with a soft tinted background when active.
- The unread dots, dark title bar, bubble chat and poke alerts all carry over, with both
  the light and dark themes updated together.

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

The pack also ships two sets of linear icons, 78 synthesized sound effects, and a plugin that
rearranges the interface into a Discord-style, content-first layout. Every asset was drawn or
synthesized for this project; there are no third-party images or audio files.

## Download

Pick any archive from the [Releases](https://github.com/mmk-1010/ts3-theme-flat-white/releases) page:

| Archive | Contents |
| --- | --- |
| `Flat-White-1.3.zip` | Light theme + light icons + plugin + sounds |
| `Flat-Dark-1.3.zip` | Dark theme + dark icons + plugin + sounds |

Both archives contain the same plugin and sounds; the plugin provides the Discord-style
layout, bubble chat, message and poke alerts, the floating user card and the dark title bar.
The theme, icon pack, plugin and sound pack can also be downloaded individually.

Notification sounds prefer the Flat White Sounds pack when it is installed and fall back to
the client's default pack; the away / microphone / headset buttons do not depend on it.

## Installation

Unzip and double-click each installer in order, restarting the client after every step:

1. **Theme**: double-click `Flat White.ts3_style` or `Flat Dark.ts3_style` (installing both is
   fine too), then restart and choose `flat_white` (light) or `flat_dark` (dark) under
   **Tools → Options → Design → Theme**.
2. **Icon pack**: double-click the matching `.ts3_iconpack`, then restart and choose
   *Flat White Icons* (light) or *Flat Dark Icons* (dark) under
   **Tools → Options → Design → Icon Packs**.
3. **Plugin**: quit TS3 completely first, then double-click the `.ts3_plugin` and follow the
   prompts. On the next start, check that *Flat White UI* is ticked under
   **Tools → Options → Addons**.
4. **Sounds**: double-click the `.ts3_soundpack` and follow the prompts (running as
   administrator is needed if the client lives under Program Files), then restart and choose
   *Flat White Sounds* under **Tools → Options → Notifications → Sound Pack**.

The theme, icon pack, plugin and sounds are all optional — installing only the theme already
reskins the client. Without the plugin, the dark theme only darkens the client area and the
title bar is still drawn by the system; with it, the title bar, borders and custom-drawn
controls go dark together.

## Uninstall

- Theme and icon pack: switch back to the defaults under **Options → Design**, or delete the
  corresponding entries from the client directory (theme) and `%APPDATA%\TS3Client\gfx`
  (icon pack).
- Plugin: delete `%APPDATA%\TS3Client\plugins\flatwhite_ui_win64.dll`.
- Sounds: delete `<client directory>\sound\flatwhite`.

## Compatibility

Tested on TeamSpeak 3 **3.6.2** (Windows x64, Qt 5.15.2). The plugin is win64 only; the theme
and icon pack are pure resources and should work on any platform, but have not been tested on
macOS or Linux.
