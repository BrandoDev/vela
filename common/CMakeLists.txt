# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

# What every Vela app (shell, Settings, Files, lock screen) gets just by
# linking vela-app: the mouse's side buttons as the Back and Forward commands
# (navigationbuttons.cpp). An object library, so its startup function is
# always linked in. GuiPrivate for one QPA call (the same as the keyboard's
# Back key takes): after a Qt update the apps must be rebuilt.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Gui GuiPrivate)

add_library(vela-app OBJECT navigationbuttons.cpp)
target_compile_options(vela-app PRIVATE -Wall -Wextra)
target_link_libraries(vela-app PUBLIC Qt6::Gui PRIVATE Qt6::GuiPrivate)
