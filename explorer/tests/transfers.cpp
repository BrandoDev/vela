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
        QString error = QStringLiteral("<nessun lavoro>");
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
        write(path("a/foto.jpg"), "contenuto");
        QDir().mkpath(path("b"));
        QCOMPARE(transfer({ path("a/foto.jpg") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/foto.jpg")), QByteArray("contenuto"));
        QCOMPARE(read(path("a/foto.jpg")), QByteArray("contenuto"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void replaceKeepsMetadata()
    {
        write(path("a/doc.txt"), "nuovo");
        write(path("b/doc.txt"), "vecchio");
        const QDateTime when = QDateTime::fromSecsSinceEpoch(1700000000);
        QFile source(path("a/doc.txt"));
        QVERIFY(source.open(QIODevice::ReadWrite));
        QVERIFY(source.setFileTime(when, QFileDevice::FileModificationTime));
        source.close();
        QFile::setPermissions(path("a/doc.txt"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        QCOMPARE(transfer({ path("a/doc.txt") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/doc.txt")), QByteArray("nuovo"));
        QCOMPARE(QFileInfo(path("b/doc.txt")).lastModified(), when);
        QVERIFY(QFileInfo(path("b/doc.txt")).permissions() & QFile::ExeOwner);
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    // Il caso della review: "Sostituisci" con il disco che si riempie a metà
    // copia. Il vecchio file deve restare intatto e il temporaneo sparire.
    void diskFullKeepsOldFile()
    {
        write(path("a/grande.bin"), QByteArray(3 << 20, 'n'));
        write(path("b/grande.bin"), "vecchio");
        // Oltre 1 MB le scritture falliscono (EFBIG), come con il disco pieno.
        rlimit old {};
        getrlimit(RLIMIT_FSIZE, &old);
        rlimit small = old;
        small.rlim_cur = 1 << 20;
        signal(SIGXFSZ, SIG_IGN);
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &small), 0);
        const QString error = transfer({ path("a/grande.bin") }, path("b"), 1);
        setrlimit(RLIMIT_FSIZE, &old);

        QVERIFY2(!error.isEmpty(), "la copia doveva fallire");
        QCOMPARE(read(path("b/grande.bin")), QByteArray("vecchio"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    // Una cartella nuova che non si riesce a copiare tutta: niente albero a
    // metà nella destinazione.
    void failedFolderLeavesNothing()
    {
        write(path("a/cartella/piccolo.txt"), "ok");
        write(path("a/cartella/sotto/grande.bin"), QByteArray(3 << 20, 'g'));
        QDir().mkpath(path("b"));
        rlimit old {};
        getrlimit(RLIMIT_FSIZE, &old);
        rlimit small = old;
        small.rlim_cur = 1 << 20;
        signal(SIGXFSZ, SIG_IGN);
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &small), 0);
        const QString error = transfer({ path("a/cartella") }, path("b"), 1);
        setrlimit(RLIMIT_FSIZE, &old);

        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(path("b/cartella")));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void copyFolderTree()
    {
        write(path("a/progetto/uno.txt"), "1");
        write(path("a/progetto/sotto/due.txt"), "2");
        QVERIFY(QFile::link(QStringLiteral("uno.txt"), path("a/progetto/collegamento")));
        QDir().mkpath(path("b"));
        QCOMPARE(transfer({ path("a/progetto") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/progetto/uno.txt")), QByteArray("1"));
        QCOMPARE(read(path("b/progetto/sotto/due.txt")), QByteArray("2"));
        QCOMPARE(QFileInfo(path("b/progetto/collegamento")).symLinkTarget(), path("b/progetto/uno.txt"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void mergeFolders()
    {
        write(path("a/foto/nuova.jpg"), "nuova");
        write(path("a/foto/comune.jpg"), "aggiornata");
        write(path("b/foto/vecchia.jpg"), "vecchia");
        write(path("b/foto/comune.jpg"), "originale");
        QCOMPARE(transfer({ path("a/foto") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/foto/nuova.jpg")), QByteArray("nuova"));
        QCOMPARE(read(path("b/foto/vecchia.jpg")), QByteArray("vecchia"));
        QCOMPARE(read(path("b/foto/comune.jpg")), QByteArray("aggiornata"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void fileReplacesFolder()
    {
        write(path("a/elemento"), "file");
        write(path("b/elemento/dentro.txt"), "cartella");
        QCOMPARE(transfer({ path("a/elemento") }, path("b"), 1), QString());
        QVERIFY(QFileInfo(path("b/elemento")).isFile());
        QCOMPARE(read(path("b/elemento")), QByteArray("file"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void folderReplacesFile()
    {
        write(path("a/elemento/dentro.txt"), "cartella");
        write(path("b/elemento"), "file");
        QCOMPARE(transfer({ path("a/elemento") }, path("b"), 1), QString());
        QVERIFY(QFileInfo(path("b/elemento")).isDir());
        QCOMPARE(read(path("b/elemento/dentro.txt")), QByteArray("cartella"));
        QVERIFY(leftovers(m_dir->path()).isEmpty());
    }

    void keepBoth()
    {
        m_policy = QStringLiteral("keep");
        write(path("a/nota.txt"), "nuova");
        write(path("b/nota.txt"), "vecchia");
        QCOMPARE(transfer({ path("a/nota.txt") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/nota.txt")), QByteArray("vecchia"));
        QCOMPARE(read(path("b/nota (2).txt")), QByteArray("nuova"));
    }

    void skipExisting()
    {
        m_policy = QStringLiteral("skip");
        write(path("a/nota.txt"), "nuova");
        write(path("b/nota.txt"), "vecchia");
        QCOMPARE(transfer({ path("a/nota.txt") }, path("b"), 1), QString());
        QCOMPARE(read(path("b/nota.txt")), QByteArray("vecchia"));
    }

    void moveReplacesFile()
    {
        write(path("a/nota.txt"), "nuova");
        write(path("b/nota.txt"), "vecchia");
        QCOMPARE(transfer({ path("a/nota.txt") }, path("b"), 2), QString());
        QCOMPARE(read(path("b/nota.txt")), QByteArray("nuova"));
        QVERIFY(!QFileInfo::exists(path("a/nota.txt")));
    }

    void copyIntoSameFolder()
    {
        write(path("a/foto.jpg"), "x");
        QCOMPARE(transfer({ path("a/foto.jpg") }, path("a"), 1), QString());
        QCOMPARE(read(path("a/foto - Copia.jpg")), QByteArray("x"));
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
