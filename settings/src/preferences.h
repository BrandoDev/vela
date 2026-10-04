#pragma once

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QVariantList>

// Le scelte dell'utente, nei due file che le contengono:
// ~/.config/Vela/vela-shell.conf per la shell (sfondo, accento, taskbar,
// notifiche), che la shell rilegge appena cambia; ~/.config/vela/vela.conf
// per il compositor (inattività, tastiera), che lo rilegge al comando
// "reload-config". In più la modalità chiara o scura delle app, che si
// passa a KDE (colori) e a GTK (gsettings).
class Preferences : public QObject {
    Q_OBJECT
    // --- Personalizzazione ---
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QStringList recentWallpapers READ recentWallpapers NOTIFY wallpaperChanged)
    Q_PROPERTY(QStringList systemWallpapers READ systemWallpapers CONSTANT)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(QString appTheme READ appTheme WRITE setAppTheme NOTIFY appThemeChanged)
    Q_PROPERTY(QString taskbarAlignment READ taskbarAlignment WRITE setTaskbarAlignment NOTIFY taskbarChanged)
    Q_PROPERTY(bool endTask READ endTask WRITE setEndTask NOTIFY taskbarChanged)
    Q_PROPERTY(bool taskView READ taskView WRITE setTaskView NOTIFY taskbarChanged)
    // --- Notifiche ---
    Q_PROPERTY(bool doNotDisturb READ doNotDisturb WRITE setDoNotDisturb NOTIFY doNotDisturbChanged)
    // --- Alimentazione ---
    Q_PROPERTY(int screenOffMinutes READ screenOffMinutes WRITE setScreenOffMinutes NOTIFY idleChanged)
    Q_PROPERTY(bool lockOnIdle READ lockOnIdle WRITE setLockOnIdle NOTIFY idleChanged)
    // --- Tastiera ---
    // [{layout, variant}] nell'ordine scelto; il primo è quello di partenza.
    Q_PROPERTY(QVariantList keyboardLayouts READ keyboardLayouts NOTIFY keyboardChanged)
    Q_PROPERTY(int repeatDelay READ repeatDelay WRITE setRepeatDelay NOTIFY keyboardChanged)
    Q_PROPERTY(int repeatRate READ repeatRate WRITE setRepeatRate NOTIFY keyboardChanged)
    // Il colore Mica della barra del titolo (lo stesso calcolo del
    // compositor, decoration.cpp): così il contenuto della finestra continua
    // la barra senza stacchi.
    Q_PROPERTY(QColor mica READ mica NOTIFY wallpaperChanged)
    Q_PROPERTY(QColor micaInactive READ micaInactive NOTIFY wallpaperChanged)

public:
    explicit Preferences(QObject* parent = nullptr);

    QString wallpaper() const { return m_wallpaper; }
    QStringList recentWallpapers() const { return m_recentWallpapers; }
    QStringList systemWallpapers() const;
    Q_INVOKABLE void setWallpaper(const QString& pathOrUrl);

    QColor accent() const { return m_accent; }
    void setAccent(const QColor& color);

    QString appTheme() const { return m_appTheme; }
    void setAppTheme(const QString& theme);

    QString taskbarAlignment() const { return m_taskbarAlignment; }
    void setTaskbarAlignment(const QString& alignment);
    bool endTask() const { return m_endTask; }
    void setEndTask(bool on);
    bool taskView() const { return m_taskView; }
    void setTaskView(bool on);

    bool doNotDisturb() const { return m_doNotDisturb; }
    void setDoNotDisturb(bool on);

    int screenOffMinutes() const { return m_screenOffMinutes; }
    void setScreenOffMinutes(int minutes);
    bool lockOnIdle() const { return m_lockOnIdle; }
    void setLockOnIdle(bool on);

    QVariantList keyboardLayouts() const { return m_layouts; }
    Q_INVOKABLE void addKeyboardLayout(const QString& layout, const QString& variant);
    Q_INVOKABLE void removeKeyboardLayout(int index);
    Q_INVOKABLE void moveKeyboardLayoutUp(int index);
    int repeatDelay() const { return m_repeatDelay; }
    void setRepeatDelay(int ms);
    int repeatRate() const { return m_repeatRate; }
    void setRepeatRate(int perSecond);

    QColor mica() const { return m_mica; }
    QColor micaInactive() const { return m_micaInactive; }

    // Rilegge tutto (la shell o un altro programma possono aver cambiato qualcosa).
    Q_INVOKABLE void reload();

signals:
    void wallpaperChanged();
    void accentChanged();
    void appThemeChanged();
    void taskbarChanged();
    void doNotDisturbChanged();
    void idleChanged();
    void keyboardChanged();

private:
    void setShell(const QString& key, const QVariant& value);
    void saveCompositor(); // vela.conf, poi "reload-config"
    void saveLayouts();
    void computeMica();

    QString m_wallpaper;
    QStringList m_recentWallpapers;
    QColor m_accent;
    QString m_appTheme;
    QString m_taskbarAlignment;
    bool m_endTask = true;
    bool m_taskView = true;
    bool m_doNotDisturb = false;
    int m_screenOffMinutes = 10;
    bool m_lockOnIdle = true;
    QVariantList m_layouts;
    int m_repeatDelay = 400;
    int m_repeatRate = 30;
    QColor m_mica;
    QColor m_micaInactive;
    QList<QPair<QString, QString>> m_compositor; // vela.conf, nell'ordine del file
};

// Manda un comando al compositor (vela-<display>.sock).
void sendToCompositor(const QByteArray& command);
