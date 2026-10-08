// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

// File thumbnails (images on the desktop, like in Windows):
//   Image { source: "image://filethumb/" + encodeURIComponent(path) }
// It uses the shared freedesktop cache (~/.cache/thumbnails), the same as
// Dolphin and Nautilus: if another app already made the thumbnail (of a video
// or a PDF too) it's taken from there; missing images are made here, in a
// separate thread, and saved for everyone.
// When there's no way to get a thumbnail the request fails and QML shows the
// file type's icon.
class FileThumbnailProvider : public QQuickAsyncImageProvider {
public:
    FileThumbnailProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    QThreadPool m_pool;
};
