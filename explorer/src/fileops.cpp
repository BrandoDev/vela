// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fileops.h"

#include "appmodel.h"
#include "foldermodel.h"
#include "links.h"

#include <QClipboard>
#include <QCollator>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <sys/stat.h>

#include <functional>

namespace {

QString trashDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
}

bool sameDevice(const QString& a, const QString& b)
{
    struct stat sa {};
    struct stat sb {};
    return ::lstat(QFile::encodeName(a).constData(), &sa) == 0 && ::stat(QFile::encodeName(b).constData(), &sb) == 0
        && sa.st_dev == sb.st_dev;
}

bool runDetached(const QString& program, const QStringList& arguments, const QString& directory)
{
    return QProcess::startDetached(program, arguments, directory);
}

// "foto.jpg" -> "foto - Copia.jpg", come Windows quando si incolla nella stessa cartella.
QString copyName(const QFileInfo& source)
{
    const bool hasSuffix = !source.isDir() && !source.suffix().isEmpty() && !source.completeBaseName().isEmpty();
    return hasSuffix ? source.completeBaseName() + QStringLiteral(" - Copia.") + source.suffix()
                     : source.fileName() + QStringLiteral(" - Copia");
}

void sendToShell(const QByteArray& command)
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    QLocalSocket socket;
    socket.connectToServer(runtimeDir + QStringLiteral("/vela-shell-") + display + QStringLiteral(".sock"));
    if (socket.waitForConnected(300)) {
        socket.write(command + '\n');
        socket.waitForBytesWritten(300);
    }
}

} // namespace

QString uniqueNameIn(const QString& directory, const QString& name)
{
    const QDir dir(directory);
    if (!QFileInfo::exists(dir.filePath(name))) {
        return name;
    }
    const QFileInfo info(name);
    const bool hasSuffix = !info.suffix().isEmpty() && !info.completeBaseName().isEmpty()
        && !QFileInfo(dir.filePath(name)).isDir();
    const QString base = hasSuffix ? info.completeBaseName() : name;
    const QString suffix = hasSuffix ? u'.' + info.suffix() : QString();
    for (int n = 2;; ++n) {
        const QString candidate = base + QStringLiteral(" (%1)").arg(n) + suffix;
        if (!QFileInfo::exists(dir.filePath(candidate))) {
            return candidate;
        }
    }
}

// --------------------------------------------------------- lavori lunghi --

struct FileOps::Job {
    int id = 0;
    QString title;
    qint64 done = 0;
    qint64 total = 0;
    QString current;
    bool finished = false;
    QString error;
    std::atomic<bool> cancelled { false };
};

FileOps::FileOps(AppModel* apps, QObject* parent)
    : QObject(parent)
    , m_apps(apps)
{
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &FileOps::clipboardChanged);
}

FileOps::~FileOps()
{
    for (const auto& job : std::as_const(m_jobs)) {
        job->cancelled = true;
    }
}

QVariantList FileOps::jobs() const
{
    QVariantList out;
    for (const auto& job : m_jobs) {
        out.append(QVariantMap {
            { QStringLiteral("id"), job->id },
            { QStringLiteral("title"), job->title },
            { QStringLiteral("done"), double(job->done) },
            { QStringLiteral("total"), double(job->total) },
            { QStringLiteral("current"), job->current },
            { QStringLiteral("finished"), job->finished },
            { QStringLiteral("error"), job->error },
        });
    }
    return out;
}

void FileOps::updateJob(int id, qint64 done, qint64 total, const QString& current, bool finished, const QString& error)
{
    for (const auto& job : std::as_const(m_jobs)) {
        if (job->id == id) {
            job->done = done;
            job->total = total;
            job->current = current;
            job->finished = finished;
            job->error = error;
        }
    }
    emit jobsChanged();
}

void FileOps::cancelJob(int id)
{
    for (const auto& job : std::as_const(m_jobs)) {
        if (job->id == id) {
            job->cancelled = true;
        }
    }
}

