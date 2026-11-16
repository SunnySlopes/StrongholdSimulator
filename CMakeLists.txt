cmake_minimum_required(VERSION 3.15)
project(stronghold_simulator LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# cubiomes
#
# This project vendors cubiomes as a git submodule at third_party/cubiomes, so
# a fresh clone needs no external source tree:
#
#   git clone --recursive <this-repo>
#   # or, in an existing clone:
#   git submodule update --init --recursive
#
# An embedding project that already owns a cubiomes checkout can override:
#
#   -DSSIM_CUBIOMES_DIR=<path-to-cubiomes>
# ---------------------------------------------------------------------------
set(SSIM_CUBIOMES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cubiomes"
    CACHE PATH "Path to the cubiomes checkout")
if(NOT EXISTS "${SSIM_CUBIOMES_DIR}/finders.c")
    message(FATAL_ERROR
        "SSIM_CUBIOMES_DIR does not look like cubiomes: ${SSIM_CUBIOMES_DIR}\n"
        "Run 'git submodule update --init --recursive', or pass "
        "-DSSIM_CUBIOMES_DIR=<path-to-cubiomes>.")
endif()

# Sources include <cubiomes/...>, so the include root is cubiomes' parent.
get_filename_component(SSIM_INCLUDE_ROOT "${SSIM_CUBIOMES_DIR}" DIRECTORY)

# Compile the cubiomes translation units this simulator needs.
#
#   finders.c      structure placement + the stronghold iterator
#   generator.c    biome generator (nextStronghold/locateBiome need it)
#   the rest is the biome pipeline generator.c pulls in
#   features/*.c   stronghold.c is the actual layout generator; the siblings are
#                  linked because finders.c's getStructurePieces() switch
#                  references them unconditionally
#
# tests.c is excluded (it defines its own main), ore.c is excluded (not needed;
# it also drags in loot tables).
set(SSIM_CUBIOMES_SOURCES
    "${SSIM_CUBIOMES_DIR}/finders.c"
    "${SSIM_CUBIOMES_DIR}/generator.c"
    "${SSIM_CUBIOMES_DIR}/biomes.c"
    "${SSIM_CUBIOMES_DIR}/biomenoise.c"
    "${SSIM_CUBIOMES_DIR}/layers.c"
    "${SSIM_CUBIOMES_DIR}/noise.c"
    "${SSIM_CUBIOMES_DIR}/terrainnoise.c"
    "${SSIM_CUBIOMES_DIR}/carver.c"
    "${SSIM_CUBIOMES_DIR}/util.c"
)
foreach(_feat stronghold fortress end_city mineshaft abandoned_camp)
    list(APPEND SSIM_CUBIOMES_SOURCES "${SSIM_CUBIOMES_DIR}/features/${_feat}.c")
endforeach()
list(REMOVE_DUPLICATES SSIM_CUBIOMES_SOURCES)

# ---------------------------------------------------------------------------
# Core library (also usable from an embedding project)
# ---------------------------------------------------------------------------
add_library(stronghold_sim STATIC
    src/stronghold_sim.c
    ${SSIM_CUBIOMES_SOURCES}
)

target_include_directories(stronghold_sim PUBLIC
    "${SSIM_INCLUDE_ROOT}"
    "${SSIM_CUBIOMES_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Deterministic floating point: no FP contraction and no LTO, so the same seed
# and version give bit-identical coordinates on every build.
if(MSVC)
    target_compile_options(stronghold_sim PRIVATE /O2 /DNDEBUG)
else()
    target_compile_options(stronghold_sim PRIVATE
        -O3 -DNDEBUG -fwrapv -fno-semantic-interposition -mpopcnt
        -ffp-contract=off -fno-lto
        -ffunction-sections -fdata-sections
        -Wall -Wextra -Wno-unused-parameter
    )
endif()

if(NOT WIN32)
    target_link_libraries(stronghold_sim PUBLIC m)
endif()

# ---------------------------------------------------------------------------
# Command-line program
# ---------------------------------------------------------------------------
add_executable(stronghold-sim src/main.c)
target_link_libraries(stronghold-sim PRIVATE stronghold_sim)
set_target_properties(stronghold-sim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
if(MINGW)
    # Self-contained binary: no MinGW runtime DLLs next to it.
    target_link_options(stronghold-sim PRIVATE -static -static-libgcc)
endif()
