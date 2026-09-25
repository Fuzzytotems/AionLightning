# AionLightning C++ port - root build (see docs/cpp-port/CONVENTIONS.md §1).
#
#   cmake -S . -B build -G Ninja [-DCMAKE_CXX_COMPILER=clang++]
#   cmake --build build && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(AionLightning LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # -std=c++20, not gnu++20 (no `linux`/`unix` macros)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(JLANG_ASSERTS "Enable Java assert statements (JASSERT) in translated code" OFF)
option(AION_USE_PCH "Use a precompiled header of <jlang/jlang.h> for the port targets" ON)
option(AION_BUILD_TESTS "Build the jlang unit tests" ON)

# ------------------------------------------------------------------ dependencies
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(BDWGC REQUIRED IMPORTED_TARGET bdw-gc)
pkg_check_modules(MARIADB REQUIRED IMPORTED_TARGET libmariadb)
pkg_check_modules(PUGIXML REQUIRED IMPORTED_TARGET pugixml)
pkg_check_modules(LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
# java.util.regex (jlang/src/regex.cpp): PCRE2, 8-bit library (apt: libpcre2-dev)
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

# Flags every target compiles with (propagated through jlang's PUBLIC usage requirements).
# GCC also gets -fno-delete-dead-exceptions: GCC >= 12 may delete a call whose only effect is
# a possible trap (e.g. an unused getter result on a null pointer), which would swallow the
# NullPointerException Java throws there. Clang has neither flag (it ignores
# -fnon-call-exceptions: NPE conversion from faults only works in GCC builds).
set(AION_REQUIRED_FLAGS -fwrapv -fno-strict-aliasing -fnon-call-exceptions -pthread
    $<$<CXX_COMPILER_ID:GNU>:-fno-delete-dead-exceptions>)

# ------------------------------------------------------------------ jlang runtime
file(GLOB JLANG_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/jlang/src/*.cpp)
add_library(jlang STATIC ${JLANG_SOURCES})
target_include_directories(jlang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/jlang/include)
target_compile_features(jlang PUBLIC cxx_std_20)
target_compile_options(jlang PUBLIC ${AION_REQUIRED_FLAGS})
target_compile_options(jlang PRIVATE -Wall -Wextra)
if(JLANG_ASSERTS)
  target_compile_definitions(jlang PUBLIC JLANG_ASSERTS)
endif()
# Only jlang sees the third-party headers; everything links through it.
target_link_libraries(jlang
  PUBLIC PkgConfig::BDWGC Threads::Threads ${CMAKE_DL_LIBS}
  PRIVATE PkgConfig::MARIADB PkgConfig::PUGIXML PkgConfig::LIBCRYPTO PkgConfig::ZLIB PkgConfig::PCRE2)
# Export executable symbols so Throwable::printStackTrace can name functions.
target_link_options(jlang INTERFACE -rdynamic)

# jlang_use_pch(<target>): compile <target> with a precompiled <jlang/jlang.h>.
# All port targets share one PCH (built once, for the jlang_pch helper target).
if(AION_USE_PCH)
  add_library(jlang_pch STATIC ${CMAKE_CURRENT_SOURCE_DIR}/jlang/pch/jlang_pch.cpp)
  target_link_libraries(jlang_pch PUBLIC jlang)
  target_precompile_headers(jlang_pch PRIVATE <jlang/jlang.h>)
endif()
function(jlang_use_pch target)
  if(AION_USE_PCH)
    target_precompile_headers(${target} REUSE_FROM jlang_pch)
  endif()
endfunction()

# ------------------------------------------------------------------ jlang tests
if(AION_BUILD_TESTS)
  enable_testing()
  file(GLOB JLANG_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/jlang/tests/*.cpp)
  add_executable(jlang_tests ${JLANG_TEST_SOURCES})
  target_link_libraries(jlang_tests PRIVATE jlang)
  target_compile_options(jlang_tests PRIVATE -Wall -Wextra)
  add_test(NAME jlang_tests COMMAND jlang_tests)
  set_tests_properties(jlang_tests PROPERTIES TIMEOUT 600)
endif()

# ------------------------------------------------------------------ port targets
# Include roots are the Java source roots (CONVENTIONS §3.1). Each module's generated
# sources.cmake defines <MODULE>_SOURCES (list of .cpp files).
set(AION_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ---- commons (library) -------------------------------------------------------------- TODO(port)
# include(${AION_ROOT}/commons/sources.cmake)
# add_library(commons STATIC ${COMMONS_SOURCES})
# target_include_directories(commons PUBLIC ${AION_ROOT}/commons/src)
# target_link_libraries(commons PUBLIC jlang)
# jlang_use_pch(commons)

# ---- geoengine (library) ------------------------------------------------------------ TODO(port)
# include(${AION_ROOT}/geoengine/sources.cmake)
# add_library(geoengine STATIC ${GEOENGINE_SOURCES})
# target_include_directories(geoengine PUBLIC ${AION_ROOT}/geoengine/src)
# target_link_libraries(geoengine PUBLIC jlang)
# jlang_use_pch(geoengine)

# ---- loginserver (executable) ------------------------------------------------------- TODO(port)
# include(${AION_ROOT}/loginserver/sources.cmake)
# add_executable(loginserver ${LOGINSERVER_SOURCES})
# target_include_directories(loginserver PRIVATE
#   ${AION_ROOT}/loginserver/src
#   ${AION_ROOT}/loginserver/data/scripts/system/database)
# target_link_libraries(loginserver PRIVATE commons jlang)
# jlang_use_pch(loginserver)

# ---- chatserver (executable) -------------------------------------------------------- TODO(port)
# include(${AION_ROOT}/chatserver/sources.cmake)
# add_executable(chatserver ${CHATSERVER_SOURCES})
# target_include_directories(chatserver PRIVATE ${AION_ROOT}/chatserver/src)
# target_link_libraries(chatserver PRIVATE commons jlang)
# jlang_use_pch(chatserver)

# ---- gameserver (executable, scripts compiled in) ----------------------------------- TODO(port)
# include(${AION_ROOT}/gameserver/sources.cmake)   # core + handlers + database scripts
# add_executable(gameserver ${GAMESERVER_SOURCES})
# target_include_directories(gameserver PRIVATE
#   ${AION_ROOT}/gameserver/src
#   ${AION_ROOT}/gameserver/data/scripts/system/handlers
#   ${AION_ROOT}/gameserver/data/scripts/system/database)
# target_link_libraries(gameserver PRIVATE commons geoengine jlang)
# jlang_use_pch(gameserver)