void FileOps::dismissJob(int id)
{
    m_jobs.removeIf([id](const std::shared_ptr<Job>& job) { return job->id == id && job->finished; });
    emit jobsChanged();
}

// Prima di copiare o spostare: c'è già qualcosa con lo stesso nome?
void FileOps::requestTransfer(const Transfer& transfer)
{
    int conflicts = 0;
    QString first;
    for (const QString& source : transfer.sources) {
        const QFileInfo info(source);
        if (info.absolutePath() == QDir(transfer.directory).absolutePath()) {
            continue; // nella stessa cartella: "- Copia" (o niente, se si sposta)
        }
        if (QFileInfo::exists(QDir(transfer.directory).filePath(info.fileName()))) {
            if (++conflicts == 1) {
                first = info.fileName();
            }
        }
    }
    if (conflicts > 0) {
        m_pending = transfer;
        emit conflict(conflicts, first, transfer.directory);
        return;
    }
    startTransfer(transfer, QStringLiteral("keep"));
}

void FileOps::resolveConflict(const QString& choice)
{
    const Transfer transfer = std::exchange(m_pending, {});
    if (choice != QLatin1String("cancel") && !transfer.sources.isEmpty()) {
        startTransfer(transfer, choice);
    }
}

void FileOps::startTransfer(const Transfer& transfer, const QString& policy)
{
    auto job = std::make_shared<Job>();
    job->id = m_nextJob++;
    const int count = int(transfer.sources.size());
    const QString where = QFileInfo(transfer.directory).fileName().isEmpty() ? transfer.directory
                                                                              : QFileInfo(transfer.directory).fileName();
    job->title = (transfer.move ? QStringLiteral("Spostamento di ") : QStringLiteral("Copia di "))
        + (count == 1 ? QStringLiteral("1 elemento") : QStringLiteral("%1 elementi").arg(count)) + QStringLiteral(" in ")
        + where;
    m_jobs.append(job);
    emit jobsChanged();

    QPointer<FileOps> self(this);
    auto post = [self, id = job->id](qint64 done, qint64 total, QString current, bool finished, QString error) {
        QMetaObject::invokeMethod(
            qApp,
            [=] {
                if (self) {
                    self->updateJob(id, done, total, current, finished, error);
                }
            },
            Qt::QueuedConnection);
    };
    QThreadPool::globalInstance()->start([self, job, transfer, policy, post] {
        // Quanto c'è da fare (solo ciò che va davvero copiato).
        qint64 total = 0;
        for (const QString& source : transfer.sources) {
            const QFileInfo info(source);
            if (transfer.move && sameDevice(source, transfer.directory)) {
                total += 1;
            } else if (info.isDir() && !info.isSymLink()) {
                QDirIterator it(source, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next();
                    total += std::max<qint64>(1, it.fileInfo().size());
                }
                total += 1;
            } else {
                total += std::max<qint64>(1, info.size());
            }
        }
        qint64 done = 0;
        QElapsedTimer lastPost;
        lastPost.start();
        QString error;
        UndoStep step;
        step.label = transfer.move ? QStringLiteral("Sposta") : QStringLiteral("Copia");
        QString firstArrived;

        // Un file, a pezzi, con avanzamento e annullamento.
        std::function<bool(const QString&, const QString&)> copyOne = [&](const QString& from, const QString& to) {
            const QFileInfo info(from);
            if (job->cancelled) {
                return false;
            }
            if (info.isSymLink()) {
                QFile::remove(to);
                const bool ok = QFile::link(info.symLinkTarget(), to);
                done += 1;
                return ok;
            }
            if (info.isDir()) {
                if (!QDir().mkpath(to)) {
                    error = QStringLiteral("Impossibile creare la cartella %1").arg(to);
                    return false;
                }
                const QFileInfoList children = QDir(from).entryInfoList(
                    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
                for (const QFileInfo& child : children) {
                    if (!copyOne(child.absoluteFilePath(), QDir(to).filePath(child.fileName()))) {
                        return false;
                    }
                }
                QFile::setPermissions(to, info.permissions());
                done += 1;
                return true;
            }
            QFile in(from);
            QFile out(to);
            if (!in.open(QIODevice::ReadOnly)) {
                error = QStringLiteral("Impossibile leggere %1").arg(info.fileName());
                return false;
            }
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                error = QStringLiteral("Impossibile scrivere %1").arg(to);
                return false;
            }
            QByteArray buffer;
            while (!in.atEnd()) {
                if (job->cancelled) {
                    out.close();
                    out.remove();
                    return false;
                }
                buffer = in.read(4 << 20);
                if (buffer.isEmpty() && in.error() != QFile::NoError) {
                    error = QStringLiteral("Errore leggendo %1").arg(info.fileName());
                    return false;
                }
                if (out.write(buffer) != buffer.size()) {
                    error = QStringLiteral("Spazio esaurito o errore scrivendo %1").arg(info.fileName());
                    out.close();
                    out.remove();
                    return false;
                }
                done += buffer.size();
                if (lastPost.elapsed() > 100) {
                    post(done, total, info.fileName(), false, QString());
                    lastPost.restart();
                }
            }
            out.close();
            out.setPermissions(info.permissions());
            if (out.open(QIODevice::ReadWrite)) {
                out.setFileTime(info.lastModified(), QFileDevice::FileModificationTime);
                out.close();
            }
            if (info.size() == 0) {
                done += 1;
            }
            return true;
        };

        for (const QString& source : transfer.sources) {
            if (job->cancelled || !error.isEmpty()) {
                break;
            }
            const QFileInfo info(source);
            const QDir target(transfer.directory);
            const bool sameDir = info.absolutePath() == target.absolutePath();
            if (sameDir && transfer.move) {
                continue;
            }
            if (transfer.directory == source || transfer.directory.startsWith(source + u'/')) {
                error = QStringLiteral("La cartella di destinazione è dentro quella di origine.");
                break;
            }
            QString name = info.fileName();
            if (sameDir) {
                name = uniqueNameIn(transfer.directory, copyName(info));
            } else if (QFileInfo::exists(target.filePath(name))) {
                if (policy == QLatin1String("skip")) {
                    continue;
                }
                if (policy == QLatin1String("keep")) {
                    name = uniqueNameIn(transfer.directory, name);
                } else if (!(info.isDir() && QFileInfo(target.filePath(name)).isDir())) {
                    // Sostituisci: via il vecchio (le cartelle invece si uniscono).
                    QFileInfo old(target.filePath(name));
                    old.isDir() ? QDir(old.absoluteFilePath()).removeRecursively() : QFile::remove(old.absoluteFilePath());
                }
            }
            const QString destination = target.filePath(name);
            const bool merging = QFileInfo(destination).isDir();
            post(done, total, info.fileName(), false, QString());
            if (transfer.move && !merging && sameDevice(source, transfer.directory)
                && ::rename(QFile::encodeName(source).constData(), QFile::encodeName(destination).constData()) == 0) {
                done += 1;
                step.moves.append({ destination, source });
            } else if (copyOne(source, destination)) {
                if (transfer.move) {
                    info.isDir() && !info.isSymLink() ? QDir(source).removeRecursively() : QFile::remove(source);
                    step.moves.append({ destination, source });
                } else if (!merging) {
                    step.created.append(destination);
                }
            } else {
                break;
            }
            if (firstArrived.isEmpty()) {
                firstArrived = destination;
            }
        }
        const bool cancelled = job->cancelled;
        post(total, total, QString(), true, cancelled ? QStringLiteral("Annullato") : error);
        QMetaObject::invokeMethod(
            qApp,
            [self, step, firstArrived, id = job->id, ok = error.isEmpty() && !cancelled] {
                if (!self) {
                    return;
                }
                if (!step.moves.isEmpty() || !step.created.isEmpty()) {
                    self->pushUndo(step);
                }
                if (!firstArrived.isEmpty()) {
                    emit self->created(firstArrived, false);
                }
                // Finito bene: il riquadro sparisce da solo dopo un attimo.
                if (ok) {
                    QTimer::singleShot(1500, self, [self, id] {
                        if (self) {
                            self->dismissJob(id);
                        }
                    });
                }
            },
            Qt::QueuedConnection);
    });
}

