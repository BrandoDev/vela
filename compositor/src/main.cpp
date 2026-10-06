// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server.hpp"
#include "supervisor.hpp"

#include <cstring>
#include <getopt.h>
#include <sys/prctl.h>

namespace {

void printUsage(const char* program)
{
    std::printf(
        "Usage: %s [--supervise] [-s command]\n"
        "\n"
        "  -s command   runs the command at startup (e.g. the shell: -s vela-shell)\n"
        "               and restarts it if it exits with an error\n"
        "  --supervise  the real session (vela-session): a separate process holds\n"
        "               the Wayland socket and restarts the compositor if it crashes;\n"
        "               Qt and KDE apps reconnect\n"
        "  -h           shows this help\n"
        "\n"
        "Environment variables:\n"
        "  VELA_TERMINAL      terminal for Alt/Super+Enter (default: konsole)\n"
        "  VELA_SCALE         output scale (e.g. 1.25, or DP-1=1.5,HDMI-A-1=1);\n"
        "                     without it, Vela picks one from each output's DPI\n"
        "  VELA_VRR=1|0       variable refresh rate always on, or never\n"
        "  VELA_LATCH=0       draw as soon as the vblank arrives, instead of as late\n"
        "                     as possible before the next one (late latching)\n"
        "  VELA_LATCH_MARGIN  minimum late-latching margin in ms (default 1)\n"
        "  VELA_SCANOUT=0     no direct scanout for full-screen apps\n"
        "  VELA_READY_WAIT=0  apply app commits right away, even if their GPU hasn't\n"
        "                     finished (the frame waits for it: for comparison)\n"
        "  VELA_REALTIME=0    no realtime scheduling for the compositor\n"
        "  VELA_SCREEN_OFF    idle minutes before locking and turning screens off (default 10, 0: never;\n"
        "                     otherwise screen-off= in ~/.config/vela/vela.conf)\n"
        "  VELA_LOCK_ON_IDLE=0  when idle, turn screens off without locking\n"
        "  VELA_LOCK          locker to use instead of vela-lock\n"
        "  VELA_VULKAN_VALIDATION=1  Vulkan validation layers (for development)\n"
        "  VELA_NATURAL_SCROLL=0  classic touchpad scrolling\n"
        "  VELA_STATS=1       every 2 s, per output: fps, frame cost, latency, missed vblanks\n"
        "  VELA_DEBUG=1       verbose log\n"
        "  XKB_DEFAULT_LAYOUT keyboard layout (e.g. it)\n",
        program);
}

} // namespace

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--supervise") == 0) {
            return vela::runSupervisor(argc, argv);
        }
    }

    std::string startup;
    int option;
    while ((option = getopt(argc, argv, "s:h")) != -1) {
        switch (option) {
        case 's':
            startup = optarg;
            break;
        case 'h':
            printUsage(argv[0]);
            return 0;
        default:
            printUsage(argv[0]);
            return 1;
        }
    }

    // Il late latching sveglia il compositor a un istante preciso prima del
    // vblank: il kernel non deve arrotondare i timer (predefinito 50 µs).
    prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);

    const char* debug = std::getenv("VELA_DEBUG");
    wlr_log_init(debug && *debug ? WLR_DEBUG : WLR_INFO, nullptr);

    vela::Server server;
    if (!server.init() || !server.start(startup)) {
        return 1;
    }
    server.run();
    server.shutdown();
    return 0;
}
