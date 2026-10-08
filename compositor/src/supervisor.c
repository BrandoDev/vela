// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// Il supervisore della sessione (vela-compositor --supervise ...).
//
// Se il compositor va in crash, la sessione non deve cadere con lui. Il
// supervisore è un processo minuscolo che non tocca la GPU né wlroots: crea
// il socket Wayland (wayland-N), lo tiene aperto e avvia il compositor vero
// passandogli il socket. Se il compositor muore per errore, ne avvia un
// altro sullo stesso socket: le app Qt e KDE, con QT_WAYLAND_RECONNECT=1, si
// ricollegano da sole e ricompaiono, come in Plasma quando KWin si riavvia.
// Le altre (Chromium, Electron, GTK, X11) si chiudono.
//
// Se lo schermo era bloccato, il compositor nuovo riparte bloccato (nero, e
// vela-lock): un crash non deve mai scoprire il desktop. Se il compositor
// va in crash tre volte in un minuto, il supervisore si arrende e la
// sessione finisce (si torna alla schermata di accesso).
//
// Il supervisore adotta le app avviate nella sessione (subreaper): quando
// la sessione finisce chiude quelle rimaste, che altrimenti resterebbero
// appese ad aspettare un compositor che non torna.

#include "supervisor.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_CRASHES 3 // in CRASH_WINDOW secondi: poi ci si arrende
#define CRASH_WINDOW 60.0

// Gli unici due stati globali: li toccano i gestori dei segnali.
static volatile sig_atomic_t stop_requested;
static volatile pid_t child_pid;

static void on_terminate(int signal)
{
    stop_requested = 1;
    if (child_pid > 0) {
        kill(child_pid, signal);
    }
}

static void say(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stderr, "vela-supervise: ");
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
}

// Il socket Wayland, come wl_display_add_socket_auto: il primo wayland-N
// libero, con il suo file di lock. Appartiene al supervisore fino alla fine.
struct wayland_socket {
    char name[32];
    char path[PATH_MAX];
    char lock_path[PATH_MAX];
    int fd;
    int lock_fd;
};

static bool create_socket(const char *runtime_dir, struct wayland_socket *s)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    for (int n = 0; n < 32; ++n) {
        snprintf(s->name, sizeof(s->name), "wayland-%d", n);
        int length = snprintf(s->lock_path, sizeof(s->lock_path), "%s/%s.lock", runtime_dir, s->name);
        if (length < 0 || (size_t)length >= sizeof(s->lock_path) || length - 5 >= (int)sizeof(address.sun_path)) {
            return false;
        }
        memcpy(s->path, s->lock_path, (size_t)length - 5);
        s->path[length - 5] = '\0';
        s->lock_fd = open(s->lock_path, O_CREAT | O_CLOEXEC | O_RDWR, S_IRUSR | S_IWUSR);
        if (s->lock_fd < 0) {
            continue;
        }
        if (flock(s->lock_fd, LOCK_EX | LOCK_NB) != 0) {
            close(s->lock_fd); // c'è già un compositor su questo numero
            continue;
        }
        unlink(s->path); // lasciato da un compositor morto
        s->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (s->fd < 0) {
            close(s->lock_fd);
            return false;
        }
        strcpy(address.sun_path, s->path);
        if (bind(s->fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(s->fd, 128) != 0) {
            close(s->fd);
            close(s->lock_fd);
            continue;
        }
        return true;
    }
    return false;
}

// I processi adottati ancora vivi (i figli diretti, tolto il compositor):
// un array allocato, da liberare; *count quanti sono.
static pid_t *children(int *count)
{
    *count = 0;
    DIR *proc = opendir("/proc");
    if (!proc) {
        return NULL;
    }
    pid_t *result = NULL;
    int capacity = 0;
    pid_t self = getpid();
    struct dirent *entry;
    while ((entry = readdir(proc))) {
        pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0) {
            continue;
        }
        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
        FILE *file = fopen(stat_path, "r");
        if (!file) {
            continue;
        }
        char line[1024] = { 0 };
        bool read = fgets(line, sizeof(line), file) != NULL;
        fclose(file);
        // "pid (nome) stato ppid ...": il nome può contenere spazi e parentesi.
        const char *end = read ? strrchr(line, ')') : NULL;
        char state = 0;
        int parent = 0;
        if (!end || sscanf(end + 1, " %c %d", &state, &parent) != 2 || parent != self || state == 'Z') {
            continue;
        }
        if (*count == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            result = realloc(result, (size_t)capacity * sizeof(*result));
        }
        result[(*count)++] = pid;
    }
    closedir(proc);
    return result;
}

static void reap_all(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

// Fine della sessione: si chiudono le app adottate rimaste (SIGTERM, e dopo
// due secondi SIGKILL a chi non ha ascoltato).
static void close_children(void)
{
    int count = 0;
    pid_t *left = children(&count);
    if (count == 0) {
        free(left);
        return;
    }
    say("closing the remaining apps (%d)", count);
    for (int i = 0; i < count; ++i) {
        kill(left[i], SIGTERM);
    }
    for (int round = 0; round < 20 && count > 0; ++round) {
        usleep(100000);
        reap_all();
        free(left);
        left = children(&count);
    }
    for (int i = 0; i < count; ++i) {
        kill(left[i], SIGKILL);
    }
    free(left);
    usleep(100000);
    reap_all();
}

static double now_seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

bool vela_lock_flag_path(const char *wayland_display, char *out, size_t size)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || !wayland_display) {
        return false;
    }
    int n = snprintf(out, size, "%s/vela-locked-%s", runtime_dir, wayland_display);
    return n > 0 && (size_t)n < size;
}

