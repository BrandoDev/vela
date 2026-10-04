#include "appmodel.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QtDebug>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

// Gli escape previsti dalla specifica Desktop Entry per i valori stringa.
QString unescape(const QString& value)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != u'\\' || i + 1 >= value.size()) {
            out.append(c);
            continue;
        }
        const QChar next = value.at(++i);
        switch (next.unicode()) {
        case 's': out.append(u' '); break;
        case 'n': out.append(u'\n'); break;
        case 't': out.append(u'\t'); break;
        case 'r': out.append(u'\r'); break;
        case '\\': out.append(u'\\'); break;
        default:
            out.append(u'\\');
            out.append(next);
            break;
        }
    }
    return out;
}

// Toglie i "field code" (%f, %U, ...) che servono solo quando si aprono file.
QString cleanExec(const QString& exec)
{
    static const QRegularExpression fieldCodes(QStringLiteral("%[fFuUdDnNickvm]"));
    QString result = exec;
    result.remove(fieldCodes);
    result.replace(QStringLiteral("%%"), QStringLiteral("%"));
    return result.simplified();
}

QString shellQuote(QString text)
{
    text.replace(u'\'', QStringLiteral("'\\''"));
    return u'\'' + text + u'\'';
}

bool listContains(const QString& list, const QString& item)
{
    const auto parts = list.split(u';', Qt::SkipEmptyParts);
    return std::any_of(parts.begin(), parts.end(), [&](const QString& part) {
        return part.compare(item, Qt::CaseInsensitive) == 0;
    });
}

// Legge un file .desktop. Restituisce false se non va mostrato nel menu.
bool parseDesktopFile(const QString& path, const QStringList& localeKeys, AppModel::Entry& entry)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    // Ogni gruppo ([Desktop Entry], [Desktop Action nuova-finestra], ...)
    // con le sue chiavi.
    QHash<QString, QHash<QString, QString>> groups;
    QString group;
    QTextStream stream(&file);
    QString line;
    while (stream.readLineInto(&line)) {
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        if (line.startsWith(u'[')) {
            group = line.trimmed().mid(1).chopped(1);
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (eq <= 0 || group.isEmpty()) {
            continue;
        }
        groups[group].insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    const QHash<QString, QString> values = groups.value(QStringLiteral("Desktop Entry"));

    if (values.value(QStringLiteral("Type")) != QLatin1String("Application")) {
        return false;
    }
    const auto isTrue = [&](const char* key) {
        return values.value(QLatin1String(key)).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    };
    if (isTrue("NoDisplay") || isTrue("Hidden")) {
        return false;
    }
    const QString onlyShowIn = values.value(QStringLiteral("OnlyShowIn"));
    if (!onlyShowIn.isEmpty() && !listContains(onlyShowIn, QStringLiteral("Vela"))
        && !listContains(onlyShowIn, QStringLiteral("KDE"))) {
        return false;
    }
    if (listContains(values.value(QStringLiteral("NotShowIn")), QStringLiteral("Vela"))) {
        return false;
    }
    const QString tryExec = values.value(QStringLiteral("TryExec"));
    if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty()
        && !QFile::exists(tryExec)) {
        return false;
    }

    // Prima la versione tradotta (Name[it_IT], Name[it]), poi quella base.
    const auto localizedIn = [&](const QHash<QString, QString>& values, const QString& key) {
        for (const QString& locale : localeKeys) {
            const auto it = values.constFind(QStringLiteral("%1[%2]").arg(key, locale));
            if (it != values.constEnd()) {
                return unescape(*it);
            }
        }
        return unescape(values.value(key));
    };
    const auto localized = [&](const QString& key) { return localizedIn(values, key); };

    entry.name = localized(QStringLiteral("Name"));
    entry.genericName = localized(QStringLiteral("GenericName"));
    entry.comment = localized(QStringLiteral("Comment"));
    entry.keywords = localized(QStringLiteral("Keywords"));
    entry.icon = values.value(QStringLiteral("Icon"));
    entry.rawExec = unescape(values.value(QStringLiteral("Exec")));
    entry.exec = cleanExec(entry.rawExec);
    entry.path = path;
    // Le azioni dell'app (le "attività" della jump list), nell'ordine dato.
    for (const QString& id : values.value(QStringLiteral("Actions")).split(u';', Qt::SkipEmptyParts)) {
        const QHash<QString, QString> action = groups.value(QStringLiteral("Desktop Action ") + id);
        AppModel::Action a { id, localizedIn(action, QStringLiteral("Name")), action.value(QStringLiteral("Icon")),
            unescape(action.value(QStringLiteral("Exec"))) };
        if (!a.name.isEmpty() && !a.exec.isEmpty()) {
            entry.actions.append(a);
        }
    }
    entry.wmClass = values.value(QStringLiteral("StartupWMClass"));
    entry.mimeTypes = values.value(QStringLiteral("MimeType")).split(u';', Qt::SkipEmptyParts);
    entry.terminal = isTrue("Terminal");
    return !entry.name.isEmpty() && !entry.exec.isEmpty();
}

} // namespace

