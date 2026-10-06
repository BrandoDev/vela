// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemactions.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtDebug>

namespace {

bool has(const QString& program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

// Un modulo delle impostazioni di KDE, aperto da solo con kcmshell6.
bool hasKcm(const QString& module)
{
    return has(QStringLiteral("kcmshell6"))
        && !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, module + QStringLiteral(".desktop"))
                .isEmpty();
}

QString quote(QString text)
{
    text.replace(u'\'', QStringLiteral("'\\''"));
    return u'\'' + text + u'\'';
}

bool spawn(const QString& command, const QString& directory = QDir::homePath())
{
    qInfo("vela-shell: avvio %s", qPrintable(command));
    return QProcess::startDetached(QStringLiteral("/bin/sh"), { QStringLiteral("-c"), command }, directory);
}

// Un'app di Vela (vela-settings, vela-files): installata, accanto alla
// shell, o nella cartella di build (settings/, explorer/ accanto a shell/).
// Vuoto dentro l'app stessa, che non deve rimandare a sé.
QString velaTool(const QString& name, const QString& buildDir)
{
    if (QCoreApplication::applicationName() == name) {
        return {};
    }
    const QString installed = QStandardPaths::findExecutable(name);
    if (!installed.isEmpty()) {
        return installed;
    }
    const QString here = QCoreApplication::applicationDirPath();
    for (const QString& candidate : { here + u'/' + name, here + QStringLiteral("/../") + buildDir + u'/' + name }) {
        if (QFileInfo(candidate).isExecutable()) {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }
    return {};
}

QString velaSettings()
{
    return velaTool(QStringLiteral("vela-settings"), QStringLiteral("settings"));
}

QString velaFiles()
{
    return velaTool(QStringLiteral("vela-files"), QStringLiteral("explorer"));
}

// Un comando in un terminale che resta aperto a mostrare com'è andata.
QString inTerminal(const QString& command)
{
    const QString script = command + QStringLiteral("; echo; read -r -p 'Premi Invio per chiudere' _");
    return terminalProgram() + QStringLiteral(" -e sh -c ") + quote(script);
}

} // namespace

QString terminalProgram()
{
    const QString chosen = qEnvironmentVariable("VELA_TERMINAL");
    if (!chosen.isEmpty()) {
        return chosen;
    }
    for (const char* candidate : { "konsole", "foot", "kitty", "alacritty", "gnome-terminal", "xterm" }) {
        if (has(QLatin1String(candidate))) {
            return QLatin1String(candidate);
        }
    }
    return QStringLiteral("xterm");
}

SystemActions::SystemActions(QObject* parent)
    : QObject(parent)
{
}

QString SystemActions::command(const QString& name) const
{
    // Il primo programma installato tra quelli che fanno la stessa cosa.
    const auto first = [](std::initializer_list<std::pair<const char*, const char*>> candidates) {
        for (const auto& [probe, command] : candidates) {
            const QString p = QLatin1String(probe);
            if (p.startsWith(QLatin1String("kcm_")) ? hasKcm(p) : has(p)) {
                return QLatin1String(command);
            }
        }
        return QLatin1String("");
    };
    // Le voci che hanno una pagina nelle Impostazioni di Vela.
    static const QHash<QString, QString> settingsPages {
        { QStringLiteral("settings"), QString() },
        { QStringLiteral("display"), QStringLiteral("display") },
        { QStringLiteral("personalize"), QStringLiteral("personalization") },
        { QStringLiteral("taskbar-settings"), QStringLiteral("taskbar") },
        { QStringLiteral("notification-settings"), QStringLiteral("notifications") },
        { QStringLiteral("power"), QStringLiteral("power") },
        { QStringLiteral("mobility"), QStringLiteral("power") },
        { QStringLiteral("network"), QStringLiteral("network") },
        { QStringLiteral("datetime"), QStringLiteral("datetime") },
        { QStringLiteral("installed-apps"), QStringLiteral("installed-apps") },
        { QStringLiteral("system"), QStringLiteral("about") },
        { QStringLiteral("sound-settings"), QStringLiteral("sound") },
        { QStringLiteral("bluetooth-settings"), QStringLiteral("bluetooth") },
        { QStringLiteral("night-light-settings"), QStringLiteral("night-light") },
        { QStringLiteral("accessibility-settings"), QStringLiteral("accessibility") },
        { QStringLiteral("colors-settings"), QStringLiteral("colors") },
    };
    if (settingsPages.contains(name)) {
        const QString program = velaSettings();
        if (!program.isEmpty()) {
            const QString page = settingsPages.value(name);
            return quote(program) + (page.isEmpty() ? QString() : QStringLiteral(" --page ") + page);
        }
    }
    if (name == QLatin1String("terminal")) {
        return terminalProgram();
    }
    if (name == QLatin1String("terminal-admin")) {
        // Come "Terminale (Admin)": una shell di root, la password la chiede polkit (run0) o sudo.
        return terminalProgram() + (has(QStringLiteral("run0")) ? QStringLiteral(" -e run0") : QStringLiteral(" -e sudo -i"));
    }
    if (name == QLatin1String("task-manager")) {
        const QString gui = first({ { "plasma-systemmonitor", "plasma-systemmonitor" },
            { "gnome-system-monitor", "gnome-system-monitor" }, { "missioncenter", "missioncenter" } });
        if (!gui.isEmpty()) {
            return gui;
        }
        return has(QStringLiteral("btop")) ? terminalProgram() + QStringLiteral(" -e btop")
            : has(QStringLiteral("htop"))  ? terminalProgram() + QStringLiteral(" -e htop")
                                           : terminalProgram() + QStringLiteral(" -e top");
    }
    if (name == QLatin1String("events")) {
        const QString gui = first({ { "kjournaldbrowser", "kjournaldbrowser" } });
        return !gui.isEmpty() ? gui : terminalProgram() + QStringLiteral(" -e journalctl -b -e");
    }
    if (name == QLatin1String("files")) {
        // Esplora di Vela, se c'è.
        if (const QString files = velaFiles(); !files.isEmpty()) {
            return quote(files);
        }
        return first({ { "xdg-open", "xdg-open \"$HOME\"" } });
    }
    if (name == QLatin1String("settings")) {
        return first({ { "systemsettings", "systemsettings" }, { "gnome-control-center", "gnome-control-center" } });
    }
    if (name == QLatin1String("system") || name == QLatin1String("computer")) {
        return first({ { "kinfocenter", "kinfocenter" } });
    }
    if (name == QLatin1String("devices")) {
        return first({ { "kinfocenter", "kinfocenter" } });
    }
    if (name == QLatin1String("network")) {
        return first({ { "kcm_networkmanagement", "kcmshell6 kcm_networkmanagement" },
            { "nm-connection-editor", "nm-connection-editor" } });
    }
    if (name == QLatin1String("disks")) {
        return first({ { "partitionmanager", "partitionmanager" }, { "gnome-disks", "gnome-disks" } });
    }
    if (name == QLatin1String("power") || name == QLatin1String("mobility")) {
        return first({ { "kcm_powerdevilprofilesconfig", "kcmshell6 kcm_powerdevilprofilesconfig" } });
    }
    if (name == QLatin1String("installed-apps")) {
        return first({ { "plasma-discover", "plasma-discover --mode Installed" }, { "gnome-software", "gnome-software" } });
    }
    if (name == QLatin1String("volume-mixer")) {
        return first({ { "pavucontrol-qt", "pavucontrol-qt" }, { "pavucontrol", "pavucontrol" },
            { "kcm_pulseaudio", "kcmshell6 kcm_pulseaudio" } });
    }
    if (name == QLatin1String("sound-settings")) {
        return first({ { "kcm_pulseaudio", "kcmshell6 kcm_pulseaudio" }, { "pavucontrol", "pavucontrol" } });
    }
    if (name == QLatin1String("datetime")) {
        return first({ { "kcm_clock", "kcmshell6 kcm_clock" } });
    }
    if (name == QLatin1String("display")) {
        // Programmi che parlano wlr-output-management, come Vela.
        return first({ { "nwg-displays", "nwg-displays" }, { "wdisplays", "wdisplays" } });
    }
    // Impostazioni di notifica, della taskbar, Personalizza: solo con le
    // Impostazioni di Vela (sopra).
    return {};
}

bool SystemActions::available(const QString& name) const
{
    return !command(name).isEmpty();
}

bool SystemActions::trigger(const QString& name) const
{
    const QString cmd = command(name);
    return !cmd.isEmpty() && spawn(cmd);
}

bool SystemActions::isLaptop() const
{
    const QDir supplies(QStringLiteral("/sys/class/power_supply"));
    for (const QString& entry : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto read = [&](const char* file) {
            QFile f(supplies.filePath(entry) + u'/' + QLatin1String(file));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
        };
        // Le batterie di mouse e tastiere hanno scope "Device".
        if (read("type") == QLatin1String("Battery") && read("scope") != QLatin1String("Device")) {
            return true;
        }
    }
    return false;
}

bool SystemActions::run(const QString& input)
{
    const QString text = input.trimmed();
    if (text.isEmpty()) {
        return false;
    }
    // Un URL o un percorso esistente si apre con l'app predefinita; il
    // resto è una riga di comando.
    const QString expanded = text.startsWith(u'~') ? QDir::homePath() + text.mid(1) : text;
    bool ok = false;
    if (text.contains(QLatin1String("://")) || QFileInfo::exists(expanded)) {
        ok = QProcess::startDetached(QStringLiteral("xdg-open"), { expanded });
    } else {
        ok = spawn(text);
    }
    if (ok) {
        QSettings settings;
        QStringList history = settings.value(QStringLiteral("run/history")).toStringList();
        history.removeAll(text);
        history.prepend(text);
        settings.setValue(QStringLiteral("run/history"), history.mid(0, 26));
    }
    return ok;
}

QStringList SystemActions::runHistory() const
{
    return QSettings().value(QStringLiteral("run/history")).toStringList();
}

void SystemActions::showInFolder(const QString& pathOrUrl) const
{
    const QUrl url = pathOrUrl.contains(QLatin1String("://")) ? QUrl(pathOrUrl) : QUrl::fromLocalFile(pathOrUrl);
    // Esplora di Vela apre la cartella con il file selezionato.
    if (const QString files = velaFiles(); !files.isEmpty() && url.isLocalFile()) {
        QProcess::startDetached(files, { url.toLocalFile() });
        return;
    }
    // Lo standard dei file manager (Dolphin, Nautilus...): apre la cartella
    // e seleziona il file.
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("ShowItems"));
    call << QStringList { url.toString() } << QString();
    if (!QDBusConnection::sessionBus().send(call) && url.isLocalFile()) {
        QProcess::startDetached(QStringLiteral("xdg-open"), { QFileInfo(url.toLocalFile()).absolutePath() });
    }
}

