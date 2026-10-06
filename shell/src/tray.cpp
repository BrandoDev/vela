// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tray.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QUrl>
#include <QtDebug>
#include <QtEndian>

#include <unistd.h>

namespace {

const QString watcherService = QStringLiteral("org.kde.StatusNotifierWatcher");
const QString watcherPath = QStringLiteral("/StatusNotifierWatcher");
const QString itemInterface = QStringLiteral("org.kde.StatusNotifierItem");
const QString menuInterface = QStringLiteral("com.canonical.dbusmenu");

// IconPixmap e simili: a(iiay), ARGB32 in ordine di rete. Si prende la
// più grande fino a 64 pixel (la tray ne disegna 16-24, anche a scala 2).
QImage decodePixmaps(const QVariant& value)
{
    if (!value.canConvert<QDBusArgument>()) {
        return {};
    }
    const QDBusArgument argument = value.value<QDBusArgument>();
    QImage best;
    argument.beginArray();
    while (!argument.atEnd()) {
        int width = 0;
        int height = 0;
        QByteArray bytes;
        argument.beginStructure();
        argument >> width >> height >> bytes;
        argument.endStructure();
        if (width <= 0 || height <= 0 || bytes.size() < qsizetype(width) * height * 4) {
            continue;
        }
        const bool better = best.isNull() || (width > best.width() && best.width() < 64)
            || (width <= 64 && best.width() > 64);
        if (!better) {
            continue;
        }
        QImage image(width, height, QImage::Format_ARGB32);
        const auto* source = reinterpret_cast<const quint32*>(bytes.constData());
        for (int y = 0; y < height; ++y) {
            auto* line = reinterpret_cast<quint32*>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                line[x] = qFromBigEndian(source[y * width + x]);
            }
        }
        best = image;
    }
    argument.endArray();
    return best;
}

// Il titolo del ToolTip: (sa(iiay)ss).
QString tooltipTitle(const QVariant& value)
{
    if (!value.canConvert<QDBusArgument>()) {
        return {};
    }
    const QDBusArgument argument = value.value<QDBusArgument>();
    QString iconName;
    QString title;
    QString description;
    argument.beginStructure();
    argument >> iconName;
    argument.beginArray();
    while (!argument.atEnd()) {
        int width = 0;
        int height = 0;
        QByteArray bytes;
        argument.beginStructure();
        argument >> width >> height >> bytes;
        argument.endStructure();
    }
    argument.endArray();
    argument >> title >> description;
    argument.endStructure();
    return title;
}

// Un'icona per nome, cercata prima nella cartella che l'app indica
// (IconThemePath), poi nel tema.
QString iconSource(const QString& name, const QString& themePath)
{
    if (name.isEmpty()) {
        return {};
    }
    if (name.startsWith(QLatin1Char('/'))) {
        return QUrl::fromLocalFile(name).toString();
    }
    if (!themePath.isEmpty()) {
        for (const char* extension : { ".svg", ".png" }) {
            const QString file = themePath + QLatin1Char('/') + name + QLatin1String(extension);
            if (QFileInfo::exists(file)) {
                return QUrl::fromLocalFile(file).toString();
            }
        }
        QDirIterator it(themePath, { name + QStringLiteral(".svg"), name + QStringLiteral(".png") }, QDir::Files,
            QDirIterator::Subdirectories);
        if (it.hasNext()) {
            return QUrl::fromLocalFile(it.next()).toString();
        }
    }
    return QStringLiteral("image://icon/") + QString::fromUtf8(QUrl::toPercentEncoding(name));
}

// Le etichette dei menu hanno le "mnemoniche" con il trattino basso.
QString menuLabel(QString label)
{
    label.replace(QLatin1String("__"), QStringLiteral("\x01"));
    label.remove(QLatin1Char('_'));
    label.replace(QStringLiteral("\x01"), QStringLiteral("_"));
    return label;
}

