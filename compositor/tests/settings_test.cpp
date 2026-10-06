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
    EXPECT_EQ(vela::setting(vela::readSettings(), "tearing", "sì"), "sì");
}

TEST_F(Settings, ReadIgnoresCommentsAndJunk)
{
    write("# commento\nluce-notturna=sì\nriga senza uguale\nintensità=40\n=vuota\n");
    const vela::Settings settings = vela::readSettings();
    EXPECT_EQ(vela::setting(settings, "luce-notturna"), "sì");
    EXPECT_EQ(vela::setting(settings, "intensità"), "40");
    EXPECT_EQ(settings.count("riga senza uguale"), 0u);
}

TEST_F(Settings, WriteCreatesUpdatesAndKeepsTheRest)
{
    vela::writeSetting("tearing", "no");
    EXPECT_EQ(vela::setting(vela::readSettings(), "tearing"), "no");
    EXPECT_EQ(contents().rfind('#', 0), 0u); // un file nuovo ha la riga di intestazione

    write("# mio commento\nspegni-schermo=10\ntearing=sì\ntearing=doppione\n");
    vela::writeSetting("tearing", "no");
    vela::writeSetting("frequenza-variabile", "sempre");
    EXPECT_EQ(contents(), "# mio commento\nspegni-schermo=10\ntearing=no\nfrequenza-variabile=sempre\n");
    EXPECT_FALSE(std::filesystem::exists(file() + ".tmp")); // scritto con una rename
}

TEST_F(Settings, Flags)
{
    write("a=sì\nb=si\nc=1\nd=true\ne=yes\nf=no\ng=0\nh=\n");
    const vela::Settings s = vela::readSettings();
    for (const char* key : { "a", "b", "c", "d", "e" }) {
        EXPECT_TRUE(vela::settingFlag(s, key, false)) << key;
    }
    EXPECT_FALSE(vela::settingFlag(s, "f", true));
    EXPECT_FALSE(vela::settingFlag(s, "g", true));
    EXPECT_TRUE(vela::settingFlag(s, "h", true)); // vuota: il predefinito
    EXPECT_TRUE(vela::settingFlag(s, "assente", true));
}
