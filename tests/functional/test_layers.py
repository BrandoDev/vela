# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""I pezzi della shell (wlr-layer-shell) con vela-panel al posto della shell
Qt: lo sfondo, la taskbar che riserva il suo spazio e sfoca ciò che ha dietro
(ext-background-effect, docs/renderer.md §8.3); e il danno: un frame
disegnato solo dove qualcosa è cambiato deve essere identico a uno
ridisegnato per intero (VELA_DEBUG_DAMAGE=1)."""

import subprocess
import time
import unittest

from harness import Session, tool

BAR = 48


def shell(vela, blur=True):
    """Sfondo e taskbar, e il tempo di comparire."""
    for args in (["wallpaper"], ["taskbar"] + (["--blur"] if blur else [])):
        vela.clients.append(subprocess.Popen([tool("vela-panel"), *args], env=vela.client_env))
    vela.wait_for(lambda s: s.output["frames"] > 0)
    time.sleep(0.6)


def drag_over_the_taskbar(vela):
    """Una finestra decorata trascinata a metà sopra la taskbar; lo schermo alla fine."""
    vela.open_window(401, 301)
    window = vela.open_window(300, 200, decorated=True)
    w = vela.state().window(window)
    out = vela.state().output
    x, y = w["x"] + 60, w["y"] + 10
    steps = ["move", x, y, "keydown", "super", "down"]
    for i in range(1, 21):
        steps += ["move", x + i * 7, y + i * (out["h"] - 120 - y) // 20, "sleep", "10"]
    vela.input(*steps, "up", "keyup", "super", "move", 5, 5)
    time.sleep(0.5)
    return vela.pixels()


class Layers(unittest.TestCase):
    def test_taskbar_reserves_its_space(self):
        with Session() as vela:
            shell(vela)
            window = vela.open_window()
            vela.keys("super+Up")
            state = vela.wait_for(lambda s: s.window(window)["maximized"], what="maximized")
            w, out = state.window(window), state.output
            self.assertEqual((w["y"], w["h"]), (out["y"], out["h"] - BAR))

    def test_taskbar_blurs_what_is_behind(self):
        with Session() as vela:
            shell(vela, blur=False)
            plain = vela.pixels()
        with Session() as vela:
            shell(vela, blur=True)
            blurred = vela.pixels()
        # Lo sfondo ha bande nette ogni 24 righe: dietro la barra solo
        # sfocato diventa una sfumatura, con molti colori.
        column = lambda image: {image.at(900, y) for y in range(image.height - BAR, image.height)}
        self.assertLessEqual(len(column(plain)), 4)
        self.assertGreaterEqual(len(column(blurred)), 12)
        # Fuori dalla barra niente cambia.
        self.assertEqual(plain.rows[:plain.height - BAR - 40], blurred.rows[:blurred.height - BAR - 40])


class Damage(unittest.TestCase):
    def check(self, scale):
        with Session(scale=scale) as vela:
            shell(vela)
            partial = drag_over_the_taskbar(vela)
        with Session(scale=scale, env={"VELA_DEBUG_DAMAGE": "1"}) as vela:
            shell(vela)
            full = drag_over_the_taskbar(vela)
        different = sum(1 for y in range(full.height) if partial.rows[y] != full.rows[y])
        self.assertEqual(different, 0, f"{different} rows differ from a full redraw")

    def test_partial_redraw_equals_full_redraw(self):
        self.check(1.0)

    def test_partial_redraw_equals_full_redraw_at_125(self):
        self.check(1.25)


if __name__ == "__main__":
    unittest.main()
