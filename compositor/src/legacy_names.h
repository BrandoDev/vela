// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_LEGACY_NAMES_H
#define VELA_LEGACY_NAMES_H

// I nomi italiani che vela.conf ha avuto fino a ottobre 2026, e quelli
// inglesi che li hanno sostituiti. Chi legge il file (il compositor, le
// Impostazioni) converte al volo; il compositor all'avvio riscrive il file
// con i nomi nuovi (vela_config_migrate). Senza dipendenze: lo compilano
// anche le Impostazioni, che sono in Qt.

// Il nome di adesso della chiave, o NULL se è già quello giusto.
const char *vela_legacy_key(const char *key);

// Il valore di adesso per quella chiave (col nome di adesso), o NULL.
const char *vela_legacy_value(const char *key, const char *value);

#endif
