# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Un'app lenta non ferma lo schermo (docs/renderer.md §7.3).

vela-slowgpu fa il commit di ogni fotogramma mentre la sua GPU lavora
ancora per ~150 ms. Vela tiene il commit in attesa finché la fence non è
segnalata (scene/readiness.cpp) e intanto mostra il fotogramma precedente:
il cursore che si muove resta a 60 Hz. Senza l'attesa (VELA_READY_WAIT=0)
ogni frame aspetterebbe la GPU dell'app e lo schermo perderebbe quasi tutti
i vblank: la terza prova controlla che il test se ne accorga davvero.
"""

import unittest

from harness import Session, tool


def move_cursor(vela, seconds=1.0):
    """Il cursore attraversa lo schermo a 60 Hz per `seconds`."""
    steps = int(seconds * 60)
    actions = []
    for i in range(steps):
        actions += ["move", 300 + i * 10, 400 + (i % 2) * 3, "sleep", "16"]
    vela.input(*actions)


class ReadyCommits(unittest.TestCase):
    def measure(self, *args, env=None):
        """Vblank persi e commit trattenuti mentre il cursore si muove per 1 s."""
        with Session(env=env) as vela:
            vela.open_window(command=[tool("vela-slowgpu"), "150", *args], timeout=15)
            before = vela.wait_for(lambda s: s["held"] >= 2 or env, timeout=5, what="held commits")
            move_cursor(vela)
            after = vela.state()
            return (after.output["missed"] - before.output["missed"],
                    after.output["frames"] - before.output["frames"],
                    after["held"] - before["held"])

    def test_explicit_sync(self):
        missed, frames, held = self.measure()
        self.assertLessEqual(missed, 2, f"{frames} frames, {held} held commits")
        self.assertGreaterEqual(held, 3)  # ~6 fotogrammi dell'app in 1 s, tutti in attesa

    def test_implicit_sync(self):
        # Anche i buffer ridisegnati ma non mostrati: restano importati solo
        # finché servono (render/texture.cpp), o il kernel farebbe aspettare
        # le loro fence a ogni nostro invio.
        missed, frames, held = self.measure("--implicit")
        self.assertLessEqual(missed, 2, f"{frames} frames, {held} held commits")
        self.assertGreaterEqual(held, 3)

    def test_without_waiting_the_output_stalls(self):
        missed, frames, held = self.measure(env={"VELA_READY_WAIT": "0"})
        self.assertEqual(held, 0)
        self.assertGreaterEqual(missed, 20, f"the app isn't slow enough: {frames} frames")


if __name__ == "__main__":
    unittest.main()
