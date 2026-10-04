#include "shellcontroller.h"

#include <QImage>
#include <QImageReader>
#include <QSettings>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QLocalSocket>
#include <QTimer>
#include <QStandardPaths>
#include <QtDebug>

#include <pwd.h>
#include <unistd.h>

namespace {

// Al primo avvio: le app di tutti i giorni, se installate (la Start salta
// quelle che non ci sono).
const QStringList defaultStartPins {
    QStringLiteral("firefox.desktop"),
    QStringLiteral("org.mozilla.firefox.desktop"),
    QStringLiteral("chromium.desktop"),
    QStringLiteral("org.kde.dolphin.desktop"),
    QStringLiteral("org.kde.konsole.desktop"),
    QStringLiteral("org.kde.kate.desktop"),
    QStringLiteral("org.kde.okular.desktop"),
    QStringLiteral("org.kde.gwenview.desktop"),
    QStringLiteral("org.kde.spectacle.desktop"),
    QStringLiteral("org.kde.kcalc.desktop"),
    QStringLiteral("org.kde.plasma-systemmonitor.desktop"),
    QStringLiteral("systemsettings.desktop"),
};

} // namespace

ShellController::ShellController(QObject* parent)
    : QObject(parent)
    , m_startPins(QSettings().value(QStringLiteral("start/pinned"), defaultStartPins).toStringList())
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                while (socket->canReadLine()) {
                    const QByteArray line = socket->readLine().trimmed();
                    if (line == "choose-source") {
                        startChooser(socket); // risponde quando l'utente sceglie
                    } else {
                        handleCommand(line);
                    }
                }
            });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                if (m_chooser == socket) {
                    m_chooser = nullptr;
                    emit chooseSourceCancelled(); // il portale ha rinunciato
                }
                // Un eventuale comando senza "a capo" finale.
                const QByteArray rest = socket->readAll().trimmed();
                if (!rest.isEmpty()) {
                    handleCommand(rest);
                }
                socket->deleteLater();
            });
        }
    });
}

QString ShellController::socketPath()
{
    // Deve coincidere con Server::sendShellCommand() nel compositor.
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    return runtimeDir + QStringLiteral("/vela-shell-") + display + QStringLiteral(".sock");
}

QByteArray ShellController::askRunningInstance(const QByteArray& command)
{
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(1000)) {
        return {};
    }
    socket.write(command + '\n');
    socket.waitForBytesWritten(1000);
    // L'utente può metterci quanto vuole: si aspetta la risposta o la chiusura.
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(-1)) {
            break;
        }
    }
    return socket.readLine().trimmed();
}

void ShellController::startChooser(QLocalSocket* socket)
{
    if (m_chooser && m_chooser != socket) {
        chooseSource(QString()); // una richiesta nuova prende il posto della vecchia
    }
    m_chooser = socket;
    emit chooseSourceRequested();
}

void ShellController::chooseSource(const QString& answer)
{
    QPointer<QLocalSocket> socket = m_chooser;
    m_chooser = nullptr;
    if (!socket) {
        return;
    }
    socket->write(answer.toUtf8() + '\n');
    socket->waitForBytesWritten(300);
    socket->disconnectFromServer();
}

bool ShellController::sendToRunningInstance(const QByteArray& command)
{
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(300)) {
        return false;
    }
    socket.write(command + '\n');
    socket.waitForBytesWritten(300);
    socket.disconnectFromServer();
    return true;
}

// Il socket dei comandi del compositor: vedi Server::listenForCommands().
bool ShellController::sendToCompositor(const QByteArray& command)
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/vela-") + display + QStringLiteral(".sock"));
    if (!socket.waitForConnected(300)) {
        qWarning("vela-shell: il compositor non risponde (%s)", command.constData());
        return false;
    }
    socket.write(command + '\n');
    socket.waitForBytesWritten(300);
    socket.disconnectFromServer();
    return true;
}

void ShellController::logout()
{
    sendToCompositor("logout");
}

void ShellController::lock()
{
    sendToCompositor("lock");
}

namespace {

QDBusInterface login1()
{
    return QDBusInterface(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"), QDBusConnection::systemBus());
}

void callLogin1(const char* method)
{
    // interactive = true: se serve un'autorizzazione, polkit la chiede.
    QDBusInterface manager = login1();
    manager.asyncCall(QLatin1String(method), true);
}

} // namespace

void ShellController::suspend()
{
    callLogin1("Suspend");
}

void ShellController::reboot()
{
    callLogin1("Reboot");
}

void ShellController::powerOff()
{
    callLogin1("PowerOff");
}

// Prima di sospendere, lo schermo si blocca: al risveglio il desktop non è
// mai esposto. logind aspetta finché teniamo aperto il "ritardo".
void ShellController::watchSleep()
{
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("PrepareForSleep"), this, SLOT(onPrepareForSleep(bool)));
    takeSleepDelay();
}

void ShellController::takeSleepDelay()
{
    if (m_sleepDelay.isValid()) {
        return;
    }
    QDBusInterface manager = login1();
    const QDBusReply<QDBusUnixFileDescriptor> reply = manager.call(QStringLiteral("Inhibit"), QStringLiteral("sleep"),
        QStringLiteral("Vela"), QStringLiteral("Blocca lo schermo prima di sospendere"), QStringLiteral("delay"));
    if (reply.isValid()) {
        m_sleepDelay = reply.value();
    }
}