void SystemActions::openUrl(const QString& url) const
{
    QProcess::startDetached(QStringLiteral("xdg-open"), { url });
}

void SystemActions::openTerminal(const QString& directory) const
{
    const QString dir = directory.isEmpty() ? QDir::homePath() : directory;
    const QString terminal = terminalProgram();
    // Konsole vuole la cartella come opzione; gli altri partono in quella corrente.
    spawn(terminal == QLatin1String("konsole") ? terminal + QStringLiteral(" --workdir ") + quote(dir) : terminal, dir);
}

QString SystemActions::desktopDirectory() const
{
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return QFileInfo(desktop).isDir() ? desktop : QDir::homePath();
}

namespace {

// Il comando che toglie il pacchetto a cui appartiene il file .desktop.
QString uninstallCommand(const QString& desktopFile)
{
    if (desktopFile.isEmpty()) {
        return {};
    }
    // Le app Flatpak: .../flatpak/exports/share/applications/<id>.desktop
    if (desktopFile.contains(QLatin1String("/flatpak/exports/")) && has(QStringLiteral("flatpak"))) {
        const QString id = QFileInfo(desktopFile).completeBaseName();
        return QStringLiteral("flatpak uninstall ") + quote(id);
    }
    if (has(QStringLiteral("plasma-discover"))) {
        return {}; // si usa Discover, vedi uninstall()
    }
    const QString file = quote(desktopFile);
    const QString root = has(QStringLiteral("run0")) ? QStringLiteral("run0 ") : QStringLiteral("sudo ");
    if (has(QStringLiteral("pacman"))) {
        return root + QStringLiteral("pacman -Rs \"$(pacman -Qqo ") + file + QStringLiteral(")\"");
    }
    if (has(QStringLiteral("dnf"))) {
        return root + QStringLiteral("dnf remove \"$(rpm -qf --qf '%{NAME}' ") + file + QStringLiteral(")\"");
    }
    if (has(QStringLiteral("zypper"))) {
        return root + QStringLiteral("zypper remove \"$(rpm -qf --qf '%{NAME}' ") + file + QStringLiteral(")\"");
    }
    if (has(QStringLiteral("apt"))) {
        return root + QStringLiteral("apt remove \"$(dpkg -S ") + file + QStringLiteral(" | cut -d: -f1)\"");
    }
    return {};
}

} // namespace

bool SystemActions::canUninstall(const QString& desktopFile) const
{
    // Le app dell'utente (~/.local/share/applications) non sono pacchetti.
    if (desktopFile.startsWith(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation))) {
        return false;
    }
    return has(QStringLiteral("plasma-discover")) || !uninstallCommand(desktopFile).isEmpty();
}

void SystemActions::uninstall(const QString& desktopFile) const
{
    const QString command = uninstallCommand(desktopFile);
    if (!command.isEmpty()) {
        spawn(inTerminal(command));
    } else if (has(QStringLiteral("plasma-discover"))) {
        // Come Windows con le app classiche: la pagina dell'app nel negozio.
        const QString id = QFileInfo(desktopFile).completeBaseName();
        spawn(QStringLiteral("plasma-discover --application ") + quote(QStringLiteral("appstream://") + id));
    }
}