AppModel::AppModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void AppModel::reload()
{
    QElapsedTimer timer;
    timer.start();

    const QString localeName = QLocale().name(); // es. it_IT
    QStringList localeKeys { localeName };
    if (const qsizetype underscore = localeName.indexOf(u'_'); underscore > 0) {
        localeKeys << localeName.left(underscore);
    }

    // Le cartelle sono in ordine di priorità: la prima occorrenza di un id
    // vince (es. ~/.local/share/applications sovrascrive /usr/share/...).
    QList<Entry> entries;
    QSet<QString> seen;
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString& dir : dirs) {
        QDirIterator it(dir, { QStringLiteral("*.desktop") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            // L'id include le sottocartelle con '-' (specifica Desktop Entry).
            QString id = QDir(dir).relativeFilePath(path);
            id.replace(u'/', u'-');
            if (seen.contains(id)) {
                continue;
            }
            seen.insert(id); // anche se nascosta: una copia locale può nascondere quella di sistema
            Entry entry;
            if (parseDesktopFile(path, localeKeys, entry)) {
                entry.id = id;
                entries.append(std::move(entry));
            }
        }
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        return collator.compare(a.name, b.name) < 0;
    });

    beginResetModel();
    m_all = std::move(entries);
    m_visible.clear();
    endResetModel();
    applyFilter();

    qInfo("vela-shell: %lld applicazioni caricate in %lld ms",
        static_cast<long long>(m_all.size()), static_cast<long long>(timer.elapsed()));
}

void AppModel::setQuery(const QString& query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    applyFilter();
}

void AppModel::applyFilter()
{
    const QString q = m_query.trimmed();
    QList<int> visible;

    if (q.isEmpty()) {
        visible.reserve(m_all.size());
        for (int i = 0; i < m_all.size(); ++i) {
            visible.append(i);
        }
    } else {
        // Punteggio semplice: prima chi inizia con il testo cercato, poi chi
        // ha una parola che inizia così, poi le corrispondenze nelle
        // descrizioni e nelle parole chiave.
        QList<QPair<int, int>> scored; // (punteggio, indice)
        for (int i = 0; i < m_all.size(); ++i) {
            const Entry& e = m_all.at(i);
            int score = 0;
            if (e.name.startsWith(q, Qt::CaseInsensitive)) {
                score = 100;
            } else if (e.name.contains(QStringLiteral(" ") + q, Qt::CaseInsensitive)) {
                score = 80;
            } else if (e.name.contains(q, Qt::CaseInsensitive)) {
                score = 60;
            } else if (e.genericName.contains(q, Qt::CaseInsensitive)
                || e.keywords.contains(q, Qt::CaseInsensitive)) {
                score = 40;
            } else if (e.comment.contains(q, Qt::CaseInsensitive)
                || e.exec.contains(q, Qt::CaseInsensitive)) {
                score = 20;
            }
            if (score > 0) {
                scored.append({ score, i });
            }
        }
        std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        visible.reserve(scored.size());
        for (const auto& [score, index] : scored) {
            visible.append(index);
        }
    }

    if (visible == m_visible) {
        return;
    }
    beginResetModel();
    m_visible = std::move(visible);
    endResetModel();
    emit countChanged();
}

int AppModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant AppModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) {
        return {};
    }
    const Entry& e = m_all.at(m_visible.at(index.row()));
    switch (role) {
    case IdRole: return e.id;
    case Qt::DisplayRole:
    case NameRole: return e.name;
    case IconRole: return e.icon;
    case CommentRole: return e.comment.isEmpty() ? e.genericName : e.comment;
    default: return {};
    }
}

QHash<int, QByteArray> AppModel::roleNames() const
{
    return {
        { IdRole, "appId" },
        { NameRole, "name" },
        { IconRole, "iconName" },
        { CommentRole, "comment" },
    };
}

bool AppModel::launch(int row)
{
    if (row < 0 || row >= m_visible.size()) {
        return false;
    }
    return launchEntry(m_all.at(m_visible.at(row)));
}

bool AppModel::launchId(const QString& id)
{
    for (const Entry& e : std::as_const(m_all)) {
        if (e.id == id) {
            return launchEntry(e);
        }
    }
    return false;
}

QVariantMap AppModel::entry(const QString& id) const
{
    for (const Entry& e : m_all) {
        if (e.id == id) {
            return { { QStringLiteral("name"), e.name }, { QStringLiteral("iconName"), e.icon } };
        }
    }
    return {};
}

QString AppModel::findDesktopId(const QString& appId) const
{
    if (appId.isEmpty()) {
        return {};
    }
    const auto baseId = [](const Entry& e) { return e.id.chopped(8); }; // senza ".desktop"
    const auto program = [](const Entry& e) {
        const QString first = e.exec.section(u' ', 0, 0);
        return first.section(u'/', -1);
    };
    // Dalla regola più affidabile alla più approssimativa. Le app moderne
    // usano come app_id il nome del proprio file .desktop.
    const std::function<bool(const Entry&)> rules[] = {
        [&](const Entry& e) { return baseId(e).compare(appId, Qt::CaseInsensitive) == 0; },
        [&](const Entry& e) { return e.wmClass.compare(appId, Qt::CaseInsensitive) == 0; },
        [&](const Entry& e) {
            return baseId(e).section(u'.', -1).compare(appId, Qt::CaseInsensitive) == 0;
        },
        [&](const Entry& e) { return program(e).compare(appId, Qt::CaseInsensitive) == 0; },
    };
    for (const auto& matches : rules) {
        for (const Entry& e : m_all) {
            if (matches(e)) {
                return e.id;
            }
        }
    }
    return {};
}

QString AppModel::iconForAppId(const QString& appId) const
{
    const QString desktopId = findDesktopId(appId);
    const QString icon = desktopId.isEmpty() ? QString() : entry(desktopId).value(QStringLiteral("iconName")).toString();
    return icon.isEmpty() ? appId : icon; // spesso il tema ha un'icona col nome dell'app_id
}

const AppModel::Entry* AppModel::find(const QString& id) const
{
    for (const Entry& e : m_all) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

QVariantList AppModel::actions(const QString& id) const
{
    QVariantList out;
    if (const Entry* e = find(id)) {
        for (const Action& a : e->actions) {
            out.append(QVariantMap { { QStringLiteral("id"), a.id }, { QStringLiteral("name"), a.name },
                { QStringLiteral("icon"), a.icon.isEmpty() ? e->icon : a.icon } });
        }
    }
    return out;
}

bool AppModel::launchAction(const QString& id, const QString& actionId)
{
    const Entry* e = find(id);
    if (!e) {
        return false;
    }
    for (const Action& a : e->actions) {
        if (a.id == actionId) {
            Entry copy = *e;
            copy.exec = cleanExec(a.exec);
            return launchEntry(copy);
        }
    }
    return false;
}

bool AppModel::launchWithFile(const QString& id, const QString& url)
{
    const Entry* e = find(id);
    if (!e) {
        return false;
    }
    // Il file prende il posto dei field code (%f, %u...); se l'app non ne
    // ha, va in fondo alla riga di comando.
    const QUrl fileUrl(url);
    const QString quoted = shellQuote(fileUrl.isLocalFile() ? fileUrl.toLocalFile() : url);
    const QString quotedUrl = shellQuote(url);
    static const QRegularExpression fileCodes(QStringLiteral("%[fF]"));
    static const QRegularExpression urlCodes(QStringLiteral("%[uU]"));
    QString exec = e->rawExec;
    if (exec.contains(fileCodes)) {
        exec.replace(fileCodes, quoted);
    } else if (exec.contains(urlCodes)) {
        exec.replace(urlCodes, quotedUrl);
    } else {
        exec += u' ' + quoted;
    }
    Entry copy = *e;
    copy.exec = cleanExec(exec);
    return launchEntry(copy);
}

// L'app predefinita per un tipo di file, da mimeapps.list (prima quello
// dell'utente, poi quelli di sistema), come fa xdg-mime.
QString defaultAppFor(const QString& mime)
{
    QStringList files;
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    files << config + QStringLiteral("/mimeapps.list") << config + QStringLiteral("/kde-mimeapps.list");
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation)) {
        files << dir + QStringLiteral("/mimeapps.list");
    }
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        files << dir + QStringLiteral("/mimeapps.list");
    }
    for (const QString& path : std::as_const(files)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        bool inDefaults = false;
        QTextStream stream(&file);
        QString line;
        while (stream.readLineInto(&line)) {
            if (line.startsWith(u'[')) {
                inDefaults = line.trimmed() == QLatin1String("[Default Applications]");
            } else if (inDefaults && line.startsWith(mime + u'=')) {
                const QString first = line.mid(mime.size() + 1).section(u';', 0, 0).trimmed();
                if (!first.isEmpty()) {
                    return first;
                }
            }
        }
    }
    return {};
}

