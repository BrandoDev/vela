#include "appmodel.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
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

    QHash<QString, QString> values;
    bool inMainGroup = false;
    QTextStream stream(&file);
    QString line;
    while (stream.readLineInto(&line)) {
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        if (line.startsWith(u'[')) {
            if (inMainGroup) {
                break; // le azioni aggiuntive non ci servono
            }
            inMainGroup = line.trimmed() == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inMainGroup) {
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (eq <= 0) {
            continue;
        }
        values.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }

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
    const auto localized = [&](const QString& key) {
        for (const QString& locale : localeKeys) {
            const auto it = values.constFind(QStringLiteral("%1[%2]").arg(key, locale));
            if (it != values.constEnd()) {
                return unescape(*it);
            }
        }
        return unescape(values.value(key));
    };

    entry.name = localized(QStringLiteral("Name"));
    entry.genericName = localized(QStringLiteral("GenericName"));
    entry.comment = localized(QStringLiteral("Comment"));
    entry.keywords = localized(QStringLiteral("Keywords"));
    entry.icon = values.value(QStringLiteral("Icon"));
    entry.exec = cleanExec(unescape(values.value(QStringLiteral("Exec"))));
    entry.wmClass = values.value(QStringLiteral("StartupWMClass"));
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
