// Desktop virtuali, come Windows 11.
//
// Ogni finestra sta su un desktop (o su tutti: "Mostra questa finestra su
// tutti i desktop"); si vedono solo quelle del desktop in uso, gli altri
// alberi sono spenti. La taskbar mostra solo le finestre del desktop in uso
// (la maniglia wlr-foreign-toplevel esiste solo per loro), mentre l'elenco
// ext-foreign-toplevel le ha tutte: la Visualizzazione attività della
// shell ne mostra le anteprime, e sa dove sta ciascuna dal messaggio
// "workspaces" (JSON) che il compositor le manda a ogni cambiamento.
//
// Il passaggio: le finestre che si lasciano vanno in layers.windowsOut, che
// scivola via sfumando; quelle nuove (layers.windows) arrivano dall'altra
// parte. A fine corsa le uscenti tornano in layers.windows, spente, nel loro
// ordine.
//
// I desktop e i loro nomi si ricordano in ~/.config/vela/desktop.conf.

#include "motion.hpp"
#include "server.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

namespace vela {

namespace {

std::string configPath()
{
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (config && *config) {
        return std::string(config) + "/vela/desktop.conf";
    }
    return home ? std::string(home) + "/.config/vela/desktop.conf" : std::string();
}

std::string jsonString(const std::string& text)
{
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                out += escaped;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

} // namespace

bool Toplevel::onCurrentWorkspace() const
{
    return sticky || workspace == server.workspaces.current;
}

void Toplevel::showInTaskbar(bool on)
{
    if (on && !handle && mapped) {
        createTaskbarHandle();
    } else if (!on && handle) {
        destroyTaskbarHandle();
    }
}

void Server::initWorkspaces()
{
    // desktop=<nome> per ogni desktop (vuoto: "Desktop N"); app-ovunque=<app_id>.
    std::ifstream in(configPath());
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("desktop=", 0) == 0) {
            workspaces.names.push_back(line.substr(8));
        } else if (line.rfind("app-ovunque=", 0) == 0 && line.size() > 12) {
            workspaces.stickyApps.push_back(line.substr(12));
        }
    }
    if (workspaces.names.empty()) {
        workspaces.names.emplace_back();
    }
}

void Server::saveWorkspaces() const
{
    const std::string path = configPath();
    if (path.empty()) {
        return;
    }
    mkdir((path.substr(0, path.rfind('/'))).c_str(), 0755);
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "# I desktop virtuali di Vela, nell'ordine (vuoto: \"Desktop N\")\n";
        for (const std::string& name : workspaces.names) {
            out << "desktop=" << name << '\n';
        }
        for (const std::string& app : workspaces.stickyApps) {
            out << "app-ovunque=" << app << '\n';
        }
    }
    std::rename(temporary.c_str(), path.c_str());
}

std::string Server::workspaceName(int index) const
{
    if (index < 0 || index >= workspaceCount()) {
        return {};
    }
    return workspaces.names[size_t(index)].empty() ? "Desktop " + std::to_string(index + 1)
                                                    : workspaces.names[size_t(index)];
}

