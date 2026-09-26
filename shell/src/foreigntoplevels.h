#pragma once

#include <QList>
#include <QObject>
#include <QRect>
#include <QString>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>

#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

class QWindow;

// Le finestre aperte, come le pubblica il compositor con il protocollo
// wlr-foreign-toplevel-management: titolo, app_id, stato, e le richieste
// per attivarle, ridurle a icona o chiuderle.

class ForeignToplevel : public QObject, public QtWayland::zwlr_foreign_toplevel_handle_v1 {
    Q_OBJECT

public:
    explicit ForeignToplevel(::zwlr_foreign_toplevel_handle_v1* handle, QObject* parent = nullptr);
    ~ForeignToplevel() override;

    // Stato applicato: cambia tutto insieme all'evento "done".
    QString title;
    QString appId;
    bool activated = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    ForeignToplevel* parentWindow = nullptr; // es. una finestra di dialogo
    quint64 lastActivated = 0; // per sapere quale finestra di un'app è la più recente
    bool ready = false; // ha ricevuto il primo "done"

    void requestActivate();
    void requestMinimize();
    // Dove sta il suo pulsante (coordinate di `panel`): il compositor ci fa
    // volare la finestra quando si riduce a icona.
    void setButtonRect(QWindow* panel, const QRect& rect);

signals:
    void changed();
    void closed();

protected:
    void zwlr_foreign_toplevel_handle_v1_title(const QString& title) override;
    void zwlr_foreign_toplevel_handle_v1_app_id(const QString& appId) override;
    void zwlr_foreign_toplevel_handle_v1_state(wl_array* state) override;
    void zwlr_foreign_toplevel_handle_v1_parent(::zwlr_foreign_toplevel_handle_v1* parent) override;
    void zwlr_foreign_toplevel_handle_v1_done() override;
    void zwlr_foreign_toplevel_handle_v1_closed() override;

private:
    struct {
        QString title;
        QString appId;
        bool activated = false;
        bool minimized = false;
        bool maximized = false;
        bool fullscreen = false;
        ForeignToplevel* parentWindow = nullptr;
    } m_pending;
    QRect m_buttonRect;
};

class ForeignToplevelManager : public QWaylandClientExtensionTemplate<ForeignToplevelManager>,
                               public QtWayland::zwlr_foreign_toplevel_manager_v1 {
    Q_OBJECT

public:
    ForeignToplevelManager();

    // Solo le finestre già descritte per intero, in ordine di apertura.
    const QList<ForeignToplevel*>& windows() const { return m_windows; }

signals:
    void windowsChanged();

protected:
    void zwlr_foreign_toplevel_manager_v1_toplevel(::zwlr_foreign_toplevel_handle_v1* handle) override;

private:
    QList<ForeignToplevel*> m_windows;
};
