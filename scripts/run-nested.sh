#!/bin/sh
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

# Avvia Vela dentro una finestra della tua sessione attuale (es. KDE),
# con la shell già caricata. Chiudi con Alt+Shift+Esc o chiudendo la finestra.
#
# Uso: scripts/run-nested.sh [cartella-di-build]

set -eu

BUILD_DIR="${1:-build}"
COMPOSITOR="$BUILD_DIR/compositor/vela-compositor"
SHELL_BIN="$BUILD_DIR/shell/vela-shell"

if [ ! -x "$COMPOSITOR" ]; then
    echo "$COMPOSITOR not found: build first (see README)." >&2
    exit 1
fi

# Layout della tastiera: quello italiano se non ne hai scelto un altro.
export XKB_DEFAULT_LAYOUT="${XKB_DEFAULT_LAYOUT:-it}"

if [ -x "$SHELL_BIN" ]; then
    SHELL_PATH="$(cd "$(dirname "$SHELL_BIN")" && pwd)/$(basename "$SHELL_BIN")"
    exec "$COMPOSITOR" -s "$SHELL_PATH"
else
    echo "Shell not built: starting only the compositor." >&2
    exec "$COMPOSITOR"
fi
