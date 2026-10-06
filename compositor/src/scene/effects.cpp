// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scene/effects.hpp"

#include "scene/scene.hpp"

#include "ext-background-effect-v1-protocol.h"

namespace vela::scene {

namespace {

// L'effetto di una superficie: regione in attesa (dal client) e corrente
// (applicata al commit). Si trova dalla superficie con un addon.
struct Effect {
    wl_resource* resource = nullptr;
    wlr_surface* surface = nullptr; // null quando la superficie non c'è più
    pixman_region32_t pending;
    pixman_region32_t current;
    bool dirty = false;
    wlr_addon addon {};
    wl_listener commit {};
};

void detachSurface(Effect* effect)
{
    if (!effect->surface) {
        return;
    }
    wl_list_remove(&effect->commit.link);
    wlr_addon_finish(&effect->addon);
    effect->surface = nullptr;
}

void addonDestroy(wlr_addon* addon)
{
    Effect* effect = wl_container_of(addon, effect, addon);
    // La superficie se ne va: l'oggetto resta, inerte.
    wl_list_remove(&effect->commit.link);
    wlr_addon_finish(&effect->addon);
    effect->surface = nullptr;
}

const wlr_addon_interface effectAddon {
    .name = "vela-background-effect",
    .destroy = addonDestroy,
};

Effect* fromResource(wl_resource* resource);

void handleDestroy(wl_client*, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

void handleSetBlurRegion(wl_client*, wl_resource* resource, wl_resource* region)
{
    Effect* effect = fromResource(resource);
    if (!effect->surface) {
        wl_resource_post_error(resource, EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
            "la superficie non c'è più");
        return;
    }
    if (region) {
        pixman_region32_copy(&effect->pending, wlr_region_from_resource(region));
    } else {
        pixman_region32_clear(&effect->pending);
    }
    effect->dirty = true;
}

const struct ext_background_effect_surface_v1_interface surfaceImpl {
    .destroy = handleDestroy,
    .set_blur_region = handleSetBlurRegion,
};

Effect* fromResource(wl_resource* resource)
{
    return static_cast<Effect*>(wl_resource_get_user_data(resource));
}

void resourceDestroy(wl_resource* resource)
{
    Effect* effect = fromResource(resource);
    const bool shown = effect->surface && pixman_region32_not_empty(&effect->current);
    detachSurface(effect);
    pixman_region32_fini(&effect->pending);
    pixman_region32_fini(&effect->current);
    delete effect;
    if (shown) {
        Scene::changed();
    }
}

void handleGetEffect(wl_client* client, wl_resource* manager, uint32_t id, wl_resource* surfaceResource)
{
    wlr_surface* surface = wlr_surface_from_resource(surfaceResource);
    if (wlr_addon_find(&surface->addons, nullptr, &effectAddon)) {
        wl_resource_post_error(manager, EXT_BACKGROUND_EFFECT_MANAGER_V1_ERROR_BACKGROUND_EFFECT_EXISTS,
            "la superficie ha già un effetto");
        return;
    }
    wl_resource* resource = wl_resource_create(client, &ext_background_effect_surface_v1_interface,
        wl_resource_get_version(manager), id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* effect = new Effect;
    effect->resource = resource;
    effect->surface = surface;
    pixman_region32_init(&effect->pending);
    pixman_region32_init(&effect->current);
    wlr_addon_init(&effect->addon, &surface->addons, nullptr, &effectAddon);
    // Stato doppio: la regione nuova vale dal commit.
    effect->commit.notify = [](wl_listener* listener, void*) {
        Effect* e = wl_container_of(listener, e, commit);
        if (e->dirty) {
            pixman_region32_copy(&e->current, &e->pending);
            e->dirty = false;
            Scene::changed();
        }
    };
    wl_signal_add(&surface->events.commit, &effect->commit);
    wl_resource_set_implementation(resource, &surfaceImpl, effect, resourceDestroy);
}

void handleManagerDestroy(wl_client*, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

const struct ext_background_effect_manager_v1_interface managerImpl {
    .destroy = handleManagerDestroy,
    .get_background_effect = handleGetEffect,
};

void bindManager(wl_client* client, void*, uint32_t version, uint32_t id)
{
    wl_resource* resource = wl_resource_create(client, &ext_background_effect_manager_v1_interface, int(version), id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &managerImpl, nullptr, nullptr);
    ext_background_effect_manager_v1_send_capabilities(resource, EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
}

} // namespace

void initBackgroundEffects(wl_display* display)
{
    wl_global_create(display, &ext_background_effect_manager_v1_interface, 1, nullptr, bindManager);
}

const pixman_region32_t* blurRegion(wlr_surface* surface)
{
    wlr_addon* addon = wlr_addon_find(&surface->addons, nullptr, &effectAddon);
    if (!addon) {
        return nullptr;
    }
    Effect* effect = wl_container_of(addon, effect, addon);
    return pixman_region32_not_empty(&effect->current) ? &effect->current : nullptr;
}

} // namespace vela::scene
