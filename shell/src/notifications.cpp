#include "notifications.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QUrl>
#include <QtDebug>

namespace {

constexpr int defaultTimeoutMs = 6000; // come Windows (circa sei secondi)
constexpr int maxVisible = 4; // le più vecchie scadono prima del tempo

// "image-data" (e i vecchi "image_data" e "icon_data"): (iiibiiay).
QImage decodeImage(const QVariant& value)
{
    if (!value.canConvert<QDBusArgument>()) {
        return {};
    }
    const QDBusArgument argument = value.value<QDBusArgument>();
    int width = 0;
    int height = 0;
    int rowStride = 0;
    bool hasAlpha = false;
    int bitsPerSample = 0;
    int channels = 0;
    QByteArray pixels;
    argument.beginStructure();
    argument >> width >> height >> rowStride >> hasAlpha >> bitsPerSample >> channels >> pixels;
    argument.endStructure();
    if (width <= 0 || height <= 0 || bitsPerSample != 8 || (channels != 3 && channels != 4)
        || pixels.size() < qsizetype(rowStride) * (height - 1) + qsizetype(width) * channels) {
        return {};
    }
    const QImage::Format format = channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    return QImage(reinterpret_cast<const uchar*>(pixels.constData()), width, height, rowStride, format).copy();
}

// Un'icona per Image: file (percorso o file://) o nome del tema.
QString iconSource(const QString& icon)
{
    if (icon.isEmpty()) {
        return {};
    }
    if (icon.startsWith(QLatin1String("file://"))) {
        return icon;
    }
    if (icon.startsWith(QLatin1Char('/'))) {
        return QUrl::fromLocalFile(icon).toString();
    }
    return QStringLiteral("image://icon/") + QString::fromUtf8(QUrl::toPercentEncoding(icon));
}

} // namespace

NotificationServer::NotificationServer(QObject* parent)
    : QAbstractListModel(parent)
{
}

NotificationServer::~NotificationServer() = default;

bool NotificationServer::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), this,
            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        return false;
    }
    if (!bus.registerService(QStringLiteral("org.freedesktop.Notifications"))) {
        qInfo("vela-shell: c'è già un server delle notifiche (%s), le notifiche vanno a lui",
            qPrintable(bus.interface()->serviceOwner(QStringLiteral("org.freedesktop.Notifications")).value()));
        bus.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));
        return false;
    }
    return true;
}

// -------------------------------------------------------------- modello --

int NotificationServer::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant NotificationServer::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_items.size())) {
        return {};
    }
    const Notification& n = m_items[size_t(index.row())];
    switch (role) {
    case IdRole: return n.id;
    case AppNameRole: return n.appName;
    case IconRole: return n.icon;
    case SummaryRole: return n.summary;
    case BodyRole: return n.body;
    case ActionsRole: return n.actions;
    case HasDefaultActionRole: return n.hasDefault;
    case CriticalRole: return n.critical;
    }
    return {};
}

QHash<int, QByteArray> NotificationServer::roleNames() const
{
    return {
        { IdRole, "notificationId" },
        { AppNameRole, "appName" },
        { IconRole, "icon" },
        { SummaryRole, "summary" },
        { BodyRole, "body" },
        { ActionsRole, "actions" },
        { HasDefaultActionRole, "hasDefaultAction" },
        { CriticalRole, "critical" },
    };
}

int NotificationServer::rowOf(uint id) const
{
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            return int(i);
        }
    }
    return -1;
}

// ------------------------------------------------------------------ QML --

void NotificationServer::invoke(uint id, const QString& action)
{
    if (rowOf(id) < 0) {
        return;
    }
    Q_EMIT ActionInvoked(id, action);
    close(id, Reason::Dismissed);
}

void NotificationServer::dismiss(uint id)
{
    close(id, Reason::Dismissed);
}

void NotificationServer::setHovered(bool hovered)
{
    // Mentre le si legge non scadono; uscito il mouse ripartono da capo.
    m_hovered = hovered;
    for (Notification& n : m_items) {
        if (n.timer) {
            if (hovered) {
                n.timer->stop();
            } else {
                n.timer->start();
            }
        }
    }
}

// ---------------------------------------------------------------- D-Bus --

