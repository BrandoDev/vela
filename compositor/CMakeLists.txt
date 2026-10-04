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
# finestra (nested.cpp).
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

add_executable(vela-compositor
    src/main.cpp
    src/nested.cpp
    src/server.cpp
    src/output.cpp
    src/pointer.cpp
    src/outputconfig.cpp
    src/toplevel.cpp
    src/xwayland.cpp
    src/layer.cpp
    src/decoration.cpp
    src/icons.cpp
    src/text.cpp
    src/workspaces.cpp
    src/accessibility.cpp
    src/render/pixelbuffer.cpp
    src/lock.cpp
    src/keyboard.cpp
    src/settings.cpp
    src/snap.cpp
    src/snapshot.cpp
    src/render/allocator.cpp
    src/render/pass.cpp
    src/render/renderer.cpp
    src/render/texture.cpp
    src/render/vulkan.cpp
    src/scene/capture.cpp
    src/scene/effects.cpp
    src/scene/frame.cpp
    src/scene/scene.cpp
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
target_link_libraries(vela-compositor PRIVATE
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
    message(STATUS "librsvg non trovata: la barra del titolo sarà senza icone")
endif()

install(TARGETS vela-compositor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
