// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Le texture: il contenuto di un buffer (di un'app, di uno schermo, del
// cursore) pronto da leggere negli shader.
//
// - dmabuf: importato così com'è, senza copie. Una sola importazione per
//   buffer (wlr_addon): le app alternano sempre gli stessi due o tre buffer.
// - wl_shm e simili: copiati in un'immagine nostra; quando l'app aggiorna,
//   si ricopia solo la parte cambiata.

#include "render/renderer.hpp"

namespace vela::render {

// Struttura semplice con wlr_texture come primo membro: da wlr_texture* si
// risale alla Texture con un cast.
struct Texture {
    wlr_texture base;
    Renderer* renderer;
    const PixelFormat* format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;

    // dmabuf importato: il buffer a cui è legata e il suo descrittore (piano
    // 0) per la sincronizzazione implicita. Il buffer resta bloccato, e
    // importato, finché wlroots usa la texture.
    wlr_buffer* buffer;
    wlr_addon addon;
    int dmabufFd;
    bool foreign; // a ogni uso: presa in carico dalla coda "foreign"

    bool uploaded; // immagine nostra: il contenuto c'è
    int refs; // riferimenti di wlroots (texture_from_buffer / destroy)
};

// nullptr se la texture non è nostra.
Texture* toTexture(wlr_texture* texture);
wlr_texture* createTexture(Renderer& renderer, wlr_buffer* buffer);
// Alla chiusura del renderer: libera subito le risorse Vulkan.
void releaseTexture(Texture* texture);

} // namespace vela::render