// ------------------------------------------------------------- aprire --

void FileOps::open(const QStringList& paths)
{
    static const QMimeDatabase mimes;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (path.endsWith(QLatin1String(".desktop")) && m_apps->launchDesktopFile(path)) {
            continue;
        }
        const QString app = defaultAppFor(mimes.mimeTypeForFile(info).name());
        if (app.isEmpty() || !m_apps->launchWithFile(app, QUrl::fromLocalFile(path).toString())) {
            runDetached(QStringLiteral("xdg-open"), { path }, info.absolutePath());
        }
    }
}

void FileOps::openWith(const QString& appId, const QString& path)
{
    m_apps->launchWithFile(appId, QUrl::fromLocalFile(path).toString());
}

QVariantList FileOps::appsFor(const QString& path) const
{
    return m_apps->appsForFile(path);
}

// ------------------------------------------------------------- appunti --

namespace {

void putFiles(const QStringList& paths, bool cut)
{
    QList<QUrl> urls;
    QByteArray gnome = cut ? "cut" : "copy";
    for (const QString& path : paths) {
        const QUrl url = QUrl::fromLocalFile(path);
        urls.append(url);
        gnome += '\n' + url.toEncoded();
    }
    if (urls.isEmpty()) {
        return;
    }
    auto* mime = new QMimeData;
    mime->setUrls(urls);
    // Come Dolphin e Nautilus: dicono se è "taglia" o "copia".
    mime->setData(QStringLiteral("application/x-kde-cutselection"), cut ? "1" : "0");
    mime->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    QGuiApplication::clipboard()->setMimeData(mime);
}

bool clipboardIsCut(const QMimeData* mime)
{
    return mime->data(QStringLiteral("application/x-kde-cutselection")) == "1"
        || mime->data(QStringLiteral("x-special/gnome-copied-files")).startsWith("cut");
}

} // namespace

