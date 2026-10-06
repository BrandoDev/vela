// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Gli schermi, per la pagina Sistema > Schermo: li legge e li cambia con
// wlr-randr (protocollo wlr-output-management, che Vela implementa e
// ricorda). Ogni modifica si può annullare: come Windows, la pagina chiede
// "Mantenere queste impostazioni?" e senza risposta torna indietro.
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
    // Cambia uno schermo: chiavi facoltative enabled, width/height/refresh,
    // scale, transform, x/y. Ricorda com'era, per revert().
    Q_INVOKABLE bool apply(const QString& name, const QVariantMap& changes);
    // Torna com'era prima dell'ultimo apply().
    Q_INVOKABLE void revert();

signals:
    void outputsChanged();

private:
    static QStringList argumentsFor(const QVariantMap& output);
    bool run(const QStringList& arguments);

    bool m_available = false;
    QVariantList m_outputs;
    QVariantList m_before; // prima dell'ultima modifica
};
