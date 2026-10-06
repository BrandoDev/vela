// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

// La chiave Exec dei file .desktop, come dice la specifica freedesktop
// (Desktop Entry, "The Exec key"): non è una riga di comando della shell.
// Si divide in argomenti (spazi; virgolette doppie, dentro cui \" \` \$ e
// \\ valgono il carattere), si espandono i field code (%f %F %u %U %i %c %k
// %%; i deprecati spariscono) e si lancia il programma con i suoi argomenti,
// senza /bin/sh: niente $VAR, ;, >, *, `...` o $(...), come fa GLib.
//
// I valori stringa del file (\s, \n...) vanno già tolti prima (unescape in
// appmodel.cpp): qui arriva il valore di Exec così come lo intende la specifica.
namespace DesktopExec {

struct Context {
    QString icon; // Icon=, per %i
    QString name; // Name= tradotto, per %c
    QString desktopFile; // il percorso del .desktop, per %k
};

struct Parsed {
    QStringList arguments; // ancora con i field code
    bool ok = true; // false: virgolette non chiuse, o riga vuota
    // Caratteri della shell fuori dalle virgolette (| & ; < > ( ) $ ` * ?):
    // non ammessi dalla specifica. KIO in quel caso passa da /bin/sh, e
    // alcuni service menu di KDE ci contano (vedi servicemenus.cpp).
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
            current += exec.at(++i); // fuori dalle virgolette: il carattere dopo, così com'è
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

// Gli argomenti chiedono i file uno per volta (%f, %u) e non tutti insieme
// (%F, %U): con più file, un processo per file (come KIO e GLib).
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

// Il percorso locale di un file (per %f/%F), o l'indirizzo (per %u/%U).
inline QString asPath(const QUrl& url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

inline QString asUrl(const QUrl& url)
{
    return url.toString(QUrl::FullyEncoded);
}

// Espande i field code. Un %F o %U da solo diventa un argomento per file;
// %f e %u prendono il primo file (vedi onePerFile); %i diventa due argomenti
// ("--icon", icona) o nessuno. Un argomento che resta vuoto sparisce.
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
            default: break; // %d %D %n %N %v %m: deprecati, spariscono
            }
        }
        // Un argomento fatto solo di field code rimasti vuoti non passa al
        // programma come stringa vuota: sparisce.
        if (!expanded.isEmpty() || !hadCode) {
            out.append(expanded);
        }
    }
    return out;
}

} // namespace DesktopExec
