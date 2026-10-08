// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferences.h"

extern "C" {
#include "legacy_names.h"
}

#include "iconprovider.h"
#include "mica.h"

#include <QDir>
#include <QTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QIcon>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocalSocket>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto defaultWallpaper = ":/vela/images/vela_splash_169.svg"; // quello della shell
const QColor defaultAccent(0x5b, 0x8c, 0xff);

QSettings shellSettings()
{
    return QSettings(QStringLiteral("Vela"), QStringLiteral("vela-shell"));
}

QString compositorConfigPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/vela/vela.conf");
}

} // namespace

void sendToCompositor(const QByteArray& command)
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/vela-") + display + QStringLiteral(".sock"));
    if (socket.waitForConnected(200)) {
        socket.write(command + '\n');
        socket.waitForBytesWritten(200);
    }
}

Preferences::Preferences(QObject* parent)
    : QObject(parent)
{
    reload();
    // vela.conf cambia anche fuori di qui (le impostazioni rapide della shell).
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        watchConfig();
        reloadCompositor();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    watchConfig();
}

void Preferences::watchConfig()
{
    const QString path = compositorConfigPath();
    const QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    if (info.exists() && !m_watcher.files().contains(path)) {
        m_watcher.addPath(path);
    }
    if (!m_watcher.directories().contains(info.absolutePath())) {
        m_watcher.addPath(info.absolutePath());
    }
}

void Preferences::reload()
{
    QSettings settings = shellSettings();
    settings.sync();

    QString wallpaper = settings.value(QStringLiteral("appearance/wallpaper")).toString();
    if (wallpaper.isEmpty() || !QFileInfo::exists(wallpaper)) {
        wallpaper = QString::fromLatin1(defaultWallpaper);
    }
    const QStringList recent = settings.value(QStringLiteral("appearance/recentWallpapers")).toStringList();
    if (wallpaper != m_wallpaper || recent != m_recentWallpapers) {
        m_wallpaper = wallpaper;
        m_recentWallpapers = recent;
        computeMica();
        emit wallpaperChanged();
    }

    QColor accent(settings.value(QStringLiteral("appearance/accent")).toString());
    if (!accent.isValid()) {
        accent = defaultAccent;
    }
    if (accent != m_accent) {
        m_accent = accent;
        emit accentChanged();
    }

    m_taskbarAlignment = settings.value(QStringLiteral("taskbar/alignment")).toString() == QLatin1String("left")
        ? QStringLiteral("left")
        : QStringLiteral("center");
    m_endTask = settings.value(QStringLiteral("taskbar/endTask"), true).toBool();
    m_taskView = settings.value(QStringLiteral("taskbar/taskView"), true).toBool();
    m_taskbarAllScreens = settings.value(QStringLiteral("taskbar/allScreens"), true).toBool();
    emit taskbarChanged();
    m_doNotDisturb = settings.value(QStringLiteral("notifications/doNotDisturb"), false).toBool();
    emit doNotDisturbChanged();
    m_clipboardHistory = settings.value(QStringLiteral("clipboard/history"), false).toBool();
    emit clipboardChanged();

    // La modalità: quella scelta qui (vela-shell.conf); per le app, se non
    // c'è ancora, quella che GTK e il portale conoscono già.
    QString appTheme = settings.value(QStringLiteral("appearance/appTheme")).toString();
    if (appTheme.isEmpty()) {
        QProcess gsettings;
        gsettings.start(QStringLiteral("gsettings"),
            { QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("color-scheme") });
        appTheme = gsettings.waitForFinished(1000) && gsettings.readAllStandardOutput().contains("light")
            ? QStringLiteral("light")
            : QStringLiteral("dark");
    }
    m_appTheme = appTheme == QLatin1String("light") ? QStringLiteral("light") : QStringLiteral("dark");
    m_shellTheme = settings.value(QStringLiteral("appearance/shellTheme")).toString() == QLatin1String("light")
        ? QStringLiteral("light")
        : QStringLiteral("dark");
    computeMica();
    emit appThemeChanged();
    emit wallpaperChanged();

    reloadCompositor();
}

