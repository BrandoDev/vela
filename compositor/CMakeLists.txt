# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(PkgConfig REQUIRED)

# wlroots cambia API a ogni versione minore e installa un pkg-config con la
# versione nel nome. Vela è scritto per la 0.20 (quella di sway 1.12); con
# -DVELA_WLROOTS=wlroots-0.19 dovrebbe compilare anche con la 0.19.
set(VELA_WLROOTS "wlroots-0.20" CACHE STRING "pkg-config name of wlroots")

pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET ${VELA_WLROOTS})
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
# Renderer di Vela (docs/renderer.md): Vulkan, buffer degli schermi con GBM.
pkg_check_modules(VULKAN REQUIRED IMPORTED_TARGET vulkan>=1.4)
pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
# Il testo del compositor (titoli delle finestre): font di KDE via fontconfig.
pkg_check_modules(FONTS REQUIRED IMPORTED_TARGET freetype2 harfbuzz fontconfig)
# Le icone delle app nella barra del titolo (SVG): facoltativo, senza niente icone.
pkg_check_modules(RSVG IMPORTED_TARGET librsvg-2.0 cairo)
find_program(GLSLC NAMES glslc REQUIRED)
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

# Protocolli che wlroots non implementa: li implementiamo noi, quindi serve
# anche il codice delle interfacce.
set(VELA_OWN_PROTOCOL_SOURCES)
function(vela_own_protocol xml name)
    set(header ${VELA_GENERATED_DIR}/${name}-protocol.h)
    set(code ${VELA_GENERATED_DIR}/${name}-protocol.c)
    add_custom_command(
        OUTPUT ${header} ${code}
        COMMAND ${WAYLAND_SCANNER} server-header ${xml} ${header}
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code}
        DEPENDS ${xml}
        COMMENT "Genero ${name} (server)")
    list(APPEND VELA_OWN_PROTOCOL_SOURCES ${header} ${code})
    set(VELA_OWN_PROTOCOL_SOURCES ${VELA_OWN_PROTOCOL_SOURCES} PARENT_SCOPE)
endfunction()

vela_own_protocol(${WAYLAND_PROTOCOLS_DIR}/staging/ext-background-effect/ext-background-effect-v1.xml
    ext-background-effect-v1)

# Lato client, per parlare con il compositor ospite quando Vela gira in una
# finestra (nested.c).
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
set(VELA_CLIENT_PROTOCOL_SOURCES)
function(vela_client_protocol xml name)
    set(header ${VELA_GENERATED_DIR}/${name}-client-protocol.h)
    set(code ${VELA_GENERATED_DIR}/${name}-protocol.c)
    add_custom_command(
        OUTPUT ${header} ${code}
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header}
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code}
        DEPENDS ${xml}
        COMMENT "Genero ${name} (client)")
    list(APPEND VELA_CLIENT_PROTOCOL_SOURCES ${header} ${code})
    set(VELA_CLIENT_PROTOCOL_SOURCES ${VELA_CLIENT_PROTOCOL_SOURCES} PARENT_SCOPE)
endfunction()

vela_client_protocol(${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml viewporter)
vela_client_protocol(${WAYLAND_PROTOCOLS_DIR}/staging/fractional-scale/fractional-scale-v1.xml fractional-scale-v1)
vela_client_protocol(${WAYLAND_PROTOCOLS_DIR}/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml
    keyboard-shortcuts-inhibit-unstable-v1)

# Shader: GLSL -> SPIR-V durante la build, inclusi nell'eseguibile come
# elenco di numeri ("nome.spv.inc", da mettere dentro un array uint32_t).
set(VELA_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${VELA_SHADER_DIR})
set(VELA_SHADERS)
foreach(shader quad.vert texture.frag rect.frag shadow.frag blur_down.frag blur_up.frag blur_mix.frag)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/${shader})
    set(output ${VELA_SHADER_DIR}/${shader}.spv.inc)
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${GLSLC} --target-env=vulkan1.4 -O -mfmt=num -o ${output} ${source}
        DEPENDS ${source} ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/push.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/shape.glsl
        COMMENT "Compilo lo shader ${shader}")
    list(APPEND VELA_SHADERS ${output})
