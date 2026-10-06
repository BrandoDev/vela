// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "outputconfig.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sys/stat.h>

namespace vela {

namespace {

std::string configPath(const char* name = "outputs.conf")
{
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (config && *config) {
        return std::string(config) + "/vela/" + name;
    }
    return home ? std::string(home) + "/.config/vela/" + name : std::string();
}

// Fino a ottobre 2026 il file era schermi.conf, con chiavi e valori in
// italiano: si legge ancora, e alla prima scrittura diventa outputs.conf.
std::string legacyPath()
{
    return configPath("schermi.conf");
}

void modernizeLegacy(std::string& key, std::string& value)
{
    static const std::pair<const char*, const char*> keys[] = {
        { "attivo", "enabled" }, { "modo", "mode" }, { "scala", "scale" }, { "rotazione", "rotation" }, { "posizione", "position" },
    };
    for (const auto& [from, to] : keys) {
        if (key == from) {
            key = to;
        }
    }
    if (value == "si" || value == "sì") {
        value = "yes";
    } else if (value == "normale") {
        value = "normal";
    } else if (value.rfind("specchio", 0) == 0) {
        value = "flipped" + value.substr(8);
    }
}

const char* transformName(wl_output_transform transform)
{
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL: return "normal";
    case WL_OUTPUT_TRANSFORM_90: return "90";
    case WL_OUTPUT_TRANSFORM_180: return "180";
    case WL_OUTPUT_TRANSFORM_270: return "270";
    case WL_OUTPUT_TRANSFORM_FLIPPED: return "flipped";
    case WL_OUTPUT_TRANSFORM_FLIPPED_90: return "flipped-90";
    case WL_OUTPUT_TRANSFORM_FLIPPED_180: return "flipped-180";
    case WL_OUTPUT_TRANSFORM_FLIPPED_270: return "flipped-270";
    }
    return "normal";
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
    const bool legacy = !in.is_open();
    if (legacy) {
        in.open(legacyPath());
    }
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
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            if (legacy) {
                modernizeLegacy(key, value);
            }
            (*current)[key] = value;
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
    saved.enabled = value("enabled") != "no";
    if (const std::string mode = value("mode"); !mode.empty()) {
        double hz = 0.0;
        if (std::sscanf(mode.c_str(), "%dx%d@%lf", &saved.width, &saved.height, &hz) >= 2) {
            saved.refreshMhz = int(hz * 1000.0 + 0.5);
        }
    }
    if (const std::string scale = value("scale"); !scale.empty()) {
        saved.scale = std::strtof(scale.c_str(), nullptr);
    }
    saved.transform = transformFromName(value("rotation"));
    if (const std::string position = value("position"); !position.empty()) {
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
        s["enabled"] = current.enabled ? "yes" : "no";
        if (current.enabled) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%dx%d@%.3f", output->width, output->height,
                output->refresh / 1000.0);
            s["mode"] = buffer;
            std::snprintf(buffer, sizeof(buffer), "%g", double(output->scale));
            s["scale"] = buffer;
            s["rotation"] = transformName(output->transform);
            std::snprintf(buffer, sizeof(buffer), "%d,%d", current.x, current.y);
            s["position"] = buffer;
        }
    }

    const std::string dir = path.substr(0, path.rfind('/'));
    mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary);
        out << "# Vela's outputs, written by Vela when you change the configuration.\n"
               "# One monitor per section: [make model serial number].\n";
        for (const auto& [name, section] : file.sections) {
            out << "\n[" << name << "]\n";
            for (const char* key : { "enabled", "mode", "scale", "rotation", "position" }) {
                if (auto it = section.find(key); it != section.end()) {
                    out << key << '=' << it->second << '\n';
                }
            }
        }
    }
    if (std::rename(temporary.c_str(), path.c_str()) == 0) {
        std::remove(legacyPath().c_str()); // ora c'è outputs.conf
    }
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
