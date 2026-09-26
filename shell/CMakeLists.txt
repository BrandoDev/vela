# GuiPrivate serve per un'unica API privata (la wl_surface della taskbar, vedi
# foreigntoplevels.cpp): dopo un aggiornamento di Qt la shell va ricompilata.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Gui GuiPrivate Qml Quick Svg WaylandClient)
find_package(LayerShellQt REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(vela-shell
    src/main.cpp
    src/appmodel.cpp
    src/appmodel.h
    src/foreigntoplevels.cpp
    src/foreigntoplevels.h
    src/iconprovider.h
    src/shellcontroller.cpp
    src/shellcontroller.h
    src/taskbarmodel.cpp
    src/taskbarmodel.h
    src/wallpaperprovider.h)

# Lo sfondo predefinito, dentro l'eseguibile: ":/vela/images/vela_splash.svg".
qt_add_resources(vela-shell "images"
    PREFIX "/vela"
    BASE ${PROJECT_SOURCE_DIR}
    FILES ${PROJECT_SOURCE_DIR}/images/vela_splash.svg)

# Elenco delle finestre aperte, pubblicato dal compositor.
qt_generate_wayland_protocol_client_sources(vela-shell
    FILES ${PROJECT_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml)

set(VELA_QML_FILES
    qml/Theme.qml
    qml/Taskbar.qml
    qml/TaskbarButton.qml
    qml/StartGlyph.qml
    qml/StartMenu.qml
    qml/AppTile.qml
    qml/Wallpaper.qml)

# I file stanno in qml/ per ordine, ma nel modulo devono stare alla radice:
# così si vedono tra loro (Theme compreso, che è un singleton) senza import.
foreach(file IN LISTS VELA_QML_FILES)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(vela-shell
    URI Vela.Shell
    VERSION 1.0
    QML_FILES ${VELA_QML_FILES})

target_compile_options(vela-shell PRIVATE -Wall -Wextra)
target_link_libraries(vela-shell PRIVATE
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg
    Qt6::WaylandClient
    Qt6::GuiPrivate # wl_surface della taskbar (vedi foreigntoplevels.cpp)
    LayerShellQt::Interface)

install(TARGETS vela-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
