# CMakeLists.txt for new Coro-Roro implementation tests

# Create test executables for each test category

# Complex Workload Tests
add_executable(complex_workload_tests complex_workload_tests.cpp)
target_link_libraries(complex_workload_tests
    PRIVATE
        corororo_new
        gtest
        gtest_main
)
target_include_directories(complex_workload_tests PRIVATE ../../include)

# Performance Tests
add_executable(performance_tests performance_tests.cpp)
target_link_libraries(performance_tests
    PRIVATE
        corororo_new
        gtest
        gtest_main
)
target_include_directories(performance_tests PRIVATE ../../include)

# Pressure Tests
add_executable(pressure_tests pressure_tests.cpp)
target_link_libraries(pressure_tests
    PRIVATE
        corororo_new
        gtest
        gtest_main
)
target_include_directories(pressure_tests PRIVATE ../../include)

# Worker Pool Tests
add_executable(worker_pool_tests worker_pool_tests.cpp)
target_link_libraries(worker_pool_tests
    PRIVATE
        corororo_new
        gtest
        gtest_main
)
target_include_directories(worker_pool_tests PRIVATE ../../include)

# Interval/Delayed Tests
add_executable(interval_tests interval_tests.cpp)
target_link_libraries(interval_tests
    PRIVATE
        corororo_new
        gtest
        gtest_main
)
target_include_directories(interval_tests PRIVATE ../../include)

# Thread Verifier Tests
add_executable(thread_verifier_tests thread_verifier_tests.cpp)
target_link_libraries(thread_verifier_tests
    PRIVATE
        corororo_new
        gtest
        gtest_main
)
target_include_directories(thread_verifier_tests PRIVATE ../../include)

# Custom target to build all new tests
add_custom_target(build_new_tests
    DEPENDS
        complex_workload_tests
        performance_tests
        pressure_tests
        worker_pool_tests
        interval_tests
        thread_verifier_tests
    COMMENT "Building all new implementation tests"
)

# Custom target to run all new tests
add_custom_target(run_new_tests
    COMMAND ${CMAKE_COMMAND} -E echo "Running Complex Workload Tests..."
    COMMAND $<TARGET_FILE:complex_workload_tests>
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Running Performance Tests..."
    COMMAND $<TARGET_FILE:performance_tests>
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Running Pressure Tests..."
    COMMAND $<TARGET_FILE:pressure_tests>
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Running Worker Pool Tests..."
    COMMAND $<TARGET_FILE:worker_pool_tests>
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Running Interval Tests..."
    COMMAND $<TARGET_FILE:interval_tests>
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Running Thread Verifier Tests..."
    COMMAND $<TARGET_FILE:thread_verifier_tests>
    DEPENDS build_new_tests
    COMMENT "Running all new implementation tests"
)

# Individual test targets for convenience
add_custom_target(run_complex_tests
    COMMAND $<TARGET_FILE:complex_workload_tests>
    DEPENDS complex_workload_tests
    COMMENT "Running complex workload tests"
)

add_custom_target(run_performance_tests
    COMMAND $<TARGET_FILE:performance_tests>
    DEPENDS performance_tests
    COMMENT "Running performance tests"
)

add_custom_target(run_pressure_tests
    COMMAND $<TARGET_FILE:pressure_tests>
    DEPENDS pressure_tests
    COMMENT "Running pressure tests"
)

add_custom_target(run_worker_pool_tests
    COMMAND $<TARGET_FILE:worker_pool_tests>
    DEPENDS worker_pool_tests
    COMMENT "Running worker pool tests"
)

add_custom_target(run_interval_tests
    COMMAND $<TARGET_FILE:interval_tests>
    DEPENDS interval_tests
    COMMENT "Running interval tests"
)

add_custom_target(run_thread_verifier_tests
    COMMAND $<TARGET_FILE:thread_verifier_tests>
    DEPENDS thread_verifier_tests
    COMMENT "Running thread verifier tests"
)
