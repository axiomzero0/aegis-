cmake_minimum_required(VERSION 3.24)
project(AegisCompiler LANGUAGES CXX VERSION 0.2.0)

# ============================================================
# Aegis Compiler — top-level CMakeLists
# ============================================================
# Laws enforced here:
#   B.1  No exceptions on the hot path  -> AEGIS_HOT_PATH_NO_EXCEPTIONS
#   B.2  PMR monotonic arena for IR      -> linked per-translation-unit
#   B.3  No RTTI                          -> -fno-rtti
#   Rule 58: -fassume* / hint macros supported by C++26
#   Rule 50: Versioned caches/artifacts.
#
# Prescribed directory layout (see docs/directory_layout.md):
#   compiler/{support,ir,frontend,passes,backend,jit,pgo}
#   runtime/{core,alloc,sync,io}
#   tools/{aegisc,lsp,repl,fmt}
#   tests/{unit,integration,regression,perf}
#   docs/
# ============================================================

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" Force)
endif()

option(AEGIS_BUILD_TESTS   "Build unit/golden tests" ON)
option(AEGIS_VERIFY_IR    "Run graph verifier after every pass (Rule 42)" ON)
option(AEGIS_DEBUG_LOGS   "Verbose pass logging" OFF)
option(AEGIS_USE_LTO      "Link-Time Optimization" OFF)
option(AEGIS_USE_CCACHE   "Use ccache when available" ON)

# ---- Compile options (global) ----
add_library(aegis_compile_options INTERFACE)
target_compile_features(aegis_compile_options INTERFACE cxx_std_26)
target_compile_options(aegis_compile_options INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:
        -fno-rtti
        -Wall -Wextra -Wpedantic -Werror
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    >
)
target_compile_definitions(aegis_compile_options INTERFACE
    $<$<CONFIG:Debug>:AEGIS_DEBUG=1>
    $<$<BOOL:${AEGIS_VERIFY_IR}>:AEGIS_VERIFY_IR=1>
    $<$<BOOL:${AEGIS_DEBUG_LOGS}>:AEGIS_DEBUG_LOGS=1>
)

# Hot-path options (no exceptions, no RTTI). Rule B.1 + Rule B.3.
add_library(aegis_hot_path_options INTERFACE)
target_compile_options(aegis_hot_path_options INTERFACE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:
        -fno-exceptions
        -fvisibility=hidden
        -fvisibility-inlines-hidden
    >
)
target_compile_definitions(aegis_hot_path_options INTERFACE
    AEGIS_HOT_PATH=1
    AEGIS_NO_EXCEPTIONS=1
)

if(AEGIS_USE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_subdirectory(compiler)
add_subdirectory(runtime)
add_subdirectory(tools)

if(AEGIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
