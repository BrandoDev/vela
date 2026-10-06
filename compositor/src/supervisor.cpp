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

#include "supervisor.hpp"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <dirent.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace vela {

namespace {

constexpr int maxCrashes = 3; // in crashWindowSeconds: poi ci si arrende
constexpr int crashWindowSeconds = 60;

volatile sig_atomic_t g_stop = 0;
volatile pid_t g_child = 0;

void onTerminate(int signal)
{
    g_stop = 1;
    if (g_child > 0) {
        kill(g_child, signal);
    }
}

void say(const char* format, const char* argument = "")
{
    std::fprintf(stderr, "vela-supervise: ");
    std::fprintf(stderr, format, argument);
    std::fputc('\n', stderr);
}

// Il socket Wayland, come wl_display_add_socket_auto: il primo wayland-N
// libero, con il suo file di lock.
struct Socket {
    std::string name;
    std::string path;
    std::string lockPath;
    int fd = -1;
    int lockFd = -1;
};

bool createSocket(const std::string& runtimeDir, Socket& socket)
{
    for (int n = 0; n < 32; ++n) {
        Socket s;
        s.name = "wayland-" + std::to_string(n);
        s.path = runtimeDir + "/" + s.name;
        s.lockPath = s.path + ".lock";
        s.lockFd = open(s.lockPath.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, S_IRUSR | S_IWUSR);
        if (s.lockFd < 0) {
            continue;
        }
        if (flock(s.lockFd, LOCK_EX | LOCK_NB) != 0) {
            close(s.lockFd); // c'è già un compositor su questo numero
            continue;
        }
        unlink(s.path.c_str()); // lasciato da un compositor morto
        s.fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        if (s.fd < 0 || s.path.size() >= sizeof(address.sun_path)) {
            close(s.lockFd);
            return false;
        }
        std::memcpy(address.sun_path, s.path.c_str(), s.path.size() + 1);
        if (bind(s.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(s.fd, 128) != 0) {
            close(s.fd);
            close(s.lockFd);
            continue;
        }
        socket = s;
        return true;
    }
    return false;
}

// I processi adottati ancora vivi (i figli diretti, tolto il compositor).
std::vector<pid_t> children()
{
    std::vector<pid_t> result;
    DIR* proc = opendir("/proc");
    if (!proc) {
        return result;
    }
    const pid_t self = getpid();
    while (const dirent* entry = readdir(proc)) {
        const pid_t pid = pid_t(std::atoi(entry->d_name));
        if (pid <= 0) {
            continue;
        }
        const std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
        FILE* file = std::fopen(statPath.c_str(), "r");
        if (!file) {
            continue;
        }
        char line[1024] {};
        const bool read = std::fgets(line, sizeof(line), file) != nullptr;
        std::fclose(file);
        // "pid (nome) stato ppid ...": il nome può contenere spazi e parentesi.
        const char* end = read ? std::strrchr(line, ')') : nullptr;
        char state = 0;
        int parent = 0;
        if (end && std::sscanf(end + 1, " %c %d", &state, &parent) == 2 && parent == self && state != 'Z') {
            result.push_back(pid);
        }
    }
    closedir(proc);
    return result;
}

void reapAll()
{
    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

// Fine della sessione: si chiudono le app adottate rimaste (SIGTERM, e dopo
// due secondi SIGKILL a chi non ha ascoltato).
void closeChildren()
{
    std::vector<pid_t> left = children();
    if (left.empty()) {
        return;
    }
    say("chiudo le app rimaste (%s)", std::to_string(left.size()).c_str());
    for (pid_t pid : left) {
        kill(pid, SIGTERM);
    }
    for (int i = 0; i < 20 && !left.empty(); ++i) {
        usleep(100'000);
        reapAll();
        left = children();
    }
    for (pid_t pid : left) {
        kill(pid, SIGKILL);
    }
    usleep(100'000);
    reapAll();
}

double now()
{
    timespec t {};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return double(t.tv_sec) + double(t.tv_nsec) / 1e9;
}

} // namespace

std::string lockFlagPath(const char* waylandDisplay)
{
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir || !waylandDisplay) {
        return {};
    }
    return std::string(runtimeDir) + "/vela-bloccato-" + waylandDisplay;
}

std::string sessionHookPath()
{
    // Accanto al compositor (cartella di build), altrimenti installato.
    char self[PATH_MAX] {};
    if (const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1); n > 0) {
        std::string dir(self, size_t(n));
        dir.resize(dir.rfind('/'));
        if (access((dir + "/vela-session-env").c_str(), X_OK) == 0) {
            return dir + "/vela-session-env";
        }
    }
    const std::string installed = std::string(VELA_LIBEXECDIR) + "/vela-session-env";
    return access(installed.c_str(), X_OK) == 0 ? installed : std::string();
}

int runSupervisor(int argc, char* argv[])
{
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir) {
        say("XDG_RUNTIME_DIR non è impostata");
        return 1;
    }
    Socket socket;
    if (!createSocket(runtimeDir, socket)) {
        say("impossibile creare il socket Wayland");
        return 1;
    }
    say("socket %s; avvio il compositor", socket.name.c_str());

    // Il compositor: lo stesso programma, con gli stessi argomenti tranne
    // --supervise.
    char self[PATH_MAX] {};
    const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0) {
        say("non trovo il mio eseguibile");
        return 1;
    }
    std::vector<char*> arguments { self };
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--supervise") != 0) {
            arguments.push_back(argv[i]);
        }
    }
    arguments.push_back(nullptr);

    // Le app avviate nella sessione (dalla shell, con doppio fork) restano
    // nostre figlie: a fine sessione si sa chi chiudere.
    prctl(PR_SET_CHILD_SUBREAPER, 1);

    struct sigaction action {};
    action.sa_handler = onTerminate;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);

    const std::string lockFlag = lockFlagPath(socket.name.c_str());
    std::vector<double> crashes;
    int exitCode = 0;
    for (bool restarted = false;; restarted = true) {
        const pid_t pid = fork();
        if (pid < 0) {
            say("fork: %s", std::strerror(errno));
            exitCode = 1;
            break;
        }
        if (pid == 0) {
            signal(SIGTERM, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGHUP, SIG_DFL);
            fcntl(socket.fd, F_SETFD, 0); // il compositor lo eredita
            setenv("VELA_WAYLAND_SOCKET_FD", std::to_string(socket.fd).c_str(), 1);
            setenv("VELA_WAYLAND_DISPLAY", socket.name.c_str(), 1);
            if (restarted) {
                setenv("VELA_RESTARTED", "1", 1);
            }
            if (!lockFlag.empty() && access(lockFlag.c_str(), F_OK) == 0) {
                setenv("VELA_START_LOCKED", "1", 1);
            }
            execv(self, arguments.data());
            _exit(127);
        }
        g_child = pid;
        // Si raccolgono anche le app adottate che finiscono: solo il
        // compositor interessa.
        int status = 0;
        for (;;) {
            const pid_t done = waitpid(-1, &status, 0);
            if (done == pid || (done < 0 && errno != EINTR)) {
                break;
            }
        }
        g_child = 0;

        if (g_stop) {
            break; // la sessione si chiude (logout, spegnimento)
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            break; // uscito da sé: "Esci" dal menu
        }
        if (WIFSIGNALED(status)) {
            say("il compositor è andato in crash (%s)", strsignal(WTERMSIG(status)));
        } else {
            say("il compositor si è chiuso per errore (codice %s)", std::to_string(WEXITSTATUS(status)).c_str());
        }
        const double t = now();
        std::erase_if(crashes, [t](double at) { return t - at > crashWindowSeconds; });
        crashes.push_back(t);
        if (crashes.size() >= size_t(maxCrashes)) {
            say("il compositor continua a chiudersi: mi arrendo, la sessione finisce");
            exitCode = 1;
            break;
        }
        say("lo riavvio");
    }

    closeChildren();

    // Se il compositor non ha potuto farlo (crash), si scollega la sessione
    // da systemd; se l'ha già fatto, non succede niente.
    if (const std::string hook = sessionHookPath(); !hook.empty()) {
        setenv("WAYLAND_DISPLAY", socket.name.c_str(), 1);
        if (const pid_t pid = fork(); pid == 0) {
            execl(hook.c_str(), hook.c_str(), "stop", static_cast<char*>(nullptr));
            _exit(127);
        } else if (pid > 0) {
            waitpid(pid, nullptr, 0);
        }
    }
    if (!lockFlag.empty()) {
        unlink(lockFlag.c_str());
    }
    unlink(socket.path.c_str());
    close(socket.fd);
    unlink(socket.lockPath.c_str());
    close(socket.lockFd);
    return exitCode;
}

} // namespace vela
