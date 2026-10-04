#include "settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace vela {

namespace {

std::string configDir()
{
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    return config && *config ? std::string(config) + "/vela"
        : home               ? std::string(home) + "/.config/vela"
                             : std::string();
}

} // namespace

Settings readSettings()
{
    Settings settings;
    const std::string path = configDir().empty() ? std::string() : configDir() + "/vela.conf";
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (!line.empty() && line.front() != '#' && eq != std::string::npos) {
            settings[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    return settings;
}

std::string setting(const Settings& settings, const std::string& key, const std::string& fallback)
{
    auto it = settings.find(key);
    return it == settings.end() ? fallback : it->second;
}

bool settingFlag(const Settings& settings, const std::string& key, bool fallback)
{
    auto it = settings.find(key);
    if (it == settings.end() || it->second.empty()) {
        return fallback;
    }
    const std::string& v = it->second;
    return v == "sì" || v == "si" || v == "1" || v == "true" || v == "yes";
}

void writeSetting(const std::string& key, const std::string& value)
{
    const std::string dir = configDir();
    if (dir.empty()) {
        return;
    }
    mkdir(dir.c_str(), 0755);
    const std::string path = dir + "/vela.conf";
    std::vector<std::string> lines;
    bool found = false;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind(key + "=", 0) == 0) {
                if (found) {
                    continue; // doppioni: ne resta uno
                }
                line = key + "=" + value;
                found = true;
            }
            lines.push_back(line);
        }
    }
    if (lines.empty()) {
        lines.push_back("# Impostazioni di Vela (le scrive l'app Impostazioni)");
    }
    if (!found) {
        lines.push_back(key + "=" + value);
    }
    // Un file nuovo al posto del vecchio: chi legge non vede mai metà file.
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        for (const std::string& line : lines) {
            out << line << '\n';
        }
        if (!out) {
            return;
        }
    }
    std::rename(temporary.c_str(), path.c_str());
}

} // namespace vela
