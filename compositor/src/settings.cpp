#include "settings.hpp"

#include <cstdlib>
#include <fstream>

namespace vela {

Settings readSettings()
{
    Settings settings;
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string path = config && *config ? std::string(config) + "/vela/vela.conf"
        : home                                 ? std::string(home) + "/.config/vela/vela.conf"
                                               : std::string();
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

} // namespace vela