QStringList NotificationServer::GetCapabilities()
{
    return { QStringLiteral("body"), QStringLiteral("body-markup"), QStringLiteral("body-hyperlinks"),
        QStringLiteral("actions"), QStringLiteral("icon-static") };
}

QString NotificationServer::GetServerInformation(QString& vendor, QString& version, QString& spec_version)
{
    vendor = QStringLiteral("Vela");
    version = QStringLiteral("0.1");
    spec_version = QStringLiteral("1.2");
    return QStringLiteral("Vela");
}

uint NotificationServer::Notify(const QString& app_name, uint replaces_id, const QString& app_icon,
    const QString& summary, const QString& body, const QStringList& actions, const QVariantMap& hints,
    int expire_timeout)
{
    const int existing = replaces_id ? rowOf(replaces_id) : -1;
    const uint id = existing >= 0 ? replaces_id : m_nextId++;

    Notification n;
    n.id = id;
    n.appName = app_name;
    n.icon = iconFor(id, app_icon, hints);
    n.summary = summary;
    n.body = body;
    n.hasDefault = false;
    for (qsizetype i = 0; i + 1 < actions.size(); i += 2) {
        if (actions[i] == QLatin1String("default")) {
            n.hasDefault = true;
        } else {
            n.actions.append(QVariantMap { { QStringLiteral("key"), actions[i] }, { QStringLiteral("label"), actions[i + 1] } });
        }
    }
    n.critical = hints.value(QStringLiteral("urgency")).toInt() == 2;

    // Scadenza: quella chiesta, o la nostra; 0 e le critiche restano.
    const int timeout = expire_timeout < 0 ? defaultTimeoutMs : expire_timeout;
    if (timeout > 0 && !n.critical) {
        n.timer = std::make_unique<QTimer>();
        n.timer->setSingleShot(true);
        n.timer->setInterval(timeout);
        connect(n.timer.get(), &QTimer::timeout, this, [this, id] { close(id, Reason::Expired); });
        if (!m_hovered) {
            n.timer->start();
        }
    }

    if (existing >= 0) {
        m_items[size_t(existing)] = std::move(n);
        Q_EMIT dataChanged(index(existing), index(existing));
    } else {
        beginInsertRows({}, int(m_items.size()), int(m_items.size()));
        m_items.push_back(std::move(n));
        endInsertRows();
        Q_EMIT countChanged();
        while (int(m_items.size()) > maxVisible) {
            close(m_items.front().id, Reason::Expired);
        }
    }
    return id;
}

void NotificationServer::CloseNotification(uint id)
{
    close(id, Reason::Closed);
}

void NotificationServer::close(uint id, Reason reason)
{
    const int row = rowOf(id);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_items.erase(m_items.begin() + row);
    endRemoveRows();
    Q_EMIT countChanged();
    m_images.remove(id);
    Q_EMIT NotificationClosed(id, uint(reason));
}

QString NotificationServer::iconFor(uint id, const QString& appIcon, const QVariantMap& hints)
{
    // In ordine di preferenza (specifica delle notifiche, 1.2).
    for (const char* key : { "image-data", "image_data", "icon_data" }) {
        const QImage image = decodeImage(hints.value(QLatin1String(key)));
        if (!image.isNull()) {
            m_images.insert(id, image);
            static uint serial = 0;
            return QStringLiteral("image://notification/%1/%2").arg(id).arg(++serial); // nuova a ogni sostituzione
        }
    }
    for (const char* key : { "image-path", "image_path" }) {
        const QString path = hints.value(QLatin1String(key)).toString();
        if (!path.isEmpty()) {
            return iconSource(path);
        }
    }
    if (!appIcon.isEmpty()) {
        return iconSource(appIcon);
    }
    const QString entry = hints.value(QStringLiteral("desktop-entry")).toString();
    return iconSource(entry.isEmpty() ? QStringLiteral("dialog-information") : entry);
}

// --------------------------------------------------------------- immagini --

QImage NotificationImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QImage image = m_server->image(id.section(QLatin1Char('/'), 0, 0).toUInt());
    if (size) {
        *size = image.size();
    }
    if (image.isNull() || !requestedSize.isValid()) {
        return image;
    }
    return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
