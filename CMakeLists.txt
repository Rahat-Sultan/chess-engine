cmake_minimum_required(VERSION 3.20)

project(chess_engine
    VERSION 0.1.0
    DESCRIPTION "Lightweight original chess engine with UCI support"
    LANGUAGES CXX
)

# ─── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Build type default ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ─── Compiler warnings ────────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
)

# ─── Optional sanitizers (Debug builds) ──────────────────────────────────────
option(ENABLE_ASAN  "Enable AddressSanitizer"         OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# ─── Export compile commands (useful for clangd / IDE integration) ────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Include paths ────────────────────────────────────────────────────────────
include_directories(${PROJECT_SOURCE_DIR}/include)

# ─── Engine library (shared between executable and tests) ────────────────────
#
# Board and other engine modules are compiled into a static library so the
# test executable can link them without duplicating compilation.
#
add_library(chess_core STATIC
    src/board.cpp
)

target_include_directories(chess_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# ─── Engine executable ────────────────────────────────────────────────────────
add_executable(chess_engine
    src/main.cpp
)

target_include_directories(chess_engine PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(chess_engine PRIVATE chess_core)

# ─── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

# Phase 1 unit tests: types + board
add_executable(test_chess
    tests/unit/test_main.cpp
    tests/unit/test_types.cpp
    tests/unit/test_board.cpp
)

target_include_directories(test_chess PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_chess PRIVATE chess_core)

add_test(NAME UnitTests COMMAND test_chess)

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS chess_engine DESTINATION bin)
