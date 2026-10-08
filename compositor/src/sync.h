// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_SYNC_H
#define VELA_SYNC_H

#include <stdbool.h>

// NVIDIA's proprietary 580 branch defaults to CPU synchronization, pending
// investigation of sync_file exhaustion. Mesa/NVK and other branches retain
// GPU synchronization. "0"/"1" override the policy, never device support.
bool vela_sync_file_enabled(bool supported, bool nvidia, unsigned driver_major, const char *override);
bool vela_sync_ready(int fd);
// Wait through EINTR and close the descriptor on every outcome. Never call
// this after a successful Vulkan import: the driver then owns the descriptor.
bool vela_sync_wait_close(int fd);

#endif
