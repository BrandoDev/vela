// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// The user's choices the shell follows live: they're in
// ~/.config/Vela/vela-shell.conf, written by the Settings app (or the shell
// itself) and reread here as soon as the file changes. From QML:
// Config.accent, Config.wallpaper...
class Config : public QObject {
    Q_OBJECT
    // The wallpaper: a chosen image, otherwise Vela's.
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY wallpaperChanged)
    // The accent color (buttons, selections, lit tiles).
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
    // "center" or "left", like "Taskbar alignment".
    Q_PROPERTY(QString taskbarAlignment READ taskbarAlignment NOTIFY taskbarChanged)
    // "End task" in the taskbar buttons' menu.
    Q_PROPERTY(bool endTask READ endTask NOTIFY taskbarChanged)
    // The Task View button on the taskbar.
    Q_PROPERTY(bool taskView READ taskView NOTIFY taskbarChanged)
    // The taskbar on all outputs (like Windows) or only on the main one.
    Q_PROPERTY(bool taskbarAllScreens READ taskbarAllScreens NOTIFY taskbarChanged)
    // Clipboard history (Win+V).
    Q_PROPERTY(bool clipboardHistory READ clipboardHistory NOTIFY clipboardChanged)
    // "Choose your mode": "dark" or "light", for the shell (taskbar, menus,
    // panels) and for apps (their windows and title bars).
    Q_PROPERTY(QString shellTheme READ shellTheme NOTIFY themeChanged)
    Q_PROPERTY(QString appTheme READ appTheme NOTIFY themeChanged)
    // Icons for the shell's mode ("l/" or "d/" for image://icon): they change
    // a moment after the mode, once the icon caches have been emptied (see
    // switchIconMode).
    Q_PROPERTY(QString iconMode READ iconMode NOTIFY iconModeChanged)

public:
    explicit Config(QObject* parent = nullptr);

    QString wallpaper() const { return m_wallpaper; }
    QColor accent() const { return m_accent; }
    QString taskbarAlignment() const { return m_taskbarAlignment; }
    bool endTask() const { return m_endTask; }
    bool taskView() const { return m_taskView; }
    bool taskbarAllScreens() const { return m_taskbarAllScreens; }
    bool clipboardHistory() const { return m_clipboardHistory; }
    bool doNotDisturb() const { return m_doNotDisturb; }
    QString shellTheme() const { return m_shellTheme; }
    QString appTheme() const { return m_appTheme; }
    QString iconMode() const { return m_iconMode; }
    void setIconMode(const QString& mode)
    {
        if (mode != m_iconMode) {
            m_iconMode = mode;
            emit iconModeChanged();
        }
    }

signals:
    void wallpaperChanged();
    void accentChanged();
    void taskbarChanged();
    void doNotDisturbChanged(bool on);
    void themeChanged();
    void clipboardChanged();
    void iconModeChanged();

private:
    void reload();
    void watch();

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_path;
    QString m_wallpaper;
    QColor m_accent;
    QString m_taskbarAlignment;
    bool m_endTask = true;
    bool m_taskView = true;
    bool m_taskbarAllScreens = true;
    bool m_clipboardHistory = false;
    bool m_doNotDisturb = false;
    QString m_shellTheme = QStringLiteral("dark");
    QString m_appTheme = QStringLiteral("dark");
    QString m_iconMode = QStringLiteral("d/");
};
