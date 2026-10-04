#include "preferences.h"

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
    emit taskbarChanged();
    m_doNotDisturb = settings.value(QStringLiteral("notifications/doNotDisturb"), false).toBool();
    emit doNotDisturbChanged();

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

void Preferences::reloadCompositor()
{
    // vela.conf
    m_compositor.clear();
    QFile file(compositorConfigPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
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
        const QString v = value(key, fallback ? QStringLiteral("sì") : QStringLiteral("no"));
        return v == QStringLiteral("sì") || v == QLatin1String("si") || v == QLatin1String("1") || v == QLatin1String("true");
    };
    m_screenOffMinutes = value(QStringLiteral("spegni-schermo"), QStringLiteral("10")).toInt();
    const QString lock = value(QStringLiteral("blocca"), QStringLiteral("sì"));
    m_lockOnIdle = lock != QLatin1String("0") && lock != QLatin1String("no");
    emit idleChanged();

    m_layouts.clear();
    const QStringList layouts = value(QStringLiteral("tastiera-layout"), {}).split(u',', Qt::SkipEmptyParts);
    const QStringList variants = value(QStringLiteral("tastiera-variante"), {}).split(u',');
    for (qsizetype i = 0; i < layouts.size(); ++i) {
        m_layouts.append(QVariantMap { { QStringLiteral("layout"), layouts[i].trimmed() },
            { QStringLiteral("variant"), i < variants.size() ? variants[i].trimmed() : QString() } });
    }
    m_repeatDelay = value(QStringLiteral("tastiera-ritardo"), QStringLiteral("400")).toInt();
    m_repeatRate = value(QStringLiteral("tastiera-velocita"), QStringLiteral("30")).toInt();
    emit keyboardChanged();

    m_nightLight = flag(QStringLiteral("luce-notturna"), false);
    m_nightStrength = std::clamp(value(QStringLiteral("luce-notturna-intensita"), QStringLiteral("48")).toInt(), 0, 100);
    m_nightSchedule = value(QStringLiteral("luce-notturna-pianifica"), QStringLiteral("no"));
    m_nightFrom = value(QStringLiteral("luce-notturna-dalle"), QStringLiteral("21:00"));
    m_nightTo = value(QStringLiteral("luce-notturna-alle"), QStringLiteral("07:00"));
    m_tearing = flag(QStringLiteral("tearing"), true);
    m_colorFilter = flag(QStringLiteral("filtri-colore"), false);
    m_colorFilterKind = value(QStringLiteral("filtro-colore"), QStringLiteral("grigi"));
    m_colorFilterShortcut = flag(QStringLiteral("filtri-colore-scorciatoia"), false);
    m_magnifierStep = value(QStringLiteral("lente-incremento"), QStringLiteral("100")).toInt();
    m_stickyKeys = flag(QStringLiteral("tasti-permanenti"), false);
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
        if (key == QLatin1String("spegni-schermo")) {
            changes.append({ key, QString::number(m_screenOffMinutes) });
        } else if (key == QLatin1String("blocca")) {
            changes.append({ key, m_lockOnIdle ? QStringLiteral("sì") : QStringLiteral("no") });
        } else if (key == QLatin1String("tastiera-layout") || key == QLatin1String("tastiera-variante")) {
            QStringList values;
            for (const QVariant& value : std::as_const(m_layouts)) {
                values << value.toMap()[key == QLatin1String("tastiera-layout") ? QStringLiteral("layout") : QStringLiteral("variant")].toString();
            }
            changes.append({ key, values.join(u',') });
        } else if (key == QLatin1String("tastiera-ritardo")) {
            changes.append({ key, QString::number(m_repeatDelay) });
        } else if (key == QLatin1String("tastiera-velocita")) {
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
            while (!lines.isEmpty() && lines.last().isEmpty()) {
                lines.removeLast();
            }
        }
    }
    if (lines.isEmpty()) {
        lines << QStringLiteral("# Impostazioni di Vela (le scrive l'app Impostazioni)");
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
        saveCompositorKeys(QStringList { QStringLiteral("spegni-schermo") });
        emit idleChanged();
    }
}