void FileOps::copy(const QStringList& paths)
{
    putFiles(paths, false);
}

void FileOps::cut(const QStringList& paths)
{
    putFiles(paths, true);
}

bool FileOps::canPaste() const
{
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls()) {
        return false;
    }
    const QList<QUrl> urls = mime->urls();
    return std::any_of(urls.cbegin(), urls.cend(), [](const QUrl& url) { return url.isLocalFile(); });
}

QSet<QString> FileOps::cutPaths() const
{
    QSet<QString> out;
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (mime && mime->hasUrls() && clipboardIsCut(mime)) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) {
                out.insert(url.toLocalFile());
            }
        }
    }
    return out;
}

void FileOps::paste(const QString& directory)
{
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls()) {
        return;
    }
    Transfer transfer;
    transfer.directory = directory;
    transfer.move = clipboardIsCut(mime);
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile())) {
            transfer.sources.append(QFileInfo(url.toLocalFile()).absoluteFilePath());
        }
    }
    if (transfer.sources.isEmpty()) {
        return;
    }
    if (transfer.move) {
        QGuiApplication::clipboard()->clear(); // spostati: non si incollano due volte
    }
    requestTransfer(transfer);
}

void FileOps::copyAsPath(const QStringList& paths)
{
    QStringList quoted;
    for (const QString& path : paths) {
        quoted.append(u'"' + path + u'"');
    }
    QGuiApplication::clipboard()->setText(quoted.join(u'\n'));
}

void FileOps::drop(const QStringList& urls, const QString& directory, int action)
{
    QStringList paths;
    for (const QString& text : urls) {
        const QUrl url(text);
        if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile())) {
            paths.append(QFileInfo(url.toLocalFile()).absoluteFilePath());
        }
    }
    if (paths.isEmpty()) {
        return;
    }
    if (directory == QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files")) {
        trash(paths);
        return;
    }
    Transfer transfer;
    transfer.directory = directory;
    transfer.move = action == 2 || (action == 0 && sameDevice(paths.first(), directory));
    for (const QString& path : std::as_const(paths)) {
        // Lasciati nella cartella dove sono già: niente da fare.
        if (QFileInfo(path).absolutePath() != QDir(directory).absolutePath() || !transfer.move) {
            transfer.sources.append(path);
        }
    }
    if (!transfer.sources.isEmpty()) {
        requestTransfer(transfer);
    }
}

