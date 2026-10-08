# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Finestre come le usa una persona: aprire, agganciare, massimizzare,
ridurre a icona, Alt+Tab, desktop virtuali, chiudere. Ogni prova parte da
una sessione nuova; gli stati si leggono dal compositor (richiesta "state")."""

import time
import unittest

from harness import Session, tool

LEFT_HALF = [0, 0, 6, 12]
RIGHT_HALF = [6, 0, 12, 12]
TOP_LEFT = [0, 0, 6, 6]


def physical(window, scale):
    """La geometria della finestra in pixel dello schermo."""
    return tuple(round(window[key] * scale) for key in ("x", "y", "w", "h"))


class Windows(unittest.TestCase):
    scale = 1.0

    def setUp(self):
        self.vela = Session(scale=self.scale)
        self.vela.start()

    def tearDown(self):
        self.vela.stop()

    def test_open_window_is_focused_and_centered(self):
        window = self.vela.open_window(400, 300)
        state = self.vela.wait_for(lambda s: s["focused"] == window, what="the new window has focus")
        w = state.window(window)
        out = state.output
        self.assertEqual((w["w"], w["h"]), (400, 300))
        # Al centro dello schermo (a meno di un'unità).
        self.assertAlmostEqual(w["x"] + w["w"] / 2, out["x"] + out["w"] / 2, delta=1)
        self.assertFalse(w["maximized"] or w["minimized"] or w["fullscreen"])

    def test_snap_left_right_and_quarters(self):
        window = self.vela.open_window()
        out = self.vela.state().output
        self.vela.keys("super+Left")
        state = self.vela.wait_for(lambda s: s.window(window)["snap"] == LEFT_HALF, what="snapped left")
        w = state.window(window)
        self.assertEqual((w["x"], w["y"]), (out["x"], out["y"]))
        self.assertAlmostEqual(w["w"], out["w"] / 2, delta=1)
        self.assertAlmostEqual(w["h"], out["h"], delta=1)
        self.vela.keys("super+Right", "super+Right")
        self.vela.wait_for(lambda s: s.window(window)["snap"] == RIGHT_HALF, what="snapped right")
        # Da una metà, Win+↑ porta al quarto in alto.
        self.vela.keys("super+Left", "super+Left", "super+Up")
        self.vela.wait_for(lambda s: s.window(window)["snap"] == TOP_LEFT, what="top-left quarter")

    def test_maximize_and_restore_keeps_geometry(self):
        window = self.vela.open_window(500, 350)
        before = self.vela.wait_for(lambda s: s["focused"] == window).window(window)
        self.vela.keys("super+Up")
        state = self.vela.wait_for(lambda s: s.window(window)["maximized"], what="maximized")
        w, out = state.window(window), state.output
        self.assertEqual((w["x"], w["y"], w["w"], w["h"]), (out["x"], out["y"], out["w"], out["h"]))
        self.vela.keys("super+Down")
        state = self.vela.wait_for(lambda s: not s.window(window)["maximized"], what="restored")
        after = state.window(window)
        self.assertEqual((after["x"], after["y"], after["w"], after["h"]),
                         (before["x"], before["y"], before["w"], before["h"]))

    def test_minimize_and_alt_tab_restores(self):
        first = self.vela.open_window()
        second = self.vela.open_window()
        self.vela.wait_for(lambda s: s["focused"] == second)
        self.vela.keys("super+Down")  # non massimizzata: si riduce a icona
        state = self.vela.wait_for(lambda s: s.window(second)["minimized"], what="minimized")
        self.assertEqual(state["focused"], first)  # il fuoco passa alla finestra sotto
        # Alt+Tab ripristina anche le finestre ridotte a icona, come su Windows.
        self.vela.input("keydown", "alt", "key", "tab", "sleep", "100", "keyup", "alt")
        state = self.vela.wait_for(lambda s: s["focused"] == second and not s.window(second)["minimized"],
                                   what="restored with Alt+Tab")

    def test_alt_tab_switches_to_previous_window(self):
        first = self.vela.open_window()
        second = self.vela.open_window()
        self.vela.wait_for(lambda s: s["focused"] == second)
        self.vela.input("keydown", "alt", "key", "tab", "sleep", "100", "keyup", "alt")
        self.vela.wait_for(lambda s: s["focused"] == first, what="Alt+Tab: the previous window")
        self.vela.input("keydown", "alt", "key", "tab", "sleep", "100", "keyup", "alt")
        self.vela.wait_for(lambda s: s["focused"] == second, what="Alt+Tab again: back again")

    def test_virtual_desktops(self):
        window = self.vela.open_window()
        self.vela.keys("super+ctrl+d")  # un desktop nuovo, e ci si va
        state = self.vela.wait_for(lambda s: s["workspaces"] == 2 and s["workspace"] == 1, what="desktop 2")
        self.assertEqual(state.window(window)["workspace"], 0)  # la finestra resta sul primo
        other = self.vela.open_window()
        self.assertEqual(self.vela.state().window(other)["workspace"], 1)
        self.vela.keys("super+ctrl+Left")
        state = self.vela.wait_for(lambda s: s["workspace"] == 0, what="desktop 1 again")
        self.assertEqual(state["focused"], window)
        # Chiudere il desktop 2: la sua finestra passa al desktop accanto.
        self.vela.command("workspace close 1")
        state = self.vela.wait_for(lambda s: s["workspaces"] == 1, what="a single desktop")
        self.assertEqual(state.window(other)["workspace"], 0)

    def test_system_prompt_keeps_focus_over_new_windows(self):
        # Il portachiavi chiede la password mentre l'app che l'ha chiesto apre
        # la sua finestra: il dialogo resta davanti e con la tastiera.
        prompt = self.vela.open_window(300, 200, app_id="org.kde.ksecretd")
        self.vela.wait_for(lambda s: s["focused"] == prompt)
        app = self.vela.open_window()
        state = self.vela.wait_for(lambda s: s.has(app), what="the app's window")
        self.assertEqual(state["focused"], prompt)
        self.assertEqual(state.windows[0]["id"], prompt)  # in cima
        # Chiuso il dialogo, la tastiera passa all'app.
        self.vela.keys("alt+F4")
        self.vela.wait_for(lambda s: not s.has(prompt) and s["focused"] == app, what="focus to the app")

    def test_close_with_alt_f4(self):
        window = self.vela.open_window()
        self.vela.wait_for(lambda s: s["focused"] == window)
        self.vela.keys("alt+F4")
        self.vela.wait_for(lambda s: not s.has(window), what="window closed")


    def test_popup_stays_on_screen(self):
        # Un menu che uscirebbe dallo schermo a destra scivola dentro
        # (popup.c); uno che ci sta resta dove l'app lo vuole.
        magenta = (255, 0, 255)
        for anchor_x, inside in ((1500, False), (40, True)):
            window = self.vela.open_window(300, 200, command=[tool("vela-pattern"), "--popup", f"{anchor_x},50",
                                                               "300", "200"])
            w = self.vela.state().window(window)
            out = self.vela.state().output
            x = w["x"] + anchor_x if inside else out["x"] + out["w"] - 200
            y = w["y"] + 50
            time.sleep(0.4)
            image = self.vela.pixels()
            at = lambda lx, ly: image.at(round(lx * self.scale), round(ly * self.scale))
            self.assertEqual(at(x + 100, y + 75), magenta, f"popup anchored at {anchor_x}")
            self.assertEqual(at(x + 3, y + 3), magenta)
            self.assertEqual(at(x + 196, y + 146), magenta)
            self.assertNotEqual(at(x - 4, y + 75), magenta)
            self.vela.keys("alt+F4")
            self.vela.wait_for(lambda s: not s.has(window), what="window closed")

class WindowsAt125(Windows):
    """Le stesse prove a scala frazionaria."""
    scale = 1.25

    def test_snapped_halves_cover_exact_pixels(self):
        window = self.vela.open_window()
        out = self.vela.state().output
        width = round(out["w"] * self.scale)
        self.vela.keys("super+Left")
        state = self.vela.wait_for(lambda s: s.window(window)["snap"] == LEFT_HALF)
        x, _, w, _ = physical(state.window(window), self.scale)
        self.assertEqual((x, w), (0, width // 2))


if __name__ == "__main__":
    unittest.main()