// Una voce di com.canonical.dbusmenu: (ia{sv}av), ricorsiva.
QVariantMap parseMenuEntry(const QDBusArgument& argument)
{
    int id = 0;
    QVariantMap properties;
    QVariantList children;
    argument.beginStructure();
    argument >> id >> properties;
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant child;
        argument >> child;
        const QVariantMap entry = parseMenuEntry(child.variant().value<QDBusArgument>());
        if (!entry.isEmpty()) {
            children.append(entry);
        }
    }
    argument.endArray();
    argument.endStructure();

    if (!properties.value(QStringLiteral("visible"), true).toBool()) {
        return {};
    }
    const QString toggle = properties.value(QStringLiteral("toggle-type")).toString();
    return {
        { QStringLiteral("id"), id },
        { QStringLiteral("label"), menuLabel(properties.value(QStringLiteral("label")).toString()) },
        { QStringLiteral("enabled"), properties.value(QStringLiteral("enabled"), true).toBool() },
        { QStringLiteral("separator"), properties.value(QStringLiteral("type")).toString() == QLatin1String("separator") },
        { QStringLiteral("checkable"), !toggle.isEmpty() },
        { QStringLiteral("radio"), toggle == QLatin1String("radio") },
        { QStringLiteral("checked"), properties.value(QStringLiteral("toggle-state")).toInt() == 1 },
        { QStringLiteral("icon"), iconSource(properties.value(QStringLiteral("icon-name")).toString(), {}) },
        { QStringLiteral("children"), children },
    };
}

// Riceve i segnali D-Bus di un'icona (nuova icona, nuovo stato...).
class ItemSignals : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
public Q_SLOTS:
    void changed() { Q_EMIT refresh(); }
    void statusChanged(const QString&) { Q_EMIT refresh(); }
Q_SIGNALS:
    void refresh();
};

} // namespace

// ------------------------------------------------------------- registro --

StatusNotifierWatcher::StatusNotifierWatcher(QObject* parent)
    : QObject(parent)
    , m_owners(new QDBusServiceWatcher(this))
{
    m_owners->setConnection(QDBusConnection::sessionBus());
    m_owners->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_owners, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString& service) {
        m_owners->removeWatchedService(service);
        for (qsizetype i = m_items.size() - 1; i >= 0; --i) {
            if (m_items[i].section(QLatin1Char('/'), 0, 0) == service) {
                const QString address = m_items.takeAt(i);
                Q_EMIT StatusNotifierItemUnregistered(address);
            }
        }
    });
}

bool StatusNotifierWatcher::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(watcherPath, this,
            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals
                | QDBusConnection::ExportAllProperties)) {
        return false;
    }
    if (!bus.registerService(watcherService)) {
        bus.unregisterObject(watcherPath);
        return false;
    }
    return true;
}

void StatusNotifierWatcher::RegisterStatusNotifierItem(const QString& service)
{
    // L'app può dare il suo nome sul bus o solo il percorso dell'oggetto
    // (libappindicator): l'indirizzo è sempre "servizio/percorso".
    QString address;
    if (service.startsWith(QLatin1Char('/'))) {
        address = message().service() + service;
    } else if (service.contains(QLatin1Char('/'))) {
        address = service;
    } else {
        address = service + QStringLiteral("/StatusNotifierItem");
    }
    if (m_items.contains(address)) {
        return;
    }
    m_items.append(address);
    m_owners->addWatchedService(address.section(QLatin1Char('/'), 0, 0));
    Q_EMIT StatusNotifierItemRegistered(address);
}

void StatusNotifierWatcher::RegisterStatusNotifierHost(const QString&)
{
    Q_EMIT StatusNotifierHostRegistered();
}

// ---------------------------------------------------------------- icone --

struct TrayModel::Item {
    QString address;
    QString service;
    QString path;
    QString title;
    QString status;
    QString icon; // sorgente per Image
    QString menuPath;
    bool itemIsMenu = false;
    bool loaded = false;
    std::unique_ptr<ItemSignals> receiver;
};

TrayModel::TrayModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

TrayModel::~TrayModel() = default;

void TrayModel::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return;
    }
    m_watcher = new StatusNotifierWatcher(this);
    if (m_watcher->registerService()) {
        connect(m_watcher, &StatusNotifierWatcher::StatusNotifierItemRegistered, this, &TrayModel::addItem);
        connect(m_watcher, &StatusNotifierWatcher::StatusNotifierItemUnregistered, this, &TrayModel::removeItem);
    } else {
        // Il registro c'è già (Plasma): ci si iscrive come host.
        delete m_watcher;
        m_watcher = nullptr;
        bus.connect(watcherService, watcherPath, watcherService, QStringLiteral("StatusNotifierItemRegistered"), this,
            SLOT(onItemRegistered(QString)));
        bus.connect(watcherService, watcherPath, watcherService, QStringLiteral("StatusNotifierItemUnregistered"),
            this, SLOT(onItemUnregistered(QString)));
        QDBusMessage get = QDBusMessage::createMethodCall(watcherService, watcherPath,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        get << watcherService << QStringLiteral("RegisteredStatusNotifierItems");
        const QDBusMessage reply = bus.call(get);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            for (const QString& address : reply.arguments().first().value<QDBusVariant>().variant().toStringList()) {
                addItem(address);
            }
        }
    }
    const QString host = QStringLiteral("org.kde.StatusNotifierHost-%1").arg(getpid());
    bus.registerService(host);
    QDBusMessage hello = QDBusMessage::createMethodCall(watcherService, watcherPath, watcherService,
        QStringLiteral("RegisterStatusNotifierHost"));
    hello << host;
    bus.asyncCall(hello);
}

