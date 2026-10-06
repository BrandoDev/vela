// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>

// I layout di tastiera che XKB conosce (evdev.xml), per "Aggiungi una
// tastiera": [{layout, variant, name}], ordinati per nome. Si leggono la
// prima volta che servono.
class KeyboardLayouts : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE QVariantList all();
    // Il nome da mostrare ("Italiano", "Inglese (USA, internazionale)...").
    Q_INVOKABLE QString name(const QString& layout, const QString& variant);

private:
    void load();
    QVariantList m_all;
};
