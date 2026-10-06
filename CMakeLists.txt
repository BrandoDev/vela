# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

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
option(VELA_BUILD_SHELL "Compila la shell e le Impostazioni (richiede Qt 6 e LayerShellQt)" ON)
option(VELA_BUILD_TOOLS "Compila gli strumenti di prova (vela-shot, vela-windows, vela-input) e le prove (ctest)" ON)
if(VELA_BUILD_TOOLS)
    enable_testing()
endif()

if(VELA_BUILD_COMPOSITOR)
    add_subdirectory(compositor)
    add_subdirectory(session)
endif()
if(VELA_BUILD_SHELL)
    add_subdirectory(shell)
    add_subdirectory(settings)
    add_subdirectory(explorer)
    add_subdirectory(lock)
endif()
if(VELA_BUILD_TOOLS)
    add_subdirectory(tools)
    # Le prove funzionali e quella di nitidezza, in sessioni senza schermo
    # (tests/functional): servono il compositor e una GPU; senza GPU
    # risultano saltate (codice 77).
    if(VELA_BUILD_COMPOSITOR)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME funzionali COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/functional/run.py)
            add_test(NAME nitidezza COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/functional/run.py --nitidezza)
            set_tests_properties(funzionali nitidezza PROPERTIES
                ENVIRONMENT "VELA_BUILD=${CMAKE_BINARY_DIR}"
                SKIP_RETURN_CODE 77
                TIMEOUT 600
                RUN_SERIAL TRUE)
        endif()
    endif()
endif()
