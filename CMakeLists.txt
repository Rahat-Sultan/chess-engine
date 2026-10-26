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

# ─── Threading support ───────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ─── Engine library (shared between executable and tests) ────────────────────
#
# Board and other engine modules are compiled into a static library so the
# test executable can link them without duplicating compilation.
#
add_library(chess_core STATIC
    src/board.cpp
    src/movegen.cpp
    src/evaluation.cpp
    src/search.cpp
    src/transposition.cpp
    src/uci.cpp
    src/nnue_evaluator.cpp
    src/nnue_probe/nnue.cpp
    src/nnue_probe/misc.cpp
)

# Suppress warnings from vendored nnue-probe C sources (they are not our code)
set_source_files_properties(
    src/nnue_probe/nnue.cpp
    src/nnue_probe/misc.cpp
    PROPERTIES COMPILE_FLAGS "-w"
)

target_include_directories(chess_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src    # for nnue_probe/ relative includes
)

target_link_libraries(chess_core PUBLIC
    Threads::Threads
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
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
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

    # Phase 2 unit tests: movegen
    add_executable(test_movegen
        tests/unit/test_main.cpp
        tests/unit/test_movegen.cpp
    )

    target_include_directories(test_movegen PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(test_movegen PRIVATE chess_core)

    add_test(NAME MovegenTests COMMAND test_movegen)

    # Phase 2 perft tests
    add_executable(test_perft
        tests/perft/test_perft.cpp
    )

    target_include_directories(test_perft PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}           # for tests/perft/perft.hpp include path
        ${PROJECT_SOURCE_DIR}/tests/unit  # for test_framework.hpp
    )

    target_link_libraries(test_perft PRIVATE chess_core)

    add_test(NAME PerftTests COMMAND test_perft)

    # Phase 3 unit tests: search + evaluation
    add_executable(test_search
        tests/unit/test_main.cpp
        tests/unit/test_search.cpp
    )

    target_include_directories(test_search PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/tests/unit
    )

    target_link_libraries(test_search PRIVATE chess_core)

    add_test(NAME SearchTests COMMAND test_search)

    # Phase 4 unit tests: uci protocol controller
    add_executable(test_uci
        tests/unit/test_main.cpp
        tests/uci/test_uci.cpp
    )

    target_include_directories(test_uci PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/tests/unit
    )

    target_link_libraries(test_uci PRIVATE chess_core)

    add_test(NAME UciTests COMMAND test_uci)

    # Phase 5 unit tests: transposition table
    add_executable(test_transposition
        tests/unit/test_main.cpp
        tests/unit/test_transposition.cpp
    )

    target_include_directories(test_transposition PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/tests/unit
    )

    target_link_libraries(test_transposition PRIVATE chess_core)

    add_test(NAME TranspositionTests COMMAND test_transposition)

    # Phase 7 unit tests: NNUE evaluator
    add_executable(test_nnue
        tests/unit/test_main.cpp
        tests/unit/test_nnue.cpp
    )

    target_include_directories(test_nnue PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/tests/unit
    )

    target_link_libraries(test_nnue PRIVATE chess_core)

    add_test(NAME NNUETests COMMAND test_nnue
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS chess_engine DESTINATION bin)