bool vela_session_hook_path(char *out, size_t size)
{
    char self[PATH_MAX] = { 0 };
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        char *slash = strrchr(self, '/');
        if (slash) {
            *slash = '\0';
            int written = snprintf(out, size, "%s/vela-session-env", self);
            if (written > 0 && (size_t)written < size && access(out, X_OK) == 0) {
                return true;
            }
        }
    }
    int written = snprintf(out, size, "%s/vela-session-env", VELA_LIBEXECDIR);
    return written > 0 && (size_t)written < size && access(out, X_OK) == 0;
}

// Fa partire `program` e ne aspetta la fine (senza lasciarlo zombie).
static void run_and_wait(const char *program, const char *argument)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl(program, program, argument, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) {
        waitpid(pid, NULL, 0);
    }
}

int vela_supervise(int argc, char **argv)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        say("XDG_RUNTIME_DIR is not set");
        return 1;
    }
    struct wayland_socket socket = { 0 };
    if (!create_socket(runtime_dir, &socket)) {
        say("can't create the Wayland socket");
        return 1;
    }
    say("socket %s; starting the compositor", socket.name);

    // Il compositor: lo stesso programma, con gli stessi argomenti tranne
    // --supervise.
    char self[PATH_MAX] = { 0 };
    if (readlink("/proc/self/exe", self, sizeof(self) - 1) <= 0) {
        say("can't find my own executable");
        return 1;
    }
    char **arguments = calloc((size_t)argc + 1, sizeof(*arguments));
    int count = 0;
    arguments[count++] = self;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--supervise") != 0) {
            arguments[count++] = argv[i];
        }
    }
    arguments[count] = NULL;

    // Le app avviate nella sessione (dalla shell, con doppio fork) restano
    // nostre figlie: a fine sessione si sa chi chiudere.
    prctl(PR_SET_CHILD_SUBREAPER, 1);

    struct sigaction action = { .sa_handler = on_terminate };
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);

    char lock_flag[PATH_MAX];
    bool has_lock_flag = vela_lock_flag_path(socket.name, lock_flag, sizeof(lock_flag));
    double crashes[MAX_CRASHES];
    int crash_count = 0;
    int exit_code = 0;
    for (bool restarted = false;; restarted = true) {
        pid_t pid = fork();
        if (pid < 0) {
            say("fork: %s", strerror(errno));
            exit_code = 1;
            break;
        }
        if (pid == 0) {
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGHUP, SIG_DFL);
            fcntl(socket.fd, F_SETFD, 0); // il compositor lo eredita
            char fd[16];
            snprintf(fd, sizeof(fd), "%d", socket.fd);
            setenv("VELA_WAYLAND_SOCKET_FD", fd, 1);
            setenv("VELA_WAYLAND_DISPLAY", socket.name, 1);
            if (restarted) {
                setenv("VELA_RESTARTED", "1", 1);
            }
            if (has_lock_flag && access(lock_flag, F_OK) == 0) {
                setenv("VELA_START_LOCKED", "1", 1);
            }
            execv(self, arguments);
            _exit(127);
        }
        child_pid = pid;
        // Si raccolgono anche le app adottate che finiscono: solo il
        // compositor interessa.
        int status = 0;
        for (;;) {
            pid_t done = waitpid(-1, &status, 0);
            if (done == pid || (done < 0 && errno != EINTR)) {
                break;
            }
        }
        child_pid = 0;

        if (stop_requested) {
            break; // la sessione si chiude (logout, spegnimento)
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            break; // uscito da sé: "Esci" dal menu
        }
        if (WIFSIGNALED(status)) {
            say("the compositor crashed (%s)", strsignal(WTERMSIG(status)));
        } else {
            say("the compositor exited with an error (code %d)", WEXITSTATUS(status));
        }
        // Si tengono solo i crash dell'ultimo minuto.
        double t = now_seconds();
        int kept = 0;
        for (int i = 0; i < crash_count; ++i) {
            if (t - crashes[i] <= CRASH_WINDOW) {
                crashes[kept++] = crashes[i];
            }
        }
        crash_count = kept;
        crashes[crash_count++] = t;
        if (crash_count >= MAX_CRASHES) {
            say("the compositor keeps exiting: giving up, the session ends");
            exit_code = 1;
            break;
        }
        say("restarting it");
    }
    free(arguments);

    close_children();

    // Se il compositor non ha potuto farlo (crash), si scollega la sessione
    // da systemd; se l'ha già fatto, non succede niente.
    char hook[PATH_MAX];
    if (vela_session_hook_path(hook, sizeof(hook))) {
        setenv("WAYLAND_DISPLAY", socket.name, 1);
        run_and_wait(hook, "stop");
    }
    if (has_lock_flag) {
        unlink(lock_flag);
    }
    unlink(socket.path);
    close(socket.fd);
    unlink(socket.lock_path);
    close(socket.lock_fd);
    return exit_code;
}