void ShellController::onPrepareForSleep(bool starting)
{
    if (!starting) {
        takeSleepDelay(); // risvegliati: pronti per la prossima volta
        return;
    }
    lock();
    // Un momento perché la schermata di blocco compaia, poi si lascia
    // sospendere (chiudendo il ritardo).
    QTimer::singleShot(700, this, [this] { m_sleepDelay = QDBusUnixFileDescriptor(); });
}

bool ShellController::canSuspend() const
{
    QDBusInterface manager = login1();
    const QDBusReply<QString> reply = manager.call(QStringLiteral("CanSuspend"));
    return reply.isValid() && reply.value() != QLatin1String("no") && reply.value() != QLatin1String("na");
}

bool ShellController::listen()
{
    const QString path = socketPath();
    QLocalServer::removeServer(path); // socket rimasto da un'esecuzione precedente
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(path)) {
        qWarning("vela-shell: impossibile ascoltare su %s: %s", qPrintable(path),
            qPrintable(m_server.errorString()));
        return false;
    }
    return true;
}

void ShellController::handleCommand(const QByteArray& command)
{
    const QList<QByteArray> parts = command.split(' ');
    if (command == "toggle-start") {
        emit toggleStartRequested();
    } else if (parts.first() == "switcher-show" && parts.size() >= 3) {
        QStringList windows;
        for (qsizetype i = 2; i < parts.size(); ++i) {
            windows << QString::fromLatin1(parts.at(i));
        }
        emit switcherShown(parts.at(1).toInt(), windows);
    } else if (parts.first() == "switcher-select" && parts.size() == 2) {
        emit switcherSelected(parts.at(1).toInt());
    } else if (command == "switcher-hide") {
        emit switcherHidden();
    } else if (command == "winx") {
        emit winXRequested();
    } else if (command == "run") {
        emit runRequested();
    } else if (command == "show-desktop") {
        emit showDesktopRequested();
    } else if (parts.first() == "window-menu" && parts.size() == 8) {
        // window-menu <id> <schermo> <x> <y> <massimizzata> <ridimensionabile> <da tastiera>
        emit windowMenuRequested(QString::fromLatin1(parts.at(1)), QString::fromUtf8(parts.at(2)), parts.at(3).toInt(),
            parts.at(4).toInt(), parts.at(5) == "1", parts.at(6) == "1", parts.at(7) == "1");
    } else if (!command.isEmpty() && command != "ping") {
        qWarning("vela-shell: comando sconosciuto '%s'", command.constData());
    }
}

QString ShellController::userName() const
{
    // Il nome completo dell'account (campo GECOS), altrimenti il login.
    if (const passwd* pw = getpwuid(getuid())) {
        const QString gecos = QString::fromLocal8Bit(pw->pw_gecos).section(u',', 0, 0).trimmed();
        if (!gecos.isEmpty()) {
            return gecos;
        }
        return QString::fromLocal8Bit(pw->pw_name);
    }
    return qEnvironmentVariable("USER");
}

QString ShellController::wallpaper() const
{
    // Quello predefinito è incluso nell'eseguibile (images/ nel sorgente).
    return qEnvironmentVariable("VELA_WALLPAPER", QStringLiteral(":/vela/images/vela_splash_169.svg"));
}

void ShellController::sendWallpaperTint() const
{
    // Ridotto a pochi pixel già in lettura (anche gli SVG): poi la media.
    QImageReader reader(wallpaper());
    reader.setScaledSize(QSize(32, 18));
    const QImage image = reader.read().convertToFormat(QImage::Format_RGB32);
    if (image.isNull()) {
        return;
    }
    qint64 sum[3] {};
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            sum[0] += qRed(pixel);
            sum[1] += qGreen(pixel);
            sum[2] += qBlue(pixel);
        }
    }
    const qint64 count = qint64(image.width()) * image.height();
    sendToCompositor(QByteArray("wallpaper-tint ") + QByteArray::number(sum[0] / count) + ' '
        + QByteArray::number(sum[1] / count) + ' ' + QByteArray::number(sum[2] / count));
}

QString ShellController::userInitial() const
{
    const QString name = userName();
    return name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper();
}

// ------------------------------------------------------- app nella Start --


void ShellController::saveStartPins()
{
    QSettings().setValue(QStringLiteral("start/pinned"), m_startPins);
    emit startPinsChanged();
}

void ShellController::pinToStart(const QString& id)
{
    if (!id.isEmpty() && !m_startPins.contains(id)) {
        m_startPins.append(id); // in fondo, come Windows
        saveStartPins();
    }
}

void ShellController::unpinFromStart(const QString& id)
{
    if (m_startPins.removeAll(id) > 0) {
        saveStartPins();
    }
}

void ShellController::moveStartPinToFront(const QString& id)
{
    if (m_startPins.removeAll(id) > 0) {
        m_startPins.prepend(id);
        saveStartPins();
    }
}

bool ShellController::endTaskEnabled() const
{
    return QSettings().value(QStringLiteral("taskbar/endTask"), true).toBool();
}

void ShellController::windowAction(const QString& window, const QString& action)
{
    sendToCompositor("window " + window.toLatin1() + ' ' + action.toLatin1());
}

bool ShellController::shiftHeld() const
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/vela-") + display + QStringLiteral(".sock"));
    if (!socket.waitForConnected(100)) {
        return false;
    }
    socket.write("modifiers\n");
    if (!socket.waitForBytesWritten(100) || !socket.waitForReadyRead(100)) {
        return false;
    }
    constexpr int shift = 1; // WLR_MODIFIER_SHIFT
    return socket.readLine().trimmed().toInt() & shift;
}
