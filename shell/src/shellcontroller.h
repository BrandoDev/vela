#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

// Punto di contatto tra la shell e il resto del sistema:
// - riceve comandi dal compositor su un socket Unix (es. "toggle-start"
//   quando premi Super);
// - espone al QML piccole informazioni di sessione.
class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString userInitial READ userInitial CONSTANT)
    // Percorso dello sfondo: VELA_WALLPAPER, oppure quello di Vela.
    Q_PROPERTY(QString wallpaper READ wallpaper CONSTANT)
    // Stato del menu Start, condiviso tra la finestra del menu e la taskbar.
    Q_PROPERTY(bool startMenuOpen READ startMenuOpen WRITE setStartMenuOpen NOTIFY startMenuOpenChanged)

public:
    explicit ShellController(QObject* parent = nullptr);

    // Percorso del socket, legato alla sessione Wayland corrente.
    static QString socketPath();
    // Invia un comando a una shell già in esecuzione. false se non c'è.
    static bool sendToRunningInstance(const QByteArray& command);

    bool listen();

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

signals:
    void toggleStartRequested();
    // Alt+Tab, comandato dal compositor: finestre (identificativi
    // ext-foreign-toplevel) in ordine di uso recente e quella selezionata.
    void switcherShown(int selected, const QStringList& windows);
    void switcherSelected(int selected);
    void switcherHidden();
    void startMenuOpenChanged();

private:
    void handleCommand(const QByteArray& command);

    QLocalServer m_server;
    bool m_startMenuOpen = false;
};
