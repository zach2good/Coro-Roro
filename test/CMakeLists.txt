set(TRACY_VERSION 0.12.2)
CPMAddPackage(
    NAME TracyClient
    GITHUB_REPOSITORY wolfpld/tracy
    GIT_TAG v${TRACY_VERSION}
    OPTIONS
        "TRACY_ENABLE ON"
        # "TRACY_FIBERS ON" # Enable for Fiber tracking on worker threads (this isn't set up quite right yet)
)

if(MSVC)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/build/tracy-profiler.exe")
        message(STATUS "Downloading Tracy client")
        file(DOWNLOAD
            https://github.com/wolfpld/tracy/releases/download/v${TRACY_VERSION}/windows-${TRACY_VERSION}.zip
            ${CMAKE_SOURCE_DIR}/build/tracy.tar.gz
            TIMEOUT 60
        )
        execute_process(COMMAND "${CMAKE_COMMAND}" -E
                tar xvf "${CMAKE_SOURCE_DIR}/build/tracy.tar.gz"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/build/
        )
    else()
        message(STATUS "Tracy client executables already present")
    endif()
endif()

CPMAddPackage(
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  VERSION 1.17.0
  OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt"
)

add_executable(corororo_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/simple_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/callable_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/interval_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/landsandboat_simulation.cpp
)

target_include_directories(corororo_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corororo_tests PRIVATE Coro-Roro gtest gtest_main)

if(MSVC)
    target_compile_options(corororo_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(corororo_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

#
# Tracy Testing
#

add_executable(tracy_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tracy_test.cpp
)

target_link_libraries(tracy_test PRIVATE Coro-Roro TracyClient)

if(MSVC)
    target_compile_options(tracy_test PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(tracy_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
