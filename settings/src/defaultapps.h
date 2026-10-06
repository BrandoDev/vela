// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

class AppModel;

// App > App predefinite, come Windows 11:
// - gli usi comuni (browser, posta, musica, video, foto, PDF, archivi,
//   documenti...): l'app che li apre, tra quelle che lo sanno fare;
// - un tipo di file o di collegamento preciso (".mkv", "mp3", "mailto");
// - per app: i tipi che sa aprire, e "Imposta come predefinita" per tutti.
// Le scelte vanno in ~/.config/mimeapps.list (mimeapps.h), che leggono KDE,
// GNOME, i browser e xdg-open.
class DefaultApps : public QObject {
    Q_OBJECT
    // [{key, label, icon, current: {id, name, icon} o vuoto, candidates: [...]}]
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    // L'app aperta nella sottopagina (per app).
    Q_PROPERTY(QString selectedApp READ selectedApp WRITE setSelectedApp NOTIFY selectedAppChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectedAppChanged)
    Q_PROPERTY(QString selectedIcon READ selectedIcon NOTIFY selectedAppChanged)

public:
    explicit DefaultApps(AppModel* apps, QObject* parent = nullptr);

    QVariantList categories() const;
    Q_INVOKABLE void setDefault(const QString& key, const QString& appId);

    // Un tipo dal testo scritto: estensione (".pdf", "pdf"), tipo MIME
    // ("audio/mpeg") o protocollo ("mailto", "https"). {found, mime, label,
    // patterns, icon, current, candidates}.
    Q_INVOKABLE QVariantMap lookup(const QString& text) const;
    Q_INVOKABLE void setDefaultForType(const QString& mime, const QString& appId);

    // Le app che aprono qualche tipo: [{id, name, icon, count}], filtrate.
    Q_INVOKABLE QVariantList apps(const QString& filter) const;
    // I tipi che l'app sa aprire: [{mime, label, patterns, icon, isDefault, current}].
    Q_INVOKABLE QVariantList typesOf(const QString& appId) const;
    // L'app diventa la predefinita per tutti i tipi che dichiara.
    Q_INVOKABLE void setDefaultForAll(const QString& appId);

    QString selectedApp() const { return m_selected; }
    void setSelectedApp(const QString& appId);
    QString selectedName() const;
    QString selectedIcon() const;

signals:
    void changed();
    void selectedAppChanged();

private:
    QVariantList candidatesFor(const QStringList& mimes, const QString& current) const;
    QVariantMap appInfo(const QString& appId) const;

    AppModel* m_apps;
    QString m_selected;
};
