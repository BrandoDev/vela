# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""Una sessione di Vela senza schermo, comandata dai test come da una persona.

Il compositor gira con il backend headless di wlroots, in cartelle di
configurazione temporanee: niente di ciò che fa tocca la sessione vera. Le
finestre sono di vela-pattern; tastiera e mouse sono quelli virtuali di
vela-input; lo stato si legge con la richiesta "state" sul socket dei
comandi (vela_state_json in command.c).

Solo libreria standard di Python. Serve una GPU con Vulkan 1.4 (il renderer
di Vela non ha ripieghi software): senza, run.py salta le prove.
"""

import json
import os
import shutil
import struct
import signal
import socket
import subprocess
import tempfile
import threading
import time
import zlib

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

    def __init__(self, scale=1.0, size="1920x1080@60", supervise=False, lock_hold_ms=None, env=None, startup=None):
        self.scale = scale
        self.size = size
        self.supervise = supervise
        self.lock_hold_ms = lock_hold_ms
        self.extra_env = env or {}
        self.startup = startup  # il comando di avvio (-s), come la shell
        self.process = None
        self.clients = []
        self.display = None
        self.directory = None
        self.shell = None

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
        if self.startup:
            command += ["-s", self.startup]
        self.log_path = os.path.join(self.directory, "vela.log")
        self.log = open(self.log_path, "w")
        self.process = subprocess.Popen(command, env=env, stdout=self.log, stderr=subprocess.STDOUT,
                                        start_new_session=True)
        self.display = self.wait_for_log(r"WAYLAND_DISPLAY=", lambda line: line.split("WAYLAND_DISPLAY=")[1].split()[0])
        runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
        self.socket_path = os.path.join(runtime, f"vela-{self.display}.sock")
        self.wait_for(lambda _: os.path.exists(self.socket_path), what="the command socket", read_state=False)
        self.client_env = dict(env, WAYLAND_DISPLAY=self.display)
        # Il primo frame: lo schermo c'è.
        self.wait_for(lambda s: s["outputs"], what="an output")

    def stop(self, keep_log=False):
        if self.shell:
            self.shell.close()
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
                print(f"\n--- Vela log ({self.log_path}) ---")
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
                raise AssertionError(f"the compositor exited (code {self.process.returncode}):\n{self.log_text()[-3000:]}")
            time.sleep(0.05)
        raise TimeoutError(f"the log is missing «{needle}»")

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

    def wait_for(self, condition, timeout=5.0, what="the condition", read_state=True):
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
        raise TimeoutError(f"after {timeout} s this still isn't true: {what}\nlast state: {last}\n"
                           f"--- end of the Vela log ---\n{self.log_text()[-2500:]}")

    def config_path(self, name="vela.conf"):
        return os.path.join(self.env["XDG_CONFIG_HOME"], "vela", name)

    def fake_shell(self):
        """Da qui in poi i messaggi di Vela alla shell arrivano a una FakeShell."""
        self.shell = FakeShell(self.display)
        return self.shell

    # -------------------------------------------------- tastiera e mouse --

    def input(self, *actions):
        subprocess.run([tool("vela-input"), *map(str, actions)], env=self.client_env, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def randr(self, *args):
        """Configura gli schermi come Impostazioni > Schermo (wlr-output-management)."""
        return subprocess.run([tool("vela-randr"), *map(str, args)], env=self.client_env, check=True,
                              capture_output=True, text=True).stdout

    def keys(self, *combos):
        actions = []
        for combo in combos:
            actions += ["key", combo, "sleep", "60"]
        self.input(*actions)

    # ------------------------------------------------------- finestre --

    def open_window(self, width=400, height=300, command=None, timeout=5.0, app_id=None, decorated=False):
        """Apre una finestra (vela-pattern, o `command`); restituisce il suo identificativo.

        decorated: con la barra del titolo di Vela (xdg-decoration)."""
        before = {w["id"] for w in self.state().windows}
        pattern = ([tool("vela-pattern")] + (["--app-id", app_id] if app_id else [])
                   + (["--decorated"] if decorated else []) + [str(width), str(height)])
        client = subprocess.Popen(command or pattern, env=self.client_env,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.clients.append(client)
        state = self.wait_for(lambda s: {w["id"] for w in s.windows} - before, timeout=timeout,
                              what="a new window")
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
        raise TimeoutError(f"window {identifier} doesn't settle")

    def screenshot(self, path):
        subprocess.run([tool("vela-shot"), path], env=self.client_env, check=True)

    def pixels(self):
        """Lo schermo adesso, come Image (vela-shot)."""
        path = os.path.join(self.directory, "shot.png")
        self.screenshot(path)
        return Image.read(path)


class FakeShell:
    """La shell Qt finta: ascolta sul suo socket e raccoglie le righe che
    Vela le manda ("toggle-start", "accessibility {...}", ...)."""

    def __init__(self, display):
        runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
        self.path = os.path.join(runtime, f"vela-shell-{display}.sock")
        if os.path.exists(self.path):
            os.unlink(self.path)
        self.server = socket.socket(socket.AF_UNIX)
        self.server.bind(self.path)
        self.server.listen(16)
        self.lines = []
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._accept, daemon=True)
        self.thread.start()

    def _accept(self):
        while True:
            try:
                connection, _ = self.server.accept()
            except OSError:
                return
            data = b""
            with connection:
                while chunk := connection.recv(65536):
                    data += chunk
            with self.lock:
                self.lines += data.decode(errors="replace").splitlines()

    def received(self, prefix=""):
        with self.lock:
            return [line for line in self.lines if line.startswith(prefix)]

    def wait_for(self, prefix, timeout=5.0):
        """L'ultima riga che comincia con `prefix`, appena arriva."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            lines = self.received(prefix)
            if lines:
                return lines[-1]
            time.sleep(0.05)
        raise TimeoutError(f"the shell never received «{prefix}»; it got: {self.received()[-10:]}")

    def clear(self):
        with self.lock:
            self.lines = []

    def close(self):
        try:
            self.server.shutdown(socket.SHUT_RDWR)  # sveglia accept()
        except OSError:
            pass
        self.server.close()
        if os.path.exists(self.path):
            os.unlink(self.path)


class Image:
    """Un PNG di vela-shot: RGB a 8 bit, senza filtri (solo libreria standard)."""

    def __init__(self, width, height, rows):
        self.width = width
        self.height = height
        self.rows = rows

    @staticmethod
    def read(path):
        with open(path, "rb") as file:
            data = file.read()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
        position = 8
        width = height = 0
        packed = b""
        while position < len(data):
            length, kind = struct.unpack(">I4s", data[position:position + 8])
            chunk = data[position + 8:position + 8 + length]
            if kind == b"IHDR":
                width, height = struct.unpack(">II", chunk[:8])
                assert chunk[8:10] == b"\x08\x02", "vela-shot writes 8-bit RGB"
            elif kind == b"IDAT":
                packed += chunk
            position += 12 + length
        raw = zlib.decompress(packed)
        stride = 1 + width * 3
        rows = [raw[y * stride + 1:(y + 1) * stride] for y in range(height)]
        return Image(width, height, rows)

    def at(self, x, y):
        row = self.rows[y]
        return tuple(row[x * 3:x * 3 + 3])

    def colors(self, x0, y0, x1, y1):
        """I colori diversi nel rettangolo [x0, x1) x [y0, y1)."""
        return {self.at(x, y) for y in range(y0, y1) for x in range(x0, x1)}


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