void TrayModel::addItem(const QString& address)
{
    for (const auto& item : m_items) {
        if (item->address == address) {
            return;
        }
    }
    auto item = std::make_unique<Item>();
    item->address = address;
    const qsizetype slash = address.indexOf(QLatin1Char('/'));
    item->service = slash < 0 ? address : address.left(slash);
    item->path = slash < 0 ? QStringLiteral("/StatusNotifierItem") : address.mid(slash);
    item->receiver = std::make_unique<ItemSignals>();
    Item* raw = item.get();
    connect(item->receiver.get(), &ItemSignals::refresh, this, [this, raw] { refresh(raw); });
    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const char* name : { "NewIcon", "NewAttentionIcon", "NewOverlayIcon", "NewTitle", "NewToolTip", "NewMenu" }) {
        bus.connect(item->service, item->path, itemInterface, QLatin1String(name), item->receiver.get(),
            SLOT(changed()));
    }
    bus.connect(item->service, item->path, itemInterface, QStringLiteral("NewStatus"), item->receiver.get(),
        SLOT(statusChanged(QString)));

    beginInsertRows({}, int(m_items.size()), int(m_items.size()));
    m_items.push_back(std::move(item));
    endInsertRows();
    refresh(raw);
}

void TrayModel::removeItem(const QString& address)
{
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i]->address == address) {
            beginRemoveRows({}, int(i), int(i));
            m_items.erase(m_items.begin() + qsizetype(i));
            endRemoveRows();
            return;
        }
    }
}

void TrayModel::refresh(Item* item)
{
    QDBusMessage get = QDBusMessage::createMethodCall(item->service, item->path,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    get << itemInterface;
    auto* call = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(get), this);
    const QString address = item->address;
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, address](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *call;
        Item* item = nullptr;
        for (const auto& candidate : m_items) {
            if (candidate->address == address) {
                item = candidate.get();
            }
        }
        if (!item || reply.isError()) {
            return;
        }
        const QVariantMap properties = reply.value();
        item->status = properties.value(QStringLiteral("Status")).toString();
        item->title = tooltipTitle(properties.value(QStringLiteral("ToolTip")));
        if (item->title.isEmpty()) {
            item->title = properties.value(QStringLiteral("Title")).toString();
        }
        item->itemIsMenu = properties.value(QStringLiteral("ItemIsMenu")).toBool();
        item->menuPath = properties.value(QStringLiteral("Menu")).value<QDBusObjectPath>().path();

        // L'icona: quella di "attenzione" se l'app la chiede; il nome nel
        // tema, altrimenti l'immagine che manda l'app.
        const bool attention = item->status == QLatin1String("NeedsAttention");
        const QString themePath = properties.value(QStringLiteral("IconThemePath")).toString();
        QString name = properties.value(QLatin1String(attention ? "AttentionIconName" : "IconName")).toString();
        QImage image = decodePixmaps(properties.value(QLatin1String(attention ? "AttentionIconPixmap" : "IconPixmap")));
        if (attention && name.isEmpty() && image.isNull()) {
            name = properties.value(QStringLiteral("IconName")).toString();
            image = decodePixmaps(properties.value(QStringLiteral("IconPixmap")));
        }
        if (!name.isEmpty()) {
            item->icon = iconSource(name, themePath);
        } else if (!image.isNull()) {
            const QString key = QString::number(++m_serial);
            m_pixmaps.insert(key, image);
            item->icon = QStringLiteral("image://tray/") + key;
        }
        item->loaded = true;
        const int row = rowOf(item);
        Q_EMIT dataChanged(index(row), index(row));
    });
}

int TrayModel::rowOf(const Item* item) const
{
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].get() == item) {
            return int(i);
        }
    }
    return -1;
}

