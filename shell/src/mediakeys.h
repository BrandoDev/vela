// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusContext>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// The keyboard's media keys, whatever window has the focus, like Windows: they
// go to the media player (MPRIS on the session bus) that is playing, otherwise
// to the one that played last, otherwise to a paused one.
class MediaKeys : public QObject, protected QDBusContext {
    Q_OBJECT

public:
    explicit MediaKeys(QObject* parent = nullptr);

    // "play-pause", "pause", "stop", "next", "previous".
    void press(const QString& key);

private Q_SLOTS:
    void onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated);

private:
    // The bus name of the player the keys are for; empty if there is none.
    QString target() const;

    QString m_lastPlaying; // the unique bus name of the player that played last
};
