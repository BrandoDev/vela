#pragma once

#include <QDBusUnixFileDescriptor>
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QStringList>

// Punto di contatto tra la shell e il resto del sistema:
// - riceve comandi dal compositor su un socket Unix (es. "toggle-start"
//   quando premi Super);
// - espone al QML piccole informazioni di sessione.
class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString userInitial READ userInitial CONSTANT)
    // Percorso dello sfondo: VELA_WALLPAPER, oppure quello di Vela (16:9).
    Q_PROPERTY(QString wallpaper READ wallpaper CONSTANT)
    // Stato del menu Start, condiviso tra la finestra del menu e la taskbar.
    Q_PROPERTY(bool startMenuOpen READ startMenuOpen WRITE setStartMenuOpen NOTIFY startMenuOpenChanged)
    // Le app aggiunte alla Start (id dei .desktop), nell'ordine scelto.
    Q_PROPERTY(QStringList startPins READ startPins NOTIFY startPinsChanged)
    // "Termina attività" nella jump list: accesa (su Windows va accesa a mano);
    // si spegne con endTask=false nel gruppo [taskbar] di
    // ~/.config/Vela/vela-shell.conf.
    Q_PROPERTY(bool endTaskEnabled READ endTaskEnabled CONSTANT)

public:
    explicit ShellController(QObject* parent = nullptr);

    // Percorso del socket, legato alla sessione Wayland corrente.
    static QString socketPath();
    // Invia un comando a una shell già in esecuzione. false se non c'è.
    static bool sendToRunningInstance(const QByteArray& command);

    bool listen();
    // Un comando al compositor, sul suo socket (es. "lock").
    static bool sendToCompositor(const QByteArray& command);
    // Maiusc premuto adesso (lo sa il compositor: la tastiera può essere di
    // un'altra app mentre si clicca la taskbar).
    Q_INVOKABLE bool shiftHeld() const;

    QString userName() const;
    QString userInitial() const;
    QString wallpaper() const;

    bool startMenuOpen() const { return m_startMenuOpen; }
    void setStartMenuOpen(bool open)
    {
        if (open != m_startMenuOpen) {
            m_startMenuOpen = open;
            emit startMenuOpenChanged();
        }
    }

    Q_INVOKABLE void toggleStartMenu() { emit toggleStartRequested(); }

    QStringList startPins() const { return m_startPins; }
    Q_INVOKABLE void pinToStart(const QString& id);
    Q_INVOKABLE void unpinFromStart(const QString& id);
    Q_INVOKABLE void moveStartPinToFront(const QString& id);
    bool endTaskEnabled() const;

    // Il menu della finestra: un'azione (restore, move, resize, minimize,
    // maximize, close) sulla finestra con quell'identificativo, o su
    // "active".
    Q_INVOKABLE void windowAction(const QString& window, const QString& action);
    // Esce dalla sessione: chiede al compositor di chiudersi (le app si
    // chiudono con lui). Non spegne né riavvia il computer.
    Q_INVOKABLE void logout();
    Q_INVOKABLE void lock(); // Win+L dalla shell
    // Il colore medio dello sfondo al compositor: la tinta delle barre del
    // titolo (Mica, docs/renderer.md §9.4).
    void sendWallpaperTint() const;
    // Blocca lo schermo prima che il computer si sospenda.
    void watchSleep();
    // Il computer, tramite systemd-logind (se serve, polkit chiede la
    // password).
    Q_INVOKABLE void suspend();
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void powerOff();
    Q_INVOKABLE bool canSuspend() const;

signals:
    void toggleStartRequested();
    // Alt+Tab, comandato dal compositor: finestre (identificativi
    // ext-foreign-toplevel) in ordine di uso recente e quella selezionata.
    void switcherShown(int selected, const QStringList& windows);
    void switcherSelected(int selected);
    void switcherHidden();
    void startMenuOpenChanged();
    void startPinsChanged();
    // Scorciatoie di Windows che arrivano dal compositor.
    void winXRequested(); // Win+X
    void runRequested(); // Win+R
    void showDesktopRequested(); // Win+D
    // Il menu della finestra (clic destro sulla barra del titolo,
    // Alt+Spazio): su quale schermo e dove, in coordinate dello schermo.
    void windowMenuRequested(const QString& window, const QString& output, int x, int y, bool maximized,
        bool resizable, bool keyboard);

private Q_SLOTS:
    void onPrepareForSleep(bool starting);

private:
    void handleCommand(const QByteArray& command);
    void takeSleepDelay();
    void saveStartPins();
    QDBusUnixFileDescriptor m_sleepDelay; // finché è aperto, logind aspetta a sospendere

    QLocalServer m_server;
    bool m_startMenuOpen = false;
    QStringList m_startPins;
};