TrayModel::Item* TrayModel::itemAt(int row) const
{
    return row >= 0 && row < int(m_items.size()) ? m_items[size_t(row)].get() : nullptr;
}

int TrayModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant TrayModel::data(const QModelIndex& index, int role) const
{
    const Item* item = itemAt(index.row());
    if (!item) {
        return {};
    }
    switch (role) {
    case IconRole:
        // Nascoste finché non si sa com'è l'icona, e quelle "passive" (le
        // app le usano per dire "adesso non serve mostrarmi").
        return item->loaded && item->status != QLatin1String("Passive") ? item->icon : QString();
    case TitleRole: return item->title;
    case AttentionRole: return item->status == QLatin1String("NeedsAttention");
    }
    return {};
}

QHash<int, QByteArray> TrayModel::roleNames() const
{
    return { { IconRole, "icon" }, { TitleRole, "title" }, { AttentionRole, "attention" } };
}

// ------------------------------------------------------------- comandi --

void TrayModel::activate(int row, int anchorX)
{
    Item* item = itemAt(row);
    if (!item) {
        return;
    }
    if (item->itemIsMenu) {
        requestMenu(row, anchorX);
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->path, itemInterface,
        QStringLiteral("Activate"));
    call << 0 << 0;
    auto* pending = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    const QString address = item->address;
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, address, anchorX](QDBusPendingCallWatcher* p) {
        p->deleteLater();
        // Molte app (libappindicator, Electron) non sanno fare Activate:
        // al clic mostrano il menu.
        if (p->isError()) {
            for (size_t i = 0; i < m_items.size(); ++i) {
                if (m_items[i]->address == address) {
                    requestMenu(int(i), anchorX);
                }
            }
        }
    });
}

void TrayModel::secondaryActivate(int row)
{
    if (Item* item = itemAt(row)) {
        QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->path, itemInterface,
            QStringLiteral("SecondaryActivate"));
        call << 0 << 0;
        QDBusConnection::sessionBus().asyncCall(call);
    }
}

void TrayModel::scroll(int row, int delta)
{
    if (Item* item = itemAt(row)) {
        QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->path, itemInterface,
            QStringLiteral("Scroll"));
        call << delta << QStringLiteral("vertical");
        QDBusConnection::sessionBus().asyncCall(call);
    }
}

void TrayModel::requestMenu(int row, int anchorX)
{
    Item* item = itemAt(row);
    if (!item || item->menuPath.isEmpty() || item->menuPath == QLatin1String("/")) {
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Alcune app riempiono il menu solo quando sta per aprirsi.
    QDBusMessage about = QDBusMessage::createMethodCall(item->service, item->menuPath, menuInterface,
        QStringLiteral("AboutToShow"));
    about << 0;
    bus.call(about, QDBus::Block, 200);

    QDBusMessage layout = QDBusMessage::createMethodCall(item->service, item->menuPath, menuInterface,
        QStringLiteral("GetLayout"));
    layout << 0 << -1 << QStringList();
    auto* pending = new QDBusPendingCallWatcher(bus.asyncCall(layout), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, row, anchorX](QDBusPendingCallWatcher* p) {
        p->deleteLater();
        const QDBusMessage reply = p->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
            return;
        }
        const QVariantMap root = parseMenuEntry(reply.arguments().at(1).value<QDBusArgument>());
        const QVariantList entries = root.value(QStringLiteral("children")).toList();
        if (!entries.isEmpty()) {
            Q_EMIT menuReady(row, anchorX, entries);
        }
    });
}

void TrayModel::activateMenuEntry(int row, int entryId)
{
    Item* item = itemAt(row);
    if (!item || item->menuPath.isEmpty()) {
        return;
    }
    QDBusMessage event = QDBusMessage::createMethodCall(item->service, item->menuPath, menuInterface,
        QStringLiteral("Event"));
    event << entryId << QStringLiteral("clicked") << QVariant::fromValue(QDBusVariant(QVariant(0)))
          << uint(QDateTime::currentSecsSinceEpoch());
    QDBusConnection::sessionBus().asyncCall(event);
}

// -------------------------------------------------------------- immagini --

QImage TrayImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QImage image = m_tray->pixmap(id);
    if (size) {
        *size = image.size();
    }
    if (image.isNull() || !requestedSize.isValid()) {
        return image;
    }
    return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

#include "tray.moc"
