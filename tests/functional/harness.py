# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Una sessione di Vela senza schermo, comandata dai test come da una persona.

Il compositor gira con il backend headless di wlroots, in cartelle di
configurazione temporanee: niente di ciò che fa tocca la sessione vera. Le
finestre sono di vela-pattern; tastiera e mouse sono quelli virtuali di
vela-input; lo stato si legge con la richiesta "state" sul socket dei
comandi (Server::stateJson).

Solo libreria standard di Python. Serve una GPU con Vulkan 1.4 (il renderer
di Vela non ha ripieghi software): senza, run.py salta le prove.
"""

import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.environ.get("VELA_BUILD", os.path.join(ROOT, "build"))


def tool(name):
    return os.path.join(BUILD, "tools", name)


class TimeoutError(AssertionError):
    pass


class Session:
    """Un compositor headless con il suo socket dei comandi.

    with Session(scale=1.25) as vela:
        window = vela.open_window()
        vela.keys("super+Left")
        vela.wait_for(lambda s: s.window(window)["snap"] == [0, 0, 6, 12])
    """

    def __init__(self, scale=1.0, size="1920x1080@60", supervise=False, lock_hold_ms=None, env=None):
        self.scale = scale
        self.size = size
        self.supervise = supervise
        self.lock_hold_ms = lock_hold_ms
        self.extra_env = env or {}
        self.process = None
        self.clients = []
        self.display = None
        self.directory = None

    # ------------------------------------------------------------ avvio --

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, kind, value, traceback):
        self.stop(keep_log=kind is not None)
        return False

    def start(self):
        self.directory = tempfile.mkdtemp(prefix="vela-functional-")
        env = {key: value for key, value in os.environ.items()
               if key not in ("WAYLAND_DISPLAY", "DISPLAY", "QT_WAYLAND_RECONNECT", "WAYLAND_SOCKET")}
        env.update({
            "VELA_TEST_MARK": "menu",
            "WLR_BACKENDS": "headless",
            "WLR_LIBINPUT_NO_DEVICES": "1",
            "VELA_DEBUG_INPUT": "1",
            "VELA_OUTPUT_SIZE": self.size,
            "VELA_SCALE": str(self.scale),
            "XDG_CONFIG_HOME": os.path.join(self.directory, "config"),
            "XDG_DATA_HOME": os.path.join(self.directory, "data"),
            "XDG_CACHE_HOME": os.path.join(self.directory, "cache"),
        })
        if self.lock_hold_ms is not None:
            env["VELA_LOCK"] = f"{tool('vela-testlock')} {self.lock_hold_ms}"
        env.update(self.extra_env)
        self.env = env
        command = [os.path.join(BUILD, "compositor", "vela-compositor")]
        if self.supervise:
            command.append("--supervise")
        self.log_path = os.path.join(self.directory, "vela.log")
        self.log = open(self.log_path, "w")
        self.process = subprocess.Popen(command, env=env, stdout=self.log, stderr=subprocess.STDOUT,
                                        start_new_session=True)
        self.display = self.wait_for_log(r"WAYLAND_DISPLAY=", lambda line: line.split("WAYLAND_DISPLAY=")[1].split()[0])
        runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
        self.socket_path = os.path.join(runtime, f"vela-{self.display}.sock")
        self.wait_for(lambda _: os.path.exists(self.socket_path), what="il socket dei comandi", read_state=False)
        self.client_env = dict(env, WAYLAND_DISPLAY=self.display)
        # Il primo frame: lo schermo c'è.
        self.wait_for(lambda s: s["outputs"], what="uno schermo")

    def stop(self, keep_log=False):
        for client in self.clients:
            if client.poll() is None:
                client.terminate()
        for client in self.clients:
            try:
                client.wait(2)
            except subprocess.TimeoutExpired:
                client.kill()
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(5)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
        if self.directory:
            self.log.close()
            if keep_log:
                print(f"\n--- log di Vela ({self.log_path}) ---")
                with open(self.log_path, errors="replace") as log:
                    print("".join(log.readlines()[-40:]))
            shutil.rmtree(self.directory, ignore_errors=True)

    def log_text(self):
        self.log.flush()
        with open(self.log_path, errors="replace") as log:
            return log.read()

    def wait_for_log(self, needle, extract=lambda line: line, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.log_text().splitlines():
                if needle in line:
                    return extract(line)
            if self.process.poll() is not None:
                raise AssertionError(f"il compositor si è chiuso (codice {self.process.returncode}):\n{self.log_text()[-3000:]}")
            time.sleep(0.05)
        raise TimeoutError(f"nel log manca «{needle}»")

    # -------------------------------------------------------- comandi --

    def command(self, line):
        with socket.socket(socket.AF_UNIX) as connection:
            connection.connect(self.socket_path)
            connection.sendall((line + "\n").encode())

    def query(self, line):
        with socket.socket(socket.AF_UNIX) as connection:
            connection.connect(self.socket_path)
            connection.sendall((line + "\n").encode())
            data = b""
            while not data.endswith(b"\n"):
                chunk = connection.recv(65536)
                if not chunk:
                    break
                data += chunk
        return json.loads(data)

    def state(self):
        return State(self.query("state"))

    def wait_for(self, condition, timeout=5.0, what="la condizione", read_state=True):
        """Aspetta che condition(stato) sia vera; restituisce lo stato."""
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            last = self.state() if read_state else None
            try:
                if condition(last):
                    return last
            except (KeyError, IndexError, TypeError):
                pass
            time.sleep(0.05)
        raise TimeoutError(f"dopo {timeout} s non è vera: {what}\nultimo stato: {last}\n"
                           f"--- fine del log di Vela ---\n{self.log_text()[-2500:]}")

    # -------------------------------------------------- tastiera e mouse --

    def input(self, *actions):
        subprocess.run([tool("vela-input"), *map(str, actions)], env=self.client_env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def keys(self, *combos):
        actions = []
        for combo in combos:
            actions += ["key", combo, "sleep", "60"]
        self.input(*actions)

    # ------------------------------------------------------- finestre --

    def open_window(self, width=400, height=300, command=None, timeout=5.0):
        """Apre una finestra (vela-pattern, o `command`); restituisce il suo identificativo."""
        before = {w["id"] for w in self.state().windows}
        client = subprocess.Popen(command or [tool("vela-pattern"), str(width), str(height)], env=self.client_env,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.clients.append(client)
        state = self.wait_for(lambda s: {w["id"] for w in s.windows} - before, timeout=timeout,
                              what="una finestra nuova")
        identifier = next(iter({w["id"] for w in state.windows} - before))
        self.wait_still(identifier)  # finita l'animazione di apertura (sale di qualche pixel)
        return identifier

    def wait_still(self, identifier, timeout=3.0):
        """Aspetta che la geometria della finestra resti ferma per un po'."""
        def geometry():
            window = self.state().window(identifier)
            return (window["x"], window["y"], window["w"], window["h"])
        deadline = time.monotonic() + timeout
        last = geometry()
        still_since = time.monotonic()
        while time.monotonic() < deadline:
            time.sleep(0.05)
            current = geometry()
            if current != last:
                last = current
                still_since = time.monotonic()
            elif time.monotonic() - still_since >= 0.3:
                return current
        raise TimeoutError(f"la finestra {identifier} non si ferma")

    def screenshot(self, path):
        subprocess.run([tool("vela-shot"), path], env=self.client_env, check=True)


class State(dict):
    """Lo stato di "state", con qualche comodità."""

    @property
    def windows(self):
        return self["windows"]

    def window(self, identifier):
        for window in self["windows"]:
            if window["id"] == identifier:
                return window
        raise KeyError(identifier)

    def has(self, identifier):
        return any(window["id"] == identifier for window in self["windows"])

    @property
    def output(self):
        return self["outputs"][0]


def gpu_available():
    """Il renderer di Vela vuole una GPU vera (nodo DRM e Vulkan 1.4)."""
    return any(name.startswith("renderD") for name in os.listdir("/dev/dri")) if os.path.isdir("/dev/dri") else False
