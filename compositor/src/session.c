// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session.h"

#include "supervisor.h"
#include "util.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/util/log.h>

void vela_session_set_environment(void)
{
    // Vela usa i servizi di KDE (portachiavi, portali, tema delle app):
    // "KDE" dopo "Vela" fa sì che Brave, Chrome e le app Electron (VS Code)
    // usino il portachiavi di KDE come dentro Plasma. Senza, cifrano i dati
    // con una chiave fissa e non leggono più quelli salvati in Plasma:
    // accessi ai siti persi, sincronizzazione e password non decifrabili.
    // Le voci di avvio automatico di Plasma non partono: hanno
    // X-systemd-skip, tranne il ripristino della sessione, che
    // vela-session-env spegne.
    // Plasma Login copia DesktopNames del .desktop così com'è ("Vela;KDE;"),
    // mentre le app si aspettano i nomi separati da ':'.
    const char *current = getenv("XDG_CURRENT_DESKTOP");
    size_t size = (current ? strlen(current) : 0) + 16;
    char *desktops = calloc(size, 1);
    vela_desktop_names(current, desktops, size);
    if (!desktops[0] || strcmp(desktops, "Vela") == 0) {
        snprintf(desktops, size, "Vela:KDE");
    }
    setenv("XDG_CURRENT_DESKTOP", desktops, 1);
    free(desktops);
    setenv("KDE_SESSION_VERSION", "6", 0);
    setenv("XDG_SESSION_DESKTOP", "vela", 0);
    setenv("XDG_SESSION_TYPE", "wayland", 1); // da una console vale "tty"
    // Le app Qt e KDE con il tema scelto in KDE (stile, colori, font,
    // icone), come dentro Plasma.
    if (access("/usr/lib/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so", F_OK) == 0) {
        setenv("QT_QPA_PLATFORMTHEME", "kde", 0);
    }
    // Il menu delle applicazioni di KDE: senza, Dolphin e gli altri non
    // sanno con cosa aprire i file.
    if (access("/etc/xdg/menus/plasma-applications.menu", F_OK) == 0) {
        setenv("XDG_MENU_PREFIX", "plasma-", 0);
    }
}

void vela_session_run_hook(const char *action)
{
    char hook[PATH_MAX];
    if (!vela_session_hook_path(hook, sizeof(hook))) {
        wlr_log(WLR_INFO, "Session: vela-session-env not found, no systemd integration");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        execl(hook, hook, action, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        wlr_log(WLR_INFO, "Session: %s %s (%s)", hook, action,
            WIFEXITED(status) && WEXITSTATUS(status) == 0 ? "done" : "failed");
    }
}
