# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Il puntatore e la tastiera sulle finestre (interact.c, bindings.c,
snap.c): i bordi invisibili per ridimensionare, "Sposta" e "Ridimensiona"
da tastiera, il menu della finestra (clic destro sul titolo, Alt+Spazio),
lo snap trascinando contro il bordo con Snap Assist, i layout di snap
(Win+Z) e i gruppi di snap. La shell è una FakeShell che raccoglie i
messaggi."""

import time
import unittest

from harness import Session


class Interaction(unittest.TestCase):
    def setUp(self):
        self.vela = Session()
        self.vela.start()
        self.shell = self.vela.fake_shell()

    def tearDown(self):
        self.vela.stop()

    def frame(self, window):
        """Il riquadro vero (barra compresa) di una finestra decorata: nello
        stato "y" è 32 sopra la barra e "h" lo comprende (stateJson)."""
        w = self.vela.state().window(window)
        return w["x"], w["y"] + 32, w["w"], w["h"] - 32

    def test_resize_from_invisible_border(self):
        window = self.vela.open_window(300, 200, decorated=True)
        x, y, w, h = self.frame(window)
        # 4 pixel fuori dal bordo destro, a metà altezza.
        grab_x, grab_y = x + w + 4, y + h // 2
        self.vela.input("move", grab_x, grab_y, "sleep", "50", "down", "sleep", "50",
                        "move", grab_x + 50, grab_y, "sleep", "50", "move", grab_x + 100, grab_y, "sleep", "100", "up")
        state = self.vela.wait_for(lambda s: s.window(window)["w"] == w + 100, what="100 wider")
        self.assertEqual(state.window(window)["x"], x)

    def test_keyboard_move_and_cancel(self):
        window = self.vela.open_window(300, 200, decorated=True)
        x, y, _, _ = self.frame(window)
        self.vela.command("window active move")
        self.vela.keys("Right", "Right", "Right", "Down", "Return")
        state = self.vela.wait_for(lambda s: s.window(window)["x"] == x + 30, what="moved 30 to the right")
        self.assertEqual(self.frame(window)[1], y + 10)
        # Esc: torna com'era.
        self.vela.command("window active move")
        self.vela.keys("Left", "Left", "Escape")
        time.sleep(0.2)
        self.assertEqual(self.vela.state().window(window)["x"], x + 30)

    def test_keyboard_resize(self):
        window = self.vela.open_window(300, 200, decorated=True)
        _, _, w, _ = self.frame(window)
        self.vela.command("window active resize")
        # Il primo tasto sceglie il bordo, i successivi lo muovono (Ctrl: di un'unità).
        self.vela.keys("Right", "Right", "Right", "ctrl+Right", "Return")
        self.vela.wait_for(lambda s: s.window(window)["w"] == w + 21, what="21 wider")

    def test_window_menu(self):
        window = self.vela.open_window(300, 200, decorated=True)
        x, y, _, _ = self.frame(window)
        self.vela.input("move", x + 120, y + 16, "sleep", "50", "click", "right")
        line = self.shell.wait_for("window-menu ")
        self.assertEqual(line.split()[1:3], [window, "HEADLESS-1"])
        self.assertEqual(line.split()[3:], [str(x + 120), str(y + 16), "0", "1", "0"])
        self.shell.clear()
        self.vela.keys("alt+space")
        line = self.shell.wait_for("window-menu ")
        self.assertEqual(line.split()[1], window)
        self.assertEqual(line.split()[-1], "1")  # da tastiera

    def test_drag_to_edge_snaps_and_offers_assist(self):
        other = self.vela.open_window(401, 301)
        window = self.vela.open_window(300, 200, decorated=True)
        x, y, _, _ = self.frame(window)
        grab_x, grab_y = x + 100, y + 16
        self.vela.input("move", grab_x, grab_y, "sleep", "50", "down", "sleep", "50",
                        "move", grab_x - 200, grab_y + 100, "sleep", "50", "move", 0, 500, "sleep", "300", "up")
        self.vela.wait_for(lambda s: s.window(window)["snap"] == [0, 0, 6, 12], what="snapped left")
        line = self.shell.wait_for("snap-assist ")
        self.assertIn(f'"window":"{window}"', line)
        self.assertIn(f'"candidates":["{other}"]', line)

    def test_snap_layouts_and_groups(self):
        first = self.vela.open_window(401, 301)
        second = self.vela.open_window(300, 200)
        self.vela.keys("super+z")
        line = self.shell.wait_for("snap-layouts ")
        self.assertEqual(line.split()[1:3], [second, "HEADLESS-1"])
        self.assertEqual(line.split()[-1], "1")
        # Come da Snap Assist: la prima a sinistra, la seconda accanto a lei.
        self.vela.command(f"window {first} snap 0 0 6 12")
        self.vela.command(f"window {second} snap 6 0 12 12 quiet {first}")
        state = self.vela.wait_for(lambda s: s.window(second)["snapGroup"] != 0, what="a snap group")
        self.assertEqual(state.window(first)["snapGroup"], state.window(second)["snapGroup"])
        groups = self.shell.wait_for("workspaces ")
        self.assertIn('"snapGroups":[{"output":"HEADLESS-1"', groups)
        # Il gruppo dalla taskbar: tutte davanti, quella scelta a fuoco.
        self.vela.command(f"window {first} activate-group")
        self.vela.wait_for(lambda s: s["focused"] == first, what="the chosen window focused")
        # Staccandosi, il gruppo di due finisce.
        self.vela.command(f"window {second} restore")
        self.vela.wait_for(lambda s: s.window(first)["snapGroup"] == 0, what="the group dissolved")


if __name__ == "__main__":
    unittest.main()
