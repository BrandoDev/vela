# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

# L'app Impostazioni di Vela: Qt Quick, come la shell, di cui riusa alcuni
# pezzi (le app installate, i programmi di sistema, lo stato del sistema).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS DBus Gui Network Qml Quick QuickControls2 Svg)

qt_standard_project_setup(REQUIRES 6.5)

set(SHELL_SRC ${PROJECT_SOURCE_DIR}/shell/src)

qt_add_executable(vela-settings
    src/main.cpp
    src/about.cpp
    src/about.h
    src/audio.cpp
    src/audio.h
    src/bluetooth.cpp
    src/bluetooth.h
    src/datetime.cpp
    src/datetime.h
    src/defaultapps.cpp
    src/defaultapps.h
    src/displays.cpp
    src/displays.h
    src/keyboardlayouts.cpp
    src/keyboardlayouts.h
    src/preferences.cpp
    src/preferences.h
    ${PROJECT_SOURCE_DIR}/compositor/src/legacy_names.c
    ${PROJECT_SOURCE_DIR}/compositor/src/legacy_names.h
    ${SHELL_SRC}/appmodel.cpp
    ${SHELL_SRC}/appmodel.h
    ${SHELL_SRC}/iconprovider.h
    ${SHELL_SRC}/mica.h
    ${SHELL_SRC}/network.cpp
    ${SHELL_SRC}/network.h
    ${SHELL_SRC}/systemactions.cpp
    ${SHELL_SRC}/systemactions.h
    ${SHELL_SRC}/systemstatus.cpp
    ${SHELL_SRC}/systemstatus.h)
target_include_directories(vela-settings PRIVATE ${SHELL_SRC} ${PROJECT_SOURCE_DIR}/compositor/src)

set(SETTINGS_QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/Card.qml
    qml/CardGroup.qml
    qml/LinkCard.qml
    qml/Toggle.qml
    qml/Choice.qml
    qml/Button.qml
    qml/Slider.qml
    qml/TextBox.qml
    qml/PageHeader.qml
    qml/Page.qml
    qml/Dialog.qml
    qml/pages/HomePage.qml
    qml/pages/SystemPage.qml
    qml/pages/DisplayPage.qml
    qml/pages/SoundPage.qml
    qml/pages/NotificationsPage.qml
    qml/pages/PowerPage.qml
    qml/pages/AboutPage.qml
    qml/pages/BluetoothPage.qml
    qml/pages/NetworkPage.qml
    qml/pages/PersonalizationPage.qml
    qml/pages/BackgroundPage.qml
    qml/pages/ColorsPage.qml
    qml/pages/TaskbarPage.qml
    qml/pages/AppsPage.qml
    qml/pages/InstalledAppsPage.qml
    qml/pages/DefaultAppsPage.qml
    qml/pages/DefaultAppPage.qml
    qml/pages/TimeLanguagePage.qml
    qml/pages/DateTimePage.qml
    qml/pages/KeyboardPage.qml
    qml/pages/NightLightPage.qml
    qml/pages/AccessibilityPage.qml
    qml/pages/MousePage.qml
    qml/pages/TouchpadPage.qml
    qml/pages/ClipboardPage.qml)

foreach(file IN LISTS SETTINGS_QML_FILES)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(vela-settings
    URI Vela.Settings
    VERSION 1.0
    QML_FILES ${SETTINGS_QML_FILES})

target_compile_options(vela-settings PRIVATE -Wall -Wextra)
target_compile_definitions(vela-settings PRIVATE VELA_VERSION="${PROJECT_VERSION}")

# Lo sfondo predefinito della shell, per mostrarlo e calcolarne il colore Mica.
qt_add_resources(vela-settings "images"
    PREFIX "/vela"
    BASE ${PROJECT_SOURCE_DIR}
    FILES ${PROJECT_SOURCE_DIR}/images/vela_splash_169.svg)
target_link_libraries(vela-settings PRIVATE
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg)

install(TARGETS vela-settings RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES vela-settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Traduzione italiana: le stringhe del codice sono in inglese, l'italiano sta
# in i18n/vela-settings_it.ts e finisce nell'eseguibile (:/i18n/vela-settings_it.qm, vedi
# shell/src/language.h). Dopo aver cambiato testi:
#   cmake --build build --target update_translations
find_package(Qt6 6.7 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(TARGETS vela-settings SOURCE_TARGETS vela-settings
    TS_FILES i18n/vela-settings_it.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_TARGET vela-settings_lupdate
    LRELEASE_TARGET vela-settings_lrelease
    LUPDATE_OPTIONS -source-language en -target-language it -no-obsolete -locations none)
