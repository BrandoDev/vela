# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Più schermi: collegati e scollegati a caldo, ognuno con la sua scala; le
finestre di uno schermo che sparisce passano a un altro e tornano al loro
posto quando lo schermo torna (Server::checkWindowsOnOutputs)."""

import shutil
import subprocess
import unittest

from harness import Session


class Outputs(unittest.TestCase):
    def setUp(self):
        self.vela = Session(scale="HEADLESS-1=1,HEADLESS-2=1.5", size="1920x1080@60")
        self.vela.start()

    def tearDown(self):
        self.vela.stop()

    def add_second(self):
        self.vela.command("test-output add 1920x1080")
        return self.vela.wait_for(lambda s: len(s["outputs"]) == 2, what="due schermi")

    def test_hotplug_with_its_own_scale(self):
        state = self.add_second()
        scales = {output["name"]: output["scale"] for output in state["outputs"]}
        self.assertEqual(scales, {"HEADLESS-1": 1, "HEADLESS-2": 1.5})
        second = next(o for o in state["outputs"] if o["name"] == "HEADLESS-2")
        self.assertEqual((second["w"], second["h"]), (1280, 720))  # 1920x1080 a 150%
        self.vela.command("test-output remove HEADLESS-2")
        self.vela.wait_for(lambda s: len(s["outputs"]) == 1, what="di nuovo uno schermo")

    def test_windows_are_rescued_and_come_back(self):
        state = self.add_second()
        second = next(o for o in state["outputs"] if o["name"] == "HEADLESS-2")
        window = self.vela.open_window(300, 200)
        # Super+trascina: la finestra sul secondo schermo.
        start = self.vela.state().window(window)
        grab_x, grab_y = start["x"] + 50, start["y"] + 50
        target_x, target_y = second["x"] + 200, second["y"] + 200
        self.vela.input("move", grab_x, grab_y, "keydown", "super", "down", "sleep", "50",
                        "move", (grab_x + target_x) // 2, (grab_y + target_y) // 2, "sleep", "50",
                        "move", target_x, target_y, "sleep", "100", "up", "keyup", "super")
        state = self.vela.wait_for(lambda s: s.window(window)["output"] == "HEADLESS-2", what="sul secondo schermo")
        placed = state.window(window)
        # Lo schermo si spegne (Impostazioni > Schermo, wlr-output-management):
        # la finestra passa sul primo, tutta dentro.
        if not shutil.which("wlr-randr"):
            self.skipTest("serve wlr-randr")
        randr = lambda *args: subprocess.run(["wlr-randr", *args], env=self.vela.client_env, check=True,
                                             stdout=subprocess.DEVNULL)
        randr("--output", "HEADLESS-2", "--off")
        state = self.vela.wait_for(lambda s: s.window(window)["output"] == "HEADLESS-1", what="salvata sul primo schermo")
        rescued, first = state.window(window), state.output
        self.assertGreaterEqual(rescued["x"], first["x"])
        self.assertLessEqual(rescued["x"] + rescued["w"], first["x"] + first["w"])
        # Lo schermo torna: la finestra torna dov'era.
        randr("--output", "HEADLESS-2", "--on")
        state = self.vela.wait_for(lambda s: s.window(window)["output"] == "HEADLESS-2", what="tornata al suo schermo")
        back = state.window(window)
        self.assertEqual((back["x"], back["y"]), (placed["x"], placed["y"]))

    def test_unplugged_output_rescues_windows(self):
        state = self.add_second()
        second = next(o for o in state["outputs"] if o["name"] == "HEADLESS-2")
        window = self.vela.open_window(300, 200)
        start = self.vela.state().window(window)
        self.vela.input("move", start["x"] + 50, start["y"] + 50, "keydown", "super", "down", "sleep", "50",
                        "move", second["x"] + 200, second["y"] + 200, "sleep", "100", "up", "keyup", "super")
        self.vela.wait_for(lambda s: s.window(window)["output"] == "HEADLESS-2")
        # Scollegato del tutto: la finestra resta raggiungibile sul primo.
        self.vela.command("test-output remove HEADLESS-2")
        state = self.vela.wait_for(lambda s: len(s["outputs"]) == 1 and s.window(window)["output"] == "HEADLESS-1",
                                   what="salvata sul primo schermo")
        rescued, first = state.window(window), state.output
        self.assertGreaterEqual(rescued["y"], first["y"])
        self.assertLessEqual(rescued["y"] + rescued["h"], first["y"] + first["h"])


if __name__ == "__main__":
    unittest.main()
