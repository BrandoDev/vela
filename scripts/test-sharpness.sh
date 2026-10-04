#!/bin/sh
# Test della nitidezza (docs/renderer.md §3.10): a ogni scala, la finestra di
# vela-pattern deve arrivare sullo schermo bit per bit, appena aperta,
# agganciata a sinistra e a destra, massimizzata e ripristinata.
#
# Uso: scripts/test-sharpness.sh [SCALE...]   (predefinite: 1 1.25 1.5 1.75 2)
# Serve Python 3 con numpy e Pillow.

set -u
cd "$(dirname "$0")/.."
BUILD=build
SCALES="${*:-1 1.25 1.5 1.75 2}"
TMP=$(mktemp -d)
FAILED=0

for scale in $SCALES; do
    echo "== scala $scale"
    # Gli angoli arrotondati (raggio 8 logici) toccano solo quei quadrati.
    CORNER=$(python3 -c "import math; print(math.ceil(8 * $scale) + 1)")
    WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 VELA_DEBUG_INPUT=1 VELA_SCALE=$scale \
        VELA_OUTPUT_SIZE=2560x1440@60 $BUILD/compositor/vela-compositor > "$TMP/log" 2>&1 &
    PID=$!
    for i in 1 2 3 4 5 6 7 8 9 10; do
        sleep 0.2
        DISPLAY_NAME=$(grep -o "WAYLAND_DISPLAY=[a-z0-9-]*" "$TMP/log" | head -1 | cut -d= -f2)
        [ -n "$DISPLAY_NAME" ] && break
    done
    export WAYLAND_DISPLAY=$DISPLAY_NAME
    $BUILD/tools/vela-pattern 401 301 2> "$TMP/pattern.log" &
    CLIENT=$!
    sleep 1

    check() {
        sleep 0.8
        $BUILD/tools/vela-shot "$TMP/shot.png" || { echo "cattura fallita"; FAILED=1; return; }
        python3 scripts/sharpness-check.py "$TMP/shot.png" "$1" "$CORNER" || FAILED=1
    }
    check aperta
    $BUILD/tools/vela-input key super+Left; check "agganciata a sinistra"
    $BUILD/tools/vela-input key super+Right key super+Right; check "agganciata a destra"
    $BUILD/tools/vela-input key super+Up; check massimizzata
    $BUILD/tools/vela-input key super+Down; check ripristinata

    kill $CLIENT 2> /dev/null
    kill $PID
    wait $PID 2> /dev/null
done
rm -rf "$TMP"
[ $FAILED -eq 0 ] && echo "Tutto bit per bit." || echo "Ci sono pixel diversi."
exit $FAILED
