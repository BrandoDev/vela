// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bindings.h"

#include "a11y.h"
#include "focus.h"
#include "input.h"
#include "interact.h"
#include "lock.h"
#include "output.h"
#include "process.h"
#include "server.h"
#include "shell.h"
#include "snap.h"
#include "switcher.h"
#include "view.h"
#include "workspace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <xkbcommon/xkbcommon.h>

// Il comando della shell, se la shell c'è.
static bool shell(struct vela_server *server, const char *line)
{
    vela_shell_send(server, line);
    return true;
}

bool vela_bindings_handle(struct vela_server *server, uint32_t modifiers, uint32_t sym)
{
    bool alt = modifiers & WLR_MODIFIER_ALT;
    bool super = modifiers & WLR_MODIFIER_LOGO;
    bool shift = modifiers & WLR_MODIFIER_SHIFT;
    bool ctrl = modifiers & WLR_MODIFIER_CTRL;
    // Le varianti con Alt+lettera esistono perché, quando Vela gira in una
    // finestra dentro KDE, KDE si tiene per sé il tasto Super. Nella
    // sessione vera restano alle app, che le usano per aprire i propri menu.
    bool alt_nested = alt && server->nested;
    bool windows_key = super || alt_nested;
    struct vela_a11y *a11y = server->a11y;

    // Ctrl+Alt+F1...F12: un'altra console (o la sessione di Plasma). La
    // tastiera traduce già la combinazione nel tasto XF86Switch_VT_n.
    if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
        if (server->session) {
            wlr_session_change_vt(server->session, (unsigned)(sym - XKB_KEY_XF86Switch_VT_1 + 1));
        }
        return true;
    }
    // L'app a fuoco tiene le scorciatoie per sé (macchina virtuale, desktop
    // remoto), o lo schermo è bloccato: resta solo il cambio di console qui
    // sopra.
    if (vela_input_shortcuts_inhibited(server->input) || server->locked) {
        return false;
    }
    // Win+L blocca lo schermo, come su Windows (Alt+L dentro KDE).
    if (windows_key && (sym == XKB_KEY_l || sym == XKB_KEY_L)) {
        vela_lock_screen(server->lock);
        return true;
    }
    if (alt && shift && sym == XKB_KEY_Escape) {
        wl_display_terminate(server->display);
        return true;
    }
    if (windows_key && sym == XKB_KEY_Return) {
        const char *terminal = getenv("VELA_TERMINAL");
        vela_spawn(terminal ? terminal : "konsole || foot || kitty || alacritty || xterm");
        return true;
    }
    struct vela_view *active = vela_views_focused(server);
    if ((alt && sym == XKB_KEY_F4) || (alt_nested && sym == XKB_KEY_q)) {
        if (active) {
            vela_view_close(active);
        }
        return true;
    }
    // Alt+Tab (dentro KDE: Alt+J), con Maiusc all'indietro. Si sceglie
    // finché Alt resta premuto; rilasciandolo si passa alla finestra scelta
    // (keyboard.c), Esc annulla.
    bool tab = sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab;
    bool nested_tab = sym == XKB_KEY_j || sym == XKB_KEY_J;
    if ((alt && tab) || (alt_nested && nested_tab)) {
        vela_switcher_step(server, shift || sym == XKB_KEY_ISO_Left_Tab ? -1 : 1);
        return true;
    }
    if (vela_switcher_active(server) && sym == XKB_KEY_Escape) {
        vela_switcher_finish(server, false);
        return true;
    }
    // Desktop virtuali, come Windows: Win+Ctrl+←/→ per passare, Win+Ctrl+D
    // per crearne uno nuovo, Win+Ctrl+F4 per chiudere quello in uso (dentro
    // KDE con Alt+Ctrl); Win+Tab (Alt+W) la Visualizzazione attività.
    if (windows_key && ctrl) {
        if (sym == XKB_KEY_Left || sym == XKB_KEY_Right) {
            vela_workspaces_switch(server, server->workspaces->current + (sym == XKB_KEY_Left ? -1 : 1), true);
            return true;
        }
        if (sym == XKB_KEY_d || sym == XKB_KEY_D) {
            vela_workspaces_switch(server, vela_workspaces_add(server), true);
            return true;
        }
        if (sym == XKB_KEY_F4) {
            vela_workspaces_remove(server, server->workspaces->current);
            return true;
        }
    }
    // Accessibilità: Win+Ctrl+C i filtri colore (se la scorciatoia è accesa
    // nelle Impostazioni, come Windows); Win+più e Win+meno la lente di
    // ingrandimento, Win+Esc la chiude.
    if (super && ctrl && xkb_keysym_to_lower(sym) == XKB_KEY_c && a11y->color_filter_shortcut) {
        vela_a11y_set_color_filter(a11y, !a11y->color_filter, true);
        return true;
    }
    if (super && (sym == XKB_KEY_plus || sym == XKB_KEY_equal || sym == XKB_KEY_KP_Add)) {
        vela_a11y_zoom(a11y, 1);
        return true;
    }
    if (super && a11y->magnifier && (sym == XKB_KEY_minus || sym == XKB_KEY_KP_Subtract)) {
        vela_a11y_zoom(a11y, -1);
        return true;
    }
    if (super && a11y->magnifier && sym == XKB_KEY_Escape) {
        vela_a11y_set_magnifier(a11y, false);
        return true;
    }
    // Win+V: la cronologia degli appunti; Win+Maiusc+S e Stamp: lo Strumento
    // di cattura (li disegna la shell).
    if (super && !ctrl && xkb_keysym_to_lower(sym) == XKB_KEY_v) {
        return shell(server, "clipboard");
    }
    if ((super && shift && xkb_keysym_to_lower(sym) == XKB_KEY_s) || sym == XKB_KEY_Print) {
        return shell(server, "snip");
    }
    if ((super && tab) || (alt_nested && (sym == XKB_KEY_w || sym == XKB_KEY_W))) {
        return shell(server, "task-view");
    }
    // Win+frecce: metà, quarti, massimizza, riduci (snap.c). Dentro KDE
    // Alt+frecce e Alt+M (massimizza o ripristina).
    if (windows_key && (sym == XKB_KEY_Left || sym == XKB_KEY_Right || (super && (sym == XKB_KEY_Up || sym == XKB_KEY_Down)))) {
        if (active) {
            vela_snap_keyboard(server, active, sym);
        }
        return true;
    }
    if (alt_nested && sym == XKB_KEY_m) {
        if (active) {
            vela_view_set_maximized(active, !active->maximized, true);
        }
        return true;
    }
    // Win+Z: i layout di snap della finestra attiva.
    if (windows_key && (sym == XKB_KEY_z || sym == XKB_KEY_Z)) {
        if (active) {
            vela_snap_show_layouts(server, active, true);
        }
        return true;
    }
    if (alt_nested && sym == XKB_KEY_s) {
        return shell(server, "toggle-start");
    }
    // Win+X: il menu del pulsante Start; Win+R: Esegui; Win+D: il desktop;
    // Win+A: impostazioni rapide; Win+N: centro notifiche e calendario;
    // Win+I: le Impostazioni; Win+E: Esplora file. Dentro KDE con Alt.
    static const struct {
        xkb_keysym_t key;
        const char *command;
    } shell_keys[] = {
        { XKB_KEY_x, "winx" },
        { XKB_KEY_r, "run" },
        { XKB_KEY_d, "show-desktop" },
        { XKB_KEY_a, "quick-settings" },
        { XKB_KEY_n, "notification-center" },
        { XKB_KEY_i, "settings" },
        { XKB_KEY_e, "files" },
    };
    xkb_keysym_t lower = xkb_keysym_to_lower(sym);
    for (size_t i = 0; windows_key && i < sizeof(shell_keys) / sizeof(shell_keys[0]); ++i) {
        if (lower == shell_keys[i].key) {
            return shell(server, shell_keys[i].command);
        }
    }
    // Alt+Spazio: il menu della finestra, sotto la sua barra del titolo.
    if (alt && !super && sym == XKB_KEY_space && active) {
        struct wlr_box frame = vela_view_frame_box(active);
        int bar = vela_view_title_bar_height(active) > 0 ? vela_view_title_bar_height(active) : 36;
        vela_window_menu_show(server, active, frame.x + 4, frame.y + bar, true);
        return true;
    }
    return false;
}

