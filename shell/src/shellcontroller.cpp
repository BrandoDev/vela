#include "shellcontroller.h"

#include <QLocalSocket>
#include <QStandardPaths>
#include <QtDebug>

#include <pwd.h>
#include <unistd.h>

ShellController::ShellController(QObject* parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                while (socket->canReadLine()) {
                    handleCommand(socket->readLine().trimmed());
                }
            });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
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
    if (command == "toggle-start") {
        emit toggleStartRequested();
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

QString ShellController::userInitial() const
{
    const QString name = userName();
    return name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper();
}
