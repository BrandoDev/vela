#include "outputconfig.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sys/stat.h>

namespace vela {

namespace {

std::string configPath()
{
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (config && *config) {
        return std::string(config) + "/vela/schermi.conf";
    }
    return home ? std::string(home) + "/.config/vela/schermi.conf" : std::string();
}

const char* transformName(wl_output_transform transform)
{
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL: return "normale";
    case WL_OUTPUT_TRANSFORM_90: return "90";
    case WL_OUTPUT_TRANSFORM_180: return "180";
    case WL_OUTPUT_TRANSFORM_270: return "270";
    case WL_OUTPUT_TRANSFORM_FLIPPED: return "specchio";
    case WL_OUTPUT_TRANSFORM_FLIPPED_90: return "specchio-90";
    case WL_OUTPUT_TRANSFORM_FLIPPED_180: return "specchio-180";
    case WL_OUTPUT_TRANSFORM_FLIPPED_270: return "specchio-270";
    }
    return "normale";
}

wl_output_transform transformFromName(const std::string& name)
{
    for (int t = WL_OUTPUT_TRANSFORM_NORMAL; t <= WL_OUTPUT_TRANSFORM_FLIPPED_270; ++t) {
        if (name == transformName(wl_output_transform(t))) {
            return wl_output_transform(t);
        }
    }
    return WL_OUTPUT_TRANSFORM_NORMAL;
}

// Il file: una sezione per monitor, "[marca modello seriale]", e sotto
// chiave=valore. Si conserva l'ordine delle sezioni.
using Section = std::map<std::string, std::string>;
struct ConfigFile {
    std::vector<std::pair<std::string, Section>> sections;

    Section* find(const std::string& key)
    {
        for (auto& [name, section] : sections) {
            if (name == key) {
                return &section;
            }
        }
        return nullptr;
    }
};

ConfigFile readConfig()
{
    ConfigFile file;
    std::ifstream in(configPath());
    std::string line;
    Section* current = nullptr;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            file.sections.push_back({ line.substr(1, line.size() - 2), {} });
            current = &file.sections.back().second;
            continue;
        }
        const size_t eq = line.find('=');
        if (current && eq != std::string::npos) {
            (*current)[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    return file;
}

} // namespace

std::string outputKey(const wlr_output* output)
{
    std::string key;
    for (const char* part : { output->make, output->model, output->serial }) {
        if (part && *part) {
            if (!key.empty()) {
                key += ' ';
            }
            key += part;
        }
    }
    return key.empty() ? std::string(output->name) : key;
}

std::optional<SavedOutput> loadSavedOutput(const wlr_output* output)
{
    ConfigFile file = readConfig();
    const Section* section = file.find(outputKey(output));
    if (!section) {
        return std::nullopt;
    }
    SavedOutput saved;
    auto value = [&](const char* key) -> std::string {
        auto it = section->find(key);
        return it == section->end() ? std::string() : it->second;
    };
    saved.enabled = value("attivo") != "no";
    if (const std::string mode = value("modo"); !mode.empty()) {
        double hz = 0.0;
        if (std::sscanf(mode.c_str(), "%dx%d@%lf", &saved.width, &saved.height, &hz) >= 2) {
            saved.refreshMhz = int(hz * 1000.0 + 0.5);
        }
    }
    if (const std::string scale = value("scala"); !scale.empty()) {
        saved.scale = std::strtof(scale.c_str(), nullptr);
    }
    saved.transform = transformFromName(value("rotazione"));
    if (const std::string position = value("posizione"); !position.empty()) {
        saved.hasPosition = std::sscanf(position.c_str(), "%d,%d", &saved.x, &saved.y) == 2;
    }
    return saved;
}

void saveOutputs(const std::vector<CurrentOutput>& outputs)
{
    const std::string path = configPath();
    if (path.empty()) {
        return;
    }
    ConfigFile file = readConfig();
    for (const CurrentOutput& current : outputs) {
        const wlr_output* output = current.output;
        const std::string key = outputKey(output);
        Section* section = file.find(key);
        if (!section) {
            file.sections.push_back({ key, {} });
            section = &file.sections.back().second;
        }
        Section& s = *section;
        s["attivo"] = current.enabled ? "si" : "no";
        if (current.enabled) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%dx%d@%.3f", output->width, output->height,
                output->refresh / 1000.0);
            s["modo"] = buffer;
            std::snprintf(buffer, sizeof(buffer), "%g", double(output->scale));
            s["scala"] = buffer;
            s["rotazione"] = transformName(output->transform);
            std::snprintf(buffer, sizeof(buffer), "%d,%d", current.x, current.y);
            s["posizione"] = buffer;
        }
    }

    const std::string dir = path.substr(0, path.rfind('/'));
    mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    const std::string temporary = path + ".nuovo";
    {
        std::ofstream out(temporary);
        out << "# Gli schermi di Vela, scritto da Vela quando cambi la configurazione.\n"
               "# Un monitor per sezione: [marca modello numero di serie].\n";
        for (const auto& [name, section] : file.sections) {
            out << "\n[" << name << "]\n";
            for (const char* key : { "attivo", "modo", "scala", "rotazione", "posizione" }) {
                if (auto it = section.find(key); it != section.end()) {
                    out << key << '=' << it->second << '\n';
                }
            }
        }
    }
    std::rename(temporary.c_str(), path.c_str());
}

wlr_output_mode* findMode(wlr_output* output, int width, int height, int refreshMhz)
{
    wlr_output_mode* best = nullptr;
    wlr_output_mode* mode;
    wl_list_for_each(mode, &output->modes, link)
    {
        if (mode->width != width || mode->height != height) {
            continue;
        }
        if (!best || std::abs(mode->refresh - refreshMhz) < std::abs(best->refresh - refreshMhz)) {
            best = mode;
        }
    }
    return best;
}

} // namespace vela
