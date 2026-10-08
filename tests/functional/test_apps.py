# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""What every Vela app gets from common/ (vela-app): the mouse's side buttons
as the Back and Forward commands, whatever has the focus. Checked on File
Explorer, through its window title."""

import os
import tempfile
import unittest

from harness import BUILD, Session

FILES = os.path.join(BUILD, "explorer", "vela-files")


@unittest.skipUnless(os.path.exists(FILES), "vela-files not built (VELA_BUILD_SHELL=OFF)")
class SideButtons(unittest.TestCase):
    def setUp(self):
        self.folders = tempfile.TemporaryDirectory(prefix="vela-sidebuttons-")
        self.first = os.path.join(self.folders.name, "first")
        self.second = os.path.join(self.first, "second")
        os.makedirs(self.second)
        # English titles whatever the system language.
        self.vela = Session(env={"LANGUAGE": "en", "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8"})
        self.vela.start()

    def tearDown(self):
        self.vela.stop()
        self.folders.cleanup()

    def title_starts(self, window, name):
        self.vela.wait_for(lambda s: s.window(window)["title"].startswith(name), timeout=5,
                           what=f"the title starts with {name}")

    def open_files(self):
        window = self.vela.open_window(command=[FILES, self.second], timeout=15)
        self.title_starts(window, "second")
        state = self.vela.state().window(window)
        return window, state["x"] + 60, state["y"] + state["h"] // 2

    # Each vela-input run is a new virtual keyboard, and its arrival takes
    # the keyboard away from the window for a moment: every test's input goes
    # in one run, like a real keyboard and mouse.

    def test_back_and_forward_whatever_has_the_focus(self):
        window, x, y = self.open_files()
        # Up to "first" (Alt+Up), back to "second" with the side button, then
        # forward again with the focus in the address bar (Ctrl+L).
        self.vela.input("move", x, y, "sleep", "300", "key", "alt+Up", "sleep", "800", "click", "back",
                        "sleep", "800", "key", "ctrl+l", "sleep", "300", "click", "forward")
        self.title_starts(window, "first")
        self.assertEqual(self.vela.state()["focused"], window)

    def test_side_button_on_a_window_without_focus(self):
        window, x, y = self.open_files()
        self.vela.input("move", x, y, "sleep", "300", "key", "alt+Up")
        self.title_starts(window, "first")
        # Another window takes the focus; the side button on File Explorer
        # activates it and goes back, like on Windows.
        other = self.vela.open_window(200, 150)
        self.vela.wait_for(lambda s: s["focused"] == other, what="the other window focused")
        self.vela.input("move", x, y, "sleep", "300", "click", "back")
        self.title_starts(window, "second")
        self.assertEqual(self.vela.state()["focused"], window)


if __name__ == "__main__":
    unittest.main()
