# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)

project(vela
    VERSION 0.1.0
    DESCRIPTION "A modern, fluid and sharp desktop environment for Linux"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

option(VELA_BUILD_COMPOSITOR "Build the compositor (needs wlroots)" ON)
option(VELA_BUILD_SHELL "Build the shell, Files and Settings (needs Qt 6 and LayerShellQt)" ON)
option(VELA_BUILD_TOOLS "Build the test tools (vela-shot, vela-windows, vela-input...) and the tests (ctest)" ON)
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
    # The functional and sharpness tests, in sessions without a display
    # (tests/functional): they need the compositor and a GPU; without a GPU
    # they are reported as skipped (code 77).
    if(VELA_BUILD_COMPOSITOR)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME functional COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/functional/run.py)
            add_test(NAME sharpness COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/functional/run.py --sharpness)
            set_tests_properties(functional sharpness PROPERTIES
                ENVIRONMENT "VELA_BUILD=${CMAKE_BINARY_DIR}"
                SKIP_RETURN_CODE 77
                TIMEOUT 600
                RUN_SERIAL TRUE)
        endif()
    endif()
endif()
