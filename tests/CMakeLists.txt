find_package(Threads REQUIRED)

set(rudesmtp_tests)

foreach(t protocol wire contract version esmtp auth)
    add_executable(test_${t} test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE rudesmtp::rudesmtp Threads::Threads)
    add_test(NAME ${t} COMMAND test_${t})
    list(APPEND rudesmtp_tests ${t})
endforeach()

# test_version checks that version() reports the project version rather than
# a literal, so it needs to know what that is.
target_compile_definitions(test_version PRIVATE
    RUDESMTP_EXPECTED_VERSION="${PROJECT_VERSION}")

# test_tls drives real TLS (both connectSSL() and startTLS()) against an
# in-process server, so unlike the rest of the suite it needs OpenSSL
# directly rather than only through rudesmtp/rudesocket. Only built when
# rudesocket itself was built with SSL support -- otherwise connectSSL()
# and startTLS() are stub implementations with nothing to test.
#
# rudesocket's own RUDESOCKET_SSL_ENABLED is not visible here: it is a plain
# (non-cache) variable set inside rudesocket's own CMakeLists.txt, scoped to
# that subdirectory whether rudesocket arrives via FetchContent or
# find_package, and never propagated to a parent or sibling scope. What IS
# visible is RUDESOCKET_WITH_SSL, which rudesocket exports as a PUBLIC
# compile definition on the rudesocket::rudesocket target for exactly this
# reason -- check the target property rather than reaching for a variable
# that would silently read as unset.
get_target_property(_rudesocket_defs rudesocket::rudesocket INTERFACE_COMPILE_DEFINITIONS)
if(_rudesocket_defs AND "RUDESOCKET_WITH_SSL" IN_LIST _rudesocket_defs)
    find_package(OpenSSL)
endif()
if(OpenSSL_FOUND)
    add_executable(test_tls test_tls.cpp)
    target_link_libraries(test_tls PRIVATE rudesmtp::rudesmtp Threads::Threads OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME tls COMMAND test_tls)
    list(APPEND rudesmtp_tests tls)
else()
    message(STATUS "rudesmtp: OpenSSL not available to the test suite - test_tls skipped")
endif()

set_tests_properties(${rudesmtp_tests} PROPERTIES TIMEOUT 60)
foreach(t ${rudesmtp_tests})
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-${t}")
    set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/run-${t}")
endforeach()

# Guards the shared pkg-config defect the other repos hit: rudesmtp.pc must
# not bake in the configure-time prefix, or "cmake --install --prefix <dir>"
# ships a .pc pointing somewhere else entirely.
add_test(NAME pkgconfig_relocatable
    COMMAND "${CMAKE_COMMAND}"
        "-DPC_FILE=${CMAKE_BINARY_DIR}/rudesmtp.pc"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_pc_relocatable.cmake")
