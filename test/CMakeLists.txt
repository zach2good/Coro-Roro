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

CPMAddPackage("gh:gabime/spdlog@1.15.3")

CPMAddPackage(
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  VERSION 1.17.0
  OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt"
)

add_executable(simple_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/simple_tests.cpp
)

add_executable(callable_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/callable_tests.cpp
)

add_executable(interval_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/interval_tests.cpp
)


add_executable(landsandboat_simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/landsandboat_simulation.cpp
)

set(TEST_TARGETS
    simple_tests
    callable_tests
    interval_tests
    landsandboat_simulation
)

foreach(target IN LISTS TEST_TARGETS)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(${target} PRIVATE Coro-Roro gtest gtest_main)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