QVariantList AppModel::allApps() const
{
    QVariantList out;
    for (const Entry& entry : m_all) {
        out.append(QVariantMap { { QStringLiteral("id"), entry.id }, { QStringLiteral("name"), entry.name },
            { QStringLiteral("icon"), entry.icon } });
    }
    return out;
}

QVariantList AppModel::appsForFile(const QString& path) const
{
    static const QMimeDatabase mimes;
    const QMimeType type = mimes.mimeTypeForFile(path);
    QStringList accepted { type.name() };
    accepted << type.allAncestors();
    accepted << type.aliases();
    const QString preferred = defaultAppFor(type.name());

    QVariantList out;
    QSet<QString> names;
    for (const Entry& e : m_all) {
        const bool opens = std::any_of(e.mimeTypes.cbegin(), e.mimeTypes.cend(),
            [&](const QString& m) { return accepted.contains(m); });
        if (!opens || names.contains(e.name)) {
            continue;
        }
        names.insert(e.name);
        const QVariantMap app { { QStringLiteral("id"), e.id }, { QStringLiteral("name"), e.name },
            { QStringLiteral("icon"), e.icon }, { QStringLiteral("isDefault"), e.id == preferred } };
        if (e.id == preferred) {
            out.prepend(app);
        } else {
            out.append(app);
        }
    }
    return out;
}

bool AppModel::launchDesktopFile(const QString& path) const
{
    const QString localeName = QLocale().name();
    QStringList localeKeys { localeName, localeName.section(u'_', 0, 0) };
    Entry entry;
    if (!parseDesktopFile(path, localeKeys, entry)) {
        return false;
    }
    return launchEntry(entry);
}

QString AppModel::idForDesktopFile(const QString& path) const
{
    const QString id = QFileInfo(path).fileName();
    return find(id) ? id : QString();
}

QString AppModel::desktopFile(const QString& id) const
{
    const Entry* e = find(id);
    return e ? e->path : QString();
}

QString AppModel::name(const QString& id) const
{
    const Entry* e = find(id);
    return e ? e->name : QString();
}

QString AppModel::program(const QString& id) const
{
    const Entry* e = find(id);
    return e ? e->exec.section(u' ', 0, 0).section(u'/', -1) : QString();
}

bool AppModel::launchEntry(const Entry& entry) const
{
    QString command = entry.exec;
    if (entry.terminal) {
        const QString terminal = qEnvironmentVariable("VELA_TERMINAL", QStringLiteral("konsole"));
        command = terminal + QStringLiteral(" -e ") + command;
    }
    qInfo("vela-shell: avvio %s", qPrintable(command));
    // "exec" fa sì che la shell venga sostituita dal programma.
    return QProcess::startDetached(QStringLiteral("/bin/sh"),
        { QStringLiteral("-c"), QStringLiteral("exec ") + command }, QDir::homePath());
}
