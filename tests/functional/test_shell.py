# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""The actual Qt shell: Super must not reset the desktop or lose a reopened menu.

Fake shell sockets and vela-panel cannot exercise Qt's layer-surface lifecycle.
Run the real shell on a private session bus and isolated configuration.
"""

import os
import re
import subprocess
import unittest

from harness import BUILD, Session

SHELL = os.path.join(BUILD, "shell", "vela-shell")


@unittest.skipUnless(os.path.exists(SHELL), "vela-shell not built (VELA_BUILD_SHELL=OFF)")
class QtShell(unittest.TestCase):
    def setUp(self):
        self.bus = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--nopidfile", "--print-address=1"],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.addCleanup(self.stop_bus)
        address = self.bus.stdout.readline().strip()
        self.assertTrue(address, "private D-Bus session failed to start")
        self.vela = Session(size="1920x1080@100", env={"VELA_SYNC_FILE": "0", "DBUS_SESSION_BUS_ADDRESS": address})
        self.vela.start()
        self.addCleanup(self.vela.stop)
        env = dict(self.vela.client_env, QT_QPA_PLATFORM="wayland",
                   QT_WAYLAND_SHELL_INTEGRATION="layer-shell", QT_WAYLAND_RECONNECT="0",
                   QT_FORCE_STDERR_LOGGING="1", QSG_RENDER_LOOP="threaded", QSG_RHI_BACKEND="opengl",
                   WAYLAND_DEBUG="client")
        self.shell = subprocess.Popen([SHELL], env=env, stdout=self.vela.log, stderr=self.vela.log)
        self.vela.clients.append(self.shell)
        self.vela.wait_for(lambda s: self.mapped(s, "vela-taskbar") and self.mapped(s, "vela-wallpaper"),
                           timeout=20, what="the real Qt desktop")

    def stop_bus(self):
        self.bus.terminate()
        try:
            self.bus.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.bus.kill()
            self.bus.wait(timeout=3)
        self.bus.stdout.close()

    @staticmethod
    def mapped(state, namespace):
        return any(layer["namespace"] == namespace and layer["mapped"] for layer in state["layers"])

    def check_desktop(self):
        self.assertIsNone(self.shell.poll(), self.vela.log_text()[-4000:])
        state = self.vela.state()
        self.assertTrue(self.mapped(state, "vela-taskbar"))
        self.assertTrue(self.mapped(state, "vela-wallpaper"))
        self.assertNotIn("layer_surface has never been configured", self.vela.log_text())

    def test_super_repeatedly_opens_and_closes_the_real_menu(self):
        # One virtual keyboard, as on the user's desktop. Creating a fresh
        # keyboard for each key would briefly change focus and close the menu.
        actions = []
        for _ in range(10):
            actions += ["key", "super", "sleep", "400", "key", "super", "sleep", "350"]
        self.vela.input(*actions)
        self.check_desktop()
        self.assertFalse(self.mapped(self.vela.state(), "vela-start-menu"))

    def test_reopening_during_close_animation_keeps_the_menu_open(self):
        self.vela.input("key", "super", "sleep", "450", "key", "super", "sleep", "40",
                        "key", "super", "sleep", "600")
        self.check_desktop()
        self.assertTrue(self.mapped(self.vela.state(), "vela-start-menu"),
                        "the previous close animation hid the reopened menu")

    def test_hidden_menu_releases_native_surface_before_next_open(self):
        for _ in range(3):
            offset = len(self.vela.log_text())
            self.vela.input("key", "super", "sleep", "450", "key", "super", "sleep", "350")
            self.check_desktop()
            log = self.vela.log_text()[offset:]
            creation = re.search(r'get_layer_surface\(new id zwlr_layer_surface_v1([@#]\d+), '
                                 r'wl_surface([@#]\d+).*?"vela-start-menu"', log)
            self.assertIsNotNone(creation, "Start layer surface missing from protocol trace")
            role, surface = map(re.escape, creation.groups())
            # Real protocol ordering, including reopening after destruction:
            # a non-null buffer must follow this role's configure and ACK.
            configure = re.search(rf'zwlr_layer_surface_v1{role}\.configure\(', log)
            ack = re.search(rf'zwlr_layer_surface_v1{role}\.ack_configure\(', log)
            attach = re.search(rf'wl_surface{surface}\.attach\(wl_buffer', log)
            self.assertIsNotNone(configure)
            self.assertIsNotNone(ack)
            self.assertIsNotNone(attach)
            self.assertLess(configure.start(), ack.start())
            self.assertLess(ack.start(), attach.start())
            self.assertRegex(log, rf'wl_surface{surface}\.destroy\(\)',
                          "hidden Start menu retained its native surface and EGL buffer queue")
            # Blur must bind to the newly created surface on every reopening,
            # rather than retain hooks to a destroyed Qt platform window.
            effect = re.search(r'get_background_effect\(new id ext_background_effect_surface_v1'
                               rf'([@#]\d+), wl_surface{surface}\)', log)
            self.assertIsNotNone(effect, "reopened menu lost compositor blur")
            self.assertRegex(log, rf'ext_background_effect_surface_v1{re.escape(effect[1])}'
                                 r'\.set_blur_region\(wl_region')

    def test_other_panels_can_reopen_after_native_surface_release(self):
        for key, namespace in [("super+r", "vela-run"), ("super+a", "vela-quick-settings"),
                               ("super+n", "vela-notification-center")]:
            with self.subTest(panel=namespace):
                for _ in range(3):
                    offset = len(self.vela.log_text())
                    self.vela.input("key", key, "sleep", "450", "key", "Escape", "sleep", "350")
                    self.check_desktop()
                    self.assertFalse(self.mapped(self.vela.state(), namespace))
                    log = self.vela.log_text()[offset:]
                    creation = re.search(r'get_layer_surface\(.*?wl_surface([@#]\d+).*?'
                                         rf'"{namespace}"', log)
                    self.assertIsNotNone(creation, f"{namespace} did not reopen")
                    surface = re.escape(creation[1])
                    self.assertRegex(log, rf'wl_surface{surface}\.attach\(wl_buffer')
                    self.assertRegex(log, rf'wl_surface{surface}\.destroy\(\)')


if __name__ == "__main__":
    unittest.main()
