#include "preferences.h"

#include "mica.h"

#include <QDir>
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

    // La modalità delle app: quella che GTK e il portale conoscono già.
    QProcess gsettings;
    gsettings.start(QStringLiteral("gsettings"),
        { QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("color-scheme") });
    const bool light = gsettings.waitForFinished(1000) && gsettings.readAllStandardOutput().contains("light");
    m_appTheme = light ? QStringLiteral("light") : QStringLiteral("dark");
    emit appThemeChanged();

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
        for (const auto& [k, v] : std::as_const(m_compositor)) {
            if (k == key) {
                return v;
            }
        }
        return fallback;
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
        m_mica = micaFromTint({}, true);
        m_micaInactive = micaFromTint({}, false);
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
    m_mica = micaFromTint(tint, true);
    m_micaInactive = micaFromTint(tint, false);
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
    // GTK, il portale e chi segue org.freedesktop.appearance.
    QProcess::startDetached(QStringLiteral("gsettings"),
        { QStringLiteral("set"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("color-scheme"),
            light ? QStringLiteral("prefer-light") : QStringLiteral("prefer-dark") });
    // Le app KDE e Qt: lo schema di colori Breeze chiaro o scuro.
    if (!QStandardPaths::findExecutable(QStringLiteral("plasma-apply-colorscheme")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"),
            { light ? QStringLiteral("BreezeLight") : QStringLiteral("BreezeDark") });
    }
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

void Preferences::saveCompositor()
{
    auto set = [this](const QString& key, const QString& value) {
        for (auto& entry : m_compositor) {
            if (entry.first == key) {
                entry.second = value;
                return;
            }
        }
        m_compositor.append({ key, value });
    };
    set(QStringLiteral("spegni-schermo"), QString::number(m_screenOffMinutes));
    set(QStringLiteral("blocca"), m_lockOnIdle ? QStringLiteral("sì") : QStringLiteral("no"));
    QStringList layouts;
    QStringList variants;
    for (const QVariant& value : std::as_const(m_layouts)) {
        layouts << value.toMap()[QStringLiteral("layout")].toString();
        variants << value.toMap()[QStringLiteral("variant")].toString();
    }
    set(QStringLiteral("tastiera-layout"), layouts.join(u','));
    set(QStringLiteral("tastiera-variante"), variants.join(u','));
    set(QStringLiteral("tastiera-ritardo"), QString::number(m_repeatDelay));
    set(QStringLiteral("tastiera-velocita"), QString::number(m_repeatRate));

    const QString path = compositorConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out << "# Impostazioni di Vela (le scrive l'app Impostazioni)\n";
    for (const auto& [key, value] : std::as_const(m_compositor)) {
        out << key << '=' << value << '\n';
    }
    out.flush();
    if (file.commit()) {
        sendToCompositor("reload-config");
    }
}

void Preferences::setScreenOffMinutes(int minutes)
{
    if (minutes != m_screenOffMinutes) {
        m_screenOffMinutes = std::max(0, minutes);
        saveCompositor();
        emit idleChanged();
    }
}

void Preferences::setLockOnIdle(bool on)
{
    if (on != m_lockOnIdle) {
        m_lockOnIdle = on;
        saveCompositor();
        emit idleChanged();
    }
}

void Preferences::saveLayouts()
{
    saveCompositor();
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
        saveCompositor();
        emit keyboardChanged();
    }
}

void Preferences::setRepeatRate(int perSecond)
{
    if (perSecond != m_repeatRate) {
        m_repeatRate = std::clamp(perSecond, 1, 100);
        saveCompositor();
        emit keyboardChanged();
    }
}
