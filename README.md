<div align="center">

<img src="images/vela_icon.svg" width="96" alt="Vela logo">

# Vela

**A fluid, sharp and lightweight Linux desktop with the comfort of Windows 11.**

Its own Wayland compositor and Vulkan renderer, a Qt Quick shell, and a file manager and
settings app built to match. Every animation is tied to your monitor's real frames.
At 100%, 125% or 150% scaling, windows reach the screen pixel for pixel.

[![Build](https://github.com/BrandoDev/vela/actions/workflows/compila.yml/badge.svg)](https://github.com/BrandoDev/vela/actions/workflows/compila.yml)
![Status](https://img.shields.io/badge/status-alpha-orange)
[![License: GPL v3+](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)
![wlroots](https://img.shields.io/badge/wlroots-0.20-4b8bbe)
![Vulkan](https://img.shields.io/badge/Vulkan-1.4-ac162c)
![Qt](https://img.shields.io/badge/Qt-6-41cd52)
![C++](https://img.shields.io/badge/C%2B%2B-20-00599c)

[Screenshots](#screenshots) · [Features](#features) · [Install](#install) ·
[Build](#build-from-source) · [How it works](#how-it-works) · [Roadmap](#roadmap)

<br>

<img src="docs/screenshots/start.webp" alt="The Vela desktop with the Start menu open over the file manager" width="100%">

</div>

---

## Why Vela

Windows 11 got the everyday desktop right: Start, Snap layouts, Alt+Tab, Win+V. Linux
desktops can be faster and lighter. Vela puts the two together, with no shortcuts
taken on quality.

<table>
<tr>
<td width="33%" valign="top">

### Fluid
Animations advance on every real vblank, never on timers: at 180 Hz they take 180 steps
per second. Late latching draws as late as safely possible before each vblank.
Fullscreen games go straight to the display (direct scanout). Variable refresh rate and
tearing are supported for games.

</td>
<td width="33%" valign="top">

### Sharp
Fractional scaling is pixel-exact. When an app renders at the screen's scale, its buffer
is copied 1:1 with no filtering, even at 125% or 175%. An automated test checks this bit
for bit after opening, snapping, maximizing and restoring a window.

</td>
<td width="33%" valign="top">

### Light
The compositor uses **17 MiB** and **0.1% CPU** at idle, waking up 3–7 times a second.
The file manager shows its first frame **about 100 ms** after launch, even in folders
with thousands of files.

</td>
</tr>
</table>

> [!NOTE]
> **Vela is alpha software.** You can already pick it at the login screen and use it
> every day: compositor, shell, file manager, settings, lock screen. Some behavior
> (gamma, tearing, touchpad gestures) has only been verified on real hardware by hand,
> not in the automated tests. **The interface is currently in Italian**; translations
> are planned.

## Screenshots

<table>
<tr>
<td width="50%"><img src="docs/screenshots/snap.webp" alt="File manager and Settings snapped side by side"><br><sub><b>Snap</b>: windows side by side with Win+arrows or by dragging to an edge or corner.</sub></td>
<td width="50%"><img src="docs/screenshots/task-view.webp" alt="Task View with two virtual desktops"><br><sub><b>Task View</b> (Win+Tab): live window previews and virtual desktops.</sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/screenshots/files.webp" alt="The Vela file manager showing a Pictures folder"><br><sub><b>Files</b>: tabs, breadcrumbs, thumbnails and previews; first frame in about 100 ms.</sub></td>
<td width="50%"><img src="docs/screenshots/alt-tab.webp" alt="Alt+Tab switcher with window previews"><br><sub><b>Alt+Tab</b> with previews, minimized windows included.</sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/screenshots/light.webp" alt="Vela in light mode"><br><sub><b>Light and dark</b> modes for the shell and for apps, switched instantly.</sub></td>
<td width="50%"><img src="docs/screenshots/lock.webp" alt="The Vela lock screen"><br><sub><b>Lock screen</b>: built on ext-session-lock, so a crash never reveals the desktop.</sub></td>
</tr>
</table>

<table>
<tr>
<td width="34%" valign="top"><img src="docs/screenshots/snap-layouts.webp" alt="Snap layouts flyout over the maximize button"><br><sub><b>Snap layouts</b>: hover the maximize button or press Win+Z.</sub></td>
<td width="33%" valign="top"><img src="docs/screenshots/context-menu.webp" alt="Windows 11 style desktop context menu"><br><sub><b>Context menus</b> modeled on Windows 11, with KDE service menus under "Show more options".</sub></td>
<td width="33%" valign="top"><img src="docs/screenshots/panels.webp" alt="Quick settings and the notification center"><br><sub><b>Quick settings</b> (Win+A) and the <b>notification center</b> (Win+N).</sub></td>
</tr>
</table>

## Features

### Windows and multitasking
- **Snap like Windows 11**: halves, quarters, and six snap layouts (thirds, 2/3 + 1/3,
  and more). **Snap Assist** offers your other windows for the empty space. Windows you
  arrange this way form a **snap group** that the taskbar shows and restores together.
- **Task View and virtual desktops** (Win+Tab): drag windows between desktops,
  rename and reorder desktops, and switch with Win+Ctrl+←/→ and a sliding animation.
  Your desktops are remembered across sessions.
- **Alt+Tab** with live previews, including minimized windows. A quick Alt+Tab just
  switches without showing the panel.
- **Window animations**: windows fade and rise as they open, shrink away as they close,
  and fly into their taskbar button when minimized.
- **Rounded corners and soft shadows**, drawn by the compositor and sharp at every
  scale. They turn off when a window is maximized, snapped or fullscreen.
- **Vela's own title bar** for apps that accept server-side decorations (Qt, KDE, X11),
  with the app icon and a Mica tint taken from your wallpaper. Apps that draw their own
  title bar, such as Chromium with tabs on top, keep theirs.
- Invisible resize borders, Super+drag to move, Super+right-drag to resize, and a window
  menu (Alt+Space) with keyboard move and resize.

### Shell
- **Taskbar**: pinned and running apps centered (or left-aligned), drag to reorder, jump
  lists with recent files, **hover previews**, a system tray (StatusNotifierItem), and
  one taskbar per monitor.
- **Start menu**: instant search, pinned apps, keyboard navigation, and the Win+X menu.
- **Quick settings** (Win+A): Wi-Fi network picker, Bluetooth, airplane mode, power
  mode, Night light, accessibility, brightness, volume and battery.
- **Notifications** with actions, a **notification center** with a calendar (Win+N),
  and Do Not Disturb.
- **Clipboard history** (Win+V): text and images, pinned items that survive a reboot,
  and one-click paste into the focused app.
- **Snipping tool** (Win+Shift+S or PrtSc): capture a rectangle, a window or a full
  screen, straight to the clipboard and to `Pictures/Screenshots`.
- **Desktop icons** with thumbnails, drag and drop to and from apps, Properties, and
  undo. Also **Run** (Win+R) and **Show desktop** (Win+D).
- **Light and dark** modes and a Windows 11 accent palette, applied to KDE and GTK apps
  too.

### Files (`vela-files`)
- Tabs, breadcrumb address bar, search in subfolders, Details and icon views (remembered
  per folder), and a **preview pane** (images, text, video and PDF thumbnails).
- Background copy and move with progress and conflict resolution, drag and drop
  everywhere, ZIP/7z/TAR, shortcuts, Favorites, Trash with restore.
- **Unmounted drives** (udisks) in This PC: double-click to mount, eject from the menu.

### Settings (`vela-settings`)
- **Displays**: drag-and-drop arrangement, scale, resolution, refresh rate, rotation, and
  "Keep these settings?" with automatic revert. Also **Night light**, variable refresh
  rate and tearing.
- **Sound, Notifications, Power, Clipboard**, Bluetooth and devices, **mouse and
  touchpad** (tap to click, scroll direction, three- and four-finger gestures), and
  Network.
- **Personalization**: wallpaper, colors, taskbar. **Apps**: installed and default apps.
  **Time and language**: date and time, keyboard layouts.
- **Accessibility**: magnifier, color filters, sticky keys.

### Graphics and gaming
- Vela's **own scene graph and Vulkan 1.4 renderer**: no `wlr_scene`, no GL fallback.
- **Live acrylic blur** (dual Kawase) behind the shell, exposed to apps through the
  standard `ext-background-effect-v1` protocol.
- **Direct scanout** for fullscreen apps, **VRR** (FreeSync/G-Sync, automatic for
  fullscreen apps by default), **tearing control** for games, explicit sync
  (`linux-drm-syncobj-v1`), relative pointer and pointer constraints.
- **Night light** in the monitor's gamma LUT, so screenshots stay untinted and games keep
  direct scanout. It can follow sunset and sunrise, computed offline from your time zone.
- Picks the **highest refresh rate** at native resolution, even when the monitor
  advertises 60 Hz as "preferred".
- **Xwayland** on demand for Steam, games and older apps.

### Accessibility and input
- Magnifier (Win+Plus), grayscale and color-blindness filters (Win+Ctrl+C), and sticky
  keys.
- Windows-style touchpad defaults and gestures: three or four fingers up for Task View,
  down for the desktop, sideways to switch apps or desktops.

### A session you can rely on
- **Crash recovery.** A tiny supervisor holds the Wayland socket. If the compositor
  crashes, a new one starts on the same socket and Qt/KDE apps reconnect and reappear,
  as with KWin. If the screen was locked, Vela comes back **locked**. Three crashes in a
  minute end the session cleanly.
- If the shell crashes, the compositor restarts it, and your windows stay open.
- A **secure lock screen** (`ext-session-lock-v1`): if the locker dies, the screen stays
  black and locked, and the locker is restarted.
- **Integrates with KDE's services**: portals, polkit agent, the KDE wallet (so Chrome,
  Brave and VS Code keep their logins), color schemes and icon themes.

## Install

Vela needs a GPU with **Vulkan 1.4**: AMD or Intel with Mesa ≥ 25.0, or NVIDIA with the
proprietary driver ≥ 570 or NVK. Once installed, **Vela** appears on the login screen
(SDDM or Plasma Login) next to Plasma.

### Arch Linux, CachyOS, EndeavourOS

Vela installs as a proper `vela-git` package:

```sh
git clone https://github.com/BrandoDev/vela.git
cd vela/packaging/arch
VELA_GIT_URL=file://$PWD/../.. makepkg -si
```

Then keep it up to date with **`vela-update`**. It fetches new commits, shows what
changed, and installs the new version. Every push to `main` is built on GitHub
Actions, so if that package is ready (and the [GitHub CLI](https://cli.github.com/) is
logged in), `vela-update` offers to download it instead of compiling. Otherwise it
builds the package locally.

```sh
vela-update                 # update if something new is available
vela-update --controlla     # only check, and list the changes
vela-update --scarica       # use GitHub's package without asking
vela-update --compila       # always build locally
vela-update --jobs 2        # fewer parallel compile jobs
```

The last three packages stay in `~/.cache/vela-update` for easy rollback.

### Other distributions

Build from source (below), then:

```sh
sudo cmake --install build   # into /usr/local, plus /etc/pam.d and /etc/xdg
```

If the session doesn't show up, your display manager may not look in `/usr/local`:

```sh
sudo ln -s /usr/local/share/wayland-sessions/vela.desktop /usr/share/wayland-sessions/
```

## Build from source

You need CMake ≥ 3.22, a C++20 compiler, **wlroots 0.20**, Qt ≥ 6.5, LayerShellQt, the
Vulkan headers, `glslc`, GBM and libdrm.

<details>
<summary><b>Arch / CachyOS / EndeavourOS</b></summary>

```sh
sudo pacman -S --needed base-devel cmake pkgconf wlroots0.20 wayland-protocols \
    libxkbcommon pixman libinput qt6-declarative qt6-svg qt6-wayland layer-shell-qt \
    vulkan-headers vulkan-icd-loader shaderc mesa libdrm librsvg
```
</details>

<details>
<summary><b>Fedora</b></summary>

```sh
sudo dnf install cmake gcc-c++ wlroots-devel wayland-devel wayland-protocols-devel \
    libxkbcommon-devel pixman-devel libinput-devel qt6-qtdeclarative-devel \
    qt6-qtsvg-devel qt6-qtwayland-devel layer-shell-qt-devel \
    vulkan-headers vulkan-loader-devel glslc mesa-libgbm-devel libdrm-devel
```
</details>

<details>
<summary><b>openSUSE Tumbleweed</b></summary>

```sh
sudo zypper install cmake gcc-c++ wlroots-devel wayland-protocols-devel \
    libxkbcommon-devel pixman-devel libinput-devel qt6-declarative-devel \
    qt6-svg-devel qt6-waylandclient-devel layer-shell-qt6-devel \
    vulkan-headers vulkan-devel shaderc libgbm-devel libdrm-devel
```
</details>

```sh
cmake -B build -G Ninja
cmake --build build
```

With wlroots 0.19 instead of 0.20, configure with `-DVELA_WLROOTS=wlroots-0.19`.

### Try it without logging out

From a running Wayland session (Plasma, for example), Vela opens in a window like a
virtual monitor:

```sh
sh scripts/run-nested.sh
```

The host desktop keeps the Super key, so every shortcut also has an Alt variant (below).
These Alt variants are only active when nested. In a real session, Alt+letter stays with
your apps for their menu mnemonics.

## Keyboard shortcuts

| Action | Shortcut | When nested |
|---|---|---|
| Start menu | <kbd>Super</kbd> | <kbd>Alt</kbd>+<kbd>S</kbd> |
| Task View | <kbd>Super</kbd>+<kbd>Tab</kbd> | <kbd>Alt</kbd>+<kbd>W</kbd> |
| Switch windows | <kbd>Alt</kbd>+<kbd>Tab</kbd> | <kbd>Alt</kbd>+<kbd>J</kbd> |
| Snap left / right | <kbd>Super</kbd>+<kbd>←</kbd> / <kbd>→</kbd> | <kbd>Alt</kbd>+<kbd>←</kbd> / <kbd>→</kbd> |
| Maximize / restore, then minimize | <kbd>Super</kbd>+<kbd>↑</kbd> / <kbd>↓</kbd> | <kbd>Alt</kbd>+<kbd>M</kbd> |
| Snap layouts | <kbd>Super</kbd>+<kbd>Z</kbd> | <kbd>Alt</kbd>+<kbd>Z</kbd> |
| Previous / next desktop | <kbd>Super</kbd>+<kbd>Ctrl</kbd>+<kbd>←</kbd> / <kbd>→</kbd> | <kbd>Alt</kbd>+<kbd>Ctrl</kbd>+<kbd>←</kbd> / <kbd>→</kbd> |
| New / close desktop | <kbd>Super</kbd>+<kbd>Ctrl</kbd>+<kbd>D</kbd> / <kbd>F4</kbd> | <kbd>Alt</kbd>+<kbd>Ctrl</kbd>+<kbd>D</kbd> |
| Show desktop | <kbd>Super</kbd>+<kbd>D</kbd> | <kbd>Alt</kbd>+<kbd>D</kbd> |
| Files / Settings | <kbd>Super</kbd>+<kbd>E</kbd> / <kbd>I</kbd> | <kbd>Alt</kbd>+<kbd>E</kbd> / <kbd>I</kbd> |
| Quick settings / notifications | <kbd>Super</kbd>+<kbd>A</kbd> / <kbd>N</kbd> | <kbd>Alt</kbd>+<kbd>A</kbd> / <kbd>N</kbd> |
| Clipboard history | <kbd>Super</kbd>+<kbd>V</kbd> | |
| Snipping tool | <kbd>Super</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>, <kbd>PrtSc</kbd> | <kbd>PrtSc</kbd> |
| Run / Win+X menu | <kbd>Super</kbd>+<kbd>R</kbd> / <kbd>X</kbd> | <kbd>Alt</kbd>+<kbd>R</kbd> / <kbd>X</kbd> |
| Terminal | <kbd>Super</kbd>+<kbd>Enter</kbd> | <kbd>Alt</kbd>+<kbd>Enter</kbd> |
| Lock | <kbd>Super</kbd>+<kbd>L</kbd> | <kbd>Alt</kbd>+<kbd>L</kbd> |
| Close window / window menu | <kbd>Alt</kbd>+<kbd>F4</kbd> / <kbd>Alt</kbd>+<kbd>Space</kbd> | <kbd>Alt</kbd>+<kbd>Q</kbd> / <kbd>Alt</kbd>+<kbd>Space</kbd> |
| Next keyboard layout | <kbd>Super</kbd>+<kbd>Space</kbd> | |
| Magnifier in / out / off | <kbd>Super</kbd>+<kbd>+</kbd> / <kbd>-</kbd> / <kbd>Esc</kbd> | |
| Color filters | <kbd>Super</kbd>+<kbd>Ctrl</kbd>+<kbd>C</kbd> | |
| Move / resize any window | <kbd>Super</kbd>+drag / right-drag | |
| Switch console | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F1</kbd>…<kbd>F12</kbd> | |
| Quit Vela | <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>Esc</kbd> | <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>Esc</kbd> |

## How it works

```mermaid
flowchart TB
    DM["Login screen<br/>(SDDM, Plasma Login)"] --> S["vela-session"]
    S --> SUP["Supervisor<br/><i>holds the Wayland socket,<br/>restarts the compositor</i>"]
    SUP --> C["vela-compositor<br/>C++20 · wlroots 0.20<br/>own scene graph · Vulkan 1.4 renderer"]
    C -- "layer-shell · foreign-toplevel<br/>ext-background-effect · image capture" --> SH["vela-shell<br/>Qt Quick: taskbar, Start, panels, desktop"]
    C --> APPS["Apps<br/>Wayland and Xwayland"]
    C --> F["vela-files"]
    C --> SET["vela-settings"]
    C -- "ext-session-lock" --> L["vela-lock"]
    C --> HW["DRM/KMS · libinput · GBM"]
```

- **The compositor** owns the screen. It runs its own scene graph and a Vulkan 1.4
  renderer: SDF rounded corners, analytic two-layer shadows, dual Kawase blur, late
  latching, direct scanout, explicit sync, and night light in the gamma LUT. It also
  implements `wlr_renderer`, so wlroots' cursor, screencopy and shm uploads share the same
  device. The full design is in [docs/renderer.md](docs/renderer.md).
- **The shell** is a separate Qt Quick process using LayerShellQt. It talks to the
  compositor through standard Wayland protocols, plus a small command socket (for
  example, Super opens Start). A shell crash never takes your windows down.
- **Motion design lives in one place**: `compositor/src/motion.hpp` and
  `shell/qml/Theme.qml` share the same cubic-bezier(0, 0, 0.2, 1) curve.
- **The session** declares itself `XDG_CURRENT_DESKTOP=Vela:KDE`. Qt/KDE apps use KDE's
  theme, and browsers and Electron apps use KDE's wallet, exactly as inside Plasma. It
  connects to systemd (`vela-session.target`), starts the portals and the polkit agent,
  and keeps the last two session logs in `~/.local/state/vela/`.

## Performance

Vela keeps itself honest with measurable goals:

| Goal | Target | Today |
|---|---|---|
| Start menu | visible on the next frame after the key | ✔ |
| Context menus | visible on the next frame after the click | ✔ |
| Files cold start | first frame within 150 ms | **104 ms** (median of 5) |
| Animations | always driven by real vblanks, never timers | ✔ |
| Compositor at idle | no CPU, few wakeups | **17 MiB**, **0.1% CPU**, 3–7 wakeups/s |
| Shell at idle | no CPU, few wakeups | **143 MiB**, **0.1% CPU**, 1–4 wakeups/s |

Numbers come from `scripts/measure.sh`, which samples PSS memory, idle CPU, wakeups per
second and, on battery, average power draw over 30 seconds. `--json` prints one line to
compare over time.

## Development

Vela is developed with automated tests that drive a real session without showing
anything on screen. `scripts/run-headless.sh` starts Vela on wlroots' headless backend.
The tools in `build/tools/` then use it like a person would:

```sh
sh scripts/run-headless.sh &            # the log prints the WAYLAND_DISPLAY
export WAYLAND_DISPLAY=wayland-1
build/tools/vela-input key super+Left   # keys, clicks, drags, typing
build/tools/vela-windows list           # windows and their state
build/tools/vela-shot screen.png        # PNG screenshot of the screen or a region
```

- **Sharpness test**: `sh scripts/test-sharpness.sh` opens a pattern window whose every
  pixel encodes its own coordinates. At 100–200% scaling, the window must reach the
  screen bit for bit when opened, snapped, maximized and restored.
- **Multiple monitors without monitors**: in a headless session, `test-output add
  2560x1440` and `test-output remove HEADLESS-2` on the command socket hot-plug virtual
  outputs, each with its own scale.
- **Continuous integration**: every push builds the whole project in an Arch Linux
  container; `main` also builds the package.

<details>
<summary><b>Environment variables</b></summary>

| Variable | Effect |
|---|---|
| `VELA_SCALE` | Output scale, e.g. `1.25`, or per output: `DP-1=1.5,HDMI-A-1=1`. Without it, Vela picks a scale from each monitor's DPI, in 25% steps like Windows |
| `VELA_TERMINAL` | Terminal for Super+Enter (default: konsole) |
| `VELA_VRR=1` / `0` | Variable refresh rate always on / never |
| `VELA_TEARING=0` | No tearing, even for games that ask for it |
| `VELA_SCANOUT=0` | Disable direct scanout (for comparison) |
| `VELA_DEBUG_SCANOUT=1` | Log why a fullscreen app isn't scanned out directly |
| `VELA_LATCH=0` | Draw right at vblank instead of late latching |
| `VELA_LATCH_MARGIN` | Minimum late-latching margin in ms (default 1, grows on its own after a late frame) |
| `VELA_REALTIME=0` | No realtime scheduling for the compositor's main thread |
| `VELA_SCREEN_OFF` | Minutes of inactivity before locking and turning screens off (default 10, 0 = never) |
| `VELA_LOCK_ON_IDLE=0` | Turn screens off when idle without locking |
| `VELA_LOCK` | Locker to use instead of `vela-lock` (e.g. `swaylock`) |
| `VELA_NATURAL_SCROLL=0` | Classic touchpad scrolling |
| `VELA_WALLPAPER` | Wallpaper image (SVG, PNG, JPEG...) |
| `VELA_ICON_THEME` | Icon theme if Qt can't find one (default breeze-dark) |
| `VELA_STATS=1` | Every 2 s, per output: fps, frame cost, draw-to-light latency, missed vblanks |
| `VELA_OUTPUT_SIZE` | Resolution for nested/headless outputs, e.g. `1920x1080@144` |
| `VELA_VULKAN_VALIDATION=1` | Enable the Vulkan validation layer |
| `VELA_DEBUG_INPUT=1` | Virtual pointer and keyboard for `vela-input` (off by default) |
| `VELA_DEBUG=1` | Verbose wlroots logging |
| `XKB_DEFAULT_LAYOUT` | Keyboard layout (e.g. `it`) |

</details>

<details>
<summary><b>Project layout</b></summary>

```
compositor/src/   the compositor
  scene/, render/   scene graph and Vulkan renderer
  supervisor.cpp    crash recovery: socket holder and restarts
  server.*          startup, focus, bindings, commands, session
  output.cpp        outputs, refresh rate, frames, VRR, hotplug
  toplevel.cpp      windows, popups, decorations, animations
  snap.cpp          halves, quarters, layouts, Snap Assist, snap groups
  workspaces.cpp    virtual desktops
  accessibility.cpp night light, color filters, magnifier, sticky keys
  input.cpp         pointer, touchpad, gestures
  lock.cpp          lock screen and idle
shell/            taskbar, Start, panels, desktop (C++ models + QML)
explorer/         vela-files, the file manager
settings/         vela-settings, one QML page per settings page
lock/             vela-lock
session/          login entry, systemd target, portals
packaging/arch/   PKGBUILD and vela-update
tools/            vela-shot, vela-input, vela-windows, vela-pattern
scripts/          nested and headless runs, sharpness test, measurements
docs/             design documents and screenshots
```
</details>

## Roadmap

**Done**
- [x] **A real session**: login entry, systemd and D-Bus, portals, polkit, Xwayland,
  lock screen, idle, notifications, system tray.
- [x] **The look**: own Vulkan renderer, live acrylic blur, rounded corners and
  shadows, Vela's title bar, Windows 11 context menus everywhere.
- [x] **Replacing Plasma, day to day**: Settings, Files, desktop icons, virtual desktops,
  Task View, Snap layouts and Snap Assist, light mode.
- [x] **Gaming and displays**: tearing control, VRR, direct scanout, multi-monitor
  hotplug with per-output scale.
- [x] **Daily comfort**: clipboard history, snipping tool, touchpad gestures, mouse and
  touchpad settings.
- [x] **Reliability**: compositor crash recovery, package and `vela-update`, CI.

**Next**
- [ ] Input methods (`text-input`, `input-method`) and an emoji panel (Win+.).
- [ ] An Updates page in Settings, with a notification when a new version is out.
- [ ] Start menu: Recommended files, and search across files and settings.
- [ ] Screen recording in the snipping tool.
- [ ] Translations (English first).
- [ ] Vela's own login screen.
- [ ] HDR and color management.

## Contributing

Vela is a young project and moves fast. Issues and ideas are welcome. Before larger
changes, open an issue to talk about it. Code, comments and commit messages are in
Italian for now. Please sign off your commits (`git commit -s`, the
[Developer Certificate of Origin](https://developercertificate.org/)): contributions
are accepted under the project's license, GPL-3.0-or-later.

## Acknowledgements

Vela stands on the shoulders of [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots),
[Qt](https://www.qt.io/), [KDE Frameworks and Plasma](https://kde.org/) (LayerShellQt,
Breeze icons, the portals and the wallet), [Mesa](https://mesa3d.org/) and the Wayland
protocols community.

## License

Vela is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License](LICENSE) as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

Every source file and the artwork in `images/` carry an
`SPDX-License-Identifier: GPL-3.0-or-later` header. The Wayland protocol files in
`protocols/` keep their original MIT-style licenses.
