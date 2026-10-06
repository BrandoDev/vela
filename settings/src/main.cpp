// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// vela-settings: le Impostazioni di Vela, come quelle di Windows 11.
//
//   vela-settings [--page <nome>]
//
// Una sola finestra: se è già aperta, una seconda chiamata le chiede solo
// di mostrare la pagina e di venire in primo piano (la shell apre così
// "Impostazioni schermo", "Personalizza" e le altre voci dei menu).

#include "about.h"
#include "appmodel.h"
#include "audio.h"
#include "bluetooth.h"
#include "datetime.h"
#include "defaultapps.h"
#include "displays.h"
#include "iconprovider.h"
#include "keyboardlayouts.h"
#include "language.h"
#include "network.h"
#include "preferences.h"
#include "systemactions.h"
#include "systemstatus.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QQuickWindow>
#include <QStandardPaths>

namespace {

QString socketPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/vela-settings-")
        + qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0")) + QStringLiteral(".sock");
}

// C'è già una finestra aperta: le si passa la pagina (e il token per venire
// in primo piano) e si esce.
bool forwardToRunningInstance(const QString& page)
{
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(300)) {
        return false;
    }
    socket.write("page " + page.toUtf8() + ' ' + qgetenv("XDG_ACTIVATION_TOKEN") + '\n');
    socket.waitForBytesWritten(300);
    return true;
}

// Riceve i "page <nome> [token]" delle chiamate successive.
class PageRouter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString initialPage READ initialPage CONSTANT)

public:
    explicit PageRouter(QString initialPage, QObject* parent = nullptr)
        : QObject(parent)
        , m_initialPage(std::move(initialPage))
    {
    }

    QString initialPage() const { return m_initialPage; }

    bool listen()
    {
        QLocalServer::removeServer(socketPath());
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket* client = m_server.nextPendingConnection()) {
                connect(client, &QLocalSocket::readyRead, this, [this, client] {
                    while (client->canReadLine()) {
                        const QList<QByteArray> words = client->readLine().trimmed().split(' ');
                        if (words.value(0) == "page") {
                            if (words.size() > 2 && !words[2].isEmpty()) {
                                qputenv("XDG_ACTIVATION_TOKEN", words[2]); // lo usa requestActivate()
                            }
                            emit pageRequested(QString::fromUtf8(words.value(1)));
                        }
                    }
                });
                connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            }
        });
        return m_server.listen(socketPath());
    }

signals:
    void pageRequested(const QString& page);

private:
    QString m_initialPage;
    QLocalServer m_server;
};

} // namespace

int main(int argc, char* argv[])
{
    // Testo con FreeType e l'hinting dei font, come Plasma e come Windows:
    // più netto del testo predefinito di Qt Quick (campi di distanza, senza
    // hinting), ai pixel veri dello schermo anche a scala frazionaria.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_DEFAULT_TEXT_RENDER_TYPE")) {
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("vela-settings"));
    QQmlApplicationEngine* qmlEngine = nullptr;
    vela::language::install(QStringLiteral("vela-settings"), [&qmlEngine] {
        QGuiApplication::setApplicationDisplayName(QCoreApplication::translate("Settings", "Settings"));
        if (qmlEngine) {
            qmlEngine->retranslate();
        }
    });
    QGuiApplication::setApplicationDisplayName(QCoreApplication::translate("Settings", "Settings"));
    QGuiApplication::setOrganizationName(QStringLiteral("Vela"));
    QGuiApplication::setDesktopFileName(QStringLiteral("vela-settings"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("preferences-system")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Vela Settings"));
    parser.addHelpOption();
    const QCommandLineOption pageOption(QStringLiteral("page"),
        QStringLiteral("The page to open: system, display, sound, notifications, power, about, bluetooth, "
                       "network, personalization, background, colors, taskbar, apps, installed-apps, "
                       "default-apps, time-language, datetime, keyboard, night-light, accessibility."),
        QStringLiteral("name"));
    parser.addOption(pageOption);
    parser.process(app);
    const QString page = parser.value(pageOption);

    if (forwardToRunningInstance(page)) {
        return 0;
    }

    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    PageRouter router(page);
    router.listen();

    AppModel apps;
    apps.reload();
    SystemActions system;
    Preferences preferences;
    // Le icone del tema adatto alla modalità delle app (breeze o breeze-dark).
    applyIconTheme(preferences.light());
    preferences.setIconMode(preferences.light() ? QStringLiteral("l/") : QStringLiteral("d/"));
    QObject::connect(&preferences, &Preferences::appThemeChanged, &preferences, [&preferences] {
        applyIconTheme(preferences.light());
        QTimer::singleShot(300, &preferences, [&preferences] {
            preferences.setIconMode(preferences.light() ? QStringLiteral("l/") : QStringLiteral("d/"));
        });
    });
    Displays displays;
    Audio audio;
    Network network;
    Bluetooth bluetooth;
    About about;
    DateTime dateTime;
    DefaultApps defaultApps(&apps);
    KeyboardLayouts layouts;
    SystemStatus status;

    QQmlApplicationEngine engine;
    qmlEngine = &engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.addImageProvider(QStringLiteral("fileicon"), new IconProvider(32));
    QQmlContext* context = engine.rootContext();
    context->setContextProperty(QStringLiteral("Router"), &router);
    context->setContextProperty(QStringLiteral("Apps"), &apps);
    context->setContextProperty(QStringLiteral("System"), &system);
    context->setContextProperty(QStringLiteral("Prefs"), &preferences);
    context->setContextProperty(QStringLiteral("Displays"), &displays);
    context->setContextProperty(QStringLiteral("Audio"), &audio);
    context->setContextProperty(QStringLiteral("Network"), &network);
    context->setContextProperty(QStringLiteral("Bluetooth"), &bluetooth);
    context->setContextProperty(QStringLiteral("About"), &about);
    context->setContextProperty(QStringLiteral("DateTime"), &dateTime);
    context->setContextProperty(QStringLiteral("DefaultApps"), &defaultApps);
    context->setContextProperty(QStringLiteral("Layouts"), &layouts);
    context->setContextProperty(QStringLiteral("Status"), &status);
    engine.loadFromModule("Vela.Settings", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}

#include "main.moc"
