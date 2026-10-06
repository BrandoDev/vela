# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""La sessione: blocco e sblocco, schermi spenti e riaccesi, e il
supervisore che riavvia il compositor dopo un crash (anche bloccato)."""

import os
import signal
import time
import unittest

from harness import Session


def child_compositor(supervisor_pid):
    """Il compositor avviato dal supervisore (suo figlio)."""
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/stat") as stat:
                fields = stat.read().rsplit(")", 1)[1].split()
            with open(f"/proc/{pid}/comm") as comm:
                name = comm.read().strip()
        except OSError:
            continue
        if int(fields[1]) == supervisor_pid and name == "vela-compositor":
            return int(pid)
    return None


class Lock(unittest.TestCase):
    def test_lock_hides_windows_and_unlock_restores_focus(self):
        with Session(lock_hold_ms=1500) as vela:
            window = vela.open_window()
            vela.wait_for(lambda s: s["focused"] == window)
            vela.command("lock")
            state = vela.wait_for(lambda s: s["locked"], what="locked")
            self.assertIsNone(state["focused"])  # nessuna finestra ha la tastiera
            state = vela.wait_for(lambda s: not s["locked"], timeout=6, what="unlocked")
            vela.wait_for(lambda s: s["focused"] == window, what="focus goes back to the window")

    def test_screens_off_and_on_while_locked(self):
        with Session(lock_hold_ms=4000) as vela:
            window = vela.open_window()
            vela.command("lock")
            vela.wait_for(lambda s: s["locked"])
            vela.command("test-power off")
            vela.wait_for(lambda s: not s.output["powered"], what="screen off")
            vela.command("test-power on")
            vela.wait_for(lambda s: s.output["powered"], what="screen back on")
            vela.wait_for(lambda s: not s["locked"], timeout=8, what="unlocked")
            self.assertTrue(vela.state().has(window))


class Supervisor(unittest.TestCase):
    def test_restart_after_crash(self):
        with Session(supervise=True) as vela:
            first = child_compositor(vela.process.pid)
            self.assertIsNotNone(first)
            os.kill(first, signal.SIGSEGV)
            vela.wait_for_log("restarted after a crash")
            second = vela.wait_for(lambda s: child_compositor(vela.process.pid) not in (None, first),
                                   read_state=False, what="a new compositor")
            vela.wait_for(lambda s: s["outputs"], what="the new compositor responds")

    def test_crash_while_locked_restarts_locked(self):
        # Il programma di blocco di prova resta 3 s, poi si sblocca da solo.
        with Session(supervise=True, lock_hold_ms=3000) as vela:
            vela.command("lock")
            vela.wait_for(lambda s: s["locked"])
            os.kill(child_compositor(vela.process.pid), signal.SIGSEGV)
            vela.wait_for_log("restarting locked")
            # Ripartito bloccato (mai il desktop scoperto), poi lo sblocco.
            time.sleep(0.5)
            vela.wait_for(lambda s: s["locked"], what="restarted locked")
            vela.wait_for(lambda s: not s["locked"], timeout=8, what="unlocked by the new locker")

    def test_gives_up_after_three_quick_crashes(self):
        with Session(supervise=True) as vela:
            for _ in range(3):
                vela.wait_for(lambda s: child_compositor(vela.process.pid), read_state=False, what="the compositor")
                child = child_compositor(vela.process.pid)
                os.kill(child, signal.SIGSEGV)
                deadline = time.monotonic() + 5
                while child_compositor(vela.process.pid) == child and time.monotonic() < deadline:
                    time.sleep(0.05)
            vela.process.wait(10)
            self.assertNotEqual(vela.process.returncode, 0)
            self.assertIn("giving up", vela.log_text())
            runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
            self.assertFalse(os.path.exists(os.path.join(runtime, vela.display)))  # socket Wayland tolto


if __name__ == "__main__":
    unittest.main()
