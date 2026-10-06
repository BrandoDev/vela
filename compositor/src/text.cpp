// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text.hpp"

#include "wlr.hpp"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace vela {

struct TextRenderer::Impl {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    hb_font_t* font = nullptr;
};

namespace {

// Il font scelto in KDE: [WM] activeFont, altrimenti [General] font, nel
// formato di Qt "famiglia,punti,..."; altrimenti quello predefinito di KDE.
void kdeFont(std::string& family, double& points)
{
    family = "Noto Sans";
    points = 10.0;
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string path = config && *config ? std::string(config) + "/kdeglobals"
        : home                                ? std::string(home) + "/.config/kdeglobals"
                                              : std::string();
    std::ifstream file(path);
    std::string line;
    std::string section;
    std::string wmFont;
    std::string generalFont;
    while (std::getline(file, line)) {
        if (!line.empty() && line.front() == '[') {
            section = line;
        } else if (section == "[WM]" && line.rfind("activeFont=", 0) == 0) {
            wmFont = line.substr(11);
        } else if (section == "[General]" && line.rfind("font=", 0) == 0) {
            generalFont = line.substr(5);
        }
    }
    const std::string chosen = !wmFont.empty() ? wmFont : generalFont;
    if (chosen.empty()) {
        return;
    }
    const size_t comma = chosen.find(',');
    family = chosen.substr(0, comma);
    if (comma != std::string::npos) {
        points = std::max(6.0, std::atof(chosen.c_str() + comma + 1));
    }
}

// Il file del font per una famiglia, come lo sceglierebbe il resto del
// sistema (fontconfig).
bool findFont(const std::string& family, std::string& file, int& index)
{
    if (!FcInit()) {
        return false;
    }
    FcPattern* pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR);
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    if (!match) {
        return false;
    }
    FcChar8* path = nullptr;
    const bool ok = FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch;
    if (ok) {
        file = reinterpret_cast<const char*>(path);
        index = 0;
        FcPatternGetInteger(match, FC_INDEX, 0, &index);
    }
    FcPatternDestroy(match);
    return ok;
}

struct Shaped {
    std::vector<hb_glyph_info_t> infos;
    std::vector<hb_glyph_position_t> positions;
    int64_t width = 0; // 26.6
};

Shaped shape(hb_font_t* font, const std::string& text)
{
    Shaped out;
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.c_str(), int(text.size()), 0, int(text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, nullptr, 0);
    unsigned count = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    out.infos.assign(infos, infos + count);
    out.positions.assign(positions, positions + count);
    for (const hb_glyph_position_t& p : out.positions) {
        out.width += p.x_advance;
    }
    hb_buffer_destroy(buffer);
    return out;
}

} // namespace

TextRenderer* TextRenderer::instance()
{
    static std::unique_ptr<TextRenderer> renderer = [] {
        std::unique_ptr<TextRenderer> r(new TextRenderer);
        if (!r->load()) {
            wlr_log(WLR_ERROR, "Testo: nessun font utilizzabile, i titoli non avranno testo");
            r.reset();
        }
        return r;
    }();
    return renderer.get();
}

TextRenderer::~TextRenderer()
{
    if (m_impl) {
        if (m_impl->font) {
            hb_font_destroy(m_impl->font);
        }
        if (m_impl->face) {
            FT_Done_Face(m_impl->face);
        }
        if (m_impl->library) {
            FT_Done_FreeType(m_impl->library);
        }
    }
}

bool TextRenderer::load()
{
    std::string family;
    double points = 10.0;
    kdeFont(family, points);
    m_pixelSize = points * 96.0 / 72.0;

    std::string file;
    int index = 0;
    if (!findFont(family, file, index)) {
        return false;
    }
    m_impl = std::make_unique<Impl>();
    if (FT_Init_FreeType(&m_impl->library) != 0
        || FT_New_Face(m_impl->library, file.c_str(), index, &m_impl->face) != 0) {
        return false;
    }
    m_impl->font = hb_ft_font_create_referenced(m_impl->face);
    wlr_log(WLR_INFO, "Testo: %s %.1f pt (%s)", family.c_str(), points, file.c_str());
    return true;
}