namespace {

// Una riga di vela.conf con i nomi di adesso (compositor/src/legacy_names.h).
QString modernLine(const QString& line)
{
    const qsizetype eq = line.indexOf(u'=');
    if (line.isEmpty() || line.startsWith(u'#') || eq < 0) {
        return line;
    }
    const QByteArray key = line.left(eq).toUtf8();
    const QByteArray value = line.mid(eq + 1).toUtf8();
    const char* newKey = vela_legacy_key(key.constData());
    const char* newValue = vela_legacy_value(newKey ? newKey : key.constData(), value.constData());
    if (!newKey && !newValue) {
        return line;
    }
    return QString::fromUtf8(newKey ? QByteArray(newKey) : key) + u'='
        + QString::fromUtf8(newValue ? QByteArray(newValue) : value);
}

} // namespace

void Preferences::reloadCompositor()
{
    // vela.conf
    m_compositor.clear();
    QFile file(compositorConfigPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = modernLine(in.readLine());
            const qsizetype eq = line.indexOf(u'=');
            if (!line.startsWith(u'#') && eq > 0) {
                m_compositor.append({ line.left(eq), line.mid(eq + 1) });
            }
        }
    }
    auto value = [this](const QString& key, const QString& fallback) {
        const QString v = compositorValue(key);
        return v.isNull() ? fallback : v;
    };
    auto flag = [&](const QString& key, bool fallback) {
        const QString v = value(key, fallback ? QStringLiteral("yes") : QStringLiteral("no"));
        return v == QLatin1String("yes") || v == QLatin1String("1") || v == QLatin1String("true");
    };
    m_screenOffMinutes = value(QStringLiteral("screen-off"), QStringLiteral("10")).toInt();
    const QString lock = value(QStringLiteral("lock-on-idle"), QStringLiteral("yes"));
    m_lockOnIdle = lock != QLatin1String("0") && lock != QLatin1String("no");
    emit idleChanged();
    m_language = value(QStringLiteral("language"), {});
    emit languageChanged();

    m_layouts.clear();
    const QStringList layouts = value(QStringLiteral("keyboard-layout"), {}).split(u',', Qt::SkipEmptyParts);
    const QStringList variants = value(QStringLiteral("keyboard-variant"), {}).split(u',');
    for (qsizetype i = 0; i < layouts.size(); ++i) {
        m_layouts.append(QVariantMap { { QStringLiteral("layout"), layouts[i].trimmed() },
            { QStringLiteral("variant"), i < variants.size() ? variants[i].trimmed() : QString() } });
    }
    m_repeatDelay = value(QStringLiteral("keyboard-repeat-delay"), QStringLiteral("400")).toInt();
    m_repeatRate = value(QStringLiteral("keyboard-repeat-rate"), QStringLiteral("30")).toInt();
    emit keyboardChanged();

    m_nightLight = flag(QStringLiteral("night-light"), false);
    m_nightStrength = std::clamp(value(QStringLiteral("night-light-strength"), QStringLiteral("48")).toInt(), 0, 100);
    m_nightSchedule = value(QStringLiteral("night-light-schedule"), QStringLiteral("no"));
    m_nightFrom = value(QStringLiteral("night-light-from"), QStringLiteral("21:00"));
    m_nightTo = value(QStringLiteral("night-light-to"), QStringLiteral("07:00"));
    m_tearing = flag(QStringLiteral("tearing"), true);
    m_vrr = value(QStringLiteral("variable-refresh"), QStringLiteral("games"));
    m_colorFilter = flag(QStringLiteral("color-filters"), false);
    m_colorFilterKind = value(QStringLiteral("color-filter"), QStringLiteral("grayscale"));
    m_colorFilterShortcut = flag(QStringLiteral("color-filters-shortcut"), false);
    m_magnifierStep = value(QStringLiteral("magnifier-step"), QStringLiteral("100")).toInt();
    m_stickyKeys = flag(QStringLiteral("sticky-keys"), false);
    m_mouseSpeed = std::clamp(value(QStringLiteral("mouse-speed"), QStringLiteral("10")).toInt(), 1, 20);
    m_mousePrecision = flag(QStringLiteral("mouse-precision"), true);
    m_mouseLeftHanded = value(QStringLiteral("mouse-primary-button"), QStringLiteral("left")) == QLatin1String("right");
    m_wheelLines = std::clamp(value(QStringLiteral("mouse-scroll-lines"), QStringLiteral("3")).toInt(), 1, 20);
    m_touchpad = flag(QStringLiteral("touchpad"), true);
    m_touchpadWithMouse = flag(QStringLiteral("touchpad-with-mouse"), true);
    m_touchpadSpeed = std::clamp(value(QStringLiteral("touchpad-speed"), QStringLiteral("10")).toInt(), 1, 20);
    m_touchpadTap = flag(QStringLiteral("touchpad-tap"), true);
    m_touchpadNatural = flag(QStringLiteral("touchpad-natural-scroll"), true);
    m_threeFingers = value(QStringLiteral("touchpad-three-fingers"), QStringLiteral("app"));
    m_fourFingers = value(QStringLiteral("touchpad-four-fingers"), QStringLiteral("desktop"));
    emit inputChanged();
    queryCompositor();
    emit nightLightChanged();
    emit accessibilityChanged();
}

