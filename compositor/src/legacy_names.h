// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_LEGACY_NAMES_H
#define VELA_LEGACY_NAMES_H

// The Italian names vela.conf had until October 2026 and the English ones that
// replaced them. Whoever reads the file (the compositor, Settings) converts on
// the fly; at startup the compositor rewrites the file with the new names
// (vela_config_migrate). No dependencies: Settings, written in Qt, compiles it
// too.

// The current name of the key, or NULL if it already is the right one.
const char *vela_legacy_key(const char *key);

// The current value for that key (under its current name), or NULL.
const char *vela_legacy_value(const char *key, const char *value);

#endif
