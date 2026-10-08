// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "process.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

void vela_exec_shell(const char *command)
{
    setsid();
    // wlroots blocca alcuni segnali per gestirli nel suo loop: i processi
    // lanciati da noi devono ripartire con una maschera pulita.
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, NULL);
    execl("/bin/sh", "/bin/sh", "-c", command, (char *)NULL);
    _exit(127);
}

void vela_spawn(const char *command)
{
    pid_t child = fork();
    if (child == 0) {
        if (fork() == 0) {
            vela_exec_shell(command);
        }
        _exit(0);
    }
    if (child > 0) {
        waitpid(child, NULL, 0);
    }
}
