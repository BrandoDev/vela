// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "output_manager.h"

#include "listen.h"
#include "output.h"
#include "output_config.h"
#include "scene/scene.h"
#include "server.h"
#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/util/log.h>

void vela_output_manager_update(struct vela_server *server)
{
    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
    struct vela_output *output;
    wl_list_for_each (output, &server->outputs, link) {
        struct wlr_output_configuration_head_v1 *head = wlr_output_configuration_head_v1_create(config, output->wlr);
        struct wlr_box box = { 0 };
        wlr_output_layout_get_box(server->output_layout, output->wlr, &box);
        head->state.enabled = output->wlr->enabled && !wlr_box_empty(&box);
        bool remembered = !head->state.enabled && output->has_last_position;
        head->state.x = remembered ? output->last_x : box.x;
        head->state.y = remembered ? output->last_y : box.y;
    }
    wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

// An output as it was applied, to remember in outputs.conf.
struct applied {
    struct wlr_output *output;
    bool enabled;
    int x, y;
};

static void apply(struct vela_server *server, struct wlr_output_configuration_v1 *config, bool test_only)
{
    bool ok = true;
    int count = wl_list_length(&config->heads);
    struct applied *applied = calloc((size_t)count + 1, sizeof(*applied));
    int n = 0;
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each (head, &config->heads, link) {
        const struct wlr_output_head_v1_state *wanted = &head->state;
        struct vela_output *output = wanted->output->data;
        if (!output) {
            ok = false;
            continue;
        }
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(wanted, &state);
        if (test_only) {
            ok = wlr_output_test_state(wanted->output, &state) && ok;
        } else if (wanted->enabled) {
            // Position first: the first frame at the new mode is already drawn
            // there.
            wlr_output_layout_add(server->output_layout, wanted->output, wanted->x, wanted->y);
            ok = vela_output_commit_mode(output, &state) && ok;
            applied[n++] = (struct applied) { wanted->output, true, wanted->x, wanted->y };
        } else {
            struct wlr_box box = { 0 };
            wlr_output_layout_get_box(server->output_layout, wanted->output, &box);
            if (!wlr_box_empty(&box)) {
                output->has_last_position = true;
                output->last_x = box.x;
                output->last_y = box.y;
            }
            ok = wlr_output_commit_state(wanted->output, &state) && ok;
            wlr_output_layout_remove(server->output_layout, wanted->output);
            applied[n++] = (struct applied) { wanted->output, false, 0, 0 };
        }
        wlr_output_state_finish(&state);
    }
    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
    if (test_only) {
        free(applied);
        return;
    }

    struct vela_output *output;
    wl_list_for_each (output, &server->outputs, link) {
        vela_output_arrange_layers(output);
    }
    // The windows of an output turned off move to another (and come back when
    // it's turned on again), maximized and snapped ones are rearranged.
    vela_views_check_outputs(server);
    struct vela_view *view;
    wl_list_for_each (view, &server->views, link) {
        vela_view_keep_in_place(view);
    }
    vela_scene_changed(server->scene);

    // The user's choices are remembered (only in the real session).
    if (server->session && ok) {
        char (*keys)[256] = calloc((size_t)n + 1, sizeof(*keys));
        struct vela_output_config_entry *entries = calloc((size_t)n + 1, sizeof(*entries));
        for (int i = 0; i < n; ++i) {
            const struct wlr_output *wlr = applied[i].output;
            vela_output_key(wlr, keys[i], sizeof(keys[i]));
            entries[i] = (struct vela_output_config_entry) { keys[i], applied[i].enabled, wlr->width, wlr->height,
                wlr->refresh, wlr->scale, wlr->transform, applied[i].x, applied[i].y };
        }
        vela_output_config_save(entries, n);
        free(entries);
        free(keys);
    }
    free(applied);
    wlr_log(WLR_INFO, "Output configuration %s", ok ? "applicata" : "only partly applied");
}

static void handle_apply(struct wl_listener *listener, void *data)
{
    struct vela_server *server = wl_container_of(listener, server, output_manager_apply);
    apply(server, data, false);
}

static void handle_test(struct wl_listener *listener, void *data)
{
    struct vela_server *server = wl_container_of(listener, server, output_manager_test);
    apply(server, data, true);
}

void vela_output_manager_init(struct vela_server *server)
{
    server->output_manager = wlr_output_manager_v1_create(server->display);
    vela_listen(&server->output_manager->events.apply, &server->output_manager_apply, handle_apply);
    vela_listen(&server->output_manager->events.test, &server->output_manager_test, handle_test);
}

void vela_output_manager_finish(struct vela_server *server)
{
    wl_list_remove(&server->output_manager_apply.link);
    wl_list_remove(&server->output_manager_test.link);
}

// ------------------------------------------------------------ test-output --

static void find_headless(struct wlr_backend *candidate, void *data)
{
    if (wlr_backend_is_headless(candidate)) {
        *(struct wlr_backend **)data = candidate;
    }
}

void vela_test_output_command(struct vela_server *server, const char *arguments)
{
    // Only without real outputs: hot plugging and unplugging in tests.
    struct wlr_backend *headless = NULL;
    wlr_multi_for_each_backend(server->backend, find_headless, &headless);
    if (!headless) {
        wlr_log(WLR_ERROR, "test-output: only with the headless backend");
        return;
    }
    int width = 0;
    int height = 0;
    char name[64] = "";
    if (sscanf(arguments, "add %dx%d", &width, &height) == 2 && width > 0 && height > 0) {
        struct wlr_output *wlr = wlr_headless_add_output(headless, (unsigned)width, (unsigned)height);
        // VELA_OUTPUT_SIZE applies to the initial outputs: this one has its
        // own.
        struct vela_output *output = wlr ? wlr->data : NULL;
        if (output) {
            struct wlr_output_state state;
            wlr_output_state_init(&state);
            wlr_output_state_set_custom_mode(&state, width, height, 0);
            vela_output_commit_mode(output, &state);
            wlr_output_state_finish(&state);
        }
        wlr_log(WLR_INFO, "test-output: connected %s (%dx%d)", wlr ? wlr->name : "(none)", width, height);
    } else if (sscanf(arguments, "remove %63s", name) == 1) {
        struct vela_output *output = vela_output_named(server, name);
        if (output) {
            wlr_log(WLR_INFO, "test-output: disconnecting %s", name);
            wlr_output_destroy(output->wlr);
        }
    }
}
