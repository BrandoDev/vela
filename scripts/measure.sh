#!/bin/sh
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

# Le misure degli "Obiettivi misurabili" del README, ripetibili: memoria di
# compositor e shell, CPU a riposo e risvegli al secondo, consumo dalla
# batteria, e a richiesta l'avvio a freddo di Esplora.
#
# Uso: scripts/measure.sh [-s SECONDI] [-d WAYLAND_DISPLAY] [--esplora] [--json]
#
#   -s SECONDI   quanto osservare la CPU e la batteria (predefinito 30): non
#                toccare mouse e tastiera nel frattempo, è "a riposo".
#   -d DISPLAY   la sessione di Vela da misurare (predefinita quella da cui
#                parte lo script, $WAYLAND_DISPLAY).
#   --esplora    apre e chiude Esplora 5 volte e misura il primo fotogramma
#                (compaiono finestre).
#   --json       una riga JSON invece della tabella (per confrontare nel tempo).
#
# Ciò che si misura:
#   memoria   PSS da /proc/PID/smaps_rollup: la memoria propria più la parte
#             delle librerie condivise che spetta al processo (la misura
#             giusta per "quanto pesa");
#   CPU       tempo di CPU (utente + sistema) nell'intervallo, in percento di
#             un core;
#   risvegli  cambi di contesto volontari al secondo: quante volte il
#             processo si sveglia senza che succeda nulla;
#   batteria  potenza media scaricata (W) da /sys/class/power_supply, solo se
#             si è a batteria.

set -eu

SECONDS_IDLE=30
DISPLAY_NAME="${WAYLAND_DISPLAY:-}"
ESPLORA=0
JSON=0
while [ $# -gt 0 ]; do
    case "$1" in
    -s) SECONDS_IDLE="$2"; shift 2 ;;
    -d) DISPLAY_NAME="$2"; shift 2 ;;
    --esplora) ESPLORA=1; shift ;;
    --json) JSON=1; shift ;;
    -h|--help) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Opzione sconosciuta: $1 (vedi --help)" >&2; exit 2 ;;
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
    echo "Non trovo una shell di Vela${DISPLAY_NAME:+ su $DISPLAY_NAME} (vedi -d)." >&2
    exit 1
fi
COMPOSITOR_PID=$(awk '{ print $4 }' "/proc/$SHELL_PID/stat")
if [ "$(cat "/proc/$COMPOSITOR_PID/comm" 2>/dev/null)" != "vela-compositor" ]; then
    echo "Il padre della shell ($COMPOSITOR_PID) non è vela-compositor." >&2
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
[ "$JSON" = 1 ] || echo "Vela su $DISPLAY_NAME: osservo per $SECONDS_IDLE s, non toccare mouse e tastiera..." >&2
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
if [ "$ESPLORA" = 1 ]; then
    FILES=$(command -v vela-files || true)
    [ -n "$FILES" ] || FILES="$(dirname "$0")/../build/explorer/vela-files"
    times=""
    for _ in 1 2 3 4 5; do
        t=$(WAYLAND_DISPLAY="$DISPLAY_NAME" VELA_FILES_TIMING=1 timeout 4 "$FILES" 2>&1 \
            | sed -n 's/.*, \([0-9]*\) ms dall.avvio del processo.*/\1/p' | head -n 1 || true)
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

printf '\n%-16s %12s %12s %16s\n' "" "memoria" "CPU a riposo" "risvegli/s"
printf '%-16s %8s MiB %11s%% %16s\n' "vela-compositor" "$MEM_C" "$CPU_C" "$WAKE_C"
printf '%-16s %8s MiB %11s%% %16s\n' "vela-shell" "$MEM_S" "$CPU_S" "$WAKE_S"
echo
if [ -n "$POWER" ]; then
    echo "Batteria: $POWER W in media (tutto il computer)"
else
    echo "Batteria: non misurata (nessuna batteria che si scarica: si è a rete)"
fi
if [ "$ESPLORA" = 1 ]; then
    echo "Esplora: primo fotogramma a ${FILES_MS:-?} ms dall'avvio del processo (mediana di 5)"
fi
