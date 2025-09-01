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

# Old test targets removed - all tests have been ported to the new test suite

# Add new comprehensive test suite
add_executable(new_complex_workload_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/new/complex_workload_tests.cpp
)

add_executable(new_performance_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/new/performance_tests.cpp
)

add_executable(new_pressure_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/new/pressure_tests.cpp
)

add_executable(new_worker_pool_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/new/worker_pool_tests.cpp
)

add_executable(new_interval_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/new/interval_tests.cpp
)

add_executable(new_thread_verifier_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/new/thread_verifier_tests.cpp
)

add_executable(new_scheduler_interval_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/new/scheduler_interval_tests.cpp
)

add_executable(new_delayed_interval_basic_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/new/delayed_interval_basic_test.cpp
)

# Simple test to verify basic compilation
add_executable(simple_test
    ${CMAKE_CURRENT_SOURCE_DIR}/simple_test.cpp
)

# References to old new_coro_roro_tests target removed

# Configure new test executables
foreach(test_target IN ITEMS
    new_complex_workload_tests
    new_performance_tests
    new_pressure_tests
    new_worker_pool_tests
    new_interval_tests
    new_thread_verifier_tests
    new_scheduler_interval_tests
    new_delayed_interval_basic_tests
    simple_test
)
    target_include_directories(${test_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(${test_target} PRIVATE Coro-Roro gtest gtest_main)

    if(MSVC)
        target_compile_options(${test_target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

# All old test targets removed - new test suite uses individual targets
