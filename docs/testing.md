# Testing Vela

Vela's test strategy covers pure compositor logic, desktop components, full headless sessions and pixel-level rendering correctness.

The goal is not only to test individual functions, but to verify properties that are visible to the user: frame timing, exact geometry at fractional scales, window state transitions, output hotplug, lock behavior, crash recovery and copy safety.

## Run everything

After building with `VELA_BUILD_TOOLS=ON` (the default):

```sh
ctest --test-dir build --output-on-failure
```

CTest includes both CPU-only and GPU-dependent suites.

## Test suites

| Suite | What it covers | GPU required |
|---|---|---:|
| **Compositor / GoogleTest** | FrameClock, vblank grid, late latching, adaptive margin, learned host latency, animation curves, output scale selection, pixel geometry, night light, color filters, sunrise/sunset and `vela.conf` parsing. | No |
| **Files** (`esplora-copie`) | Copy/move behavior, staged replacement and failure handling including a simulated full disk with `RLIMIT_FSIZE`. | No |
| **Shell** (`shell-*`) | `.desktop` `Exec=` parsing and default-app resolution through `mimeapps.list`. | No |
| **Functional** (`funzionali`) | Opening and manipulating windows, snap, maximize/restore, minimize/Alt+Tab, virtual desktops, output hotplug, per-output scale, lock/unlock, screen power, compositor crash recovery, and an app whose GPU finishes 150 ms late (explicit and implicit sync) without a missed vblank. | Yes |
| **Sharpness** (`nitidezza`) | Pixel-level checks that windows reach the expected physical pixels across fractional scales and common window states. | Yes |

The compositor currently contains 43 GoogleTest cases; the functional harness contains 26 end-to-end scenarios.

## Run a subset

Compositor unit tests:

```sh
ctest --test-dir build -R '^compositor\.' --output-on-failure
```

Files and shell tests:

```sh
ctest --test-dir build -R 'esplora-copie|shell-' --output-on-failure
```

Functional session tests:

```sh
ctest --test-dir build -R '^funzionali$' --output-on-failure
```

Sharpness only:

```sh
ctest --test-dir build -R '^nitidezza$' --output-on-failure
```

For direct invocation, the functional entry point is:

```sh
python3 tests/functional/run.py
python3 tests/functional/run.py --nitidezza
```

The sharpness check additionally needs NumPy and Pillow available to `python3`.

## GPU requirements

The functional and sharpness suites use the real Vulkan renderer. They intentionally do not fall back to software rendering.

They require:

- a usable `/dev/dri/renderD*` device;
- a GPU/driver exposing Vulkan 1.4 as required by Vela;
- NumPy and Pillow for the pixel sharpness suite.

If the harness cannot find a usable GPU, it exits with status 77. CTest marks that as **skipped**, not failed.

### Primary development hardware

The complete GPU-dependent suite is run regularly during development on:

- **GPU:** AMD Radeon RX 9070 XT
- **Driver:** Mesa/RADV
- **OS:** CachyOS

The functional and sharpness suites currently pass in full on that machine.

This gives the renderer regular real-hardware coverage during development. It is not the same as a broad hardware matrix: Intel and NVIDIA paths currently receive less repeated real-hardware testing than AMD/RADV.

## Headless functional harness

`scripts/run-headless.sh` starts a real Vela session on wlroots' headless backend. No Vela window appears on the current desktop, but apps can be opened, moved, snapped and captured exactly as in a visible session.

```sh
sh scripts/run-headless.sh &
export WAYLAND_DISPLAY=wayland-1
```

Use the actual display name printed in the log.

The test tools can then drive and inspect it:

```sh
build/tools/vela-input key super+Left
build/tools/vela-windows list
build/tools/vela-shot screen.png
```

The command socket is:

```text
$XDG_RUNTIME_DIR/vela-$WAYLAND_DISPLAY.sock
```

Important test commands include:

```text
state
test-output add 2560x1440
test-output remove HEADLESS-2
test-power off
test-power on
reload-config
```

`state` returns JSON describing the compositor-visible state. Functional scenarios assert against that state instead of relying on arbitrary sleeps.

A simplified scenario looks like:

```python
with Session(scale=1.25) as vela:
    window = vela.open_window()
    vela.keys("super+Left")
    vela.wait_for(lambda s: s.window(window)["snap"] == [0, 0, 6, 12])
```

## Sharpness testing

The sharpness suite launches the compositor headlessly at several output scales and compares captured pixels against deterministic expectations.

The standalone script can also be run directly:

```sh
sh scripts/test-sharpness.sh
```

By default it checks multiple fractional scales. Explicit scales may be passed as arguments:

```sh
sh scripts/test-sharpness.sh 1 1.25 1.5 1.75 2
```

The test exercises normal, snapped, maximized and restored window states so fractional-scale correctness is checked across window-management transitions rather than only in a static scene.

## Slow apps

`tests/functional/test_ready.py` checks that an app whose GPU is late never makes the
screen wait (see [renderer.md](renderer.md) §7.3). `tools/vela-slowgpu` commits each frame
while a Vulkan compute job, on a queue separate from the graphics one, still runs for about
150 ms; its fence becomes the buffer's acquire point (`linux-drm-syncobj-v1`) or, with
`--implicita`, the dmabuf's write fence. While the cursor moves for a second, the test reads
from `state` the missed vblanks, the frames shown and the commits held back:

| Scenario | Expectation |
|---|---|
| explicit sync | at most 2 missed vblanks, commits held back |
| implicit sync | at most 2 missed vblanks, commits held back |
| `VELA_READY_WAIT=0` | at least 20 missed vblanks: proves the test really detects a stall |

## Vulkan validation and renderer diagnosis

For renderer work, run relevant scenarios with Vulkan validation enabled:

```sh
VELA_VULKAN_VALIDATION=1 ctest --test-dir build -R 'funzionali|nitidezza' --output-on-failure
```

Additional rendering switches such as `VELA_DEBUG_DAMAGE`, `VELA_DEBUG_SYNC`, `VELA_DEBUG_LINEAR` and `VELA_DEBUG_SCANOUT` are documented in [configuration.md](configuration.md).

## Continuous integration

GitHub Actions builds Vela and runs every test that does not require a real DRM/Vulkan device. The GPU-dependent suites remain registered in CTest but are reported as skipped on hosted runners.

That means CI protects compilation and CPU-only logic on every push, while the primary development workstation currently provides the repeated real-GPU validation.

A future hardware CI matrix would broaden driver and GPU coverage rather than introduce GPU testing from scratch.
