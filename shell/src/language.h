// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// La lingua dell'interfaccia di Vela: italiano o inglese.
//
// Si sceglie in Impostazioni > Ora e lingua > Lingua, chiave language= di
// ~/.config/vela/vela.conf: "it", "en" o niente (come il sistema: italiano
// se il sistema è in italiano, inglese altrimenti). Le stringhe del codice
// sono in inglese; l'italiano è una traduzione di Qt (i18n/*_it.ts, compilata
// nell'eseguibile). Insieme alla lingua cambia il locale predefinito: date,
// mesi e giorni seguono la scelta.
//
// Ogni app guarda vela.conf: quando la lingua cambia, la traduzione si
// ricarica e `changed` ritraduce l'interfaccia (QQmlEngine::retranslate),
// senza riavviare nulla.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QStandardPaths>
#include <QString>
#include <QTranslator>

#include <functional>

namespace vela::language {

inline QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/vela/vela.conf");
}

// La scelta in vela.conf: "it", "en", o vuota (come il sistema).
inline QString configured()
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QString value;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        // "lingua=" è il nome di prima (compositor/src/legacy_names.c).
        if (line.startsWith(QLatin1String("language="))) {
            value = line.mid(9).trimmed();
        } else if (line.startsWith(QLatin1String("lingua="))) {
            value = line.mid(7).trimmed();
        }
    }
    return value == QLatin1String("it") || value == QLatin1String("en") ? value : QString();
}

// La lingua da usare: "it" o "en".
inline QString effective()
{
    const QString chosen = configured();
    if (!chosen.isEmpty()) {
        return chosen;
    }
    return QLocale::system().language() == QLocale::Italian ? QStringLiteral("it") : QStringLiteral("en");
}

// Carica la traduzione `name` (la risorsa :/i18n/<name>_it.qm) e la tiene
// allineata a vela.conf. Da chiamare una volta, dopo aver creato l'app.
inline void install(const QString& name, std::function<void()> changed = {})
{
    struct State {
        QString name;
        std::function<void()> changed;
        QTranslator* translator = nullptr;
        QString current;
    };
    auto* state = new State { name, std::move(changed), nullptr, {} };
    auto apply = [state] {
        const QString lang = effective();
        if (lang == state->current) {
            return;
        }
        const bool first = state->current.isEmpty();
        state->current = lang;
        if (state->translator) {
            QCoreApplication::removeTranslator(state->translator);
            delete state->translator;
            state->translator = nullptr;
        }
        const QLocale::Territory territory = QLocale::system().territory();
        QLocale::setDefault(QLocale(lang == QLatin1String("en") ? QLocale::English : QLocale::Italian, territory));
        if (lang == QLatin1String("it")) {
            state->translator = new QTranslator(QCoreApplication::instance());
            if (state->translator->load(QStringLiteral(":/i18n/%1_it.qm").arg(state->name))) {
                QCoreApplication::installTranslator(state->translator);
            }
        }
        if (!first && state->changed) {
            state->changed();
        }
    };
    apply();

    // vela.conf si sostituisce con una rename: si guarda anche la cartella,
    // e il file si riaggiunge ogni volta che ricompare.
    auto* watcher = new QFileSystemWatcher(QCoreApplication::instance());
    const QString path = configPath();
    const QString directory = QFileInfo(path).absolutePath();
    QDir().mkpath(directory);
    watcher->addPath(directory);
    if (QFile::exists(path)) {
        watcher->addPath(path);
    }
    auto refresh = [watcher, path, apply] {
        if (QFile::exists(path) && !watcher->files().contains(path)) {
            watcher->addPath(path);
        }
        apply();
    };
    QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, watcher, refresh);
    QObject::connect(watcher, &QFileSystemWatcher::fileChanged, watcher, refresh);
}

} // namespace vela::language
