// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// ~/.config/vela/vela.conf (config.c): lettura, scrittura che conserva
// il resto, valori sì/no. In una cartella temporanea (XDG_CONFIG_HOME).

extern "C" {
#include "config.h"
}

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

class Settings : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::string pattern = (std::filesystem::temp_directory_path() / "vela-settings-test-XXXXXX").string();
        ASSERT_NE(mkdtemp(pattern.data()), nullptr);
        m_dir = pattern;
        setenv("XDG_CONFIG_HOME", m_dir.c_str(), 1);
    }
    void TearDown() override { std::filesystem::remove_all(m_dir); }

    std::string file() const { return m_dir + "/vela/vela.conf"; }
    std::string contents() const
    {
        std::ifstream in(file());
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }
    void write(const std::string& text) const
    {
        std::filesystem::create_directories(m_dir + "/vela");
        std::ofstream(file()) << text;
    }

    std::string m_dir;
};

// vela.conf letto adesso; si libera da solo.
struct Read {
    vela_config config;
    Read() { vela_config_read(&config); }
    ~Read() { vela_config_finish(&config); }
    const char* get(const char* key, const char* fallback = "") const
    {
        return vela_config_get(&config, key, fallback);
    }
    bool flag(const char* key, bool fallback) const { return vela_config_flag(&config, key, fallback); }
};

} // namespace

TEST_F(Settings, MissingFileIsEmpty)
{
    EXPECT_EQ(Read().config.count, 0);
    EXPECT_STREQ(Read().get("tearing", "yes"), "yes");
}

TEST_F(Settings, ReadIgnoresCommentsAndJunk)
{
    write("# comment\nnight-light=yes\nline without equals\nstrength=40\n=empty\n");
    const Read settings;
    EXPECT_STREQ(settings.get("night-light"), "yes");
    EXPECT_STREQ(settings.get("strength"), "40");
    EXPECT_EQ(settings.get("line without equals", nullptr), nullptr);
}

TEST_F(Settings, WriteCreatesUpdatesAndKeepsTheRest)
{
    vela_config_write("tearing", "no");
    EXPECT_STREQ(Read().get("tearing"), "no");
    EXPECT_EQ(contents().rfind('#', 0), 0u); // un file nuovo ha la riga di intestazione

    write("# my comment\nscreen-off=10\ntearing=yes\ntearing=duplicate\n");
    vela_config_write("tearing", "no");
    vela_config_write("variable-refresh", "always");
    EXPECT_EQ(contents(), "# my comment\nscreen-off=10\ntearing=no\nvariable-refresh=always\n");
    EXPECT_FALSE(std::filesystem::exists(file() + ".tmp")); // scritto con una rename
}

TEST_F(Settings, Flags)
{
    write("c=1\nd=true\ne=yes\nf=no\ng=0\nh=\n");
    const Read s;
    for (const char* key : { "c", "d", "e" }) {
        EXPECT_TRUE(s.flag(key, false)) << key;
    }
    EXPECT_FALSE(s.flag("f", true));
    EXPECT_FALSE(s.flag("g", true));
    EXPECT_TRUE(s.flag("h", true)); // vuota: il predefinito
    EXPECT_TRUE(s.flag("missing", true));
}

// I file scritti prima di ottobre 2026 hanno chiavi e valori in italiano.
TEST_F(Settings, LegacyItalianNames)
{
    write("# commento\nluce-notturna=sì\nluce-notturna-pianifica=tramonto\nfrequenza-variabile=giochi\n"
          "mouse-pulsante-principale=destro\nfiltro-colore=grigi\ntouchpad-tocco=si\nlingua=it\n");
    const Read s;
    EXPECT_TRUE(s.flag("night-light", false));
    EXPECT_STREQ(s.get("night-light-schedule"), "sunset");
    EXPECT_STREQ(s.get("variable-refresh"), "games");
    EXPECT_STREQ(s.get("mouse-primary-button"), "right");
    EXPECT_STREQ(s.get("color-filter"), "grayscale");
    EXPECT_TRUE(s.flag("touchpad-tap", false));
    EXPECT_STREQ(s.get("language"), "it");

    // All'avvio il file si riscrive con i nomi nuovi; i commenti restano.
    vela_config_migrate();
    EXPECT_EQ(contents(), "# commento\nnight-light=yes\nnight-light-schedule=sunset\nvariable-refresh=games\n"
                          "mouse-primary-button=right\ncolor-filter=grayscale\ntouchpad-tap=yes\nlanguage=it\n");
}
