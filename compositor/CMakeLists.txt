find_package(PkgConfig REQUIRED)

# wlroots cambia API a ogni versione minore e installa un pkg-config con la
# versione nel nome. Vela è scritto per la 0.20 (quella di sway 1.12); con
# -DVELA_WLROOTS=wlroots-0.19 dovrebbe compilare anche con la 0.19.
set(VELA_WLROOTS "wlroots-0.20" CACHE STRING "Nome pkg-config di wlroots")

pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET ${VELA_WLROOTS})
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.32)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)

# Alcuni header di wlroots includono gli header "server" dei protocolli.
# Con wlroots 0.19 ogni compositor deve generarli da sé; la 0.20 ne installa
# alcuni, ma generarli qui (come fa sway) funziona con entrambe.
set(VELA_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${VELA_GENERATED_DIR})
set(VELA_PROTOCOL_HEADERS)

function(vela_server_protocol xml name)
    set(header ${VELA_GENERATED_DIR}/${name}-protocol.h)
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${WAYLAND_SCANNER} server-header ${xml} ${header}
        DEPENDS ${xml}
        COMMENT "Genero ${name}-protocol.h")
    list(APPEND VELA_PROTOCOL_HEADERS ${header})
    set(VELA_PROTOCOL_HEADERS ${VELA_PROTOCOL_HEADERS} PARENT_SCOPE)
endfunction()

vela_server_protocol(${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell)
vela_server_protocol(${WAYLAND_PROTOCOLS_DIR}/staging/cursor-shape/cursor-shape-v1.xml cursor-shape-v1)
vela_server_protocol(${PROJECT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
    wlr-layer-shell-unstable-v1)

add_executable(vela-compositor
    src/main.cpp
    src/server.cpp
    src/output.cpp
    src/toplevel.cpp
    src/layer.cpp
    src/keyboard.cpp
    src/snap.cpp
    src/snapshot.cpp
    ${VELA_PROTOCOL_HEADERS})

target_include_directories(vela-compositor PRIVATE ${VELA_GENERATED_DIR})
target_compile_definitions(vela-compositor PRIVATE WLR_USE_UNSTABLE)
target_compile_options(vela-compositor PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(vela-compositor PRIVATE
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND_SERVER
    PkgConfig::XKBCOMMON
    PkgConfig::PIXMAN
    PkgConfig::LIBINPUT)

install(TARGETS vela-compositor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