// --------------------------------------------------------------- nuovo --

QString FileOps::createFolder(const QString& directory)
{
    const QString name = uniqueNameIn(directory, QStringLiteral("Nuova cartella"));
    if (!QDir(directory).mkdir(name)) {
        emit failed(QStringLiteral("Impossibile creare la cartella qui."));
        return {};
    }
    const QString path = QDir(directory).filePath(name);
    UndoStep step;
    step.label = QStringLiteral("Nuovo");
    step.created.append(path);
    pushUndo(step);
    emit created(path, true);
    return path;
}

QString FileOps::createFile(const QString& directory, const QString& templatePath)
{
    const QString name = uniqueNameIn(directory,
        templatePath.isEmpty() ? QStringLiteral("Nuovo documento di testo.txt") : QFileInfo(templatePath).fileName());
    const QString path = QDir(directory).filePath(name);
    bool ok = false;
    if (templatePath.isEmpty()) {
        QFile file(path);
        ok = file.open(QIODevice::WriteOnly);
    } else {
        ok = QFile::copy(templatePath, path);
    }
    if (!ok) {
        emit failed(QStringLiteral("Impossibile creare il file qui."));
        return {};
    }
    UndoStep step;
    step.label = QStringLiteral("Nuovo");
    step.created.append(path);
    pushUndo(step);
    emit created(path, true);
    return path;
}

QVariantList FileOps::templates() const
{
    QVariantList out;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TemplatesLocation);
    if (dir.isEmpty() || QDir(dir) == QDir::home()) {
        return out;
    }
    static const QMimeDatabase mimes;
    for (const QFileInfo& info : QDir(dir).entryInfoList(QDir::Files, QDir::Name)) {
        out.append(QVariantMap { { QStringLiteral("name"), info.completeBaseName() },
            { QStringLiteral("icon"), mimes.mimeTypeForFile(info).iconName() },
            { QStringLiteral("path"), info.absoluteFilePath() } });
    }
    return out;
}

QString FileOps::rename(const QString& path, const QString& newName)
{
    const QString clean = newName.trimmed();
    const QFileInfo info(path);
    if (clean.isEmpty() || clean == info.fileName()) {
        return info.fileName();
    }
    if (clean.contains(u'/') || clean == QLatin1String(".") || clean == QLatin1String("..")) {
        return QStringLiteral("!Il nome non può contenere il carattere /");
    }
    const QString target = info.absoluteDir().filePath(clean);
    if (QFileInfo::exists(target)) {
        return QStringLiteral("!In questa cartella c'è già un elemento chiamato \"%1\".").arg(clean);
    }
    if (!QFile::rename(path, target)) {
        return QStringLiteral("!Impossibile rinominare \"%1\".").arg(info.fileName());
    }
    UndoStep step;
    step.label = QStringLiteral("Rinomina");
    step.moves.append({ target, path });
    pushUndo(step);
    return clean;
}

// ------------------------------------------------------------- Cestino --

void FileOps::trash(const QStringList& paths)
{
    UndoStep step;
    step.label = QStringLiteral("Elimina");
    for (const QString& path : paths) {
        QString inTrash;
        if (QFile::moveToTrash(path, &inTrash)) {
            step.trashed.append(inTrash);
            step.originals.append(path);
        } else {
            emit failed(QStringLiteral("Impossibile spostare \"%1\" nel Cestino.").arg(QFileInfo(path).fileName()));
        }
    }
    if (!step.trashed.isEmpty()) {
        pushUndo(step);
    }
}

