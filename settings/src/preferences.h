#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QObject>
#include <QStringList>
#include <QVariantList>

// Le scelte dell'utente, nei due file che le contengono:
// ~/.config/Vela/vela-shell.conf per la shell (sfondo, accento, modalità,
// taskbar, notifiche), che la shell rilegge appena cambia;
// ~/.config/vela/vela.conf per il compositor (inattività, tastiera, Luce
// notturna, accessibilità, tearing), che lo rilegge al comando
// "reload-config". vela.conf lo scrive anche il compositor (le impostazioni
// rapide accendono Luce notturna, filtri e tasti permanenti): si riscrivono
// solo le chiavi cambiate, sul file appena riletto, e lo si osserva. In più
// la modalità chiara o scura delle app passa a KDE (colori, icone) e a GTK
// (gsettings).
class Preferences : public QObject {
    Q_OBJECT
    // --- Personalizzazione ---
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QStringList recentWallpapers READ recentWallpapers NOTIFY wallpaperChanged)
    Q_PROPERTY(QStringList systemWallpapers READ systemWallpapers CONSTANT)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(QString appTheme READ appTheme WRITE setAppTheme NOTIFY appThemeChanged)
    // La modalità di Vela (taskbar, menu, pannelli): "dark" o "light".
    Q_PROPERTY(QString shellTheme READ shellTheme WRITE setShellTheme NOTIFY appThemeChanged)
    // Le Impostazioni stesse sono chiare (seguono la modalità delle app).
    Q_PROPERTY(bool light READ light NOTIFY appThemeChanged)
    // "l/" o "d/" per image://icon, un attimo dopo la modalità (applyIconTheme).
    Q_PROPERTY(QString iconMode READ iconMode NOTIFY iconModeChanged)
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
    // --- Schermo: Luce notturna e giochi ---
    Q_PROPERTY(bool nightLight READ nightLight WRITE setNightLight NOTIFY nightLightChanged)
    Q_PROPERTY(int nightStrength READ nightStrength WRITE setNightStrength NOTIFY nightLightChanged)
    // "no", "tramonto" (dal tramonto all'alba) o "ore"
    Q_PROPERTY(QString nightSchedule READ nightSchedule WRITE setNightSchedule NOTIFY nightLightChanged)
    Q_PROPERTY(QString nightFrom READ nightFrom WRITE setNightFrom NOTIFY nightLightChanged) // "21:00"
    Q_PROPERTY(QString nightTo READ nightTo WRITE setNightTo NOTIFY nightLightChanged)
    // Le ore del sole di oggi (le calcola il compositor), vuote se non si sanno.
    Q_PROPERTY(QString sunset READ sunset NOTIFY nightLightChanged)
    Q_PROPERTY(QString sunrise READ sunrise NOTIFY nightLightChanged)
    Q_PROPERTY(bool tearing READ tearing WRITE setTearing NOTIFY accessibilityChanged)
    // --- Accessibilità ---
    Q_PROPERTY(bool magnifier READ magnifier WRITE setMagnifier NOTIFY accessibilityChanged)
    Q_PROPERTY(int magnifierStep READ magnifierStep WRITE setMagnifierStep NOTIFY accessibilityChanged)
    Q_PROPERTY(bool colorFilter READ colorFilter WRITE setColorFilter NOTIFY accessibilityChanged)
    // grigi, deuteranopia, protanopia, tritanopia
    Q_PROPERTY(QString colorFilterKind READ colorFilterKind WRITE setColorFilterKind NOTIFY accessibilityChanged)
    Q_PROPERTY(bool colorFilterShortcut READ colorFilterShortcut WRITE setColorFilterShortcut NOTIFY accessibilityChanged)
    Q_PROPERTY(bool stickyKeys READ stickyKeys WRITE setStickyKeys NOTIFY accessibilityChanged)
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
    QString shellTheme() const { return m_shellTheme; }
    void setShellTheme(const QString& theme);
    bool light() const { return m_appTheme == QLatin1String("light"); }
    QString iconMode() const { return m_iconMode; }
    void setIconMode(const QString& mode)
    {
        if (mode != m_iconMode) {
            m_iconMode = mode;
            emit iconModeChanged();
        }
    }

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

    bool nightLight() const { return m_nightLight; }
    void setNightLight(bool on);
    int nightStrength() const { return m_nightStrength; }
    void setNightStrength(int strength);
    QString nightSchedule() const { return m_nightSchedule; }
    void setNightSchedule(const QString& schedule);
    QString nightFrom() const { return m_nightFrom; }
    void setNightFrom(const QString& time);
    QString nightTo() const { return m_nightTo; }
    void setNightTo(const QString& time);
    QString sunset() const { return m_sunset; }
    QString sunrise() const { return m_sunrise; }
    bool tearing() const { return m_tearing; }
    void setTearing(bool on);

    bool magnifier() const { return m_magnifier; }
    void setMagnifier(bool on);
    int magnifierStep() const { return m_magnifierStep; }
    void setMagnifierStep(int percent);
    bool colorFilter() const { return m_colorFilter; }
    void setColorFilter(bool on);
    QString colorFilterKind() const { return m_colorFilterKind; }
    void setColorFilterKind(const QString& kind);
    bool colorFilterShortcut() const { return m_colorFilterShortcut; }
    void setColorFilterShortcut(bool on);
    bool stickyKeys() const { return m_stickyKeys; }
    void setStickyKeys(bool on);

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
    void nightLightChanged();
    void iconModeChanged();
    void accessibilityChanged();

private:
    void setShell(const QString& key, const QVariant& value);
    // vela.conf: riletto, cambiate solo queste chiavi, poi "reload-config".
    void saveCompositor(const QList<QPair<QString, QString>>& changes);
    void saveCompositorKeys(const QStringList& keys); // i valori di ora di queste chiavi
    QString compositorValue(const QString& key) const;
    void reloadCompositor(); // vela.conf e lo stato del compositor
    void queryCompositor(); // Luce notturna, lente...: lo stato di adesso
    void watchConfig();
    void saveLayouts();
    void computeMica();

    QString m_wallpaper;
    QStringList m_recentWallpapers;
    QColor m_accent;
    QString m_appTheme;
    QString m_shellTheme;
    QString m_iconMode = QStringLiteral("d/");
    QString m_taskbarAlignment;
    bool m_endTask = true;
    bool m_taskView = true;
    bool m_doNotDisturb = false;
    int m_screenOffMinutes = 10;
    bool m_lockOnIdle = true;
    QVariantList m_layouts;
    int m_repeatDelay = 400;
    int m_repeatRate = 30;
    QColor m_tint; // il colore medio dello sfondo
    QColor m_mica;
    QColor m_micaInactive;
    bool m_nightLight = false;
    int m_nightStrength = 48;
    QString m_nightSchedule = QStringLiteral("no");
    QString m_nightFrom = QStringLiteral("21:00");
    QString m_nightTo = QStringLiteral("07:00");
    QString m_sunset;
    QString m_sunrise;
    bool m_tearing = true;
    bool m_magnifier = false;
    int m_magnifierStep = 100;
    bool m_colorFilter = false;
    QString m_colorFilterKind = QStringLiteral("grigi");
    bool m_colorFilterShortcut = false;
    bool m_stickyKeys = false;
    QList<QPair<QString, QString>> m_compositor; // vela.conf, nell'ordine del file
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

// Manda un comando al compositor (vela-<display>.sock).
void sendToCompositor(const QByteArray& command);
