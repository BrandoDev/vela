#pragma once

#include <QDBusUnixFileDescriptor>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QObject>
#include <QString>
#include <QStringList>

// Punto di contatto tra la shell e il resto del sistema:
// - riceve comandi dal compositor su un socket Unix (es. "toggle-start"
//   quando premi Super);
// - espone al QML piccole informazioni di sessione.
class QWindow;

class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString userInitial READ userInitial CONSTANT)
    // Stato del menu Start, condiviso tra la finestra del menu e la taskbar.
    Q_PROPERTY(bool startMenuOpen READ startMenuOpen WRITE setStartMenuOpen NOTIFY startMenuOpenChanged)
    // Le app aggiunte alla Start (id dei .desktop), nell'ordine scelto.
    Q_PROPERTY(QStringList startPins READ startPins NOTIFY startPinsChanged)
    // Il nome dello schermo principale (lì si aprono i pannelli chiesti da tastiera).
    Q_PROPERTY(QString primaryScreen READ primaryScreen NOTIFY primaryScreenChanged)

public:
    explicit ShellController(QObject* parent = nullptr);

    // Percorso del socket, legato alla sessione Wayland corrente.
    static QString socketPath();
    // Invia un comando a una shell già in esecuzione. false se non c'è.
    static bool sendToRunningInstance(const QByteArray& command);
    // Un comando con risposta (aspetta quanto serve): "choose-source".
    static QByteArray askRunningInstance(const QByteArray& command);

    // La condivisione dello schermo (xdg-desktop-portal-wlr): la risposta
    // per il portale, "Monitor: <schermo>", "Window: <identificativo>", o
    // vuota se l'utente annulla.
    Q_INVOKABLE void chooseSource(const QString& answer);

    bool listen();
    // Un comando al compositor, sul suo socket (es. "lock").
    static bool sendToCompositor(const QByteArray& command);
    // Maiusc premuto adesso (lo sa il compositor: la tastiera può essere di
    // un'altra app mentre si clicca la taskbar).
    Q_INVOKABLE bool shiftHeld() const;
    // Una finestra della shell ancorata in basso a sinistra (le anteprime
    // della taskbar): a `left` pixel dal bordo sinistro dello schermo.
    Q_INVOKABLE void placeAtLeft(QWindow* window, int left);
    // Un pannello della shell (nascosto) sullo schermo `name`: alla prossima
    // apertura compare lì.
    Q_INVOKABLE void placeOnScreen(QWindow* window, const QString& name);
    // Alt+Tab: un clic sull'anteprima `index` (il compositor ci passa).
    Q_INVOKABLE void pickSwitcher(int index) { sendToCompositor("switcher-pick " + QByteArray::number(index)); }

    QString userName() const;
    QString primaryScreen() const;
    QString userInitial() const;

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
    void sendWallpaperTint(const QString& wallpaper) const;
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
    void primaryScreenChanged();
    void chooseSourceRequested();
    void chooseSourceCancelled();
    // Scorciatoie di Windows che arrivano dal compositor.
    void winXRequested(); // Win+X
    void runRequested(); // Win+R
    void showDesktopRequested(); // Win+D
    void quickSettingsRequested(); // Win+A
    void notificationCenterRequested(); // Win+N
    void settingsRequested(); // Win+I
    void filesRequested(); // Win+E
    void taskViewRequested(); // Win+Tab
    void workspacesReceived(const QByteArray& json); // lo stato dei desktop virtuali
    // I layout di snap sotto il pulsante Ingrandisci (Win+Z): x, y nello schermo.
    void snapLayoutsRequested(const QString& window, const QString& output, int x, int y, bool keyboard);
    // Dopo uno snap: le finestre da proporre per gli spazi liberi (JSON).
    void snapAssistRequested(const QString& json);
    void propertiesRequested(const QStringList& paths); // da Esplora
    void shareRequested(const QStringList& paths); // "Condividi", da Esplora
    void openWithRequested(const QString& path); // "Scegli un'altra app", da Esplora
    void newShortcutRequested(const QString& folder); // "Nuovo > Collegamento", da Esplora
    void accessibilityReceived(const QByteArray& json); // dal compositor
    // Il menu della finestra (clic destro sulla barra del titolo,
    // Alt+Spazio): su quale schermo e dove, in coordinate dello schermo.
    void windowMenuRequested(const QString& window, const QString& output, int x, int y, bool maximized,
        bool resizable, bool keyboard);

private Q_SLOTS:
    void onPrepareForSleep(bool starting);

private:
    void handleCommand(const QByteArray& command);
    void takeSleepDelay();
    void startChooser(QLocalSocket* socket);
    QPointer<QLocalSocket> m_chooser; // il portale che aspetta la scelta
    void saveStartPins();
    QDBusUnixFileDescriptor m_sleepDelay; // finché è aperto, logind aspetta a sospendere

    QLocalServer m_server;
    bool m_startMenuOpen = false;
    QStringList m_startPins;
};
