#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

"""A fake polkitd for vela-polkit-agent's tests (docs/polkit-agent.md §10).

It runs on a private D-Bus bus, which the agent uses as its system bus
(DBUS_SYSTEM_BUS_ADDRESS): the real agent never talks to the real polkitd. It
owns org.freedesktop.PolicyKit1 and accepts the agent's registration; the test
drives it through the org.vela.Test interface on /org/vela/Test:

    Begin(cookie, action, message, icon, details, uids)  asks the agent
    Cancel(cookie)                                       CancelAuthentication
    Result(cookie) -> "pending" | "ok" | "error:<name>"
    Registrations() -> [(kind, session-id, locale, path)]

    fake_polkitd.py ADDRESS [--reject] [--backend NAME]
        --reject: "agent already registered"
        --backend: the backend name to report (the real polkitd says "js")
"""

import sys

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

AUTHORITY = "org.freedesktop.PolicyKit1.Authority"
AGENT = "org.freedesktop.PolicyKit1.AuthenticationAgent"


class Failed(dbus.DBusException):
    _dbus_error_name = "org.freedesktop.PolicyKit1.Error.Failed"


class Authority(dbus.service.Object):
    def __init__(self, bus, reject, backend):
        super().__init__(bus, "/org/freedesktop/PolicyKit1/Authority")
        self.bus = bus
        self.reject = reject
        self.backend = backend
        self.agent = None  # (bus name, path)
        self.registrations = []

    def _register(self, subject, locale, path, sender):
        kind, info = subject
        session = str(info.get("session-id", ""))
        self.registrations.append((str(kind), session, str(locale), str(path)))
        if self.reject:
            raise Failed("An authentication agent already exists for the given subject")
        self.agent = (sender, str(path))

    @dbus.service.method(AUTHORITY, in_signature="(sa{sv})so", out_signature="", sender_keyword="sender")
    def RegisterAuthenticationAgent(self, subject, locale, path, sender=None):
        self._register(subject, locale, path, sender)

    @dbus.service.method(AUTHORITY, in_signature="(sa{sv})soa{sv}", out_signature="", sender_keyword="sender")
    def RegisterAuthenticationAgentWithOptions(self, subject, locale, path, options, sender=None):
        self._register(subject, locale, path, sender)

    @dbus.service.method(AUTHORITY, in_signature="(sa{sv})s", out_signature="")
    def UnregisterAuthenticationAgent(self, subject, path):
        self.agent = None

    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        return {"BackendName": self.backend, "BackendVersion": "0", "BackendFeatures": dbus.UInt32(0)}


class Control(dbus.service.Object):
    def __init__(self, bus, authority):
        super().__init__(bus, "/org/vela/Test")
        self.bus = bus
        self.authority = authority
        self.results = {}

    def _agent(self):
        if not self.authority.agent:
            raise Failed("no agent registered")
        name, path = self.authority.agent
        return dbus.Interface(self.bus.get_object(name, path, introspect=False), AGENT)

    @dbus.service.method("org.vela.Test", in_signature="ssssa{ss}au", out_signature="")
    def Begin(self, cookie, action, message, icon, details, uids):
        identities = dbus.Array([dbus.Struct(("unix-user", {"uid": dbus.UInt32(uid)}), signature="sa{sv}")
                                 for uid in uids], signature="(sa{sv})")
        self.results[str(cookie)] = "pending"

        def done(*_):
            self.results[str(cookie)] = "ok"

        def failed(error):
            self.results[str(cookie)] = "error:" + (error.get_dbus_name() or "?")

        self._agent().BeginAuthentication(action, message, icon, dbus.Dictionary(details, signature="ss"),
                                          cookie, identities, reply_handler=done, error_handler=failed,
                                          timeout=600)

    @dbus.service.method("org.vela.Test", in_signature="s", out_signature="")
    def Cancel(self, cookie):
        self._agent().CancelAuthentication(cookie, reply_handler=lambda *_: None, error_handler=lambda *_: None)

    @dbus.service.method("org.vela.Test", in_signature="s", out_signature="s")
    def Result(self, cookie):
        return self.results.get(str(cookie), "unknown")

    @dbus.service.method("org.vela.Test", in_signature="", out_signature="a(ssss)")
    def Registrations(self):
        return self.authority.registrations


def main():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.bus.BusConnection(sys.argv[1])
    backend = sys.argv[sys.argv.index("--backend") + 1] if "--backend" in sys.argv else "vela-fake-polkitd"
    authority = Authority(bus, "--reject" in sys.argv, backend)
    Control(bus, authority)
    # Names only after the objects: whoever waits for the name finds everything ready.
    names = [dbus.service.BusName("org.freedesktop.PolicyKit1", bus), dbus.service.BusName("org.vela.Test", bus)]
    print("ready", flush=True)
    GLib.MainLoop().run()
    del names


if __name__ == "__main__":
    main()
