find_package(Threads REQUIRED)

set(rudesmtp_tests)

foreach(t protocol wire contract version)
    add_executable(test_${t} test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE rudesmtp::rudesmtp Threads::Threads)
    add_test(NAME ${t} COMMAND test_${t})
    list(APPEND rudesmtp_tests ${t})
endforeach()

# test_version checks that version() reports the project version rather than
# a literal, so it needs to know what that is.
target_compile_definitions(test_version PRIVATE
    RUDESMTP_EXPECTED_VERSION="${PROJECT_VERSION}")

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
