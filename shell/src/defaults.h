// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// What vela-shell.conf holds before the user chooses: the shell, Settings,
// the apps' look (Appearance) and the lock screen start from the same values.

#include <QColor>
#include <QString>

namespace vela::defaults {

inline QColor accent()
{
    return QColor(0x5b, 0x8c, 0xff);
}

// Vela's own, inside the executable (qt_add_resources "images").
inline QString wallpaper()
{
    return QStringLiteral(":/vela/images/vela_splash_169.svg");
}

} // namespace vela::defaults
