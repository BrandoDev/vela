// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>

#include <atomic>
#include <memory>

class AppModel;

// Ciò che Esplora fa con i file, come Windows: aprire, appunti (taglia,
// copia, incolla, anche verso Dolphin e Nautilus), Nuovo, rinominare,
// Cestino ed eliminazione, comprimere ed estrarre, e Annulla (Ctrl+Z).
// Copie e spostamenti lunghi girano in un altro thread con il loro
// avanzamento (il riquadro "N elementi in copia" di Windows); se nella
// destinazione ci sono già file con lo stesso nome si chiede prima cosa fare.
class FileOps : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    // [{id, title, done, total, current, finished, error}]
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)

public:
    explicit FileOps(AppModel* apps, QObject* parent = nullptr);
    ~FileOps() override;

    bool canPaste() const;
    QSet<QString> cutPaths() const;
    QString undoText() const;
    QVariantList jobs() const;

    // Aprire: le cartelle le apre Esplora stesso; i file con l'app predefinita.
    Q_INVOKABLE void open(const QStringList& paths);
    Q_INVOKABLE void openWith(const QString& appId, const QString& path);
    Q_INVOKABLE QVariantList appsFor(const QString& path) const;

    Q_INVOKABLE void copy(const QStringList& paths);
    Q_INVOKABLE void cut(const QStringList& paths);
    Q_INVOKABLE void paste(const QString& directory);
    Q_INVOKABLE void copyAsPath(const QStringList& paths);
    // File lasciati qui (trascinati): `action` 1 copia, 2 sposta, 0 come
    // Windows (spostati se sullo stesso disco, altrimenti copiati).
    Q_INVOKABLE void drop(const QStringList& urls, const QString& directory, int action);

    // Nuovo: restituiscono il percorso, per rinominarlo subito.
    Q_INVOKABLE QString createFolder(const QString& directory);
    Q_INVOKABLE QString createFile(const QString& directory, const QString& templatePath);
    Q_INVOKABLE QVariantList templates() const; // [{name, icon, path}]
    // Il nuovo nome, o un messaggio d'errore che comincia con "!".
    Q_INVOKABLE QString rename(const QString& path, const QString& newName);

    Q_INVOKABLE void trash(const QStringList& paths);
    Q_INVOKABLE void deletePermanently(const QStringList& paths);
    Q_INVOKABLE void emptyTrash();
    Q_INVOKABLE void restoreFromTrash(const QStringList& paths); // file in …/Trash/files
    Q_INVOKABLE QString originalLocation(const QString& trashedPath) const;

    Q_INVOKABLE void compress(const QStringList& paths, const QString& format);
    Q_INVOKABLE bool isArchive(const QString& path) const;
    Q_INVOKABLE void extractAll(const QString& path);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void cancelJob(int id);
    Q_INVOKABLE void dismissJob(int id);

    // Conflitti: "replace", "skip", "keep" (mantieni entrambi) o "cancel".
    Q_INVOKABLE void resolveConflict(const QString& choice);
    Q_INVOKABLE QString formatSize(double bytes) const;
    // Le sottocartelle (per la freccia tra i pezzi del percorso), ordinate.
    Q_INVOKABLE QStringList subfolders(const QString& path, bool hidden) const;
    // "~/Documenti" e simili: il percorso vero, "" se non è una cartella.
    Q_INVOKABLE QString resolve(const QString& text, const QString& base) const;
    Q_INVOKABLE void newWindow(const QString& location) const;
    // La finestra Proprietà è quella della shell (la stessa del desktop).
    Q_INVOKABLE void showProperties(const QStringList& paths);
    // Anche Condividi, "Scegli un'altra app" e Nuovo > Collegamento.
    Q_INVOKABLE void share(const QStringList& paths);
    Q_INVOKABLE void chooseApp(const QString& path);
    Q_INVOKABLE void newShortcut(const QString& directory);

    // Collegamenti (links.h): "Crea collegamento" li mette accanto agli
    // originali (o sul desktop, se lì non si può scrivere); "Incolla
    // collegamento" crea qui quelli dei file negli appunti.
    Q_INVOKABLE void createLinks(const QStringList& paths);
    Q_INVOKABLE void pasteLinks(const QString& directory);
    Q_INVOKABLE bool isLink(const QString& path) const;
    Q_INVOKABLE QString linkTarget(const QString& path) const;

    // Il riquadro dei dettagli: {name, type, size, sizeText, modified,
    // created, isDir, items, mime, width, height, location}.
    Q_INVOKABLE QVariantMap details(const QString& path) const;
    // Il riquadro di anteprima: l'inizio di un file di testo (vuoto se non
    // lo è), e se è un'immagine che Qt sa leggere.
    Q_INVOKABLE QString previewText(const QString& path) const;
    Q_INVOKABLE bool isImage(const QString& path) const;

signals:
    void clipboardChanged();
    void undoChanged();
    void jobsChanged();
    // Nella destinazione c'è già qualcosa con lo stesso nome: la vista chiede.
    void conflict(int count, const QString& firstName, const QString& directory);
    // Un file appena creato o arrivato: la vista lo seleziona (e lo fa rinominare).
    void created(const QString& path, bool rename);
    void failed(const QString& message);

private:
    struct UndoStep {
        QString label;
        QList<QPair<QString, QString>> moves; // (dove è ora, dove era)
        QStringList created;
        QStringList trashed; // dove sono nel Cestino
        QStringList originals; // e da dove venivano
    };
    struct Job;
    struct Transfer {
        QStringList sources;
        QString directory;
        bool move = false;
    };

    void pushUndo(UndoStep step);
    void startTransfer(const Transfer& transfer, const QString& policy);
    void requestTransfer(const Transfer& transfer);
    void updateJob(int id, qint64 done, qint64 total, const QString& current, bool finished, const QString& error);

    AppModel* m_apps;
    QList<UndoStep> m_undo;
    QList<std::shared_ptr<Job>> m_jobs;
    int m_nextJob = 1;
    Transfer m_pending; // in attesa della risposta al conflitto
};

// Un nome libero nella cartella: "nome (2).ext", come Windows.
QString uniqueNameIn(const QString& directory, const QString& name);