void FileOps::deletePermanently(const QStringList& paths)
{
    for (const QString& path : paths) {
        const QFileInfo info(path);
        const bool ok = info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!ok) {
            emit failed(QStringLiteral("Impossibile eliminare \"%1\".").arg(info.fileName()));
        }
        // Dal Cestino: via anche la sua scheda.
        if (path.startsWith(trashDir() + QStringLiteral("/files/"))) {
            QFile::remove(trashDir() + QStringLiteral("/info/") + info.fileName() + QStringLiteral(".trashinfo"));
        }
    }
}

void FileOps::emptyTrash()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("gio")).isEmpty()) {
        runDetached(QStringLiteral("gio"), { QStringLiteral("trash"), QStringLiteral("--empty") }, QDir::homePath());
    } else {
        runDetached(QStringLiteral("ktrash6"), { QStringLiteral("--empty") }, QDir::homePath());
    }
}

QString FileOps::originalLocation(const QString& trashedPath) const
{
    QFile info(trashDir() + QStringLiteral("/info/") + QFileInfo(trashedPath).fileName() + QStringLiteral(".trashinfo"));
    if (!info.open(QIODevice::ReadOnly)) {
        return {};
    }
    for (const QByteArray& line : info.readAll().split('\n')) {
        if (line.startsWith("Path=")) {
            return QUrl::fromPercentEncoding(line.mid(5).trimmed());
        }
    }
    return {};
}

void FileOps::restoreFromTrash(const QStringList& paths)
{
    for (const QString& path : paths) {
        const QString original = originalLocation(path);
        if (original.isEmpty()) {
            continue;
        }
        const QFileInfo target(original);
        QDir().mkpath(target.absolutePath());
        const QString destination = QDir(target.absolutePath()).filePath(uniqueNameIn(target.absolutePath(), target.fileName()));
        if (QFile::rename(path, destination)) {
            QFile::remove(trashDir() + QStringLiteral("/info/") + QFileInfo(path).fileName() + QStringLiteral(".trashinfo"));
        }
    }
}

// --------------------------------------------------------- comprimere --

void FileOps::compress(const QStringList& paths, const QString& format)
{
    if (paths.isEmpty()) {
        return;
    }
    const QFileInfo first(paths.first());
    const QString directory = first.absolutePath();
    QStringList names;
    for (const QString& path : paths) {
        names.append(QFileInfo(path).fileName());
    }
    // L'archivio prende il nome del primo elemento, come in Windows.
    const QString base = first.isDir() ? first.fileName() : first.completeBaseName();
    const QString archive = uniqueNameIn(directory, base + u'.' + format);
    QStringList arguments { QStringLiteral("-a"), QStringLiteral("-cf"), archive, QStringLiteral("--") };
    arguments.append(names);
    if (runDetached(QStringLiteral("bsdtar"), arguments, directory)) {
        UndoStep step;
        step.label = QStringLiteral("Comprimi");
        step.created.append(QDir(directory).filePath(archive));
        pushUndo(step);
        emit created(QDir(directory).filePath(archive), false);
    }
}

bool FileOps::isArchive(const QString& path) const
{
    static const QStringList suffixes { QStringLiteral(".zip"), QStringLiteral(".7z"), QStringLiteral(".tar"),
        QStringLiteral(".tar.gz"), QStringLiteral(".tgz"), QStringLiteral(".tar.xz"), QStringLiteral(".tar.zst"),
        QStringLiteral(".tar.bz2"), QStringLiteral(".rar") };
    return std::any_of(suffixes.cbegin(), suffixes.cend(),
        [&](const QString& s) { return path.endsWith(s, Qt::CaseInsensitive); });
}

