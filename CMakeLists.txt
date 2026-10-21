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

# ─── Engine executable ───────────────────────────────────────────────────────
#
# Only main.cpp exists in Phase 0.
# Each subsequent phase adds the relevant .cpp files to this list.
#
add_executable(chess_engine
    src/main.cpp
)

target_include_directories(chess_engine PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# ─── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

# Test targets will be added in Phase 1+ as test source files are created.
# Example for reference (uncomment when tests/unit/test_types.cpp exists):
#
# add_executable(test_types tests/unit/test_types.cpp src/board.cpp)
# target_include_directories(test_types PRIVATE ${PROJECT_SOURCE_DIR}/include)
# add_test(NAME TypesTest COMMAND test_types)

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS chess_engine DESTINATION bin)
