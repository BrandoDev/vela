#include "icons.hpp"

#include "wlr.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#if VELA_HAVE_RSVG
#include <cairo.h>
#include <librsvg/rsvg.h>
#endif

namespace fs = std::filesystem;

namespace vela {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::stringstream stream(s);
    std::string item;
    while (std::getline(stream, item, sep)) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

// Le cartelle dati XDG, dalla più importante.
std::vector<std::string> dataDirs()
{
    std::vector<std::string> dirs;
    const char* home = std::getenv("HOME");
    const char* dataHome = std::getenv("XDG_DATA_HOME");
    if (dataHome && *dataHome) {
        dirs.push_back(dataHome);
    } else if (home) {
        dirs.push_back(std::string(home) + "/.local/share");
    }
    const char* dataDirs = std::getenv("XDG_DATA_DIRS");
    for (const std::string& dir : split(dataDirs && *dataDirs ? dataDirs : "/usr/local/share:/usr/share", ':')) {
        dirs.push_back(dir);
    }
    return dirs;
}

// Una sezione [nome] di un file .ini/.desktop: chiave -> valore.
std::map<std::string, std::string> readSection(const std::string& path, const std::string& section)
{
    std::map<std::string, std::string> values;
    std::ifstream file(path);
    std::string line;
    bool inside = false;
    while (std::getline(file, line)) {
        if (!line.empty() && line.front() == '[') {
            inside = line == "[" + section + "]";
        } else if (inside) {
            const size_t eq = line.find('=');
            if (eq != std::string::npos) {
                values[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }
    }
    return values;
}

} // namespace

IconLoader& IconLoader::instance()
{
    static IconLoader loader;
    return loader;
}

IconLoader::IconLoader()
{
    for (const std::string& dir : dataDirs()) {
        m_iconDirs.push_back(dir + "/icons");
    }
    // Il tema scelto in KDE ([Icons] Theme in kdeglobals), altrimenti Breeze.
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string kdeglobals = config && *config ? std::string(config) + "/kdeglobals"
        : home                                      ? std::string(home) + "/.config/kdeglobals"
                                                    : std::string();
    m_theme = readSection(kdeglobals, "Icons")["Theme"];
    if (m_theme.empty()) {
        m_theme = "breeze";
    }
    // La catena: il tema, quelli che eredita (Inherits), infine hicolor.
    std::vector<std::string> pending { m_theme };
    while (!pending.empty()) {
        const std::string theme = pending.front();
        pending.erase(pending.begin());
        if (std::find(m_themeChain.begin(), m_themeChain.end(), theme) != m_themeChain.end()) {
            continue;
        }
        m_themeChain.push_back(theme);
        for (const std::string& base : m_iconDirs) {
            const std::string index = base + "/" + theme + "/index.theme";
            if (fs::exists(index)) {
                for (const std::string& parent : split(readSection(index, "Icon Theme")["Inherits"], ',')) {
                    pending.push_back(parent);
                }
                break;
            }
        }
    }
    if (std::find(m_themeChain.begin(), m_themeChain.end(), "hicolor") == m_themeChain.end()) {
        m_themeChain.push_back("hicolor");
    }
}

void IconLoader::loadDesktopEntries()
{
    m_loaded = true;
    // Dalla cartella più importante: il primo che si trova vince.
    for (const std::string& dir : dataDirs()) {
        const std::string apps = dir + "/applications";
        std::error_code error;
        for (fs::recursive_directory_iterator it(apps, error), end; it != end && !error; it.increment(error)) {
            if (!it->is_regular_file() || it->path().extension() != ".desktop") {
                continue;
            }
            const auto entry = readSection(it->path().string(), "Desktop Entry");
            const auto icon = entry.find("Icon");
            if (icon == entry.end() || icon->second.empty()) {
                continue;
            }
            // L'id con le sottocartelle unite da '-' (specifica Desktop Entry).
            std::string id = fs::relative(it->path(), apps).replace_extension().string();
            std::replace(id.begin(), id.end(), '/', '-');
            m_iconById.emplace(lower(id), icon->second);
            const auto wmClass = entry.find("StartupWMClass");
            if (wmClass != entry.end() && !wmClass->second.empty()) {
                m_iconByClass.emplace(lower(wmClass->second), icon->second);
            }
        }
    }
}

std::string IconLoader::iconName(const std::string& appId)
{
    if (!m_loaded) {
        loadDesktopEntries();
    }
    const std::string id = lower(appId);
    // Dalla regola più sicura: il nome del .desktop è l'app_id, poi la
    // classe X11, poi l'ultima parte di un id a dominio inverso.
    if (auto it = m_iconById.find(id); it != m_iconById.end()) {
        return it->second;
    }
    if (auto it = m_iconByClass.find(id); it != m_iconByClass.end()) {
        return it->second;
    }
    for (const auto& [desktopId, icon] : m_iconById) {
        const size_t dot = desktopId.rfind('.');
        if (dot != std::string::npos && desktopId.substr(dot + 1) == id) {
            return icon;
        }
    }
    return appId; // spesso il tema ha un'icona col nome dell'app
}

std::string IconLoader::findFile(const std::string& name, int size)
{
    if (name.empty()) {
        return {};
    }
    if (name.front() == '/') {
        return fs::exists(name) ? name : std::string();
    }
    // Nel tema e in quelli che eredita: la cartella di dimensione più
    // vicina; a parità, l'SVG (si disegna esatto a qualunque misura).
    for (const std::string& theme : m_themeChain) {
        std::string best;
        int bestScore = INT_MAX;
        for (const std::string& base : m_iconDirs) {
            const std::string root = base + "/" + theme;
            const std::string index = root + "/index.theme";
            if (!fs::exists(index)) {
                continue;
            }
            for (const std::string& dir : split(readSection(index, "Icon Theme")["Directories"], ',')) {
                const auto info = readSection(index, dir);
                const int dirSize = info.count("Size") ? std::atoi(info.at("Size").c_str()) : 0;
                const std::string type = info.count("Type") ? info.at("Type") : "Threshold";
                for (const char* ext : { ".svg", ".png" }) {
                    const std::string path = root + "/" + dir + "/" + name + ext;
                    if (!fs::exists(path)) {
                        continue;
                    }
                    int score = type == "Scalable" ? 0 : std::abs(dirSize - size) * 2;
                    if (dirSize < size && type != "Scalable") {
                        score += 1000; // ingrandire una PNG piccola: ultima scelta
                    }
                    if (std::string(ext) == ".svg") {
                        score = std::max(0, score - 1);
                    } else {
                        score += 1;
                    }
                    if (score < bestScore) {
                        bestScore = score;
                        best = path;
                    }
                }
            }
        }
        if (!best.empty()) {
            return best;
        }
    }
    for (const char* ext : { ".svg", ".png", ".xpm" }) {
        const std::string pixmap = "/usr/share/pixmaps/" + name + ext;
        if (fs::exists(pixmap) && std::string(ext) != ".xpm") {
            return pixmap;
        }
    }
    return {};
}

IconLoader::Image IconLoader::render(const std::string& path, int size)
{
    Image image;
#if VELA_HAVE_RSVG
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surface);
    bool ok = false;
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".svg") == 0) {
        GError* error = nullptr;
        if (RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &error)) {
            const RsvgRectangle viewport { 0, 0, double(size), double(size) };
            ok = rsvg_handle_render_document(handle, cr, &viewport, &error);
            g_object_unref(handle);
        }
        if (error) {
            g_error_free(error);
        }
    } else {
        cairo_surface_t* png = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
            const int w = cairo_image_surface_get_width(png);
            const int h = cairo_image_surface_get_height(png);
            const double scale = double(size) / std::max(w, h);
            cairo_translate(cr, (size - w * scale) / 2.0, (size - h * scale) / 2.0);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, png, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint(cr);
            ok = true;
        }
        cairo_surface_destroy(png);
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    if (ok) {
        // Cairo ARGB32 è già ARGB8888 premoltiplicato, riga per riga.
        const int stride = cairo_image_surface_get_stride(surface);
        const unsigned char* data = cairo_image_surface_get_data(surface);
        image.width = size;
        image.height = size;
        image.pixels.resize(size_t(size) * size_t(size));
        for (int y = 0; y < size; ++y) {
            std::memcpy(&image.pixels[size_t(y) * size_t(size)], data + size_t(y) * size_t(stride), size_t(size) * 4);
        }
    }
    cairo_surface_destroy(surface);
#else
    (void)path;
    (void)size;
#endif
    return image;
}

const IconLoader::Image& IconLoader::appIcon(const std::string& appId, int size)
{
    const auto key = std::make_pair(appId, size);
    if (auto it = m_cache.find(key); it != m_cache.end()) {
        return it->second;
    }
    Image image;
    const std::string path = findFile(iconName(appId), size);
    if (!path.empty()) {
        image = render(path, size);
    }
    if (image.pixels.empty()) {
        wlr_log(WLR_DEBUG, "Icone: niente icona per %s", appId.c_str());
    }
    return m_cache.emplace(key, std::move(image)).first->second;
}

} // namespace vela
