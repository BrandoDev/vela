// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// ~/.config/vela/vela.conf (settings.cpp): lettura, scrittura che conserva
// il resto, valori sì/no. In una cartella temporanea (XDG_CONFIG_HOME).

#include "settings.hpp"

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

} // namespace

TEST_F(Settings, MissingFileIsEmpty)
{
    EXPECT_TRUE(vela::readSettings().empty());
    EXPECT_EQ(vela::setting(vela::readSettings(), "tearing", "yes"), "yes");
}

TEST_F(Settings, ReadIgnoresCommentsAndJunk)
{
    write("# comment\nnight-light=yes\nline without equals\nstrength=40\n=empty\n");
    const vela::Settings settings = vela::readSettings();
    EXPECT_EQ(vela::setting(settings, "night-light"), "yes");
    EXPECT_EQ(vela::setting(settings, "strength"), "40");
    EXPECT_EQ(settings.count("line without equals"), 0u);
}

TEST_F(Settings, WriteCreatesUpdatesAndKeepsTheRest)
{
    vela::writeSetting("tearing", "no");
    EXPECT_EQ(vela::setting(vela::readSettings(), "tearing"), "no");
    EXPECT_EQ(contents().rfind('#', 0), 0u); // un file nuovo ha la riga di intestazione

    write("# my comment\nscreen-off=10\ntearing=yes\ntearing=duplicate\n");
    vela::writeSetting("tearing", "no");
    vela::writeSetting("variable-refresh", "always");
    EXPECT_EQ(contents(), "# my comment\nscreen-off=10\ntearing=no\nvariable-refresh=always\n");
    EXPECT_FALSE(std::filesystem::exists(file() + ".tmp")); // scritto con una rename
}

TEST_F(Settings, Flags)
{
    write("c=1\nd=true\ne=yes\nf=no\ng=0\nh=\n");
    const vela::Settings s = vela::readSettings();
    for (const char* key : { "c", "d", "e" }) {
        EXPECT_TRUE(vela::settingFlag(s, key, false)) << key;
    }
    EXPECT_FALSE(vela::settingFlag(s, "f", true));
    EXPECT_FALSE(vela::settingFlag(s, "g", true));
    EXPECT_TRUE(vela::settingFlag(s, "h", true)); // vuota: il predefinito
    EXPECT_TRUE(vela::settingFlag(s, "missing", true));
}

// I file scritti prima di ottobre 2026 hanno chiavi e valori in italiano.
TEST_F(Settings, LegacyItalianNames)
{
    write("# commento\nluce-notturna=sì\nluce-notturna-pianifica=tramonto\nfrequenza-variabile=giochi\n"
          "mouse-pulsante-principale=destro\nfiltro-colore=grigi\ntouchpad-tocco=si\nlingua=it\n");
    const vela::Settings s = vela::readSettings();
    EXPECT_TRUE(vela::settingFlag(s, "night-light", false));
    EXPECT_EQ(vela::setting(s, "night-light-schedule"), "sunset");
    EXPECT_EQ(vela::setting(s, "variable-refresh"), "games");
    EXPECT_EQ(vela::setting(s, "mouse-primary-button"), "right");
    EXPECT_EQ(vela::setting(s, "color-filter"), "grayscale");
    EXPECT_TRUE(vela::settingFlag(s, "touchpad-tap", false));
    EXPECT_EQ(vela::setting(s, "language"), "it");

    // All'avvio il file si riscrive con i nomi nuovi; i commenti restano.
    vela::migrateSettings();
    EXPECT_EQ(contents(), "# commento\nnight-light=yes\nnight-light-schedule=sunset\nvariable-refresh=games\n"
                          "mouse-primary-button=right\ncolor-filter=grayscale\ntouchpad-tap=yes\nlanguage=it\n");
}
