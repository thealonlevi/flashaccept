# CMakeLists.txt for flashaccept — a fast io_uring TCP accept engine for Linux.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   cmake --install build --prefix /usr/local
#
# Installs the header, static + shared libraries (with SONAME libflashaccept.so.1),
# a pkg-config file (flashaccept.pc), and a CMake package config so downstreams can
#   find_package(flashaccept CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE flashaccept::flashaccept)
#
# The Makefile remains a fully equivalent canonical build; this file exists so that
# vcpkg / Conan / CMake consumers get first-class support.

cmake_minimum_required(VERSION 3.14)
project(flashaccept VERSION 1.0.1 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(FLASHACCEPT_BUILD_EXAMPLES "Build example programs" ON)
option(FLASHACCEPT_BUILD_SHARED   "Build the shared library" ON)
option(FLASHACCEPT_INSTALL        "Generate install rules" ON)

# --- liburing (>= 2.3) -------------------------------------------------------
# Prefer a CMake config (vcpkg/Conan may ship one), then pkg-config, then a bare
# library/header search. Whichever path wins exposes the target liburing::liburing.
if(NOT TARGET liburing::liburing)
    find_package(liburing CONFIG QUIET)
endif()
if(NOT TARGET liburing::liburing)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(liburing QUIET IMPORTED_TARGET GLOBAL liburing>=2.3)
        if(TARGET PkgConfig::liburing)
            add_library(liburing::liburing ALIAS PkgConfig::liburing)
        endif()
    endif()
endif()
if(NOT TARGET liburing::liburing)
    find_library(URING_LIBRARY NAMES uring REQUIRED)
    find_path(URING_INCLUDE_DIR NAMES liburing.h REQUIRED)
    add_library(liburing::liburing UNKNOWN IMPORTED GLOBAL)
    set_target_properties(liburing::liburing PROPERTIES
        IMPORTED_LOCATION "${URING_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${URING_INCLUDE_DIR}")
endif()

find_package(Threads REQUIRED)

# --- libraries ---------------------------------------------------------------
# One source set, built into a static and (optionally) a shared library. Public
# headers are exposed via the include/ dir at build time and ${INCLUDEDIR} once
# installed; liburing/Threads are PRIVATE (the public header does not expose them)
# but CMake still threads them onto consumers of the *static* archive.
set(_fa_targets flashaccept_static)

add_library(flashaccept_static STATIC src/flashaccept.c)
set_target_properties(flashaccept_static PROPERTIES OUTPUT_NAME flashaccept)

if(FLASHACCEPT_BUILD_SHARED)
    add_library(flashaccept_shared SHARED src/flashaccept.c)
    set_target_properties(flashaccept_shared PROPERTIES
        OUTPUT_NAME flashaccept
        VERSION   ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
    list(APPEND _fa_targets flashaccept_shared)
    # Canonical consumer name flashaccept::flashaccept resolves to the shared lib;
    # the static archive is also exported as flashaccept::flashaccept-static.
    set_target_properties(flashaccept_shared PROPERTIES EXPORT_NAME flashaccept)
    set_target_properties(flashaccept_static PROPERTIES EXPORT_NAME flashaccept-static)
    add_library(flashaccept::flashaccept ALIAS flashaccept_shared)
    add_library(flashaccept::flashaccept-static ALIAS flashaccept_static)
else()
    set_target_properties(flashaccept_static PROPERTIES EXPORT_NAME flashaccept)
    add_library(flashaccept::flashaccept ALIAS flashaccept_static)
endif()

foreach(t IN LISTS _fa_targets)
    target_compile_options(${t} PRIVATE -O2 -Wall -Wextra)
    target_include_directories(${t} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(${t} PRIVATE liburing::liburing Threads::Threads)
endforeach()

# --- example -----------------------------------------------------------------
if(FLASHACCEPT_BUILD_EXAMPLES)
    add_executable(echo_server examples/echo_server.c)
    target_link_libraries(echo_server PRIVATE flashaccept_static liburing::liburing Threads::Threads)
    set_target_properties(echo_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                          ${CMAKE_CURRENT_SOURCE_DIR}/examples)
endif()

# --- install / export --------------------------------------------------------
if(FLASHACCEPT_INSTALL)
    install(TARGETS ${_fa_targets}
        EXPORT  flashaccept-targets
        RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES include/flashaccept.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT flashaccept-targets
        FILE        flashaccept-targets.cmake
        NAMESPACE   flashaccept::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flashaccept)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/flashaccept-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/flashaccept-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flashaccept)
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/flashaccept-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/flashaccept-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/flashaccept-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/flashaccept)

    # pkg-config (kept in lockstep with the Makefile's flashaccept.pc).
    # Make it relocatable: derive prefix from the .pc's own location (${pcfiledir})
    # so the file is correct under any --prefix and after vcpkg/Conan relocation.
    set(PKG_CONFIG_REQUIRES "liburing >= 2.3")
    file(RELATIVE_PATH _fa_pc_rel
        "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
    string(REGEX REPLACE "/+$" "" _fa_pc_rel "${_fa_pc_rel}")
    if(_fa_pc_rel STREQUAL "")
        set(FA_PC_PREFIX "\${pcfiledir}")
    else()
        set(FA_PC_PREFIX "\${pcfiledir}/${_fa_pc_rel}")
    endif()
    if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
        set(FA_PC_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
    else()
        set(FA_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
    endif()
    if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
        set(FA_PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
    else()
        set(FA_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
    endif()
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/flashaccept.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/flashaccept.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/flashaccept.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
