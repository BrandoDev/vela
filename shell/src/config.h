#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// Le scelte dell'utente che la shell segue dal vivo: stanno in
// ~/.config/Vela/vela-shell.conf, le scrive l'app Impostazioni (o la shell
// stessa) e qui si rileggono appena il file cambia. Dal QML: Config.accent,
// Config.wallpaper...
class Config : public QObject {
    Q_OBJECT
    // Lo sfondo: un'immagine scelta, altrimenti quello di Vela.
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY wallpaperChanged)
    // Il colore d'accento (pulsanti, selezioni, riquadri accesi).
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
    // "center" o "left", come "Allineamento della barra delle applicazioni".
    Q_PROPERTY(QString taskbarAlignment READ taskbarAlignment NOTIFY taskbarChanged)
    // "Termina attività" nel menu dei pulsanti della taskbar.
    Q_PROPERTY(bool endTask READ endTask NOTIFY taskbarChanged)

public:
    explicit Config(QObject* parent = nullptr);

    QString wallpaper() const { return m_wallpaper; }
    QColor accent() const { return m_accent; }
    QString taskbarAlignment() const { return m_taskbarAlignment; }
    bool endTask() const { return m_endTask; }
    bool doNotDisturb() const { return m_doNotDisturb; }

    static QColor defaultAccent() { return QColor(0x5b, 0x8c, 0xff); }
    static QString defaultWallpaper() { return QStringLiteral(":/vela/images/vela_splash_169.svg"); }

signals:
    void wallpaperChanged();
    void accentChanged();
    void taskbarChanged();
    void doNotDisturbChanged(bool on);

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
    bool m_doNotDisturb = false;
};
