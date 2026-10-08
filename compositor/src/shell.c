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
        // Line and newline in a single send: the shell reads by lines.
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

// -------------------------------------------------------------- supervision --

void vela_shell_start(struct vela_server *server, const char *command)
{
    vela_child_start(&server->shell, server->loop, command);
}

void vela_shell_stop(struct vela_server *server)
{
    vela_child_stop(&server->shell);
}
