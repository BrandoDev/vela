#include "server.hpp"

#include <getopt.h>
#include <sys/prctl.h>

namespace {

void printUsage(const char* program)
{
    std::printf(
        "Uso: %s [-s comando]\n"
        "\n"
        "  -s comando   esegue il comando all'avvio (es. la shell: -s vela-shell)\n"
        "               e lo riavvia se si chiude per errore\n"
        "  -h           mostra questo aiuto\n"
        "\n"
        "Variabili d'ambiente:\n"
        "  VELA_TERMINAL      terminale per Alt/Super+Invio (predefinito: konsole)\n"
        "  VELA_SCALE         scala degli schermi (es. 1.25, o DP-1=1.5,HDMI-A-1=1);\n"
        "                     senza, Vela la sceglie dai DPI di ogni schermo\n"
        "  VELA_VRR=1         attiva il refresh variabile\n"
        "  VELA_LATCH=0       disegna appena arriva il vblank, invece che il più tardi\n"
        "                     possibile prima del successivo (late latching)\n"
        "  VELA_LATCH_MARGIN  margine minimo del late latching in ms (predefinito 1)\n"
        "  VELA_SCANOUT=0     niente scanout diretto delle app a schermo intero\n"
        "  VELA_REALTIME=0    niente scheduling realtime per il compositor\n"
        "  VELA_VULKAN_VALIDATION=1  validation layer di Vulkan (per lo sviluppo)\n"
        "  VELA_NATURAL_SCROLL=0  scorrimento classico sul touchpad\n"
        "  VELA_STATS=1       ogni 2 s, per schermo: fps, costo dei frame, latenza, vblank persi\n"
        "  VELA_DEBUG=1       log dettagliato\n"
        "  XKB_DEFAULT_LAYOUT layout della tastiera (es. it)\n",
        program);
}

} // namespace

int main(int argc, char* argv[])
{
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
