#!/usr/bin/env python3
"""Controlla bit per bit la finestra di vela-pattern in uno screenshot.

Ogni pixel del client codifica le sue coordinate nel buffer
(R = x % 256, G = y % 256, B = (x // 256) * 16 + y // 256): ogni pixel dello
schermo "vota" per l'origine della finestra; si prende quella più votata e
si confronta l'intero rettangolo con il motivo atteso.

Uso: sharpness-check.py SCREENSHOT.png NOME [ANGOLO]
ANGOLO: il lato in pixel dei quadrati agli angoli da non contare (gli angoli
arrotondati delle finestre, docs/renderer.md §8.1): lì il compositor
ritaglia, e i pixel non sono più quelli dell'app.
Stampa una riga di risultato; esce con 1 se anche un solo pixel è diverso.
"""
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
    # Gli angoli arrotondati: ritagliati dal compositor, non contano.
    region[:corner, :corner] = True
    region[:corner, -corner:] = True
    region[-corner:, :corner] = True
    region[-corner:, -corner:] = True
bad = int(region.size - region.sum())
status = "OK" if bad == 0 else "DIVERSI"
print(f"{name:<22} finestra {x1 - x0}x{y1 - y0} in ({x0},{y0}), origine buffer ({origin_x},{origin_y}): "
      f"{bad} pixel diversi su {region.size}  {status}")
if bad:
    by, bx = np.nonzero(~region)
    print(f"{'':<22} primi errori: {list(zip((bx[:5] + x0).tolist(), (by[:5] + y0).tolist()))}")
sys.exit(1 if bad else 0)
