// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "diagnostics.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool vela_process_resources(pid_t pid, struct vela_process_resources *out)
{
    *out = (struct vela_process_resources) { 0 };
    if (pid <= 0) {
        return false;
    }
    char path[128];
    snprintf(path, sizeof(path), "/proc/%ld/fd", (long)pid);
    DIR *dir = opendir(path);
    if (!dir) {
        return false;
    }
    int directory_fd = dirfd(dir);
    if (directory_fd < 0) {
        closedir(dir);
        return false;
    }
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
            continue;
        }
        // Do not count the sampler's own directory handle in self-tests.
        if (pid == getpid() && atoi(entry->d_name) == directory_fd) {
            continue;
        }
        ++out->fds;
        char target[256];
        ssize_t length = readlinkat(directory_fd, entry->d_name, target, sizeof(target) - 1);
        if (length < 0) {
            continue; // the compositor closed it during the snapshot
        }
        target[length] = '\0';
        if (strstr(target, "sync_file")) {
            ++out->sync_file;
        } else if (strstr(target, "dmabuf")) {
            ++out->dmabuf;
        } else if (strstr(target, "eventfd")) {
            ++out->eventfd;
        } else if (strncmp(target, "socket:", 7) == 0) {
            ++out->sockets;
        }
    }
    closedir(dir);
    snprintf(path, sizeof(path), "/proc/%ld/limits", (long)pid);
    FILE *file = fopen(path, "r");
    char line[512];
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            if (sscanf(line, "Max open files %llu", &out->nofile) == 1) {
                break;
            }
        }
        fclose(file);
    }
    snprintf(path, sizeof(path), "/proc/%ld/status", (long)pid);
    file = fopen(path, "r");
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            if (sscanf(line, "VmRSS: %llu kB", &out->rss_kib) == 1) {
                break;
            }
        }
        fclose(file);
    }
    return true;
}
