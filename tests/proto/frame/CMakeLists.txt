# Cible fuzz_frame hors CTest : build standalone avec clang (libFuzzer + ASan + UBSan).
# Usage : scripts/fuzz.sh frame <secondes>
cmake_minimum_required(VERSION 3.20)
project(fuzz_frame C)

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "fuzz_frame exige clang (CMAKE_C_COMPILER=clang)")
endif()

set(P2P_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../src")
set(FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -g -O1)

add_executable(fuzz_frame
    fuzz_frame.c
    "${P2P_SRC}/proto/frame/frame.c"
    "${P2P_SRC}/core/buf/buf.c")
target_include_directories(fuzz_frame PRIVATE "${P2P_SRC}")
target_compile_options(fuzz_frame PRIVATE ${FUZZ_FLAGS} -Wall -Wextra -Werror -Wpedantic)
target_link_options(fuzz_frame PRIVATE ${FUZZ_FLAGS})
