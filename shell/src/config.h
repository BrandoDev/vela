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
    // Il pulsante della Visualizzazione attività sulla taskbar.
    Q_PROPERTY(bool taskView READ taskView NOTIFY taskbarChanged)
    // La taskbar su tutti gli schermi (come Windows) o solo sul principale.
    Q_PROPERTY(bool taskbarAllScreens READ taskbarAllScreens NOTIFY taskbarChanged)
    // La cronologia degli Appunti (Win+V).
    Q_PROPERTY(bool clipboardHistory READ clipboardHistory NOTIFY clipboardChanged)
    // "Scegli la modalità": "dark" o "light", per la shell (taskbar, menu,
    // pannelli) e per le app (le loro finestre e le barre del titolo).
    Q_PROPERTY(QString shellTheme READ shellTheme NOTIFY themeChanged)
    Q_PROPERTY(QString appTheme READ appTheme NOTIFY themeChanged)
    // Le icone della modalità della shell ("l/" o "d/" per image://icon):
    // cambiano un attimo dopo la modalità, quando le cache delle icone sono
    // state svuotate (vedi applyIconTheme).
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

    static QColor defaultAccent() { return QColor(0x5b, 0x8c, 0xff); }
    static QString defaultWallpaper() { return QStringLiteral(":/vela/images/vela_splash_169.svg"); }

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
