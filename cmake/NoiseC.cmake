# noise-c : sous-build autotools hors source (third_party/ reste intact), référence C interne.
# Cible importée : noise::protocol ; target de build : noise_c.
set(P2P_NOISE_SRC "${CMAKE_SOURCE_DIR}/third_party/noise-c")
set(P2P_NOISE_BUILD "${CMAKE_BINARY_DIR}/noise-c")
set(P2P_NOISE_PREFIX "${P2P_NOISE_BUILD}/install")
set(P2P_NOISE_LIB "${P2P_NOISE_PREFIX}/lib/libnoiseprotocol.a")

if(NOT EXISTS "${P2P_NOISE_SRC}/configure.ac")
    message(FATAL_ERROR "noise-c absent : cloner dans third_party/noise-c (voir README)")
endif()

include(ExternalProject)
ExternalProject_Add(noise_c
    PREFIX "${P2P_NOISE_BUILD}"
    SOURCE_DIR "${P2P_NOISE_BUILD}/src"
    BINARY_DIR "${P2P_NOISE_BUILD}/src"
    DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory "${P2P_NOISE_SRC}" "${P2P_NOISE_BUILD}/src"
    CONFIGURE_COMMAND sh -c "sh ./autogen.sh && sh ./configure --disable-shared --prefix=${P2P_NOISE_PREFIX} CFLAGS=-O2"
    BUILD_COMMAND make -j2
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${P2P_NOISE_LIB}"
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON)

# CMake exige que les include dirs importés existent à la génération ; noise-c les installe à la compilation.
file(MAKE_DIRECTORY "${P2P_NOISE_PREFIX}/include")
add_library(noise::protocol STATIC IMPORTED GLOBAL)
set_target_properties(noise::protocol PROPERTIES
    IMPORTED_LOCATION "${P2P_NOISE_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${P2P_NOISE_PREFIX}/include"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${P2P_NOISE_PREFIX}/include")