std::string Server::workspacesJson() const
{
    // {"current":0,"names":["Desktop 1"],"windows":{"<id ext>":0 (-1: tutti)},"stickyApps":[...]}
    std::string json = "{\"current\":" + std::to_string(workspaces.current) + ",\"names\":[";
    for (int i = 0; i < workspaceCount(); ++i) {
        json += (i ? "," : "") + jsonString(workspaceName(i));
    }
    json += "],\"windows\":{";
    bool first = true;
    for (Toplevel* toplevel : toplevels) {
        if (!toplevel->mapped || !toplevel->extHandle || !toplevel->extHandle->identifier) {
            continue;
        }
        json += (first ? "" : ",") + jsonString(toplevel->extHandle->identifier) + ":"
            + std::to_string(toplevel->sticky ? -1 : toplevel->workspace);
        first = false;
    }
    json += "},\"stickyApps\":[";
    for (size_t i = 0; i < workspaces.stickyApps.size(); ++i) {
        json += (i ? "," : "") + jsonString(workspaces.stickyApps[i]);
    }
    // I gruppi di snap: [{"output":nome,"windows":[{"id":...,"tile":[x0,y0,x1,y1]}]}].
    json += "],\"snapGroups\":[";
    std::vector<uint32_t> groups;
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->snapGroup && std::find(groups.begin(), groups.end(), toplevel->snapGroup) == groups.end()) {
            groups.push_back(toplevel->snapGroup);
        }
    }
    bool firstGroup = true;
    for (uint32_t group : groups) {
        std::string members;
        Output* out = nullptr;
        for (Toplevel* toplevel : toplevels) {
            if (toplevel->snapGroup != group || !toplevel->mapped || !toplevel->extHandle
                || !toplevel->extHandle->identifier) {
                continue;
            }
            out = out ? out : toplevel->output();
            const Snap& s = toplevel->snap;
            members += std::string(members.empty() ? "" : ",") + "{\"id\":" + jsonString(toplevel->extHandle->identifier)
                + ",\"tile\":[" + std::to_string(s.x0) + "," + std::to_string(s.y0) + "," + std::to_string(s.x1) + ","
                + std::to_string(s.y1) + "]}";
        }
        if (members.empty()) {
            continue;
        }
        json += std::string(firstGroup ? "" : ",") + "{\"output\":" + jsonString(out ? out->wlr->name : "")
            + ",\"windows\":[" + members + "]}";
        firstGroup = false;
    }
    return json + "]}";
}

void Server::announceWorkspaces()
{
    sendShellCommand("workspaces " + workspacesJson());
}

void Server::syncTaskbarHandles()
{
    // Nell'ordine di apertura: la taskbar le mostra in quell'ordine.
    std::vector<Toplevel*> ordered(toplevels.begin(), toplevels.end());
    std::sort(ordered.begin(), ordered.end(), [](Toplevel* a, Toplevel* b) { return a->mapSerial < b->mapSerial; });
    for (Toplevel* toplevel : ordered) {
        if (toplevel->mapped) {
            toplevel->showInTaskbar(toplevel->onCurrentWorkspace());
        }
    }
}

void Server::workspaceMapped(Toplevel* toplevel)
{
    toplevel->mapSerial = workspaces.nextMapSerial++;
    toplevel->workspace = workspaces.current;
    const char* app = toplevel->appId();
    toplevel->sticky = app && std::find(workspaces.stickyApps.begin(), workspaces.stickyApps.end(), app)
        != workspaces.stickyApps.end();
}

void Server::workspaceForget(Toplevel* toplevel)
{
    std::erase(workspaces.outgoing, toplevel);
}

void Server::switchWorkspace(int index, bool refocusAfter)
{
    if (index < 0 || index >= workspaceCount() || index == workspaces.current || locked) {
        return;
    }
    finishWorkspaceSwitch(); // un passaggio ancora in corso finisce subito
    const int previous = workspaces.current;
    workspaces.current = index;
    workspaces.direction = index > previous ? 1 : -1;

    // Le uscenti, dal basso verso l'alto: in layers.windowsOut, nello stesso ordine.
    const std::vector<scene::Node*> children = layers.windows->children();
    for (scene::Node* node : children) {
        auto* owner = static_cast<SceneOwner*>(node->data);
        if (!owner || owner->kind != SceneKind::Toplevel) {
            continue;
        }
        auto* toplevel = static_cast<Toplevel*>(owner);
        if (toplevel->sticky || toplevel->workspace != previous || !toplevel->mapped || toplevel->minimized) {
            continue;
        }
        toplevel->tree->reparent(layers.windowsOut.get());
        workspaces.outgoing.push_back(toplevel);
    }
    // Tutte le altre: accese se sono del nuovo desktop. Quelle a schermo
    // intero stanno in un altro strato e cambiano senza scorrere.
    for (Toplevel* toplevel : toplevels) {
        if (!toplevel->mapped || toplevel->minimized) {
            continue;
        }
        if (std::find(workspaces.outgoing.begin(), workspaces.outgoing.end(), toplevel) == workspaces.outgoing.end()) {
            toplevel->tree->setEnabled(toplevel->onCurrentWorkspace());
        }
    }
    if (cursorMode != CursorMode::Passthrough && grabbed && !grabbed->onCurrentWorkspace()) {
        endSnapZone(false);
        grabbed = nullptr;
        cursorMode = CursorMode::Passthrough;
    }

    workspaces.startMs = -1.0; // al primo frame
    layers.windowsOut->setPosition(0, 0);
    layers.windowsOut->setOpacity(1.0f);
    layers.windows->setOpacity(0.0f);
    scheduleFrames();

    syncTaskbarHandles();
    if (refocusAfter) {
        Toplevel* focused = focusedToplevel();
        if (!focused || !focused->onCurrentWorkspace()) {
            refocus();
        }
    }
    wlr_log(WLR_INFO, "Desktop %d di %d", index + 1, workspaceCount());
    announceWorkspaces();
}

