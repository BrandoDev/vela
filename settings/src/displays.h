// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Outputs, for the System > Display page: read and changed with wlr-randr (the
// wlr-output-management protocol, which Vela implements and remembers). Every
// change can be undone: like Windows, the page asks "Keep these display
// settings?" and without an answer goes back.
class Displays : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit Displays(QObject* parent = nullptr);

    bool available() const { return m_available; }
    // [{name, description, enabled, x, y, scale, transform, current:{width,height,refresh},
    //   modes:[{width,height,refresh,preferred,current}]}]
    QVariantList outputs() const { return m_outputs; }

    Q_INVOKABLE void refresh();
    // Changes one output: optional keys enabled, width/height/refresh, scale,
    // transform, x/y. Remembers how it was, for revert().
    Q_INVOKABLE bool apply(const QString& name, const QVariantMap& changes);
    // Goes back to how it was before the last apply().
    Q_INVOKABLE void revert();

signals:
    void outputsChanged();

private:
    static QStringList argumentsFor(const QVariantMap& output);
    bool run(const QStringList& arguments);

    bool m_available = false;
    QVariantList m_outputs;
    QVariantList m_before; // before the last change
};
