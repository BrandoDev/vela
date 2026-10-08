// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>

// The keyboard layouts XKB knows (evdev.xml), for "Add a keyboard": [{layout,
// variant, name}], sorted by name. Read the first time they're needed.
class KeyboardLayouts : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE QVariantList all();
    // The name to show ("Italian", "English (US, intl.)...").
    Q_INVOKABLE QString name(const QString& layout, const QString& variant);

private:
    void load();
    QVariantList m_all;
};
