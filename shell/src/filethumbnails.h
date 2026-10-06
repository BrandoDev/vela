// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

// Le miniature dei file (le immagini sul desktop, come in Windows):
//   Image { source: "image://filethumb/" + encodeURIComponent(percorso) }
// Usa la cache condivisa di freedesktop (~/.cache/thumbnails), la stessa di
// Dolphin e Nautilus: se un'altra app ha già fatto la miniatura (anche di
// un video o di un PDF) la si prende da lì; le immagini che mancano si
// fanno qui, in un thread a parte, e si salvano per tutti.
// Se non c'è modo di avere una miniatura la richiesta fallisce e il QML
// mostra l'icona del tipo di file.
class FileThumbnailProvider : public QQuickAsyncImageProvider {
public:
    FileThumbnailProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    QThreadPool m_pool;
};
