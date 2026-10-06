// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Le prove della chiave Exec dei file .desktop (desktopexec.h): come si
// divide in argomenti e come si espandono i field code, secondo la
// specifica freedesktop.
//
//   ctest --test-dir build -R exec      (o build/shell/vela-desktopexec-test)

#include "desktopexec.h"

#include <QTest>

class DesktopExecTest : public QObject {
    Q_OBJECT

    static QStringList run(const QString& exec, const QList<QUrl>& files = {},
        const DesktopExec::Context& context = {})
    {
        const DesktopExec::Parsed parsed = DesktopExec::split(exec);
        return parsed.ok ? DesktopExec::expand(parsed.arguments, files, context) : QStringList { QStringLiteral("<non valido>") };
    }

private slots:
    void plainArguments()
    {
        QCOMPARE(run(QStringLiteral("kate --new  -b")), (QStringList { "kate", "--new", "-b" }));
    }

    void quotedArguments()
    {
        QCOMPARE(run(QStringLiteral("\"/opt/My App/app\" \"un argomento\"")),
            (QStringList { "/opt/My App/app", "un argomento" }));
        // Dentro le virgolette \" \` \$ \\ valgono il carattere.
        QCOMPARE(run(QStringLiteral("echo \"a \\\"b\\\" \\$HOME \\`x\\` c\\\\d\"")),
            (QStringList { "echo", "a \"b\" $HOME `x` c\\d" }));
    }

    void noShellSemantics()
    {
        // Nessuna espansione: $HOME, ; e > restano lettere.
        const DesktopExec::Parsed parsed = DesktopExec::split(QStringLiteral("app $HOME ; rm > x"));
        QVERIFY(parsed.ok);
        QVERIFY(parsed.shellSyntax);
        QCOMPARE(parsed.arguments, (QStringList { "app", "$HOME", ";", "rm", ">", "x" }));
        // Gli stessi caratteri tra virgolette sono testo e basta.
        QVERIFY(!DesktopExec::split(QStringLiteral("sh -c \"echo $HOME | wc\"")).shellSyntax);
    }

    void invalid()
    {
        QVERIFY(!DesktopExec::split(QStringLiteral("app \"non chiuso")).ok);
        QVERIFY(!DesktopExec::split(QStringLiteral("   ")).ok);
    }

    void singleFile()
    {
        const QUrl file = QUrl::fromLocalFile(QStringLiteral("/home/alex/Le mie foto/mare.jpg"));
        QCOMPARE(run(QStringLiteral("gwenview %f"), { file }),
            (QStringList { "gwenview", "/home/alex/Le mie foto/mare.jpg" }));
        QCOMPARE(run(QStringLiteral("firefox %u"), { file }),
            (QStringList { "firefox", "file:///home/alex/Le%20mie%20foto/mare.jpg" }));
        QCOMPARE(run(QStringLiteral("app --file=%f"), { file }),
            (QStringList { "app", "--file=/home/alex/Le mie foto/mare.jpg" }));
        // Senza file il field code sparisce.
        QCOMPARE(run(QStringLiteral("gwenview %f")), (QStringList { "gwenview" }));
    }

    void manyFiles()
    {
        const QList<QUrl> files { QUrl::fromLocalFile(QStringLiteral("/a b")), QUrl::fromLocalFile(QStringLiteral("/c")) };
        QCOMPARE(run(QStringLiteral("kate %F"), files), (QStringList { "kate", "/a b", "/c" }));
        QCOMPARE(run(QStringLiteral("vlc %U"), files), (QStringList { "vlc", "file:///a%20b", "file:///c" }));
        QVERIFY(DesktopExec::onePerFile(DesktopExec::split(QStringLiteral("app %f")).arguments));
        QVERIFY(!DesktopExec::onePerFile(DesktopExec::split(QStringLiteral("app %F")).arguments));
        QVERIFY(!DesktopExec::takesFiles(DesktopExec::split(QStringLiteral("app --new")).arguments));
    }

    void otherCodes()
    {
        const DesktopExec::Context context { QStringLiteral("kate"), QStringLiteral("Kate"),
            QStringLiteral("/usr/share/applications/org.kde.kate.desktop") };
        QCOMPARE(run(QStringLiteral("kate %i --title=%c %k 100%%"), {}, context),
            (QStringList { "kate", "--icon", "kate", "--title=Kate", "/usr/share/applications/org.kde.kate.desktop",
                "100%" }));
        // %i senza icona: nessun argomento. Deprecati: spariscono.
        QCOMPARE(run(QStringLiteral("app %i %d %m")), (QStringList { "app" }));
    }
};

QTEST_GUILESS_MAIN(DesktopExecTest)
#include "desktopexec.moc"
