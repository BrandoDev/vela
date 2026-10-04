# GuiPrivate serve per un'unica API privata (la wl_surface della taskbar, vedi
# foreigntoplevels.cpp): dopo un aggiornamento di Qt la shell va ricompilata.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS DBus Gui GuiPrivate Qml Quick Svg WaylandClient)
find_package(LayerShellQt REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(vela-shell
    src/main.cpp
    src/appmodel.cpp
    src/appmodel.h
    src/backgroundeffects.cpp
    src/backgroundeffects.h
    src/filethumbnails.cpp
    src/filethumbnails.h
    src/fileproperties.cpp
    src/fileproperties.h
    src/desktopmodel.cpp
    src/desktopmodel.h
    src/foreigntoplevels.cpp
    src/foreigntoplevels.h
    src/iconprovider.h
    src/jumplists.cpp
    src/jumplists.h
    src/notifications.cpp
    src/shellcontroller.cpp
    src/shellcontroller.h
    src/servicemenus.cpp
    src/servicemenus.h
    src/systemactions.cpp
    src/systemactions.h
    src/taskbarmodel.cpp
    src/tray.cpp
    src/taskbarmodel.h
    src/wallpaperprovider.h
    src/wallpapers.cpp
    src/wallpapers.h
    src/windowcapture.cpp
    src/windowcapture.h)

# Lo sfondo predefinito, dentro l'eseguibile: ":/vela/images/vela_splash_169.svg".
qt_add_resources(vela-shell "images"
    PREFIX "/vela"
    BASE ${PROJECT_SOURCE_DIR}
    FILES ${PROJECT_SOURCE_DIR}/images/vela_splash_169.svg)

# Elenco delle finestre aperte, pubblicato dal compositor.
qt_generate_wayland_protocol_client_sources(vela-shell
    FILES ${PROJECT_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml)

# Anteprime delle finestre (Alt+Tab): protocolli ext-*, usati con libwayland
# direttamente (vedi windowcapture.cpp).
find_package(PkgConfig REQUIRED)
pkg_check_modules(SHELL_WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.37)
pkg_get_variable(SHELL_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_check_modules(SHELL_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)
set(SHELL_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${SHELL_GENERATED_DIR})
foreach(name
        ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1
        ext-image-capture-source/ext-image-capture-source-v1
        ext-image-copy-capture/ext-image-copy-capture-v1
        ext-background-effect/ext-background-effect-v1)
    get_filename_component(base ${name} NAME)
    set(xml ${SHELL_WAYLAND_PROTOCOLS_DIR}/staging/${name}.xml)
    set(header ${SHELL_GENERATED_DIR}/${base}-client-protocol.h)
    set(code ${SHELL_GENERATED_DIR}/${base}-protocol.c)
    add_custom_command(
        OUTPUT ${header} ${code}
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header}
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code}
        DEPENDS ${xml}
        COMMENT "Genero il client di ${base}")
    target_sources(vela-shell PRIVATE ${header} ${code})
endforeach()
target_include_directories(vela-shell PRIVATE ${SHELL_GENERATED_DIR})

set(VELA_QML_FILES
    qml/Theme.qml
    qml/Taskbar.qml
    qml/TaskbarButton.qml
    qml/StartGlyph.qml
    qml/StartMenu.qml
    qml/AppTile.qml
    qml/Switcher.qml
    qml/Wallpaper.qml
    qml/NotificationPopups.qml
    qml/Menus.qml
    qml/ContextMenu.qml
    qml/MenuPanel.qml
    qml/MenuTextField.qml
    qml/RunDialog.qml
    qml/ConfirmDialog.qml
    qml/PropertiesDialog.qml
    qml/PanelShadow.qml)

# I file stanno in qml/ per ordine, ma nel modulo devono stare alla radice:
# così si vedono tra loro (Theme compreso, che è un singleton) senza import.
foreach(file IN LISTS VELA_QML_FILES)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
set_source_files_properties(qml/Theme.qml qml/Menus.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(vela-shell
    URI Vela.Shell
    VERSION 1.0
    QML_FILES ${VELA_QML_FILES})

target_compile_options(vela-shell PRIVATE -Wall -Wextra)
target_link_libraries(vela-shell PRIVATE
    Qt6::DBus
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg
    Qt6::WaylandClient
    PkgConfig::SHELL_WAYLAND_CLIENT
    Qt6::GuiPrivate # wl_surface della taskbar (vedi foreigntoplevels.cpp)
    LayerShellQt::Interface)

install(TARGETS vela-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
