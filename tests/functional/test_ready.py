# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""A slow app doesn't hold up the screen (docs/renderer.md §7.3).

vela-slowgpu commits every frame while its GPU keeps working for another
~150 ms. Vela holds the commit until the fence signals (scene/ready.c) and
meanwhile shows the previous frame: the moving cursor stays at 60 Hz.
Without the wait (VELA_READY_WAIT=0) every frame would wait for the app's
GPU and the output would miss almost every vblank: the third test checks
that the test really notices."""

import unittest

from harness import Session, tool


def move_cursor(vela, seconds=1.0):
    """The cursor crosses the output at 60 Hz for `seconds`."""
    steps = int(seconds * 60)
    actions = []
    for i in range(steps):
        actions += ["move", 300 + i * 10, 400 + (i % 2) * 3, "sleep", "16"]
    vela.input(*actions)


class ReadyCommits(unittest.TestCase):
    def measure(self, *args, env=None):
        """Missed vblanks and held commits while the cursor moves for 1 s."""
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
        self.assertGreaterEqual(held, 3) # ~6 app frames in 1 s, all held

    def test_implicit_sync(self):
        # Buffers redrawn but not shown too: they stay imported only as long as
        # needed (render/texture.c), or the kernel would make every submission
        # of ours wait for their fences.
        missed, frames, held = self.measure("--implicit")
        self.assertLessEqual(missed, 2, f"{frames} frames, {held} held commits")
        self.assertGreaterEqual(held, 3)

    def test_without_waiting_the_output_stalls(self):
        missed, frames, held = self.measure(env={"VELA_READY_WAIT": "0"})
        self.assertEqual(held, 0)
        self.assertGreaterEqual(missed, 20, f"the app isn't slow enough: {frames} frames")


if __name__ == "__main__":
    unittest.main()
