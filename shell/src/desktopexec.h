// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

// The Exec key of .desktop files, as the freedesktop spec says (Desktop Entry,
// "The Exec key"): it isn't a shell command line. It's split into arguments
// (spaces; double quotes, inside which \" \` \$ and \\ stand for the
// character), field codes are expanded (%f %F %u %U %i %c %k %%; deprecated
// ones vanish) and the program is launched with its arguments, without
// /bin/sh: no $VAR, ;, >, *, `...` or $(...), as GLib does.
//
// The file's string escapes (\s, \n...) must already be removed (unescape in
// appmodel.cpp): what arrives here is the Exec value as the spec means it.
namespace DesktopExec {

struct Context {
    QString icon; // Icon=, for %i
    QString name; // translated Name=, for %c
    QString desktopFile; // the .desktop path, for %k
};

struct Parsed {
    QStringList arguments; // still with the field codes
    bool ok = true; // false: unclosed quotes, or an empty line
    // Shell characters outside quotes (| & ; < > ( ) $ ` * ?): not allowed by
    // the spec. KIO goes through /bin/sh in that case, and some KDE service
    // menus rely on it (see servicemenus.cpp).
    bool shellSyntax = false;
};

inline Parsed split(const QString& exec)
{
    static const QString meta = QStringLiteral("|&;<>()$`*?");
    Parsed parsed;
    QString current;
    bool inArgument = false;
    bool quoted = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (quoted) {
            if (c == u'"') {
                quoted = false;
            } else if (c == u'\\' && i + 1 < exec.size() && QStringLiteral("\"`$\\").contains(exec.at(i + 1))) {
                current += exec.at(++i);
            } else {
                current += c;
            }
            continue;
        }
        if (c == u' ' || c == u'\t' || c == u'\n') {
            if (inArgument) {
                parsed.arguments.append(current);
                current.clear();
                inArgument = false;
            }
            continue;
        }
        inArgument = true;
        if (c == u'"') {
            quoted = true;
        } else if (c == u'\\' && i + 1 < exec.size()) {
            current += exec.at(++i); // outside quotes: the next character, as it is
        } else {
            if (meta.contains(c)) {
                parsed.shellSyntax = true;
            }
            current += c;
        }
    }
    if (inArgument) {
        parsed.arguments.append(current);
    }
    parsed.ok = !quoted && !parsed.arguments.isEmpty();
    return parsed;
}

// The arguments ask for files one at a time (%f, %u) rather than all together
// (%F, %U): with several files, one process per file (like KIO and GLib).
inline bool onePerFile(const QStringList& arguments)
{
    bool single = false;
    for (const QString& argument : arguments) {
        if (argument == QLatin1String("%F") || argument == QLatin1String("%U")) {
            return false;
        }
        single = single || argument.contains(QLatin1String("%f")) || argument.contains(QLatin1String("%u"));
    }
    return single;
}

inline bool takesFiles(const QStringList& arguments)
{
    for (const QString& argument : arguments) {
        for (const char* code : { "%f", "%F", "%u", "%U" }) {
            if (argument.contains(QLatin1String(code))) {
                return true;
            }
        }
    }
    return false;
}

// A file's local path (for %f/%F), or the address (for %u/%U).
inline QString asPath(const QUrl& url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

inline QString asUrl(const QUrl& url)
{
    return url.toString(QUrl::FullyEncoded);
}

// Expands field codes. A lone %F or %U becomes one argument per file; %f and
// %u take the first file (see onePerFile); %i becomes two arguments ("--icon",
// icon) or none. An argument left empty vanishes.
inline QStringList expand(const QStringList& arguments, const QList<QUrl>& files, const Context& context)
{
    QStringList out;
    for (const QString& argument : arguments) {
        if (argument == QLatin1String("%F") || argument == QLatin1String("%U")) {
            for (const QUrl& file : files) {
                out.append(argument == QLatin1String("%F") ? asPath(file) : asUrl(file));
            }
            continue;
        }
        if (argument == QLatin1String("%i")) {
            if (!context.icon.isEmpty()) {
                out << QStringLiteral("--icon") << context.icon;
            }
            continue;
        }
        if ((argument == QLatin1String("%f") || argument == QLatin1String("%u")) && files.isEmpty()) {
            continue;
        }
        QString expanded;
        bool hadCode = false;
        for (qsizetype i = 0; i < argument.size(); ++i) {
            const QChar c = argument.at(i);
            if (c != u'%' || i + 1 >= argument.size()) {
                expanded += c;
                continue;
            }
            hadCode = true;
            switch (argument.at(++i).unicode()) {
            case 'f': expanded += files.isEmpty() ? QString() : asPath(files.first()); break;
            case 'F': {
                QStringList paths;
                for (const QUrl& file : files) {
                    paths.append(asPath(file));
                }
                expanded += paths.join(u' ');
                break;
            }
            case 'u': expanded += files.isEmpty() ? QString() : asUrl(files.first()); break;
            case 'U': {
                QStringList urls;
                for (const QUrl& file : files) {
                    urls.append(asUrl(file));
                }
                expanded += urls.join(u' ');
                break;
            }
            case 'c': expanded += context.name; break;
            case 'k': expanded += context.desktopFile; break;
            case '%': expanded += u'%'; break;
            default: break; // %d %D %n %N %v %m: deprecated, they vanish
            }
        }
        // An argument made only of field codes left empty doesn't reach the
        // program as an empty string: it vanishes.
        if (!expanded.isEmpty() || !hadCode) {
            out.append(expanded);
        }
    }
    return out;
}

} // namespace DesktopExec