void Preferences::setLockOnIdle(bool on)
{
    if (on != m_lockOnIdle) {
        m_lockOnIdle = on;
        saveCompositorKeys(QStringList { QStringLiteral("blocca") });
        emit idleChanged();
    }
}

void Preferences::saveLayouts()
{
    saveCompositorKeys(QStringList { QStringLiteral("tastiera-layout"), QStringLiteral("tastiera-variante") });
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
        saveCompositorKeys(QStringList { QStringLiteral("tastiera-ritardo") });
        emit keyboardChanged();
    }
}

void Preferences::setRepeatRate(int perSecond)
{
    if (perSecond != m_repeatRate) {
        m_repeatRate = std::clamp(perSecond, 1, 100);
        saveCompositorKeys(QStringList { QStringLiteral("tastiera-velocita") });
        emit keyboardChanged();
    }
}

// ------------------------------------------- Luce notturna e accessibilità --

namespace {

QString yesNo(bool on)
{
    return on ? QStringLiteral("sì") : QStringLiteral("no");
}

} // namespace

void Preferences::setNightLight(bool on)
{
    if (on != m_nightLight) {
        m_nightLight = on;
        saveCompositor({ { QStringLiteral("luce-notturna"), yesNo(on) } });
        emit nightLightChanged();
    }
}

void Preferences::setNightStrength(int strength)
{
    strength = std::clamp(strength, 0, 100);
    if (strength != m_nightStrength) {
        m_nightStrength = strength;
        saveCompositor({ { QStringLiteral("luce-notturna-intensita"), QString::number(strength) } });
        emit nightLightChanged();
    }
}

void Preferences::setNightSchedule(const QString& schedule)
{
    if (schedule != m_nightSchedule) {
        m_nightSchedule = schedule;
        saveCompositor({ { QStringLiteral("luce-notturna-pianifica"), schedule } });
        emit nightLightChanged();
    }
}

void Preferences::setNightFrom(const QString& time)
{
    if (time != m_nightFrom && QTime::fromString(time, QStringLiteral("HH:mm")).isValid()) {
        m_nightFrom = time;
        saveCompositor({ { QStringLiteral("luce-notturna-dalle"), time } });
        emit nightLightChanged();
    }
}

void Preferences::setNightTo(const QString& time)
{
    if (time != m_nightTo && QTime::fromString(time, QStringLiteral("HH:mm")).isValid()) {
        m_nightTo = time;
        saveCompositor({ { QStringLiteral("luce-notturna-alle"), time } });
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
        saveCompositor({ { QStringLiteral("lente-incremento"), QString::number(percent) } });
        emit accessibilityChanged();
    }
}

void Preferences::setColorFilter(bool on)
{
    if (on != m_colorFilter) {
        m_colorFilter = on;
        saveCompositor({ { QStringLiteral("filtri-colore"), yesNo(on) } });
        emit accessibilityChanged();
    }
}

void Preferences::setColorFilterKind(const QString& kind)
{
    if (kind != m_colorFilterKind) {
        m_colorFilterKind = kind;
        saveCompositor({ { QStringLiteral("filtro-colore"), kind } });
        emit accessibilityChanged();
    }
}

void Preferences::setColorFilterShortcut(bool on)
{
    if (on != m_colorFilterShortcut) {
        m_colorFilterShortcut = on;
        saveCompositor({ { QStringLiteral("filtri-colore-scorciatoia"), yesNo(on) } });
        emit accessibilityChanged();
    }
}

void Preferences::setStickyKeys(bool on)
{
    if (on != m_stickyKeys) {
        m_stickyKeys = on;
        saveCompositor({ { QStringLiteral("tasti-permanenti"), yesNo(on) } });
        emit accessibilityChanged();
    }
}
