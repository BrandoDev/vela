#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class AppModel;

// Le icone del desktop, come in Windows 11: i file della cartella Desktop
// (XDG_DESKTOP_DIR, es. ~/Scrivania) più il Cestino, in una griglia che si
// riempie per colonne dall'angolo in alto a sinistra. Le icone spostate a
// mano restano dove sono (salvate), finché non si riordina.
class DesktopModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString directory READ directory CONSTANT)
    // Visualizza: 0 icone grandi, 1 medie, 2 piccole.
    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY viewChanged)
    Q_PROPERTY(bool autoArrange READ autoArrange WRITE setAutoArrange NOTIFY viewChanged)
    Q_PROPERTY(bool alignToGrid READ alignToGrid WRITE setAlignToGrid NOTIFY viewChanged)
    Q_PROPERTY(bool showIcons READ showIcons WRITE setShowIcons NOTIFY viewChanged)
    // Ordina per: 0 nome, 1 dimensione, 2 tipo elemento, 3 data ultima modifica.
    Q_PROPERTY(int sortMode READ sortMode NOTIFY viewChanged)
    // Le misure della griglia (logiche), per la dimensione scelta.
    Q_PROPERTY(int iconPixels READ iconPixels NOTIFY viewChanged)
    Q_PROPERTY(int cellWidth READ cellWidth NOTIFY viewChanged)
    Q_PROPERTY(int cellHeight READ cellHeight NOTIFY viewChanged)
    // L'area in cui stanno le icone (lo schermo senza la taskbar).
    Q_PROPERTY(qreal areaWidth READ areaWidth WRITE setAreaWidth NOTIFY areaChanged)
    Q_PROPERTY(qreal areaHeight READ areaHeight WRITE setAreaHeight NOTIFY areaChanged)
    // "Annulla …": il nome dell'ultima azione annullabile (vuoto: nessuna).
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        UrlRole,
        IconRole,
        IsDirRole,
        IsAppRole, // un collegamento .desktop
        IsTrashRole,
        TypeRole,
        XRole, // posizione nella griglia (logica, dentro l'area)
        YRole,
    };

    explicit DesktopModel(AppModel* apps, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString directory() const { return m_dir; }
    int iconSize() const { return m_iconSize; }
    void setIconSize(int size);
    bool autoArrange() const { return m_autoArrange; }
    void setAutoArrange(bool on);
    bool alignToGrid() const { return m_alignToGrid; }
    void setAlignToGrid(bool on);
    bool showIcons() const { return m_showIcons; }
    void setShowIcons(bool on);
    int sortMode() const { return m_sortMode; }
    int iconPixels() const;
    int cellWidth() const;
    int cellHeight() const;
    qreal areaWidth() const { return m_areaWidth; }
    void setAreaWidth(qreal width);
    qreal areaHeight() const { return m_areaHeight; }
    void setAreaHeight(qreal height);
    QString undoText() const;
    bool canPaste() const;

    // Riordina (Ordina per): le icone tornano in fila in quell'ordine.
    Q_INVOKABLE void sortBy(int mode);
    Q_INVOKABLE void refresh();
    // Trascinata lì (coordinate logiche dell'area): si aggancia alla cella
    // libera più vicina se "Allinea icone alla griglia".
    Q_INVOKABLE void moveTo(const QString& path, qreal x, qreal y);

    Q_INVOKABLE void open(const QStringList& paths);
    Q_INVOKABLE bool rename(const QString& path, const QString& newName);
    Q_INVOKABLE void trash(const QStringList& paths);
    Q_INVOKABLE void emptyTrash();
    Q_INVOKABLE bool trashEmpty() const;
    Q_INVOKABLE void undo();
    // Nuovo: restituiscono il percorso, per rinominarlo subito.
    Q_INVOKABLE QString createFolder();
    Q_INVOKABLE QString createFile(const QString& templatePath); // vuoto: documento di testo
    // I modelli di documento (XDG_TEMPLATES_DIR): [{name, icon, path}].
    Q_INVOKABLE QVariantList templates() const;
    Q_INVOKABLE void copy(const QStringList& paths);
    Q_INVOKABLE void cut(const QStringList& paths);
    Q_INVOKABLE void paste();
    Q_INVOKABLE void copyAsPath(const QStringList& paths);
    // File lasciati qui da un'app (trascinati): sul desktop nel punto
    // (x, y), in una cartella, o nel Cestino ("trash:/"). Come Windows:
    // spostati se sono sullo stesso disco, altrimenti copiati.
    Q_INVOKABLE void drop(const QStringList& urls, const QString& target, qreal x, qreal y);
    // Comprimi in: "zip", "7z" o "tar".
    Q_INVOKABLE void compress(const QStringList& paths, const QString& format);

signals:
    void viewChanged();
    void areaChanged();
    void undoChanged();
    void clipboardChanged();
    // Un file appena creato (Nuovo, Incolla): la vista lo seleziona e, se
    // chiesto, lo fa rinominare.
    void created(const QString& path, bool rename);

private:
    struct Item {
        QString name;
        QString path;
        QString icon;
        QString type; // descrizione del tipo, per ordinare
        bool isDir = false;
        bool isApp = false;
        bool isTrash = false;
        qint64 size = 0;
        QDateTime modified;
        QPointF position; // scelta a mano (salvata); (-1, -1): nessuna
    };
    struct UndoStep {
        QString label; // "Rinomina", "Elimina", "Nuovo", "Incolla"
        QList<QPair<QString, QString>> moves; // da -> a, per tornare indietro
        QStringList created; // da togliere (nel cestino)
        QStringList trashed; // percorsi nel cestino, da rimettere al loro posto
        QStringList originals;
    };

    void reload();
    void layout(bool notify = true);
    void savePosition(const QString& name, const QPointF& position);
    QPointF cellPosition(int column, int row) const;
    int rows() const;
    QString uniqueName(const QString& name) const;
    static QString uniqueNameIn(const QString& dir, const QString& name);
    void pushUndo(UndoStep step);

    AppModel* m_apps;
    QString m_dir;
    QList<Item> m_items;
    QList<QPointF> m_layout; // dove si vede ogni icona
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    int m_iconSize = 1;
    int m_sortMode = 0;
    bool m_autoArrange = false;
    bool m_alignToGrid = true;
    bool m_showIcons = true;
    qreal m_areaWidth = 0;
    qreal m_areaHeight = 0;
    QList<UndoStep> m_undo;
    QString m_pendingSelect; // file appena creato, da annunciare quando compare
    bool m_pendingRename = false;
};
