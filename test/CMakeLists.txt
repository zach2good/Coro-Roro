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

add_executable(coro_roro_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_verifier_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler_basic_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler_performance_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler_complex_workload_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler_pressure_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scheduler_interval_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/worker_pool_tests.cpp
)

target_include_directories(coro_roro_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(coro_roro_tests PRIVATE Coro-Roro gtest gtest_main spdlog_header_only)

if(MSVC)
    target_compile_options(coro_roro_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(coro_roro_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

###

add_executable(new_coro_roro_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_new_basic.cpp
)

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

target_include_directories(new_coro_roro_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(new_coro_roro_tests PRIVATE corororo_new gtest gtest_main)

# Configure new test executables
foreach(test_target IN ITEMS
    new_complex_workload_tests
    new_performance_tests
    new_pressure_tests
    new_worker_pool_tests
    new_interval_tests
    new_thread_verifier_tests
)
    target_include_directories(${test_target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(${test_target} PRIVATE corororo_new gtest gtest_main)

    if(MSVC)
        target_compile_options(${test_target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

if(MSVC)
    target_compile_options(new_coro_roro_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(new_coro_roro_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