bool Server::tickWorkspaceSwitch(double nowMs)
{
    if (workspaces.direction == 0) {
        return false;
    }
    if (workspaces.startMs < 0.0) {
        workspaces.startMs = nowMs;
    }
    const double x = std::clamp((nowMs - workspaces.startMs) / motion::workspaceSwitchMs, 0.0, 1.0);
    const double t = motion::decelerate(x);
    int narrowest = 0;
    for (Output* output : outputs) {
        int width = 0;
        int height = 0;
        wlr_output_effective_resolution(output->wlr, &width, &height);
        narrowest = narrowest == 0 ? width : std::min(narrowest, width);
    }
    const double distance = narrowest * motion::workspaceSlideFraction * workspaces.direction;
    layers.windowsOut->setPosition(std::round(-distance * t), 0);
    layers.windowsOut->setOpacity(float(1.0 - t));
    // Le nuove diventano opache subito: sfumando insieme si vedrebbero
    // l'una attraverso l'altra.
    layers.windows->setPosition(std::round(distance * (1.0 - t)), 0);
    layers.windows->setOpacity(float(std::min(1.0, x * 4.0)));
    if (x >= 1.0) {
        finishWorkspaceSwitch();
        return false;
    }
    return true;
}

void Server::finishWorkspaceSwitch()
{
    if (workspaces.direction == 0) {
        return;
    }
    workspaces.direction = 0;
    // Le uscenti tornano al loro strato, spente, sotto le altre e nel loro ordine.
    for (auto it = workspaces.outgoing.rbegin(); it != workspaces.outgoing.rend(); ++it) {
        Toplevel* toplevel = *it;
        toplevel->tree->reparent(layers.windows.get());
        const auto& children = layers.windows->children();
        if (children.size() > 1) {
            toplevel->tree->placeBelow(children.front() == toplevel->tree.get() ? children[1] : children.front());
        }
        toplevel->tree->setEnabled(toplevel->onCurrentWorkspace() && !toplevel->minimized);
    }
    workspaces.outgoing.clear();
    layers.windowsOut->setPosition(0, 0);
    layers.windowsOut->setOpacity(1.0f);
    layers.windows->setPosition(0, 0);
    layers.windows->setOpacity(1.0f);
    scene::Scene::changed();
}

int Server::addWorkspace()
{
    workspaces.names.emplace_back();
    saveWorkspaces();
    announceWorkspaces();
    return workspaceCount() - 1;
}