void vela_window_menu_show(struct vela_server *server, struct vela_view *view, double lx, double ly, bool keyboard)
{
    if (!view || !view->ext_handle || server->locked) {
        return;
    }
    struct vela_output *output = vela_output_at(server, lx, ly);
    if (!output) {
        output = vela_view_output(view);
    }
    if (!output) {
        return;
    }
    // Alla shell: la finestra, lo schermo e il punto in coordinate dello schermo.
    struct wlr_box box = vela_output_box(output);
    char line[512];
    snprintf(line, sizeof(line), "window-menu %s %s %d %d %d %d %d", view->ext_handle->identifier,
        output->wlr->name, (int)lround(lx - box.x), (int)lround(ly - box.y), view->maximized ? 1 : 0,
        vela_view_resizable(view) ? 1 : 0, keyboard ? 1 : 0);
    vela_shell_send(server, line);
}

static bool starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

void vela_window_action(struct vela_server *server, struct vela_view *view, const char *action)
{
    if (strcmp(action, "activate") == 0) {
        // Dalla Visualizzazione attività: in primo piano (sul suo desktop).
        if (view->minimized) {
            vela_view_set_minimized(view, false);
        } else {
            vela_focus_view(server, view);
        }
    } else if (strcmp(action, "restore") == 0) {
        if (view->minimized) {
            vela_view_set_minimized(view, false);
            vela_focus_view(server, view);
        } else if (view->maximized) {
            vela_view_set_maximized(view, false, true);
        } else if (!vela_snap_is_none(view->snap)) {
            vela_view_set_snap(view, vela_snap_none, NULL);
        }
    } else if (strcmp(action, "minimize") == 0) {
        vela_view_set_minimized(view, true);
    } else if (strcmp(action, "maximize") == 0) {
        vela_view_set_maximized(view, true, true);
    } else if (strcmp(action, "close") == 0) {
        vela_view_close(view);
    } else if (strcmp(action, "move") == 0) {
        vela_interact_begin_keyboard(server, view, VELA_CURSOR_MOVE);
    } else if (strcmp(action, "resize") == 0) {
        vela_interact_begin_keyboard(server, view, VELA_CURSOR_RESIZE);
    } else if (strcmp(action, "snap-left") == 0 || strcmp(action, "snap-right") == 0) {
        // Dalla Visualizzazione attività: "Aggancia a sinistra/destra".
        vela_focus_view(server, view);
        vela_view_set_snap(view, strcmp(action, "snap-left") == 0 ? vela_snap_left : vela_snap_right, NULL);
    } else if (starts_with(action, "snap ")) {
        // Dai layout di snap e da Snap Assist: "snap x0 y0 x1 y1 [quiet
        // [finestra]]", in dodicesimi. Da Snap Assist anche la finestra
        // accanto a cui va: insieme fanno un gruppo di snap.
        struct vela_snap tile = vela_snap_none;
        char quiet[8] = "";
        char origin[128] = "";
        int n = sscanf(action + 5, "%d %d %d %d %7s %127s", &tile.x0, &tile.y0, &tile.x1, &tile.y1, quiet, origin);
        if (n >= 4 && vela_snap_valid(tile)) {
            if (view->minimized) {
                vela_view_set_minimized(view, false);
            }
            vela_focus_view(server, view);
            vela_view_set_snap(view, tile, NULL);
            if (strcmp(quiet, "quiet") != 0) {
                vela_snap_offer_assist(server, view);
            } else if (n >= 6) {
                struct vela_view *other;
                wl_list_for_each (other, &server->views, link) {
                    if (other->ext_handle && strcmp(origin, other->ext_handle->identifier) == 0) {
                        vela_snap_join_group(server, view, other);
                    }
                }
            }
        }
    } else if (strcmp(action, "activate-group") == 0) {
        // Il gruppo di snap dalla taskbar: tutte le sue finestre davanti.
        vela_snap_activate_group(server, view);
    } else if (starts_with(action, "move-to ")) {
        vela_workspaces_move_view(server, view, atoi(action + 8));
    } else if (strcmp(action, "move-to-new") == 0) {
        vela_workspaces_move_view(server, view, vela_workspaces_add(server));
    } else if (strcmp(action, "sticky") == 0 || strcmp(action, "unsticky") == 0) {
        vela_workspaces_set_sticky(server, view, strcmp(action, "sticky") == 0);
    } else if (strcmp(action, "app-sticky") == 0 || strcmp(action, "app-unsticky") == 0) {
        vela_workspaces_set_app_sticky(server, vela_view_app_id(view), strcmp(action, "app-sticky") == 0);
    }
}
