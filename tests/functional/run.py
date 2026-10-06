#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Le prove funzionali di Vela, per ctest.

    run.py              gli scenari (finestre, schermi, sessione)
    run.py --sharpness  la prova bit per bit di scripts/test-sharpness.sh

Serve una GPU con Vulkan 1.4: il renderer non ha ripieghi software. Senza
(per esempio sui server della CI) esce con 77, che ctest conta come prova
saltata e non fallita.
"""

import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SKIP = 77

sys.path.insert(0, HERE)
from harness import gpu_available  # noqa: E402


def main():
    if not gpu_available():
        print("No GPU (/dev/dri/renderD*): functional tests skipped.")
        return SKIP
    if "--sharpness" in sys.argv:
        # Il controllo dei pixel (scripts/sharpness-check.py) gira con il
        # python3 del PATH: è lì che servono numpy e Pillow.
        if subprocess.run(["python3", "-c", "import numpy, PIL"], capture_output=True).returncode != 0:
            print("python3 needs numpy and Pillow: sharpness test skipped.")
            return SKIP
        env = {key: value for key, value in os.environ.items() if key not in ("WAYLAND_DISPLAY", "DISPLAY")}
        env["VELA_TEST_MARK"] = "menu"
        return subprocess.run(["sh", "scripts/test-sharpness.sh"], cwd=ROOT, env=env).returncode
    suite = unittest.defaultTestLoader.discover(HERE, top_level_dir=HERE)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
