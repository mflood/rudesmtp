cmake_minimum_required(VERSION 3.16)

project(rudesmtp
    VERSION 2.1.0
    DESCRIPTION "C++ SMTP client library"
    HOMEPAGE_URL "https://github.com/mflood/rudesmtp"
    LANGUAGES CXX)

# PROJECT_IS_TOP_LEVEL is CMake 3.21+; this project still supports 3.16, and
# an undefined variable would read as false and silently drop the tests.
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL TRUE)
    else()
        set(PROJECT_IS_TOP_LEVEL FALSE)
    endif()
endif()

include(GNUInstallDirs)
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

option(RUDESMTP_BUILD_EXAMPLES "Build example programs" ON)

# ---- Dependency: rudesocket ----
#
# This has always been a hard dependency -- every byte on the wire goes through
# rude::Socket -- but nothing said so.  The autotools build carried an autoscan
# stub, AC_CHECK_LIB([rudesocket], [main]) with its "# FIXME" comment still
# attached, which probes for a symbol a C++ library does not export and so
# passed or failed for reasons unrelated to whether rudesocket was usable.  No
# header check existed at all, and no README, NEWS or INSTALL file mentioned
# rudesocket even once.
#
# 1.7.1 is the floor for two reasons: startSSL() arrives in 1.7.0 and STARTTLS
# cannot be done without it, and 1.7.1 is where ~Socket started closing the
# connection.  Before that every SMTP session leaked its descriptor when the
# SMTP object went out of scope, which is a defect this library would inherit
# rather than one it could fix.
set(RUDESMTP_RUDESOCKET_VERSION 1.7.1)

find_package(rudesocket ${RUDESMTP_RUDESOCKET_VERSION} QUIET)
if(NOT rudesocket_FOUND)
    message(STATUS "rudesocket ${RUDESMTP_RUDESOCKET_VERSION} not found; fetching it")
    include(FetchContent)
    FetchContent_Declare(rudesocket
        GIT_REPOSITORY https://github.com/mflood/rudesocket.git
        GIT_TAG v${RUDESMTP_RUDESOCKET_VERSION})
    FetchContent_MakeAvailable(rudesocket)
endif()

add_library(rudesmtp
    src/smtp.cpp
    src/smtpimpl.cpp)

add_library(rudesmtp::rudesmtp ALIAS rudesmtp)

target_compile_features(rudesmtp PUBLIC cxx_std_17)

# rudesocket is used only inside the implementation; smtp.h does not expose it,
# so consumers do not need its headers. It is still needed to link.
target_link_libraries(rudesmtp PRIVATE rudesocket::rudesocket)

# version() reports the real project version rather than a literal that
# tracks nothing.
configure_file(src/rudesmtp_version.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rudesmtp_version.h" @ONLY)
target_include_directories(rudesmtp PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Public header is consumed as <rude/smtp.h>; stage it under that path so
# in-tree consumers see the same layout as installed ones.
configure_file(src/smtp.h "${CMAKE_CURRENT_BINARY_DIR}/include/rude/smtp.h" COPYONLY)
target_include_directories(rudesmtp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Shared-library version follows the old libtool -version-info 1:0:0 lineage,
# advanced for the 2.0.0 interface change.
set_target_properties(rudesmtp PROPERTIES
    VERSION 2.1.0
    SOVERSION 2
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(MSVC)
    target_compile_options(rudesmtp PRIVATE /W4)
else()
    target_compile_options(rudesmtp PRIVATE -Wall -Wextra)
endif()

# ---- Install ----

install(TARGETS rudesmtp
    EXPORT rudesmtp-targets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(FILES src/smtp.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/rude")

install(EXPORT rudesmtp-targets
    FILE rudesmtp-targets.cmake
    NAMESPACE rudesmtp::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rudesmtp")

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rudesmtp-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
# A consumer linking rudesmtp needs rudesocket resolvable, so the config
# declares it rather than leaving them to discover it from a link error.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/rudesmtp-config.cmake"
    "include(CMakeFindDependencyMacro)\nfind_dependency(rudesocket ${RUDESMTP_RUDESOCKET_VERSION})\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/rudesmtp-targets.cmake\")\n")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/rudesmtp-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/rudesmtp-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/rudesmtp")

# The .pc file must not bake in the configure-time CMAKE_INSTALL_PREFIX: that
# breaks "cmake --install --prefix <dir>", which is what the README teaches.
# Express prefix relative to ${pcfiledir} instead, so the file stays correct
# wherever it lands.  Walk back up one "../" per component of
# <libdir>/pkgconfig -- computed by hand rather than with file(RELATIVE_PATH),
# which requires absolute arguments and rejects "/" as a root on Windows.
# Falls back to the absolute prefix if the caller set an absolute
# CMAKE_INSTALL_LIBDIR, where a relative path is not meaningful.
if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(RUDESMTP_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
else()
    file(TO_CMAKE_PATH "${CMAKE_INSTALL_LIBDIR}/pkgconfig" RUDESMTP_PC_SUBDIR)
    string(REGEX REPLACE "/+" "/" RUDESMTP_PC_SUBDIR "${RUDESMTP_PC_SUBDIR}")
    string(REGEX REPLACE "^/+|/+$" "" RUDESMTP_PC_SUBDIR "${RUDESMTP_PC_SUBDIR}")
    string(REPLACE "/" ";" RUDESMTP_PC_PARTS "${RUDESMTP_PC_SUBDIR}")
    set(RUDESMTP_PC_REL_PREFIX "")
    foreach(part IN LISTS RUDESMTP_PC_PARTS)
        if(NOT part STREQUAL "" AND NOT part STREQUAL ".")
            string(APPEND RUDESMTP_PC_REL_PREFIX "../")
        endif()
    endforeach()
    if(RUDESMTP_PC_REL_PREFIX STREQUAL "")
        set(RUDESMTP_PC_REL_PREFIX "./")
    endif()
    set(RUDESMTP_PC_PREFIX "\${pcfiledir}/${RUDESMTP_PC_REL_PREFIX}")
endif()

configure_file(rudesmtp.pc.in "${CMAKE_CURRENT_BINARY_DIR}/rudesmtp.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rudesmtp.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

# ---- Tests & examples ----

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(PROJECT_IS_TOP_LEVEL AND RUDESMTP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