TextRenderer::Image TextRenderer::render(const std::string& utf8, double pixelSize, uint32_t color, int maxWidth)
{
    Image image;
    FT_Face face = m_impl->face;
    FT_Set_Char_Size(face, 0, FT_F26Dot6(std::lround(pixelSize * 64.0)), 72, 72);
    hb_ft_font_changed(m_impl->font);

    Shaped text = shape(m_impl->font, utf8);
    // Troppo lungo: si taglia e si aggiunge "…".
    if (maxWidth > 0 && text.width > int64_t(maxWidth) * 64) {
        const Shaped ellipsis = shape(m_impl->font, "…");
        int64_t width = 0;
        size_t keep = 0;
        while (keep < text.infos.size()
            && width + text.positions[keep].x_advance + ellipsis.width <= int64_t(maxWidth) * 64) {
            width += text.positions[keep].x_advance;
            ++keep;
        }
        text.infos.resize(keep);
        text.positions.resize(keep);
        text.infos.insert(text.infos.end(), ellipsis.infos.begin(), ellipsis.infos.end());
        text.positions.insert(text.positions.end(), ellipsis.positions.begin(), ellipsis.positions.end());
        text.width = width + ellipsis.width;
    }

    const FT_Size_Metrics& metrics = face->size->metrics;
    const int ascender = int(std::ceil(metrics.ascender / 64.0));
    const int descender = int(std::floor(metrics.descender / 64.0));
    image.width = std::max(1, int(std::ceil(text.width / 64.0)) + 1);
    image.height = std::max(1, ascender - descender);
    image.pixels.assign(size_t(image.width) * size_t(image.height), 0);

    const double alpha = ((color >> 24) & 0xff) / 255.0;
    const double red = ((color >> 16) & 0xff) / 255.0;
    const double green = ((color >> 8) & 0xff) / 255.0;
    const double blue = (color & 0xff) / 255.0;

    int64_t pen = 0;
    for (size_t i = 0; i < text.infos.size(); ++i) {
        const hb_glyph_position_t& position = text.positions[i];
        if (FT_Load_Glyph(face, text.infos[i].codepoint, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) == 0) {
            const FT_GlyphSlot slot = face->glyph;
            const FT_Bitmap& bitmap = slot->bitmap;
            const int originX = int((pen + position.x_offset + 32) >> 6) + slot->bitmap_left;
            const int originY = ascender - int((position.y_offset + 32) >> 6) - slot->bitmap_top;
            for (unsigned row = 0; row < bitmap.rows; ++row) {
                const int y = originY + int(row);
                if (y < 0 || y >= image.height) {
                    continue;
                }
                for (unsigned col = 0; col < bitmap.width; ++col) {
                    const int x = originX + int(col);
                    if (x < 0 || x >= image.width) {
                        continue;
                    }
                    const double coverage = bitmap.buffer[row * unsigned(bitmap.pitch) + col] / 255.0 * alpha;
                    if (coverage <= 0.0) {
                        continue;
                    }
                    uint32_t& dst = image.pixels[size_t(y) * size_t(image.width) + size_t(x)];
                    // "Sopra" in premoltiplicato: i glifi vicini possono toccarsi.
                    const double keep = 1.0 - coverage;
                    auto channel = [&](int shift, double value) {
                        const double old = ((dst >> shift) & 0xff) / 255.0;
                        return uint32_t(std::lround(std::min(1.0, value * coverage + old * keep) * 255.0)) << shift;
                    };
                    dst = channel(24, 1.0) | channel(16, red) | channel(8, green) | channel(0, blue);
                }
            }
        }
        pen += position.x_advance;
    }
    return image;
}

} // namespace vela
