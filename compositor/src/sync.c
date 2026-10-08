// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sync.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

bool vela_sync_file_enabled(bool supported, bool nvidia, unsigned driver_major, const char *override)
{
    if (!supported || (override && strcmp(override, "0") == 0)) {
        return false;
    }
    return (override && strcmp(override, "1") == 0) || !nvidia || driver_major != 580;
}

bool vela_sync_ready(int fd)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    // A fence with an error is also finished; POLLNVAL is not a fence.
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLERR));
}

bool vela_sync_wait_close(int fd)
{
    if (fd < 0) {
        return false;
    }
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int result;
    do {
        result = poll(&pfd, 1, -1);
    } while (result < 0 && errno == EINTR);
    bool ready = result > 0 && (pfd.revents & (POLLIN | POLLERR));
    close(fd);
    return ready;
}
