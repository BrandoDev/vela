// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shell.h"

#include "process.h"
#include "server.h"
#include "util.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/pidfd.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>

void vela_shell_send(struct vela_server *server, const char *line)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || !server->socket_name[0]) {
        return;
    }
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    if (!vela_format(address.sun_path, sizeof(address.sun_path), "%s/vela-shell-%s.sock", runtime_dir,
            server->socket_name)) {
        return;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        return;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0) {
        // Riga e a capo in un solo invio: la shell legge per righe.
        size_t length = strlen(line);
        char *message = malloc(length + 1);
        memcpy(message, line, length);
        message[length] = '\n';
        send(fd, message, length + 1, MSG_NOSIGNAL);
        free(message);
    } else {
        wlr_log(WLR_DEBUG, "Shell not reachable on %s", address.sun_path);
    }
    close(fd);
}

// ------------------------------------------------------------- supervisione --

static void stop_watching(struct vela_shell *shell)
{
    if (shell->source) {
        wl_event_source_remove(shell->source);
        shell->source = NULL;
    }
    if (shell->pidfd >= 0) {
        close(shell->pidfd);
    }
    shell->pidfd = -1;
    shell->pid = -1;
}

static int handle_exit(int fd, uint32_t mask, void *data)
{
    struct vela_server *server = data;
    struct vela_shell *shell = &server->shell;
    int status = 0;
    waitpid(shell->pid, &status, 0);
    double uptime = (vela_now_ns() - shell->started_ns) / 1e9;
    stop_watching(shell);
    char *command = shell->command;
    shell->command = NULL;

    // Uscita pulita (es. "già in esecuzione"): era voluta.
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        wlr_log(WLR_INFO, "\"%s\" exited", command);
        free(command);
        return 0;
    }
    char reason[64];
    if (WIFSIGNALED(status)) {
        snprintf(reason, sizeof(reason), "signal %s", strsignal(WTERMSIG(status)));
    } else {
        snprintf(reason, sizeof(reason), "code %d", WEXITSTATUS(status));
    }
    // Se si chiude di continuo appena partito, riavviarlo non serve a nulla.
    shell->quick_crashes = uptime < 5.0 ? shell->quick_crashes + 1 : 0;
    if (shell->quick_crashes >= 3) {
        wlr_log(WLR_ERROR, "\"%s\" keeps exiting (%s): not restarting it", command, reason);
    } else {
        wlr_log(WLR_ERROR, "\"%s\" exited (%s): restarting it", command, reason);
        vela_shell_start(server, command);
    }
    free(command);
    return 0;
}

void vela_shell_start(struct vela_server *server, const char *command)
{
    struct vela_shell *shell = &server->shell;
    // Fork singolo, così il processo resta nostro figlio e un pidfd nel
    // ciclo di Wayland ci avvisa quando termina.
    free(shell->command);
    shell->command = strdup(command);
    pid_t pid = fork();
    if (pid < 0) {
        wlr_log_errno(WLR_ERROR, "Can't start \"%s\"", command);
        return;
    }
    if (pid == 0) {
        // La shell muore con il compositor: se questo va in crash, il
        // supervisore ne avvia un altro con una shell nuova, e la vecchia non
        // deve ricollegarsi (QT_WAYLAND_RECONNECT) e fare doppione.
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        vela_exec_shell(command);
    }
    int pidfd = pidfd_open(pid, 0);
    if (pidfd < 0) {
        wlr_log_errno(WLR_ERROR, "pidfd_open: \"%s\" won't be restarted", command);
        return;
    }
    shell->pid = pid;
    shell->pidfd = pidfd;
    shell->started_ns = vela_now_ns();
    shell->source = wl_event_loop_add_fd(server->loop, pidfd, WL_EVENT_READABLE, handle_exit, server);
}

void vela_shell_stop(struct vela_server *server)
{
    stop_watching(&server->shell);
    free(server->shell.command);
    server->shell.command = NULL;
}
