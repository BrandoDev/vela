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

add_executable(vela-shot vela-shot.c)
target_link_libraries(vela-shot PRIVATE proto-screencopy PkgConfig::TOOLS_ZLIB)

add_executable(vela-windows vela-windows.c)
target_link_libraries(vela-windows PRIVATE proto-foreign-toplevel)

add_executable(vela-input vela-input.c)
target_link_libraries(vela-input PRIVATE
    proto-virtual-pointer proto-virtual-keyboard proto-xdg-output PkgConfig::TOOLS_XKBCOMMON)

foreach(tool vela-shot vela-windows vela-input)
    target_compile_options(${tool} PRIVATE -Wall -Wextra -Wno-unused-parameter)
endforeach()
