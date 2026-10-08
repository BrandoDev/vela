// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_UTIL_H
#define VELA_UTIL_H

// Piccoli aiuti senza dipendenze, usati da tutto il compositor: il tempo
// in nanosecondi e le variabili d'ambiente.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define VELA_NS_PER_MS INT64_C(1000000)
#define VELA_NS_PER_SEC INT64_C(1000000000)

int64_t vela_timespec_ns(const struct timespec *t);
// CLOCK_MONOTONIC, in nanosecondi.
int64_t vela_now_ns(void);
struct timespec vela_ns_timespec(int64_t ns);

// La variabile c'è, non è vuota e non vale "0" (VELA_STATS=1).
bool vela_env_flag(const char *name);
// La variabile vale esattamente "0": spegne qualcosa che è acceso di
// solito (VELA_SCANOUT=0).
bool vela_env_off(const char *name);
// La variabile comincia con "1" (le diagnosi VELA_DEBUG_*=1).
bool vela_env_one(const char *name);
// Un intero, o `fallback` se la variabile manca o è vuota.
int vela_env_int(const char *name, int fallback);

// snprintf che dice se il testo ci sta: false se è stato tagliato (un
// percorso tagliato non va usato).
bool vela_format(char *out, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));

// Un array allocato con posto per almeno `needed` elementi da `size` byte:
// se non basta, la capacità raddoppia (realloc). Uso:
//   list = vela_grow(list, &capacity, count + 1, sizeof(*list));
void *vela_grow(void *items, int *capacity, int needed, size_t size);

// I nomi dei desktop di XDG_CURRENT_DESKTOP separati da ':' come vogliono le
// app ("Vela;KDE;" diventa "Vela:KDE": Plasma Login copia DesktopNames del
// .desktop così com'è). false se non ci sta in `size`.
bool vela_desktop_names(const char *current, char *out, size_t size);

static inline int vela_min(int a, int b) { return a < b ? a : b; }
static inline int vela_max(int a, int b) { return a > b ? a : b; }
static inline int vela_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline double vela_clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float vela_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

#endif
