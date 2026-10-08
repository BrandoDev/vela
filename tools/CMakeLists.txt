# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

# Strumenti per provare Vela senza toccarlo con le mani: catture dello
# schermo, elenco/comando delle finestre, mouse e tastiera simulati.
# Parlano solo protocolli Wayland standard.

find_package(PkgConfig REQUIRED)
pkg_check_modules(TOOLS_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(TOOLS_XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(TOOLS_ZLIB REQUIRED IMPORTED_TARGET zlib)
pkg_check_modules(TOOLS_WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_get_variable(TOOLS_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER NAMES wayland-scanner REQUIRED)

set(TOOLS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${TOOLS_GENERATED_DIR})

# Header e codice client di un protocollo, in una libreria statica.
function(vela_client_protocol target xml name)
    set(header ${TOOLS_GENERATED_DIR}/${name}-client-protocol.h)
    set(code ${TOOLS_GENERATED_DIR}/${name}-protocol.c)
    add_custom_command(
        OUTPUT ${header} ${code}
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header}
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code}
        DEPENDS ${xml}
        COMMENT "Genero il client di ${name}")
    add_library(${target} STATIC ${code} ${header})
    target_include_directories(${target} PUBLIC ${TOOLS_GENERATED_DIR})
    target_link_libraries(${target} PUBLIC PkgConfig::TOOLS_WAYLAND_CLIENT)
endfunction()

set(PROTOCOLS ${PROJECT_SOURCE_DIR}/protocols)
vela_client_protocol(proto-screencopy ${PROTOCOLS}/wlr-screencopy-unstable-v1.xml
    wlr-screencopy-unstable-v1)
vela_client_protocol(proto-foreign-toplevel ${PROTOCOLS}/wlr-foreign-toplevel-management-unstable-v1.xml
    wlr-foreign-toplevel-management-unstable-v1)
vela_client_protocol(proto-virtual-pointer ${PROTOCOLS}/wlr-virtual-pointer-unstable-v1.xml
    wlr-virtual-pointer-unstable-v1)
vela_client_protocol(proto-virtual-keyboard ${PROTOCOLS}/virtual-keyboard-unstable-v1.xml
    virtual-keyboard-unstable-v1)
vela_client_protocol(proto-xdg-output
    ${TOOLS_WAYLAND_PROTOCOLS_DIR}/unstable/xdg-output/xdg-output-unstable-v1.xml
    xdg-output-unstable-v1)

vela_client_protocol(proto-xdg-shell ${TOOLS_WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell)
vela_client_protocol(proto-viewporter ${TOOLS_WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml viewporter)
vela_client_protocol(proto-fractional-scale
    ${TOOLS_WAYLAND_PROTOCOLS_DIR}/staging/fractional-scale/fractional-scale-v1.xml fractional-scale-v1)
vela_client_protocol(proto-xdg-decoration
    ${TOOLS_WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
    xdg-decoration-unstable-v1)

add_executable(vela-shot vela-shot.c)
target_link_libraries(vela-shot PRIVATE proto-screencopy PkgConfig::TOOLS_ZLIB)

add_executable(vela-windows vela-windows.c)
target_link_libraries(vela-windows PRIVATE proto-foreign-toplevel)

add_executable(vela-input vela-input.c)
target_link_libraries(vela-input PRIVATE
    proto-virtual-pointer proto-virtual-keyboard proto-xdg-output PkgConfig::TOOLS_XKBCOMMON)

vela_client_protocol(proto-session-lock
    ${TOOLS_WAYLAND_PROTOCOLS_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml ext-session-lock-v1)

# Programma di blocco di prova: blocca e si sblocca da solo, senza password.
add_executable(vela-testlock vela-testlock.c)
target_link_libraries(vela-testlock PRIVATE proto-session-lock)

# Client di prova della nitidezza (scripts/test-sharpness.sh).
add_executable(vela-pattern vela-pattern.c)
target_link_libraries(vela-pattern PRIVATE proto-xdg-shell proto-viewporter proto-fractional-scale
    proto-xdg-decoration)

# Pezzi di shell finti (sfondo, taskbar sfocata) per le prove senza la shell Qt.
vela_client_protocol(proto-layer-shell ${PROTOCOLS}/wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1)
vela_client_protocol(proto-background-effect
    ${TOOLS_WAYLAND_PROTOCOLS_DIR}/staging/ext-background-effect/ext-background-effect-v1.xml ext-background-effect-v1)
add_executable(vela-panel vela-panel.c)
target_link_libraries(vela-panel PRIVATE proto-layer-shell proto-background-effect proto-xdg-shell)

# Configurazione degli schermi come Impostazioni > Schermo, senza wlr-randr.
vela_client_protocol(proto-output-management ${PROTOCOLS}/wlr-output-management-unstable-v1.xml
    wlr-output-management-unstable-v1)
add_executable(vela-randr vela-randr.c)
target_link_libraries(vela-randr PRIVATE proto-output-management)

# Un'app X11 (attraverso Xwayland) per le prove delle finestre X11.
pkg_check_modules(TOOLS_XCB IMPORTED_TARGET xcb)
if(TOOLS_XCB_FOUND)
    add_executable(vela-x11 vela-x11.c)
    target_link_libraries(vela-x11 PRIVATE PkgConfig::TOOLS_XCB)
    target_compile_options(vela-x11 PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Un'app con la GPU lenta (tests/functional/test_ready.py): un lavoro di
# calcolo Vulkan fa da rendering e la sua fence arriva tardi.
pkg_check_modules(TOOLS_VULKAN REQUIRED IMPORTED_TARGET vulkan)
pkg_check_modules(TOOLS_GBM REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(TOOLS_DRM REQUIRED IMPORTED_TARGET libdrm)
find_program(TOOLS_GLSLC NAMES glslc REQUIRED)
vela_client_protocol(proto-linux-dmabuf
    ${TOOLS_WAYLAND_PROTOCOLS_DIR}/stable/linux-dmabuf/linux-dmabuf-v1.xml linux-dmabuf-v1)
vela_client_protocol(proto-linux-drm-syncobj
    ${TOOLS_WAYLAND_PROTOCOLS_DIR}/staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml linux-drm-syncobj-v1)
set(TOOLS_SHADER ${CMAKE_CURRENT_BINARY_DIR}/shaders/slow.comp.spv.inc)
add_custom_command(
    OUTPUT ${TOOLS_SHADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMAND ${TOOLS_GLSLC} --target-env=vulkan1.3 -O -mfmt=num -o ${TOOLS_SHADER}
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/slow.comp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/slow.comp
    COMMENT "Compilo lo shader di vela-slowgpu")
add_executable(vela-slowgpu vela-slowgpu.c ${TOOLS_SHADER})
target_include_directories(vela-slowgpu PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders)
target_link_libraries(vela-slowgpu PRIVATE proto-xdg-shell proto-linux-dmabuf proto-linux-drm-syncobj
    PkgConfig::TOOLS_VULKAN PkgConfig::TOOLS_GBM PkgConfig::TOOLS_DRM)

foreach(tool vela-shot vela-windows vela-input vela-pattern vela-testlock vela-slowgpu vela-panel vela-randr)
    target_compile_options(${tool} PRIVATE -Wall -Wextra -Wno-unused-parameter)
endforeach()
