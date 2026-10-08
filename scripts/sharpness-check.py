#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Checks the vela-pattern window in a screenshot, bit for bit.

Each client pixel encodes its coordinates in the buffer
(R = x % 256, G = y % 256, B = (x // 256) * 16 + y // 256): every screen
pixel "votes" for the window's origin; the most voted one is taken and the
whole rectangle is compared with the expected pattern.

Usage: sharpness-check.py SCREENSHOT.png NAME [CORNER]
CORNER: the side in pixels of the corner squares not to count (the windows'
rounded corners, docs/renderer.md §8.1): the compositor clips there, and the
pixels are no longer the app's.
Prints one result line; exits with 1 if even a single pixel differs."""
import sys

import numpy as np
from PIL import Image

path, name = sys.argv[1], sys.argv[2]
corner = int(sys.argv[3]) if len(sys.argv) > 3 else 0
img = np.asarray(Image.open(path).convert("RGB")).astype(np.int64)
h, w, _ = img.shape
r, g, b = img[..., 0], img[..., 1], img[..., 2]
px = r + 256 * (b >> 4)
py = g + 256 * (b & 15)
ys, xs = np.mgrid[0:h, 0:w]
ox = xs - px
oy = ys - py

key = (ox + 100000) * 200000 + (oy + 100000)
values, counts = np.unique(key, return_counts=True)
best = values[np.argmax(counts)]
origin_x = int(best // 200000 - 100000)
origin_y = int(best % 200000 - 100000)

voters = (ox == origin_x) & (oy == origin_y)
vy, vx = np.nonzero(voters)
x0, x1, y0, y1 = vx.min(), vx.max() + 1, vy.min(), vy.max() + 1
region = voters[y0:y1, x0:x1].copy()
if corner > 0:
    # Rounded corners: clipped by the compositor, they don't count.
    region[:corner, :corner] = True
    region[:corner, -corner:] = True
    region[-corner:, :corner] = True
    region[-corner:, -corner:] = True
bad = int(region.size - region.sum())
status = "OK" if bad == 0 else "DIFFERENT"
print(f"{name:<22} window {x1 - x0}x{y1 - y0} at ({x0},{y0}), buffer origin ({origin_x},{origin_y}): "
      f"{bad} of {region.size} pixels differ  {status}")
if bad:
    by, bx = np.nonzero(~region)
    print(f"{'':<22} first errors: {list(zip((bx[:5] + x0).tolist(), (by[:5] + y0).tolist()))}")
sys.exit(1 if bad else 0)
