// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Tests of the .desktop Exec key (desktopexec.h): how it's split into
// arguments and how field codes are expanded, per the freedesktop spec.
//
//   ctest --test-dir build -R exec      (or build/shell/vela-desktopexec-test)

#include "desktopexec.h"

#include <QTest>

class DesktopExecTest : public QObject {
    Q_OBJECT

    static QStringList run(const QString& exec, const QList<QUrl>& files = {},
        const DesktopExec::Context& context = {})
    {
        const DesktopExec::Parsed parsed = DesktopExec::split(exec);
        return parsed.ok ? DesktopExec::expand(parsed.arguments, files, context) : QStringList { QStringLiteral("<invalid>") };
    }

private slots:
    void plainArguments()
    {
        QCOMPARE(run(QStringLiteral("kate --new  -b")), (QStringList { "kate", "--new", "-b" }));
    }

    void quotedArguments()
    {
        QCOMPARE(run(QStringLiteral("\"/opt/My App/app\" \"one argument\"")),
            (QStringList { "/opt/My App/app", "one argument" }));
        // Inside quotes \" \` \$ \\ stand for the character.
        QCOMPARE(run(QStringLiteral("echo \"a \\\"b\\\" \\$HOME \\`x\\` c\\\\d\"")),
            (QStringList { "echo", "a \"b\" $HOME `x` c\\d" }));
    }

    void noShellSemantics()
    {
        // No expansion: $HOME, ; and > stay letters.
        const DesktopExec::Parsed parsed = DesktopExec::split(QStringLiteral("app $HOME ; rm > x"));
        QVERIFY(parsed.ok);
        QVERIFY(parsed.shellSyntax);
        QCOMPARE(parsed.arguments, (QStringList { "app", "$HOME", ";", "rm", ">", "x" }));
        // The same characters inside quotes are just text.
        QVERIFY(!DesktopExec::split(QStringLiteral("sh -c \"echo $HOME | wc\"")).shellSyntax);
    }

    void invalid()
    {
        QVERIFY(!DesktopExec::split(QStringLiteral("app \"unclosed")).ok);
        QVERIFY(!DesktopExec::split(QStringLiteral("   ")).ok);
    }

    void singleFile()
    {
        const QUrl file = QUrl::fromLocalFile(QStringLiteral("/home/alex/My Pictures/beach.jpg"));
        QCOMPARE(run(QStringLiteral("gwenview %f"), { file }),
            (QStringList { "gwenview", "/home/alex/My Pictures/beach.jpg" }));
        QCOMPARE(run(QStringLiteral("firefox %u"), { file }),
            (QStringList { "firefox", "file:///home/alex/My%20Pictures/beach.jpg" }));
        QCOMPARE(run(QStringLiteral("app --file=%f"), { file }),
            (QStringList { "app", "--file=/home/alex/My Pictures/beach.jpg" }));
        // Without files the field code vanishes.
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
        // %i without an icon: no argument. Deprecated ones vanish.
        QCOMPARE(run(QStringLiteral("app %i %d %m")), (QStringList { "app" }));
    }
};

QTEST_GUILESS_MAIN(DesktopExecTest)
#include "desktopexec.moc"