endforeach()

# I moduli senza wlroots né GPU (logica pura, provata dai test): il
# compositor e le prove li condividono.
add_library(vela-pure STATIC
    src/buffer.c
    src/color.c
    src/config.c
    src/frame_clock.c
    src/geometry.c
    src/legacy_names.c
    src/motion.c
    src/output_config.c
    src/sun.c
    src/util.c)
target_include_directories(vela-pure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(vela-pure PUBLIC _GNU_SOURCE)
target_compile_options(vela-pure PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_target_properties(vela-pure PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(vela-pure PUBLIC ${MATH_LIBRARY})
endif()

add_executable(vela-compositor
    src/a11y.c
    src/bindings.c
    src/command.c
    src/decoration.c
    src/focus.c
    src/icons.c
    src/input.c
    src/interact.c
    src/keyboard.c
    src/layer.c
    src/lock.c
    src/main.c
    src/nested.c
    src/output.c
    src/output_manager.c
    src/popup.c
    src/process.c
    src/server.c
    src/session.c
    src/shell.c
    src/snap.c
    src/snapshot.c
    src/supervisor.c
    src/switcher.c
    src/text.c
    src/view.c
    src/workspace.c
    src/xwayland.c
    src/render/allocator.c
    src/render/formats.c
    src/render/pass.c
    src/render/pixel_buffer.c
    src/render/renderer.c
    src/render/texture.c
    src/render/vulkan.c
    src/scene/capture.c
    src/scene/effects.c
    src/scene/frame.c
    src/scene/ready.c
    src/scene/scene.c
    src/scene/surface.c
    ${VELA_SHADERS}
    ${VELA_PROTOCOL_HEADERS}
    ${VELA_CLIENT_PROTOCOL_SOURCES}
    ${VELA_OWN_PROTOCOL_SOURCES})

target_include_directories(vela-compositor PRIVATE
    ${VELA_GENERATED_DIR} ${VELA_SHADER_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(vela-compositor PRIVATE WLR_USE_UNSTABLE
    VELA_LIBEXECDIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}")
# Con Vulkan si inizializzano le strutture indicando solo i campi che
# servono (gli altri valgono zero): l'avviso sui campi omessi sarebbe rumore.
target_compile_options(vela-compositor PRIVATE -Wall -Wextra -Wno-unused-parameter
    -Wno-missing-field-initializers)
set_target_properties(vela-compositor PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(vela-compositor PRIVATE
    vela-pure
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND_SERVER
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::XKBCOMMON
    PkgConfig::PIXMAN
    PkgConfig::LIBINPUT
    PkgConfig::VULKAN
    PkgConfig::GBM
    PkgConfig::LIBDRM
    PkgConfig::FONTS)
if(RSVG_FOUND)
    target_link_libraries(vela-compositor PRIVATE PkgConfig::RSVG)
    target_compile_definitions(vela-compositor PRIVATE VELA_HAVE_RSVG=1)
else()
    message(STATUS "librsvg not found: the title bar will have no icons")
endif()

install(TARGETS vela-compositor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Le prove del compositor (ctest), senza GPU né wlroots: il tempo dei frame,
# le curve, la geometria della nitidezza, il colore, il sole, le impostazioni.
# Servono GoogleTest (pacchetto gtest) e gli strumenti di prova accesi.
if(VELA_BUILD_TOOLS)
    find_package(GTest)
    if(GTest_FOUND)
        add_executable(vela-compositor-tests
            tests/frame_clock_test.cpp
            tests/motion_test.cpp
            tests/geometry_test.cpp
            tests/color_sun_test.cpp
            tests/settings_test.cpp
            tests/output_config_test.cpp
            tests/buffer_test.cpp)
        target_compile_options(vela-compositor-tests PRIVATE -Wall -Wextra)
        target_link_libraries(vela-compositor-tests PRIVATE vela-pure GTest::gtest_main)
        include(GoogleTest)
        gtest_discover_tests(vela-compositor-tests TEST_PREFIX "compositor.")
    else()
        message(STATUS "GoogleTest not found: no compositor tests (package gtest)")
    endif()
endif()