void FileOps::extractAll(const QString& path)
{
    // "Estrai tutto": in una cartella col nome dell'archivio, accanto.
    const QFileInfo info(path);
    QString base = info.fileName();
    for (const QString& s : { QStringLiteral(".tar.gz"), QStringLiteral(".tar.xz"), QStringLiteral(".tar.zst"),
             QStringLiteral(".tar.bz2") }) {
        if (base.endsWith(s, Qt::CaseInsensitive)) {
            base.chop(s.size());
        }
    }
    if (base == info.fileName()) {
        base = info.completeBaseName();
    }
    const QString folder = uniqueNameIn(info.absolutePath(), base);
    const QString target = QDir(info.absolutePath()).filePath(folder);
    QDir().mkpath(target);
    runDetached(QStringLiteral("bsdtar"), { QStringLiteral("-xf"), path, QStringLiteral("-C"), target }, info.absolutePath());
    UndoStep step;
    step.label = QStringLiteral("Estrai");
    step.created.append(target);
    pushUndo(step);
    emit created(target, false);
}

// ------------------------------------------------------------- annulla --

void FileOps::pushUndo(UndoStep step)
{
    m_undo.append(std::move(step));
    if (m_undo.size() > 30) {
        m_undo.removeFirst();
    }
    emit undoChanged();
}

QString FileOps::undoText() const
{
    return m_undo.isEmpty() ? QString() : m_undo.last().label;
}

void FileOps::undo()
{
    if (m_undo.isEmpty()) {
        return;
    }
    const UndoStep step = m_undo.takeLast();
    emit undoChanged();
    for (const auto& [current, original] : step.moves) {
        if (!QFileInfo::exists(original)) {
            QDir().mkpath(QFileInfo(original).absolutePath());
            if (!QFile::rename(current, original)) {
                // Tra dischi diversi: si riporta indietro copiando.
                startTransfer({ { current }, QFileInfo(original).absolutePath(), true }, QStringLiteral("keep"));
            }
        }
    }
    for (const QString& path : step.created) {
        QFile::moveToTrash(path);
    }
    for (qsizetype i = 0; i < step.trashed.size(); ++i) {
        // Dal Cestino (…/Trash/files/nome) al suo posto, senza la scheda in …/Trash/info.
        const QString inTrash = step.trashed.at(i);
        if (!QFileInfo::exists(step.originals.at(i)) && QFile::rename(inTrash, step.originals.at(i))) {
            const QFileInfo info(inTrash);
            QFile::remove(info.absoluteDir().absoluteFilePath(
                QStringLiteral("../info/") + info.fileName() + QStringLiteral(".trashinfo")));
        }
    }
}

void FileOps::showProperties(const QStringList& paths)
{
    sendToShell("properties " + QJsonDocument(QJsonArray::fromStringList(paths)).toJson(QJsonDocument::Compact));
}

void FileOps::share(const QStringList& paths)
{
    sendToShell("share " + QJsonDocument(QJsonArray::fromStringList(paths)).toJson(QJsonDocument::Compact));
}

void FileOps::chooseApp(const QString& path)
{
    sendToShell("open-with " + QJsonDocument(QJsonArray::fromStringList({ path })).toJson(QJsonDocument::Compact));
}

void FileOps::newShortcut(const QString& directory)
{
    sendToShell("new-shortcut " + QJsonDocument(QJsonArray::fromStringList({ directory })).toJson(QJsonDocument::Compact));
}

// ------------------------------------------------------- collegamenti --

void FileOps::createLinks(const QStringList& paths)
{
    UndoStep step;
    step.label = QStringLiteral("Nuovo");
    QString last;
    for (const QString& path : paths) {
        QString directory = QFileInfo(path).absolutePath();
        if (!QFileInfo(directory).isWritable()) {
            // Come Windows: qui non si può, va sul desktop.
            directory = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        }
        const QString link = createLink(path, directory);
        if (link.isEmpty()) {
            emit failed(QStringLiteral("Impossibile creare il collegamento a \"%1\".").arg(QFileInfo(path).fileName()));
            continue;
        }
        step.created.append(link);
        last = link;
    }
    if (!step.created.isEmpty()) {
        pushUndo(step);
        emit created(last, false);
    }
}

