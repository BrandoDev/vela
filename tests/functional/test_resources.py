# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Sustained rendering and post-hang evidence, with a real GPU and 1024 FDs.

The NVIDIA report exhausted descriptors after seconds of rendering. Exercise
both synchronization paths beyond that limit and check actual /proc growth.
"""

import os
import re
import signal
import time
import unittest

from harness import Session, tool


def descriptor_counts(pid):
    links = []
    for fd in os.scandir(f"/proc/{pid}/fd"):
        try:
            links.append(os.readlink(fd.path))
        except FileNotFoundError:
            pass
    return len(links), sum("sync_file" in link for link in links)


class RetainedResources(unittest.TestCase):
    def test_supervisor_keeps_sampling_a_frozen_compositor(self):
        with Session(supervise=True) as vela:
            line = vela.wait_for_log("vela-supervise: resources ")
            pid = int(re.search(r"pid=(\d+)", line)[1])
            os.kill(pid, signal.SIGSTOP)
            try:
                before = vela.log_text().count("vela-supervise: resources ")
                vela.wait_for(lambda _: vela.log_text().count("vela-supervise: resources ") > before,
                              timeout=5, read_state=False, what="resources while compositor is stopped")
            finally:
                os.kill(pid, signal.SIGCONT)
            vela.state() # the compositor responds after resuming

    def exercise_rendering(self, mode):
        with Session(supervise=True, nofile=1024, size="800x600@120",
                     env={"VELA_SYNC_FILE": mode}) as vela:
            line = vela.wait_for_log("vela-supervise: resources ")
            pid = int(re.search(r"pid=(\d+)", line)[1])
            vela.open_window(command=[tool("vela-slowgpu"), "1", "--implicit"], timeout=15)
            if mode == "0":
                self.assertIn("CPU synchronization", vela.log_text())
                self.assertNotIn("Explicit sync with apps (linux-drm-syncobj-v1) enabled", vela.log_text())
            start = vela.state().output["frames"]
            vela.wait_for(lambda s: s.output["frames"] >= start + 120, timeout=10, what="renderer warm-up")
            baseline = descriptor_counts(pid)
            start = vela.state().output["frames"]
            deadline = time.monotonic() + 35
            samples = []
            while time.monotonic() < deadline:
                state = vela.state()
                samples.append(descriptor_counts(pid))
                self.assertLessEqual(samples[-1][0], baseline[0] + 64,
                                     f"FD growth while rendering: baseline={baseline}, samples={samples}")
                if state.output["frames"] >= start + 1200:
                    break
                time.sleep(0.25)
            else:
                self.fail("compositor did not render 1200 additional frames in 35 seconds")
            self.assertLessEqual(samples[-1][1], baseline[1] + 16, f"sync_file growth: {samples}")
            self.assertNotIn("Too many open files", vela.log_text())

    def test_gpu_sync_has_bounded_descriptors_over_1200_frames(self):
        self.exercise_rendering("1")

    def test_cpu_sync_has_bounded_descriptors_over_1200_frames(self):
        self.exercise_rendering("0")


if __name__ == "__main__":
    unittest.main()