void Server::removeWorkspace(int index)
{
    if (index < 0 || index >= workspaceCount() || workspaceCount() <= 1) {
        return;
    }
    finishWorkspaceSwitch();
    // Come Windows: le finestre passano al desktop a sinistra (o a destra, se era il primo).
    const int target = index > 0 ? index - 1 : 0; // dopo la rimozione, il primo diventa lo 0
    const bool wasCurrent = index == workspaces.current;
    if (wasCurrent) {
        switchWorkspace(index > 0 ? index - 1 : 1, false);
        finishWorkspaceSwitch();
    }
    workspaces.names.erase(workspaces.names.begin() + index);
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->workspace == index) {
            toplevel->workspace = target;
        } else if (toplevel->workspace > index) {
            --toplevel->workspace;
        }
    }
    if (workspaces.current > index) {
        --workspaces.current;
    }
    // Le finestre arrivate sul desktop in uso si accendono.
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->mapped && !toplevel->minimized) {
            toplevel->tree->setEnabled(toplevel->onCurrentWorkspace());
        }
    }
    syncTaskbarHandles();
    if (wasCurrent) {
        refocus();
    }
    saveWorkspaces();
    announceWorkspaces();
}

void Server::renameWorkspace(int index, const std::string& name)
{
    if (index < 0 || index >= workspaceCount()) {
        return;
    }
    std::string clean;
    for (const char c : name) {
        if (c != '\n' && c != '\r') {
            clean += c;
        }
    }
    // Il nome predefinito non si salva: così segue la numerazione.
    workspaces.names[size_t(index)] = clean == "Desktop " + std::to_string(index + 1) ? std::string() : clean;
    saveWorkspaces();
    announceWorkspaces();
}

void Server::moveWorkspace(int from, int to)
{
    if (from < 0 || to < 0 || from >= workspaceCount() || to >= workspaceCount() || from == to) {
        return;
    }
    finishWorkspaceSwitch();
    // I nomi predefiniti seguono la posizione; quelli scelti seguono il desktop.
    std::string name = workspaces.names[size_t(from)];
    workspaces.names.erase(workspaces.names.begin() + from);
    workspaces.names.insert(workspaces.names.begin() + to, name);
    auto remap = [&](int i) {
        if (i == from) {
            return to;
        }
        if (from < to && i > from && i <= to) {
            return i - 1;
        }
        if (from > to && i >= to && i < from) {
            return i + 1;
        }
        return i;
    };
    for (Toplevel* toplevel : toplevels) {
        toplevel->workspace = remap(toplevel->workspace);
    }
    workspaces.current = remap(workspaces.current);
    saveWorkspaces();
    announceWorkspaces();
}

void Server::moveToWorkspace(Toplevel* toplevel, int index)
{
    if (!toplevel || index < 0 || index >= workspaceCount()) {
        return;
    }
    toplevel->sticky = false;
    toplevel->workspace = index;
    toplevel->leaveSnapGroup(); // il gruppo resta sull'altro desktop
    const bool wasFocused = focusedToplevel() == toplevel;
    if (toplevel->mapped && !toplevel->minimized) {
        toplevel->tree->setEnabled(toplevel->onCurrentWorkspace());
    }
    syncTaskbarHandles();
    if (wasFocused && !toplevel->onCurrentWorkspace()) {
        toplevel->setActivated(false);
        toplevels.remove(toplevel);
        toplevels.push_back(toplevel);
        refocus();
    }
    announceWorkspaces();
}

void Server::setSticky(Toplevel* toplevel, bool on)
{
    if (!toplevel || toplevel->sticky == on) {
        return;
    }
    toplevel->sticky = on;
    // Tolta da "tutti i desktop", resta su quello in uso.
    toplevel->workspace = workspaces.current;
    syncTaskbarHandles();
    announceWorkspaces();
}

void Server::setAppSticky(const std::string& appId, bool on)
{
    if (appId.empty()) {
        return;
    }
    std::erase(workspaces.stickyApps, appId);
    if (on) {
        workspaces.stickyApps.push_back(appId);
    }
    for (Toplevel* toplevel : toplevels) {
        if (toplevel->appId() && appId == toplevel->appId()) {
            toplevel->sticky = on;
            toplevel->workspace = workspaces.current;
            if (toplevel->mapped && !toplevel->minimized) {
                toplevel->tree->setEnabled(true);
            }
        }
    }
    syncTaskbarHandles();
    saveWorkspaces();
    announceWorkspaces();
}

} // namespace vela
