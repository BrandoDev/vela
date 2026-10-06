# Developing Vela

This document covers the day-to-day development workflow for Vela: building, running nested or headless sessions, the repository layout, and the tools used to inspect a live compositor.

For renderer design and frame scheduling, see [renderer.md](renderer.md). For the complete test setup, see [testing.md](testing.md). Runtime overrides and debug switches are documented in [configuration.md](configuration.md).

## Build

Vela requires CMake 3.22 or newer, Ninja, a C++20 compiler, wlroots 0.20, Qt 6.5 or newer, LayerShellQt, Vulkan headers and loader, `glslc`, GBM, libdrm, FreeType, HarfBuzz, Fontconfig and PAM. librsvg is optional (app icons in the title bar). The development tools and tests also need zlib, GoogleTest and Python 3. Distribution package lists are in the [README](../README.md#build-from-source).

The normal development build is:

```sh
cmake -B build -G Ninja
cmake --build build
```

Without an explicit build type, Vela defaults to `RelWithDebInfo`.

Useful CMake options:

| Option | Default | Purpose |
|---|---:|---|
| `VELA_BUILD_COMPOSITOR` | `ON` | Build the Wayland compositor and session integration. |
| `VELA_BUILD_SHELL` | `ON` | Build the Qt Quick shell, Settings, Files and lock screen. |
| `VELA_BUILD_TOOLS` | `ON` | Build development tools and enable the CTest suites. |
| `VELA_WLROOTS` | `wlroots-0.20` | pkg-config name used for wlroots. Only wlroots 0.20 is supported. |

For example:

```sh
cmake -B build -G Ninja -DVELA_BUILD_TOOLS=ON
cmake --build build
```

## Run Vela nested

The fastest way to work on Vela without ending the current Wayland session is:

```sh
sh scripts/run-nested.sh
```

Vela opens as a window that behaves like a virtual monitor. If `build/shell/vela-shell` exists, the script starts it together with the compositor.

The host compositor normally owns the Super key, so Vela exposes Alt-based alternatives while nested. The full mapping is in [shortcuts.md](shortcuts.md).

A different build directory can be supplied as the first argument:

```sh
sh scripts/run-nested.sh build-debug
```

## Run Vela headless

For automated testing and compositor work that does not need a visible window:

```sh
sh scripts/run-headless.sh
```

This starts wlroots' headless backend, disables physical libinput devices, enables Vela's debug input protocol, and creates a virtual output. The log prints the `WAYLAND_DISPLAY` selected for the session.

A typical inspection workflow is:

```sh
sh scripts/run-headless.sh &
export WAYLAND_DISPLAY=wayland-1

build/tools/vela-input key super+Left
build/tools/vela-windows list
build/tools/vela-shot screen.png
```

The exact display name may differ; use the one printed by the compositor.

## Development tools

The tools under `build/tools/` are small clients intended for development and functional tests.

| Tool | Purpose |
|---|---|
| `vela-input` | Inject keys, clicks, drags and typed text when `VELA_DEBUG_INPUT=1`. |
| `vela-windows` | Inspect windows and compositor-visible state. |
| `vela-shot` | Capture an output or region to PNG. |
| `vela-pattern` | Produce deterministic visual test content. |
| `vela-testlock` | Test the lock path without requiring a password; it unlocks itself. |
| `vela-slowgpu` | An app whose GPU finishes each frame late (Vulkan compute job, explicit or `--implicita` sync), to check that the screen never waits for it. |

The compositor also exposes a command socket at:

```text
$XDG_RUNTIME_DIR/vela-$WAYLAND_DISPLAY.sock
```

It accepts one command per line. Headless tests use commands such as:

```text
state
test-output add 2560x1440
test-output remove HEADLESS-2
test-power off
test-power on
reload-config
```

`state` returns the compositor's visible state as JSON and is the primary assertion surface used by the functional test harness.

## Project layout

```text
compositor/src/     compositor core
  scene/            scene graph, flattening, occlusion and damage
  render/           Vulkan renderer, allocation, synchronization and shaders
  supervisor.cpp    Wayland socket holder and compositor crash recovery
  server.*          startup, focus, bindings, commands and session glue
  output.cpp        outputs, frame scheduling, refresh rate, VRR and hotplug
  toplevel.cpp      windows, popups, decorations and animations
  snap.cpp          snap layouts, Snap Assist and snap groups
  workspaces.cpp    virtual desktops
  accessibility.cpp night light, color filters, magnifier and sticky keys
  input.cpp         pointer, touchpad and gestures
  lock.cpp          session lock and idle handling

compositor/tests/   compositor unit tests
shell/              Qt Quick shell: taskbar, launcher, panels and desktop
explorer/           vela-files
settings/           vela-settings
lock/               vela-lock
session/            login entry, systemd target and portal integration
packaging/arch/     PKGBUILD and vela-update
tools/              development and test clients
tests/functional/   headless functional scenarios
scripts/            nested/headless launchers, sharpness and measurements
docs/               design and developer documentation
```

## Architecture boundaries

Vela deliberately uses wlroots for low-level Wayland, DRM/KMS, libinput and backend plumbing while keeping the parts that define the desktop's rendering behavior in Vela itself.

The compositor owns its scene graph, Vulkan renderer, frame clock, damage handling, direct-scanout policy, window-management policy and compositor-side decoration path. The shell is a separate Qt Quick process, so a shell crash does not take client windows down with it.

The renderer design is documented in detail in [renderer.md](renderer.md).

## Debugging rendering

Several environment variables are intended specifically for renderer diagnosis. The most useful ones are:

```sh
VELA_VULKAN_VALIDATION=1
VELA_DEBUG_SCANOUT=1
VELA_DEBUG_DAMAGE=1
VELA_DEBUG_SYNC=1
VELA_DEBUG_LINEAR=1
VELA_STATS=1
```

Their exact behavior is documented in [configuration.md](configuration.md).

For Vulkan work, enabling `VELA_VULKAN_VALIDATION=1` during development is strongly recommended.

## Tests

Run the complete suite with:

```sh
ctest --test-dir build --output-on-failure
```

The GPU-dependent functional and sharpness suites are part of CTest as well. They are run regularly on Vela's primary development machine with an AMD Radeon RX 9070 XT and Mesa/RADV, where they currently pass in full. GitHub-hosted CI skips those suites because it does not expose a usable DRM/Vulkan device.

See [testing.md](testing.md) for individual suites, GPU requirements and the headless harness.

## Contributing

Vela is still moving quickly. Before a large architectural change, opening an issue first is preferred so the direction can be discussed before substantial code is written.

Code, comments and commit messages are currently in Italian. Commits should be signed off with:

```sh
git commit -s
```

Contributions are accepted under GPL-3.0-or-later and the Developer Certificate of Origin.
