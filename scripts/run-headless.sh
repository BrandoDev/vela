#!/bin/sh
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

# Avvia Vela senza finestra (backend "headless" di wlroots): nulla compare
# sullo schermo, ma le app si aprono e si può guardare e comandare tutto con
# gli strumenti in tools/ (vela-shot, vela-windows, vela-input).
# Serve per i test automatici, anche mentre usi il computer.
#
# Uso: scripts/run-headless.sh [cartella-di-build]
# Il log dice su quale WAYLAND_DISPLAY gira; chiudi con Ctrl+C o SIGTERM.

set -eu

BUILD_DIR="${1:-build}"
COMPOSITOR="$BUILD_DIR/compositor/vela-compositor"
SHELL_BIN="$BUILD_DIR/shell/vela-shell"

if [ ! -x "$COMPOSITOR" ]; then
    echo "Non trovo $COMPOSITOR: compila prima (vedi README)." >&2
    exit 1
fi

export WLR_BACKENDS=headless
export WLR_LIBINPUT_NO_DEVICES=1
export VELA_OUTPUT_SIZE="${VELA_OUTPUT_SIZE:-1920x1080}"
# Mouse e tastiera virtuali per vela-input.
export VELA_DEBUG_INPUT=1
# Le app avviate da qui non devono provare a riconnettersi altrove.
unset QT_WAYLAND_RECONNECT

if [ -x "$SHELL_BIN" ]; then
    SHELL_PATH="$(cd "$(dirname "$SHELL_BIN")" && pwd)/$(basename "$SHELL_BIN")"
    exec "$COMPOSITOR" -s "$SHELL_PATH"
else
    exec "$COMPOSITOR"
fi
