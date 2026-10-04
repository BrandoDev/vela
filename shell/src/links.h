#pragma once

// Collegamenti e Preferiti, come Windows, in comune tra il desktop della
// shell ed Esplora.
//
// Un collegamento a un file o a una cartella è un collegamento simbolico
// ("foto - Collegamento.jpg"); uno a un indirizzo web è un file .desktop di
// tipo Link, come fanno KDE e GNOME. I Preferiti (la sezione della Home di
// Esplora) stanno nelle impostazioni di Esplora.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QUrl>

#include <unistd.h>

// "foto.jpg" -> "foto - Collegamento.jpg"; "Documenti" -> "Documenti - Collegamento".
inline QString linkNameFor(const QFileInfo& target)
{
    const bool hasSuffix = !target.isDir() && !target.suffix().isEmpty() && !target.completeBaseName().isEmpty();
    return hasSuffix ? target.completeBaseName() + QStringLiteral(" - Collegamento.") + target.suffix()
                     : target.fileName() + QStringLiteral(" - Collegamento");
}

// Un nome libero nella cartella: "nome (2).ext", come Windows.
inline QString freeNameIn(const QString& directory, const QString& name)
{
    const QDir dir(directory);
    if (!QFileInfo::exists(dir.filePath(name)) && !QFileInfo(dir.filePath(name)).isSymLink()) {
        return name;
    }
    const QFileInfo info(name);
    const QString base = info.completeBaseName().isEmpty() ? name : info.completeBaseName();
    const QString suffix = info.completeBaseName().isEmpty() || info.suffix().isEmpty() ? QString() : u'.' + info.suffix();
    for (int i = 2;; ++i) {
        const QString candidate = base + QStringLiteral(" (%1)").arg(i) + suffix;
        if (!QFileInfo::exists(dir.filePath(candidate)) && !QFileInfo(dir.filePath(candidate)).isSymLink()) {
            return candidate;
        }
    }
}

// Un collegamento a `target` dentro `directory` (col nome dato, o
// "X - Collegamento"). Il percorso creato, o vuoto se non si può.
inline QString createLink(const QString& target, const QString& directory, const QString& name = {})
{
    const QFileInfo info(target);
    const QString chosen = freeNameIn(directory, name.isEmpty() ? linkNameFor(info) : name);
    const QString path = QDir(directory).filePath(chosen);
    if (::symlink(QFile::encodeName(info.absoluteFilePath()).constData(), QFile::encodeName(path).constData()) != 0) {
        return {};
    }
    return path;
}

// Nuovo > Collegamento: `target` è un percorso (anche "~/...") o un
// indirizzo web. Il percorso creato; altrimenti vuoto e `error` dice perché.
inline QString createShortcut(const QString& directory, const QString& target, QString name, QString* error)
{
    QString text = target.trimmed();
    if (text.startsWith(u'"') && text.endsWith(u'"') && text.size() > 1) {
        text = text.mid(1, text.size() - 2);
    }
    if (text.startsWith(QLatin1String("~/")) || text == QLatin1String("~")) {
        text = QDir::homePath() + text.mid(1);
    }
    const QUrl url(text);
    const bool web = !url.scheme().isEmpty() && url.scheme() != QLatin1String("file") && !text.startsWith(u'/');
    if (!web) {
        const QString local = url.isLocalFile() ? url.toLocalFile() : text;
        if (!QFileInfo::exists(local)) {
            if (error) {
                *error = QStringLiteral("Impossibile trovare il file \"%1\".").arg(local);
            }
            return {};
        }
        const QFileInfo info(local);
        if (!name.isEmpty() && !info.isDir() && !info.suffix().isEmpty() && !name.endsWith(u'.' + info.suffix())) {
            name += u'.' + info.suffix(); // l'estensione resta: il tipo di file si riconosce
        }
        const QString path = createLink(local, directory, name.isEmpty() ? info.fileName() : name);
        if (path.isEmpty() && error) {
            *error = QStringLiteral("Impossibile creare il collegamento qui.");
        }
        return path;
    }
    if (name.isEmpty()) {
        name = url.host().isEmpty() ? text : url.host();
    }
    const QString file = freeNameIn(directory, name + QStringLiteral(".desktop"));
    const QString path = QDir(directory).filePath(file);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Impossibile creare il collegamento qui.");
        }
        return {};
    }
    out.write("[Desktop Entry]\nType=Link\nName=" + name.toUtf8() + "\nURL=" + url.toString().toUtf8()
        + "\nIcon=text-html\n");
    return path;
}

// ------------------------------------------------------------ Preferiti --

inline QStringList favoriteFiles()
{
    QSettings settings(QStringLiteral("Vela"), QStringLiteral("vela-files"));
    settings.sync();
    return settings.value(QStringLiteral("favorites")).toStringList();
}

inline bool isFavorite(const QString& path)
{
    return favoriteFiles().contains(path);
}

inline void setFavorite(const QString& path, bool on)
{
    QSettings settings(QStringLiteral("Vela"), QStringLiteral("vela-files"));
    settings.sync();
    QStringList list = settings.value(QStringLiteral("favorites")).toStringList();
    list.removeAll(path);
    if (on) {
        list.prepend(path);
    }
    settings.setValue(QStringLiteral("favorites"), list);
    settings.sync();
}
