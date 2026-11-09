cmake_minimum_required(VERSION 3.26)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED ENV{CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type")
endif ()

project(nightmare_lizalfos VERSION 1.0.0 LANGUAGES CXX)

# The Dusklight version to build against: a release tag (e.g. "v2.0.0") or a commit SHA.
# Build against Kamilink's Lazy Tweaks fork (pinned commit) so the mod's headers match the
# game binary it will run in. Override with -DDUSKLIGHT_DIR=<your checkout> to skip fetching.
set(DUSKLIGHT_REPOSITORY "https://github.com/kamilink7/dusklight.git" CACHE STRING
        "Dusklight git repository to fetch from" FORCE)
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "c5087758ad491c337b337fbb471ab40e84874e81")
endif ()
# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
# Use -DDUSKLIGHT_DIR= to point at an existing Dusklight checkout.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

configure_file(mod.json.in "${CMAKE_CURRENT_BINARY_DIR}/mod.json" @ONLY NEWLINE_STYLE LF)

add_mod(nightmare_lizalfos
        FEATURES game fmt          # remove for service/asset-only mods; add webgpu for GfxService
        SOURCES src/mod.cpp
        MOD_JSON "${CMAKE_CURRENT_BINARY_DIR}/mod.json"
        RES_DIR res            # mod resources, including icon.png and banner.png
        OVERLAY_DIR overlay    # game file overlays; remove if unused
        TEXTURES_DIR textures  # texture replacements; remove if unused
)