void FileOps::pasteLinks(const QString& directory)
{
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls()) {
        return;
    }
    UndoStep step;
    step.label = QStringLiteral("Nuovo");
    QString last;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        // Nella stessa cartella dell'originale il nome dice che è un collegamento.
        const QString link = createLink(info.absoluteFilePath(), directory,
            info.absolutePath() == QDir(directory).absolutePath() ? QString() : info.fileName());
        if (!link.isEmpty()) {
            step.created.append(link);
            last = link;
        }
    }
    if (step.created.isEmpty()) {
        emit failed(QStringLiteral("Impossibile creare i collegamenti qui."));
        return;
    }
    pushUndo(step);
    emit created(last, false);
}

bool FileOps::isLink(const QString& path) const
{
    return QFileInfo(path).isSymLink();
}

QString FileOps::linkTarget(const QString& path) const
{
    return QFileInfo(path).symLinkTarget();
}

// ------------------------------------------- anteprima e dettagli --

QVariantMap FileOps::details(const QString& path) const
{
    static const QMimeDatabase mimes;
    const QFileInfo info(path);
    QVariantMap out {
        { QStringLiteral("name"), info.fileName().isEmpty() ? path : info.fileName() },
        { QStringLiteral("location"), info.absolutePath() },
        { QStringLiteral("isDir"), info.isDir() },
        { QStringLiteral("modified"), info.lastModified() },
        { QStringLiteral("created"), info.birthTime().isValid() ? info.birthTime() : info.metadataChangeTime() },
    };
    const QMimeType mime = mimes.mimeTypeForFile(info);
    out[QStringLiteral("mime")] = mime.name();
    out[QStringLiteral("icon")] = info.isDir() ? QStringLiteral("folder") : mime.iconName();
    out[QStringLiteral("type")] = info.isDir() ? QStringLiteral("Cartella di file") : mime.comment();
    if (info.isDir()) {
        out[QStringLiteral("items")] = int(QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
    } else {
        out[QStringLiteral("size")] = double(info.size());
        out[QStringLiteral("sizeText")] = ::formatSize(info.size());
        QImageReader reader(path);
        if (reader.canRead()) {
            const QSize size = reader.size();
            if (size.isValid()) {
                out[QStringLiteral("width")] = size.width();
                out[QStringLiteral("height")] = size.height();
            }
        }
    }
    if (info.isSymLink()) {
        out[QStringLiteral("target")] = info.symLinkTarget();
    }
    return out;
}

QString FileOps::previewText(const QString& path) const
{
    static const QMimeDatabase mimes;
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > 8 * 1024 * 1024) {
        return {};
    }
    const QMimeType mime = mimes.mimeTypeForFile(info);
    if (!mime.inherits(QStringLiteral("text/plain")) && mime.name() != QLatin1String("application/json")
        && mime.name() != QLatin1String("application/xml")) {
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    // Basta l'inizio: il riquadro ne mostra poche decine di righe.
    QString text = QString::fromUtf8(file.read(32 * 1024));
    text.replace(u'\t', QStringLiteral("    "));
    return text.isEmpty() ? QStringLiteral(" ") : text;
}

bool FileOps::isImage(const QString& path) const
{
    return QImageReader(path).canRead();
}

// ------------------------------------------------------------ varie --

QString FileOps::formatSize(double bytes) const
{
    return ::formatSize(qint64(bytes));
}

QStringList FileOps::subfolders(const QString& path, bool hidden) const
{
    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (hidden) {
        filters |= QDir::Hidden;
    }
    QStringList names = QDir(path).entryList(filters);
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), collator);
    return names;
}

QString FileOps::resolve(const QString& text, const QString& base) const
{
    QString path = text.trimmed();
    if (path.startsWith(QLatin1String("file://"))) {
        path = QUrl(path).toLocalFile();
    }
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/"))) {
        path = QDir::homePath() + path.mid(1);
    }
    if (!path.startsWith(u'/')) {
        path = QDir(base.startsWith(u'/') ? base : QDir::homePath()).filePath(path);
    }
    const QFileInfo info(path);
    return info.isDir() ? info.absoluteFilePath() : QString();
}

void FileOps::newWindow(const QString& location) const
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
        location.startsWith(u'/') ? QStringList { location } : QStringList {});
}
