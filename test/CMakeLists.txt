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
    ${CMAKE_CURRENT_SOURCE_DIR}/coroutine_task_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_verifier_tests.cpp
)

target_include_directories(coro_roro_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(coro_roro_tests PRIVATE Coro-Roro gtest gtest_main spdlog_header_only)

add_executable(coro_roro_perf_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/performance_tests.cpp
)

target_include_directories(coro_roro_perf_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(coro_roro_perf_tests PRIVATE Coro-Roro TracyClient spdlog_header_only)
