#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQuickImageProvider>
#include <QVariantList>

class Clipboard;
class NotificationServer;
class QQmlEngine;
class QQuickWindow;
class WindowCapture;

// Lo Strumento di cattura (Win+Maiusc+S, Stamp), come Windows 11: lo
// schermo si ferma (una fotografia di ogni schermo) e si sceglie cosa
// tenere: un rettangolo, una finestra o uno schermo intero. Il ritaglio va
// negli appunti (anche nella cronologia) e in Immagini/Screenshot, e una
// notifica lo dice; un clic sulla notifica lo apre.
class Snip : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // "rect", "window" o "screen"
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    // Le finestre visibili, dalla più in alto: [{x, y, w, h, title}] nel layout.
    Q_PROPERTY(QVariantList windows READ windows NOTIFY activeChanged)

public:
    Snip(QQmlEngine* engine, WindowCapture* capture, Clipboard* clipboard, NotificationServer* notifications,
        QObject* parent = nullptr);

    bool active() const { return m_active; }
    QString mode() const { return m_mode; }
    void setMode(const QString& mode);
    QVariantList windows() const { return m_windows; }

    void start();
    Q_INVOKABLE void cancel();
    // Il ritaglio, in coordinate logiche dello schermo `screen`.
    Q_INVOKABLE void finish(const QString& screen, qreal x, qreal y, qreal width, qreal height);

    QImage image(const QString& screen) const;

signals:
    void activeChanged();
    void modeChanged();

private:
    void showOverlays();
    void closeOverlays();

    QQmlComponent m_component;
    WindowCapture* m_capture;
    Clipboard* m_clipboard;
    NotificationServer* m_notifications;
    bool m_active = false;
    QString m_mode = QStringLiteral("rect");
    int m_waiting = 0;
    QVariantList m_windows;
    QList<QPointer<QQuickWindow>> m_overlays;
    QHash<uint, QString> m_saved; // notifica -> file salvato
};

// "image://snip/<schermo>": la fotografia dello schermo da ritagliare.
class SnipImageProvider : public QQuickImageProvider {
public:
    explicit SnipImageProvider(Snip* snip)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_snip(snip)
    {
    }
    QImage requestImage(const QString& id, QSize* size, const QSize&) override
    {
        const QImage image = m_snip->image(id.section(u'/', 0, 0));
        if (size) {
            *size = image.size();
        }
        return image;
    }

private:
    Snip* m_snip;
};
