cmake_minimum_required(VERSION 3.22)

project(vela
    VERSION 0.1.0
    DESCRIPTION "Desktop environment moderno e leggero per Linux"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Tipo di build" FORCE)
endif()

include(GNUInstallDirs)

option(VELA_BUILD_COMPOSITOR "Compila il compositor (richiede wlroots)" ON)
option(VELA_BUILD_SHELL "Compila la shell (richiede Qt 6 e LayerShellQt)" ON)

if(VELA_BUILD_COMPOSITOR)
    add_subdirectory(compositor)
endif()
if(VELA_BUILD_SHELL)
    add_subdirectory(shell)
endif()
