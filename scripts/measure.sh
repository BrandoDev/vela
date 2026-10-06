#!/bin/sh
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

# Repeatable measurements for the README's performance table: memory of the
# compositor and the shell, idle CPU and wakeups per second, battery power
# draw, and on request the cold start of File Explorer.
#
# Usage: scripts/measure.sh [-s SECONDS] [-d WAYLAND_DISPLAY] [--files] [--json]
#
#   -s SECONDS   how long to watch CPU and battery (default 30): don't touch
#                the mouse or keyboard meanwhile, it's "idle".
#   -d DISPLAY   the Vela session to measure (default: the one the script
#                runs in, $WAYLAND_DISPLAY).
#   --files      opens and closes File Explorer 5 times and measures its
#                first frame (windows appear).
#   --json       one JSON line instead of the table (to compare over time).
#
# What is measured:
#   memory    PSS from /proc/PID/smaps_rollup: the process's own memory plus
#             its share of the shared libraries (the right measure for
#             "how much it weighs");
#   CPU       CPU time (user + system) over the interval, as a percentage
#             of one core;
#   wakeups   voluntary context switches per second: how often the process
#             wakes up while nothing happens;
#   battery   average power drawn (W) from /sys/class/power_supply, only
#             on battery.

set -eu

SECONDS_IDLE=30
DISPLAY_NAME="${WAYLAND_DISPLAY:-}"
FILES_RUN=0
JSON=0
while [ $# -gt 0 ]; do
    case "$1" in
    -s) SECONDS_IDLE="$2"; shift 2 ;;
    -d) DISPLAY_NAME="$2"; shift 2 ;;
    --files) FILES_RUN=1; shift ;;
    --json) JSON=1; shift ;;
    -h|--help) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done

# La shell di quella sessione (la lancia il compositor con WAYLAND_DISPLAY
# già impostato) e il compositor, che ne è il padre.
SHELL_PID=""
for pid in $(pgrep -u "$(id -u)" -x vela-shell || true); do
    display=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | sed -n 's/^WAYLAND_DISPLAY=//p')
    if [ -z "$DISPLAY_NAME" ] || [ "$display" = "$DISPLAY_NAME" ]; then
        SHELL_PID=$pid
        DISPLAY_NAME=$display
        break
    fi
done
if [ -z "$SHELL_PID" ]; then
    echo "No Vela shell found${DISPLAY_NAME:+ on $DISPLAY_NAME} (see -d)." >&2
    exit 1
fi
COMPOSITOR_PID=$(awk '{ print $4 }' "/proc/$SHELL_PID/stat")
if [ "$(cat "/proc/$COMPOSITOR_PID/comm" 2>/dev/null)" != "vela-compositor" ]; then
    echo "The shell's parent ($COMPOSITOR_PID) isn't vela-compositor." >&2
    exit 1
fi

pss_mib() {
    awk '/^Pss:/ { printf "%.1f", $2 / 1024 }' "/proc/$1/smaps_rollup"
}
cpu_ticks() {
    # utime + stime: campi 14 e 15, contando dopo il nome tra parentesi.
    sed 's/^.*) //' "/proc/$1/stat" | awk '{ print $12 + $13 }'
}
wakeups() {
    awk '/^voluntary_ctxt_switches:/ { print $2 }' "/proc/$1/status"
}
battery_uw() {
    # Potenza istantanea in µW di tutte le batterie che si scaricano; vuoto se a rete.
    total=""
    for bat in /sys/class/power_supply/BAT*; do
        [ -d "$bat" ] || continue
        [ "$(cat "$bat/status" 2>/dev/null)" = "Discharging" ] || continue
        if [ -r "$bat/power_now" ]; then
            p=$(cat "$bat/power_now")
        elif [ -r "$bat/current_now" ] && [ -r "$bat/voltage_now" ]; then
            p=$(( $(cat "$bat/current_now") * $(cat "$bat/voltage_now") / 1000000 ))
        else
            continue
        fi
        total=$(( ${total:-0} + p ))
    done
    echo "$total"
}

