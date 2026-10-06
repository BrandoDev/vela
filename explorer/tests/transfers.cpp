// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Le prove delle copie e degli spostamenti di Esplora, senza interfaccia:
// i casi in cui un file manager non deve mai perdere dati. Ogni prova lavora
// in una cartella temporanea propria.
//
//   ctest --test-dir build -R esplora      (o build/explorer/vela-files-test)

#include "appmodel.h"
#include "fileops.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <csignal>
#include <sys/resource.h>

namespace {

void write(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(data), data.size());
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<illeggibile>");
}

// I temporanei della copia (".nome.vela-copy-XXXXXX") rimasti sotto `dir`.
QStringList leftovers(const QString& dir)
{
    QStringList found;
    QDirIterator it(dir, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileName().contains(QLatin1String(".vela-copy-"))) {
            found.append(it.filePath());
        }
    }
    return found;
}

} // namespace

class Transfers : public QObject {
    Q_OBJECT

    AppModel m_apps;
    FileOps* m_ops = nullptr;
    QTemporaryDir* m_dir = nullptr;
    QString m_policy = QStringLiteral("replace"); // la risposta ai conflitti

    QString path(const QString& relative) const { return m_dir->filePath(relative); }

    // Trascina `sources` in `directory` (2 = sposta, 1 = copia) e aspetta la
    // fine del lavoro. Restituisce l'errore del lavoro (vuoto se è andato bene).
    QString transfer(const QStringList& sources, const QString& directory, int action)
    {
        QStringList urls;
        for (const QString& source : sources) {
            urls.append(QUrl::fromLocalFile(source).toString());
        }
        const int before = int(m_ops->jobs().size());
        m_ops->drop(urls, directory, action);
        QString error = QStringLiteral("<no job>");
        const bool finished = QTest::qWaitFor(
            [&] {
                const QVariantList jobs = m_ops->jobs();
                if (jobs.size() <= before) {
                    return false;
                }
                const QVariantMap job = jobs.last().toMap();
                if (!job.value(QStringLiteral("finished")).toBool()) {
                    return false;
                }
                error = job.value(QStringLiteral("error")).toString();
                return true;
            },
            30000);
        return finished ? error : QStringLiteral("<tempo scaduto>");
    }

private slots:
    void init()
    {
        m_dir = new QTemporaryDir;
        QVERIFY(m_dir->isValid());
        m_ops = new FileOps(&m_apps);
        m_policy = QStringLiteral("replace");
        connect(m_ops, &FileOps::conflict, this, [this] { m_ops->resolveConflict(m_policy); });
    }

    void cleanup()
    {
        delete m_ops;
        delete m_dir;
    }