QString Preferences::compositorValue(const QString& key) const
{
    for (const auto& [k, v] : std::as_const(m_compositor)) {
        if (k == key) {
            return v;
        }
    }
    return {};
}

void Preferences::queryCompositor()
{
    // Ciò che non sta nel file: la lente (si apre e si chiude) e le ore del sole.
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/vela-") + display + QStringLiteral(".sock"));
    if (!socket.waitForConnected(200)) {
        return;
    }
    socket.write("accessibility\n");
    socket.waitForBytesWritten(200);
    QByteArray reply;
    while (!reply.contains('\n') && socket.waitForReadyRead(200)) {
        reply += socket.readAll();
    }
    const QJsonObject state = QJsonDocument::fromJson(reply.trimmed()).object();
    if (state.isEmpty()) {
        return;
    }
    m_magnifier = state[QStringLiteral("magnifier")].toBool();
    m_hasTouchpad = state[QStringLiteral("touchpad")].toBool();
    m_sunset = state[QStringLiteral("sunset")].toString();
    m_sunrise = state[QStringLiteral("sunrise")].toString();
}

void Preferences::setShell(const QString& key, const QVariant& value)
{
    QSettings settings = shellSettings();
    settings.setValue(key, value);
    settings.sync();
}

// ------------------------------------------------------------- sfondo --

QStringList Preferences::systemWallpapers() const
{
    // Gli sfondi installati: i pacchetti di KDE (una cartella con
    // contents/images/<risoluzione>.<ext>, di cui si prende la più grande)
    // e le immagini sciolte di GNOME e simili.
    QStringList out;
    const QStringList roots = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("wallpapers"),
        QStandardPaths::LocateDirectory)
        + QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("backgrounds"),
            QStandardPaths::LocateDirectory);
    const QStringList images { QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
        QStringLiteral("*.webp"), QStringLiteral("*.svg") };
    for (const QString& root : roots) {
        const QDir dir(root);
        for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            if (entry.isDir()) {
                QDir contents(entry.filePath() + QStringLiteral("/contents/images"));
                QFileInfoList candidates = contents.entryInfoList(images, QDir::Files);
                if (candidates.isEmpty()) {
                    continue;
                }
                // La più grande per area (i nomi sono "3840x2160.png").
                auto area = [](const QFileInfo& f) {
                    const QStringList wh = f.completeBaseName().split(u'x');
                    return wh.size() == 2 ? wh[0].toLongLong() * wh[1].toLongLong() : 0LL;
                };
                std::sort(candidates.begin(), candidates.end(),
                    [&](const QFileInfo& a, const QFileInfo& b) { return area(a) > area(b); });
                out.append(candidates.first().filePath());
            } else if (QDir::match(images, entry.fileName())) {
                out.append(entry.filePath());
            }
        }
    }
    return out;
}

void Preferences::setWallpaper(const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (path.isEmpty()) {
        return;
    }
    // Le immagini recenti, come Windows: le ultime cinque, senza quella di Vela.
    QStringList recent = m_recentWallpapers;
    recent.removeAll(path);
    if (!path.startsWith(u':')) {
        recent.prepend(path);
    }
    while (recent.size() > 5) {
        recent.removeLast();
    }
    QSettings settings = shellSettings();
    settings.setValue(QStringLiteral("appearance/wallpaper"), path.startsWith(u':') ? QString() : path);
    settings.setValue(QStringLiteral("appearance/recentWallpapers"), recent);
    settings.sync();
    m_wallpaper = path;
    m_recentWallpapers = recent;
    computeMica();
    emit wallpaperChanged();
}