HZ=$(getconf CLK_TCK)
c0=$(cpu_ticks "$COMPOSITOR_PID"); s0=$(cpu_ticks "$SHELL_PID")
w0c=$(wakeups "$COMPOSITOR_PID"); w0s=$(wakeups "$SHELL_PID")
power_sum=0
power_samples=0
[ "$JSON" = 1 ] || echo "Vela on $DISPLAY_NAME: watching for $SECONDS_IDLE s, don't touch the mouse or keyboard..." >&2
i=0
while [ "$i" -lt "$SECONDS_IDLE" ]; do
    sleep 1
    p=$(battery_uw)
    if [ -n "$p" ]; then
        power_sum=$((power_sum + p))
        power_samples=$((power_samples + 1))
    fi
    i=$((i + 1))
done
c1=$(cpu_ticks "$COMPOSITOR_PID"); s1=$(cpu_ticks "$SHELL_PID")
w1c=$(wakeups "$COMPOSITOR_PID"); w1s=$(wakeups "$SHELL_PID")

cpu_percent() { awk -v d="$1" -v hz="$HZ" -v s="$SECONDS_IDLE" 'BEGIN { printf "%.2f", d / hz / s * 100 }'; }
per_second() { awk -v d="$1" -v s="$SECONDS_IDLE" 'BEGIN { printf "%.1f", d / s }'; }

MEM_C=$(pss_mib "$COMPOSITOR_PID"); MEM_S=$(pss_mib "$SHELL_PID")
CPU_C=$(cpu_percent $((c1 - c0))); CPU_S=$(cpu_percent $((s1 - s0)))
WAKE_C=$(per_second $((w1c - w0c))); WAKE_S=$(per_second $((w1s - w0s)))
if [ "$power_samples" -gt 0 ]; then
    POWER=$(awk -v sum="$power_sum" -v n="$power_samples" 'BEGIN { printf "%.2f", sum / n / 1000000 }')
else
    POWER=""
fi

# Avvio a freddo di Esplora: il primo fotogramma, dall'avvio del processo.
FILES_MS=""
if [ "$FILES_RUN" = 1 ]; then
    FILES=$(command -v vela-files || true)
    [ -n "$FILES" ] || FILES="$(dirname "$0")/../build/explorer/vela-files"
    times=""
    for _ in 1 2 3 4 5; do
        t=$(WAYLAND_DISPLAY="$DISPLAY_NAME" VELA_FILES_TIMING=1 timeout 4 "$FILES" 2>&1 \
            | sed -n 's/.*, \([0-9]*\) ms after process start.*/\1/p' | head -n 1 || true)
        [ -n "$t" ] && times="$times $t"
    done
    # La mediana delle cinque prove.
    FILES_MS=$(echo "$times" | tr ' ' '\n' | grep . | sort -n | awk '{ a[NR] = $1 } END { if (NR) print a[int((NR + 1) / 2)] }')
fi

if [ "$JSON" = 1 ]; then
    printf '{"date":"%s","display":"%s","seconds":%s,' "$(date -Iseconds)" "$DISPLAY_NAME" "$SECONDS_IDLE"
    printf '"compositor":{"pss_mib":%s,"cpu_percent":%s,"wakeups_per_s":%s},' "$MEM_C" "$CPU_C" "$WAKE_C"
    printf '"shell":{"pss_mib":%s,"cpu_percent":%s,"wakeups_per_s":%s},' "$MEM_S" "$CPU_S" "$WAKE_S"
    printf '"battery_w":%s,"files_first_frame_ms":%s}\n' "${POWER:-null}" "${FILES_MS:-null}"
    exit 0
fi

printf '\n%-16s %12s %12s %16s\n' "" "memory" "idle CPU" "wakeups/s"
printf '%-16s %8s MiB %11s%% %16s\n' "vela-compositor" "$MEM_C" "$CPU_C" "$WAKE_C"
printf '%-16s %8s MiB %11s%% %16s\n' "vela-shell" "$MEM_S" "$CPU_S" "$WAKE_S"
echo
if [ -n "$POWER" ]; then
    echo "Battery: $POWER W on average (the whole computer)"
else
    echo "Battery: not measured (no discharging battery: on AC power)"
fi
if [ "$FILES_RUN" = 1 ]; then
    echo "File Explorer: first frame ${FILES_MS:-?} ms after process start (median of 5)"
fi