    void copyNewFile()
    {
        write(path("a/photo.jpg"), "content");
        QDir().mkpath(path("b"));
        QCOMPARE(transfer({ path("a/photo.jpg") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/photo.jpg")), QByteArray("content"));
        QCOMPARE(read(path("a/photo.jpg")), QByteArray("content"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void replaceKeepsMetadata()
    {
        write(path("a/doc.txt"), "new");
        write(path("b/doc.txt"), "old");
        const QDateTime when = QDateTime::fromSecsSinceEpoch(1700000000);
        QFile source(path("a/doc.txt"));
        QVERIFY(source.open(QIODevice::ReadWrite));
        QVERIFY(source.setFileTime(when, QFileDevice::FileModificationTime));
        source.close();
        QFile::setPermissions(path("a/doc.txt"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        QCOMPARE(transfer({ path("a/doc.txt") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/doc.txt")), QByteArray("new"));
        QCOMPARE(QFileInfo(path("b/doc.txt")).lastModified(), when);
        QVERIFY(QFileInfo(path("b/doc.txt")).permissions() & QFile::ExeOwner);
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    // Il caso della review: "Sostituisci" con il disco che si riempie a metà
    // copia. Il vecchio file deve restare intatto e il temporaneo sparire.
    void diskFullKeepsOldFile()
    {
        write(path("a/big.bin"), QByteArray(3 << 20, 'n'));
        write(path("b/big.bin"), "old");
        // Oltre 1 MB le scritture falliscono (EFBIG), come con il disco pieno.
        rlimit old {};
        getrlimit(RLIMIT_FSIZE, &old);
        rlimit small = old;
        small.rlim_cur = 1 << 20;
        signal(SIGXFSZ, SIG_IGN);
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &small), 0);
        const QString error = transfer({ path("a/big.bin") }, path("b"), 1);
        setrlimit(RLIMIT_FSIZE, &old);

        QVERIFY2(!error.isEmpty(), "the copy should have failed");
        QCOMPARE(read(path("b/big.bin")), QByteArray("old"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    // Una cartella nuova che non si riesce a copiare tutta: niente albero a
    // metà nella destinazione.
    void failedFolderLeavesNothing()
    {
        write(path("a/folder/small.txt"), "ok");
        write(path("a/folder/sub/big.bin"), QByteArray(3 << 20, 'g'));
        QDir().mkpath(path("b"));
        rlimit old {};
        getrlimit(RLIMIT_FSIZE, &old);
        rlimit small = old;
        small.rlim_cur = 1 << 20;
        signal(SIGXFSZ, SIG_IGN);
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &small), 0);
        const QString error = transfer({ path("a/folder") }, path("b"), 1);
        setrlimit(RLIMIT_FSIZE, &old);

        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(path("b/folder")));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void copyFolderTree()
    {
        write(path("a/project/one.txt"), "1");
        write(path("a/project/sub/two.txt"), "2");
        QVERIFY(QFile::link(QStringLiteral("one.txt"), path("a/project/link")));
        QDir().mkpath(path("b"));
        QCOMPARE(transfer({ path("a/project") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/project/one.txt")), QByteArray("1"));
        QCOMPARE(read(path("b/project/sub/two.txt")), QByteArray("2"));
        QCOMPARE(QFileInfo(path("b/project/link")).symLinkTarget(), path("b/project/one.txt"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void mergeFolders()
    {
        write(path("a/photo/new.jpg"), "new");
        write(path("a/photo/common.jpg"), "updated");
        write(path("b/photo/old.jpg"), "old");
        write(path("b/photo/common.jpg"), "original");
        QCOMPARE(transfer({ path("a/photo") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/photo/new.jpg")), QByteArray("new"));
        QCOMPARE(read(path("b/photo/old.jpg")), QByteArray("old"));
        QCOMPARE(read(path("b/photo/common.jpg")), QByteArray("updated"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void fileReplacesFolder()
    {
        write(path("a/item"), "file");
        write(path("b/item/inside.txt"), "folder");
        QCOMPARE(transfer({ path("a/item") }, path("b"), 1), QString());
        QVERIFY(QFileInfo(path("b/item")).isFile());
        QCOMPARE(read(path("b/item")), QByteArray("file"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void folderReplacesFile()
    {
        write(path("a/item/inside.txt"), "folder");
        write(path("b/item"), "file");
        QCOMPARE(transfer({ path("a/item") }, path("b"), 1), QString());
        QVERIFY(QFileInfo(path("b/item")).isDir());
        QCOMPARE(read(path("b/item/inside.txt")), QByteArray("folder"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void keepBoth()
    {
        m_policy = QStringLiteral("keep");
        write(path("a/note.txt"), "new");
        write(path("b/note.txt"), "old");
        QCOMPARE(transfer({ path("a/note.txt") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/note.txt")), QByteArray("old"));
        QCOMPARE(read(path("b/note (2).txt")), QByteArray("new"));
    }

    void skipExisting()
    {
        m_policy = QStringLiteral("skip");
        write(path("a/note.txt"), "new");
        write(path("b/note.txt"), "old");
        QCOMPARE(transfer({ path("a/note.txt") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/note.txt")), QByteArray("old"));
    }

    void moveReplacesFile()
    {
        write(path("a/note.txt"), "new");
        write(path("b/note.txt"), "old");
        QCOMPARE(transfer({ path("a/note.txt") }, path("b"), 2), QString());
        QCOMPARE(read(path("b/note.txt")), QByteArray("new"));
        QVERIFY(!QFileInfo::exists(path("a/note.txt")));
    }

    void copyIntoSameFolder()
    {
        write(path("a/photo.jpg"), "x");
        QCOMPARE(transfer({ path("a/photo.jpg") }, path("a"), 1), QString());
        QCOMPARE(read(path("a/photo - Copy.jpg")), QByteArray("x"));
    }
};

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    Transfers test;
    return QTest::qExec(&test, argc, argv);
}

#include "transfers.moc"
