// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_DIAGNOSTICS_H
#define VELA_DIAGNOSTICS_H

#include <stdbool.h>
#include <sys/types.h>

struct vela_process_resources {
    unsigned fds, sync_file, dmabuf, eventfd, sockets;
    unsigned long long nofile, rss_kib;
};

// Read-only, best-effort /proc snapshot. File names and socket peers are not
// retained. Called by the supervisor, independently of the compositor loop.
bool vela_process_resources(pid_t pid, struct vela_process_resources *out);

#endif
