// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Le voci dei menu che aprono un pezzo del sistema (Win+X, orologio,
// taskbar...), tradotte nei programmi Linux che fanno la stessa cosa
// (docs/renderer.md §14.11). Una voce senza programma installato è
// "non disponibile" e il menu la mostra disattivata.
class SystemActions : public QObject {
    Q_OBJECT

public:
    explicit SystemActions(QObject* parent = nullptr);

    // Nomi: installed-apps, mobility, power, events, system, devices,
    // network, disks, computer, terminal, terminal-admin, task-manager,
    // settings, files, datetime, display, notification-settings,
    // taskbar-settings, personalize.
    Q_INVOKABLE bool available(const QString& name) const;
    Q_INVOKABLE bool trigger(const QString& name) const;
    // Un portatile (una batteria che non è quella di un mouse o simili).
    Q_INVOKABLE bool isLaptop() const;

    // "Esegui" (Win+R): un programma, una cartella, un documento o un URL.
    Q_INVOKABLE bool run(const QString& text);
    Q_INVOKABLE QStringList runHistory() const;

    // "Apri percorso file": la cartella nel file manager, col file selezionato.
    Q_INVOKABLE void showInFolder(const QString& pathOrUrl) const;
    Q_INVOKABLE void openUrl(const QString& url) const;
    Q_INVOKABLE void openTerminal(const QString& directory) const;
    // La cartella Desktop (o la home, se non c'è).
    Q_INVOKABLE QString desktopDirectory() const;

    // "Disinstalla": col gestore dell'app (Discover, Flatpak o il gestore
    // dei pacchetti in un terminale, dove si conferma come in Windows).
    Q_INVOKABLE bool canUninstall(const QString& desktopFile) const;
    Q_INVOKABLE void uninstall(const QString& desktopFile) const;

private:
    QString command(const QString& name) const;
};

// Il terminale preferito e le righe di comando per usarlo.
QString terminalProgram();
