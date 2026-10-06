#pragma once

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QVariantList>

#include <atomic>

class QThread;

struct wl_event_queue;
struct wl_registry;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_offer_v1;
struct ext_data_control_source_v1;

// La cronologia degli appunti (Win+V), come Windows 11: ciò che si copia
// (testo e immagini) finisce in un elenco; un clic lo rimette negli appunti
// e lo incolla nell'app a fuoco. Gli elementi fissati restano anche dopo il
// riavvio (~/.local/share/vela/clipboard), gli altri solo per la sessione.
// Le password copiate dai gestori di password non si ricordano.
//
// Gli appunti si leggono e si scrivono col protocollo ext-data-control (la
// shell non ha una finestra a fuoco), su libwayland direttamente, sulla
// stessa connessione di Qt ma su una coda di eventi propria, servita da un
// thread: quando la shell stessa (Qt) legge gli appunti che abbiamo messo
// noi, il thread principale aspetta i dati e non potrebbe mandarli. Da qui
// passano anche le immagini dello Strumento di cattura.
class Clipboard : public QObject {
    Q_OBJECT
    // [{id, kind ("text"/"image"), text, pinned, time}], dal più recente
    // (prima i fissati, come Windows).
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    // "Cronologia degli Appunti" (Impostazioni > Sistema > Appunti).
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit Clipboard(QObject* parent = nullptr);
    ~Clipboard() override;

    QVariantList items() const;
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    // Rimette l'elemento negli appunti e chiede al compositor di incollarlo.
    Q_INVOKABLE void paste(int id);
    Q_INVOKABLE void remove(int id);
    Q_INVOKABLE void togglePin(int id);
    Q_INVOKABLE void clear(); // tutto tranne i fissati

    // Negli appunti (e nella cronologia, se accesa).
    void copyText(const QString& text);
    void copyImage(const QImage& image);

    QImage image(int id) const;

signals:
    void itemsChanged();
    void enabledChanged();

private:
    struct Item {
        int id = 0;
        QString text;
        QImage image;
        QByteArray png; // l'immagine com'è arrivata (o codificata), per rimetterla
        bool pinned = false;
        QDateTime time;
    };
    struct Offer;
    friend struct ClipboardCallbacks;

    void add(Item item);
    void setSelection(const Item& item);
    void readOffer(Offer* offer); // nel thread degli appunti
    void savePinned() const;
    void loadPinned();
    void flush();
    void run(); // il thread: serve la coda degli appunti

    wl_event_queue* m_queue = nullptr;
    QThread* m_thread = nullptr;
    std::atomic<bool> m_stop { false };
    int m_wake[2] { -1, -1 }; // per svegliare il thread quando si chiude
    QMutex m_mutex; // m_sourceText e m_sourcePng, letti dal thread
    wl_registry* m_registry = nullptr;
    ext_data_control_manager_v1* m_manager = nullptr;
    ext_data_control_device_v1* m_device = nullptr;
    QByteArray m_sourceText;
    QByteArray m_sourcePng;
    QHash<ext_data_control_offer_v1*, Offer*> m_offers; // solo nel thread
    std::atomic<bool> m_ignoreNext { false }; // la selezione che abbiamo messo noi
    QList<Item> m_items;
    int m_nextId = 1;
    std::atomic<bool> m_enabled { false };
};

// Le immagini della cronologia per il QML: "image://clipboard/<id>".
class ClipboardImageProvider : public QQuickImageProvider {
public:
    explicit ClipboardImageProvider(Clipboard* clipboard)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_clipboard(clipboard)
    {
    }
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override
    {
        QImage image = m_clipboard->image(id.section(u'/', 0, 0).toInt());
        if (requested.isValid() && !image.isNull()) {
            image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (size) {
            *size = image.size();
        }
        return image;
    }

private:
    Clipboard* m_clipboard;
};
