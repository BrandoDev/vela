// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Le icone delle app nella barra del titolo (docs/renderer.md §9.6): dal
// file .desktop dell'app (Icon=) al tema di icone di KDE (specifica
// freedesktop), disegnate alla dimensione fisica esatta. Gli SVG con
// librsvg, preferiti sempre; le PNG solo se non c'è altro, ridotte con un
// filtro di qualità. Senza librsvg (opzionale) niente icone.

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vela {

class IconLoader {
public:
    static IconLoader& instance();

    struct Image {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels; // ARGB8888 premoltiplicato
    };
    // L'icona dell'app (app_id Wayland o classe X11), size x size pixel.
    // Vuota se non si trova.
    const Image& appIcon(const std::string& appId, int size);

private:
    IconLoader();
    void loadDesktopEntries();
    std::string iconName(const std::string& appId);
    std::string findFile(const std::string& name, int size);
    static Image render(const std::string& path, int size);

    std::string m_theme;
    std::vector<std::string> m_themeChain; // il tema, quelli che eredita, hicolor
    std::vector<std::string> m_iconDirs; // .../icons
    std::map<std::string, std::string> m_iconById; // id del .desktop (senza estensione) -> Icon
    std::map<std::string, std::string> m_iconByClass; // StartupWMClass -> Icon
    std::map<std::pair<std::string, int>, Image> m_cache;
    bool m_loaded = false;
};

} // namespace vela