void Preferences::computeMica()
{
    // Il colore medio dello sfondo, come lo calcola la shell per il compositor.
    QImageReader reader(m_wallpaper);
    reader.setScaledSize(QSize(32, 18));
    const QImage image = reader.read().convertToFormat(QImage::Format_RGB32);
    if (image.isNull()) {
        m_tint = QColor();
        m_mica = micaFromTint({}, true, light());
        m_micaInactive = micaFromTint({}, false, light());
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
    // Arrotondato a 0-255 come nel comando "wallpaper-tint".
    const QColor tint(int(sum[0] / count), int(sum[1] / count), int(sum[2] / count));
    m_tint = tint;
    m_mica = micaFromTint(tint, true, light());
    m_micaInactive = micaFromTint(tint, false, light());
}

// ------------------------------------------------------ colori e tema --

void Preferences::setAccent(const QColor& color)
{
    if (!color.isValid() || color == m_accent) {
        return;
    }
    m_accent = color;
    setShell(QStringLiteral("appearance/accent"), color.name());
    emit accentChanged();
}

void Preferences::setAppTheme(const QString& theme)
{
    if (theme == m_appTheme || (theme != QLatin1String("light") && theme != QLatin1String("dark"))) {
        return;
    }
    m_appTheme = theme;
    const bool light = theme == QLatin1String("light");
    // La shell e il compositor (barre del titolo), Esplora.
    setShell(QStringLiteral("appearance/appTheme"), theme);
    // GTK, il portale e chi segue org.freedesktop.appearance.
    QProcess::startDetached(QStringLiteral("gsettings"),
        { QStringLiteral("set"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("color-scheme"),
            light ? QStringLiteral("prefer-light") : QStringLiteral("prefer-dark") });
    // Le app KDE e Qt: lo schema di colori Breeze chiaro o scuro, e le icone adatte.
    if (!QStandardPaths::findExecutable(QStringLiteral("plasma-apply-colorscheme")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"),
            { light ? QStringLiteral("BreezeLight") : QStringLiteral("BreezeDark") });
    }
    const QString icons = QIcon::themeName();
    applyIconTheme(light); // anche le nostre
    if (QIcon::themeName() != icons) {
        const QString changer = QStringLiteral("/usr/lib/plasma-changeicons");
        if (QFileInfo(changer).isExecutable()) {
            QProcess::startDetached(changer, { QIcon::themeName() });
        } else if (!QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6")).isEmpty()) {
            QProcess::startDetached(QStringLiteral("kwriteconfig6"),
                { QStringLiteral("--file"), QStringLiteral("kdeglobals"), QStringLiteral("--group"), QStringLiteral("Icons"),
                    QStringLiteral("--key"), QStringLiteral("Theme"), QIcon::themeName() });
        }
    }
    computeMica();
    emit appThemeChanged();
    emit wallpaperChanged();
}

void Preferences::setShellTheme(const QString& theme)
{
    if (theme == m_shellTheme || (theme != QLatin1String("light") && theme != QLatin1String("dark"))) {
        return;
    }
    m_shellTheme = theme;
    setShell(QStringLiteral("appearance/shellTheme"), theme);
    emit appThemeChanged();
}

// ------------------------------------------------------------ taskbar --

void Preferences::setTaskbarAlignment(const QString& alignment)
{
    if (alignment != m_taskbarAlignment) {
        m_taskbarAlignment = alignment;
        setShell(QStringLiteral("taskbar/alignment"), alignment);
        emit taskbarChanged();
    }
}

void Preferences::setEndTask(bool on)
{
    if (on != m_endTask) {
        m_endTask = on;
        setShell(QStringLiteral("taskbar/endTask"), on);
        emit taskbarChanged();
    }
}

void Preferences::setTaskbarAllScreens(bool on)
{
    if (on != m_taskbarAllScreens) {
        m_taskbarAllScreens = on;
        setShell(QStringLiteral("taskbar/allScreens"), on);
        emit taskbarChanged();
    }
}

void Preferences::setTaskView(bool on)
{
    if (on != m_taskView) {
        m_taskView = on;
        setShell(QStringLiteral("taskbar/taskView"), on);
        emit taskbarChanged();
    }
}

void Preferences::setDoNotDisturb(bool on)
{
    if (on != m_doNotDisturb) {
        m_doNotDisturb = on;
        setShell(QStringLiteral("notifications/doNotDisturb"), on);
        emit doNotDisturbChanged();
    }
}

// ------------------------------------------------------- compositor --

void Preferences::saveCompositorKeys(const QStringList& keys)
{
    QList<QPair<QString, QString>> changes;
    for (const QString& key : keys) {
        if (key == QLatin1String("screen-off")) {
            changes.append({ key, QString::number(m_screenOffMinutes) });
        } else if (key == QLatin1String("lock-on-idle")) {
            changes.append({ key, m_lockOnIdle ? QStringLiteral("yes") : QStringLiteral("no") });
        } else if (key == QLatin1String("language")) {
            changes.append({ key, m_language });
        } else if (key == QLatin1String("keyboard-layout") || key == QLatin1String("keyboard-variant")) {
            QStringList values;
            for (const QVariant& value : std::as_const(m_layouts)) {
                values << value.toMap()[key == QLatin1String("keyboard-layout") ? QStringLiteral("layout") : QStringLiteral("variant")].toString();
            }
            changes.append({ key, values.join(u',') });
        } else if (key == QLatin1String("keyboard-repeat-delay")) {
            changes.append({ key, QString::number(m_repeatDelay) });
        } else if (key == QLatin1String("keyboard-repeat-rate")) {
            changes.append({ key, QString::number(m_repeatRate) });
        }
    }
    saveCompositor(changes);
}

void Preferences::saveCompositor(const QList<QPair<QString, QString>>& changes)
{
    // Il file com'è adesso (il compositor può averlo cambiato), con queste chiavi nuove.
    const QString path = compositorConfigPath();
    QStringList lines;
    {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
            lines = QString::fromUtf8(in.readAll()).split(u'\n');
            for (QString& line : lines) {
                line = modernLine(line); // i nomi italiani di prima, se il compositor non li ha già cambiati
            }
            while (!lines.isEmpty() && lines.last().isEmpty()) {
                lines.removeLast();
            }
        }
    }
    if (lines.isEmpty()) {
        lines << QStringLiteral("# Vela settings (written by the Settings app)");
    }
    for (const auto& [key, value] : changes) {
        bool found = false;
        for (QString& line : lines) {
            if (line.startsWith(key + u'=')) {
                line = key + u'=' + value;
                found = true;
                break;
            }
        }
        if (!found) {
            lines << key + u'=' + value;
        }
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    file.write((lines.join(u'\n') + u'\n').toUtf8());
    if (file.commit()) {
        // Il compositor rilegge (e la nostra copia del file si aggiorna dal watcher).
        sendToCompositor("reload-config");
    }
}

void Preferences::setScreenOffMinutes(int minutes)
{
    if (minutes != m_screenOffMinutes) {
        m_screenOffMinutes = std::max(0, minutes);
        saveCompositorKeys(QStringList { QStringLiteral("screen-off") });
        emit idleChanged();
    }
}

void Preferences::setLockOnIdle(bool on)
{
    if (on != m_lockOnIdle) {
        m_lockOnIdle = on;
        saveCompositorKeys(QStringList { QStringLiteral("lock-on-idle") });
        emit idleChanged();
    }
}

void Preferences::setLanguage(const QString& language)
{
    if (language != m_language) {
        m_language = language;
        saveCompositorKeys(QStringList { QStringLiteral("language") });
        emit languageChanged();
    }
}

void Preferences::saveLayouts()
{
    saveCompositorKeys(QStringList { QStringLiteral("keyboard-layout"), QStringLiteral("keyboard-variant") });
    emit keyboardChanged();
}

void Preferences::addKeyboardLayout(const QString& layout, const QString& variant)
{
    for (const QVariant& value : std::as_const(m_layouts)) {
        const QVariantMap entry = value.toMap();
        if (entry[QStringLiteral("layout")] == layout && entry[QStringLiteral("variant")] == variant) {
            return;
        }
    }
    m_layouts.append(QVariantMap { { QStringLiteral("layout"), layout }, { QStringLiteral("variant"), variant } });
    saveLayouts();
}

void Preferences::removeKeyboardLayout(int index)
{
    if (index >= 0 && index < m_layouts.size()) {
        m_layouts.removeAt(index);
        saveLayouts();
    }
}

void Preferences::moveKeyboardLayoutUp(int index)
{
    if (index > 0 && index < m_layouts.size()) {
        m_layouts.swapItemsAt(index, index - 1);
        saveLayouts();
    }
}

void Preferences::setRepeatDelay(int ms)
{
    if (ms != m_repeatDelay) {
        m_repeatDelay = std::clamp(ms, 100, 2000);
        saveCompositorKeys(QStringList { QStringLiteral("keyboard-repeat-delay") });
        emit keyboardChanged();
    }
}

void Preferences::setRepeatRate(int perSecond)
{
    if (perSecond != m_repeatRate) {
        m_repeatRate = std::clamp(perSecond, 1, 100);
        saveCompositorKeys(QStringList { QStringLiteral("keyboard-repeat-rate") });
        emit keyboardChanged();
    }
}

// ------------------------------------------- Luce notturna e accessibilità --

namespace {

QString yesNo(bool on)
{
    return on ? QStringLiteral("yes") : QStringLiteral("no");
}

} // namespace

void Preferences::setNightLight(bool on)
{
    if (on != m_nightLight) {
        m_nightLight = on;
        saveCompositor({ { QStringLiteral("night-light"), yesNo(on) } });
        emit nightLightChanged();
    }
}

void Preferences::setNightStrength(int strength)
{
    strength = std::clamp(strength, 0, 100);
    if (strength != m_nightStrength) {
        m_nightStrength = strength;
        saveCompositor({ { QStringLiteral("night-light-strength"), QString::number(strength) } });
        emit nightLightChanged();
    }
}

void Preferences::setNightSchedule(const QString& schedule)
{
    if (schedule != m_nightSchedule) {
        m_nightSchedule = schedule;
        saveCompositor({ { QStringLiteral("night-light-schedule"), schedule } });
        emit nightLightChanged();
    }
}

void Preferences::setNightFrom(const QString& time)
{
    if (time != m_nightFrom && QTime::fromString(time, QStringLiteral("HH:mm")).isValid()) {
        m_nightFrom = time;
        saveCompositor({ { QStringLiteral("night-light-from"), time } });
        emit nightLightChanged();
    }
}

void Preferences::setNightTo(const QString& time)
{
    if (time != m_nightTo && QTime::fromString(time, QStringLiteral("HH:mm")).isValid()) {
        m_nightTo = time;
        saveCompositor({ { QStringLiteral("night-light-to"), time } });
        emit nightLightChanged();
    }
}

void Preferences::setTearing(bool on)
{
    if (on != m_tearing) {
        m_tearing = on;
        saveCompositor({ { QStringLiteral("tearing"), yesNo(on) } });
        emit accessibilityChanged();
    }
}

void Preferences::setVrr(const QString& mode)
{
    if (mode != m_vrr) {
        m_vrr = mode;
        saveCompositor({ { QStringLiteral("variable-refresh"), mode } });
        emit accessibilityChanged();
    }
}

void Preferences::setMagnifier(bool on)
{
    if (on != m_magnifier) {
        m_magnifier = on;
        sendToCompositor(on ? "magnifier on" : "magnifier off");
        emit accessibilityChanged();
    }
}

void Preferences::setMagnifierStep(int percent)
{
    if (percent != m_magnifierStep) {
        m_magnifierStep = percent;
        saveCompositor({ { QStringLiteral("magnifier-step"), QString::number(percent) } });
        emit accessibilityChanged();
    }
}

void Preferences::setColorFilter(bool on)
{
    if (on != m_colorFilter) {
        m_colorFilter = on;
        saveCompositor({ { QStringLiteral("color-filters"), yesNo(on) } });
        emit accessibilityChanged();
    }
}

void Preferences::setColorFilterKind(const QString& kind)
{
    if (kind != m_colorFilterKind) {
        m_colorFilterKind = kind;
        saveCompositor({ { QStringLiteral("color-filter"), kind } });
        emit accessibilityChanged();
    }
}

void Preferences::setColorFilterShortcut(bool on)
{
    if (on != m_colorFilterShortcut) {
        m_colorFilterShortcut = on;
        saveCompositor({ { QStringLiteral("color-filters-shortcut"), yesNo(on) } });
        emit accessibilityChanged();
    }
}

void Preferences::setStickyKeys(bool on)
{
    if (on != m_stickyKeys) {
        m_stickyKeys = on;
        saveCompositor({ { QStringLiteral("sticky-keys"), yesNo(on) } });
        emit accessibilityChanged();
    }
}

// ------------------------------------------------------ mouse e touchpad --

void Preferences::setMouseSpeed(int value)
{
    value = std::clamp(value, 1, 20);
    if (value != m_mouseSpeed) {
        m_mouseSpeed = value;
        saveCompositor({ { QStringLiteral("mouse-speed"), QString::number(value) } });
        emit inputChanged();
    }
}

void Preferences::setMousePrecision(bool on)
{
    if (on != m_mousePrecision) {
        m_mousePrecision = on;
        saveCompositor({ { QStringLiteral("mouse-precision"), yesNo(on) } });
        emit inputChanged();
    }
}

void Preferences::setMouseLeftHanded(bool on)
{
    if (on != m_mouseLeftHanded) {
        m_mouseLeftHanded = on;
        saveCompositor({ { QStringLiteral("mouse-primary-button"), on ? QStringLiteral("right") : QStringLiteral("left") } });
        emit inputChanged();
    }
}

void Preferences::setWheelLines(int lines)
{
    lines = std::clamp(lines, 1, 20);
    if (lines != m_wheelLines) {
        m_wheelLines = lines;
        saveCompositor({ { QStringLiteral("mouse-scroll-lines"), QString::number(lines) } });
        emit inputChanged();
    }
}

void Preferences::setTouchpad(bool on)
{
    if (on != m_touchpad) {
        m_touchpad = on;
        saveCompositor({ { QStringLiteral("touchpad"), yesNo(on) } });
        emit inputChanged();
    }
}

void Preferences::setTouchpadWithMouse(bool on)
{
    if (on != m_touchpadWithMouse) {
        m_touchpadWithMouse = on;
        saveCompositor({ { QStringLiteral("touchpad-with-mouse"), yesNo(on) } });
        emit inputChanged();
    }
}

void Preferences::setTouchpadSpeed(int value)
{
    value = std::clamp(value, 1, 20);
    if (value != m_touchpadSpeed) {
        m_touchpadSpeed = value;
        saveCompositor({ { QStringLiteral("touchpad-speed"), QString::number(value) } });
        emit inputChanged();
    }
}

void Preferences::setTouchpadTap(bool on)
{
    if (on != m_touchpadTap) {
        m_touchpadTap = on;
        saveCompositor({ { QStringLiteral("touchpad-tap"), yesNo(on) } });
        emit inputChanged();
    }
}

void Preferences::setTouchpadNatural(bool on)
{
    if (on != m_touchpadNatural) {
        m_touchpadNatural = on;
        saveCompositor({ { QStringLiteral("touchpad-natural-scroll"), yesNo(on) } });
        emit inputChanged();
    }
}

void Preferences::setThreeFingers(const QString& action)
{
    if (action != m_threeFingers) {
        m_threeFingers = action;
        saveCompositor({ { QStringLiteral("touchpad-three-fingers"), action } });
        emit inputChanged();
    }
}

void Preferences::setFourFingers(const QString& action)
{
    if (action != m_fourFingers) {
        m_fourFingers = action;
        saveCompositor({ { QStringLiteral("touchpad-four-fingers"), action } });
        emit inputChanged();
    }
}

// ----------------------------------------------------------------- appunti --

void Preferences::setClipboardHistory(bool on)
{
    if (on != m_clipboardHistory) {
        m_clipboardHistory = on;
        setShell(QStringLiteral("clipboard/history"), on);
        emit clipboardChanged();
    }
}

void Preferences::clearClipboard()
{
    // Alla shell, sul suo socket.
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/vela-shell-") + display + QStringLiteral(".sock"));
    if (socket.waitForConnected(200)) {
        socket.write("clipboard-clear\n");
        socket.waitForBytesWritten(200);
    }
}
