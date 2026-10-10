cmake_minimum_required(VERSION 3.20)
project(p2pmsg C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(P2P_VALGRIND "Run every test under valgrind memcheck (preset debug)" OFF)

set(P2P_SRC_ROOT "${CMAKE_SOURCE_DIR}/src")
set(P2P_TEST_ROOT "${CMAKE_SOURCE_DIR}/tests")

add_compile_options(-Wall -Wextra -Werror -Wpedantic)

include(cmake/P2PModule.cmake)
include(cmake/P2PDeps.cmake)
include(cmake/NoiseC.cmake)

enable_testing()

# Modules SPEC §4.1 (ordre = dépendances). Un module absent du disque est ignoré.
set(P2P_MODULES
    core/log
    core/buf
    core/loop
    net/tcp
    net/discovery
    proto/frame
    proto/msg
    sec/session
    peer/manager
    store/history
    app/api
    app/cli
    ui/gtk)

foreach(_mod IN LISTS P2P_MODULES)
    if(EXISTS "${P2P_SRC_ROOT}/${_mod}/CMakeLists.txt")
        add_subdirectory("${P2P_SRC_ROOT}/${_mod}")
    endif()
endforeach()

# Installation : binaires dans bin/, documentation dans share/doc/p2pmsg.
include(GNUInstallDirs)
if(TARGET p2pmsg)
    install(TARGETS p2pmsg RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(TARGET p2pmsg-gtk)
    install(TARGETS p2pmsg-gtk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES README.md LICENSE DESTINATION share/doc/p2pmsg)
