cmake_minimum_required(VERSION 3.16)
project(solar_sensor_host_tests NONE)

# Enable CTest for this project
include(CTest)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT EXISTS "${PROJECT_ROOT}/main/include/secrets.hpp")
    if(EXISTS "${PROJECT_ROOT}/main/include/secrets.example.hpp")
        message(STATUS "secrets.hpp not found, creating from secrets.example.hpp")
        configure_file("${PROJECT_ROOT}/main/include/secrets.example.hpp"
                       "${PROJECT_ROOT}/main/include/secrets.hpp" COPYONLY)
    endif()
endif()

# Define the tests for CTest.
add_test(NAME test_solar_sensor COMMAND ../test_solar_sensor/build/test_solar_sensor.elf)
add_test(NAME test_slow_sensors_task COMMAND ../test_slow_sensors_task/build/test_slow_sensors_task.elf)
add_test(NAME test_command_handler COMMAND ../test_command_handler/build/test_command_handler.elf)
add_test(NAME test_day_night_controller COMMAND ../test_day_night_controller/build/test_day_night_controller.elf)
add_test(NAME test_ota_controller COMMAND ../test_ota_controller/build/test_ota_controller.elf)
add_test(NAME test_solar_sensor_nvs COMMAND ../test_solar_sensor_nvs/build/test_solar_sensor_nvs.elf)
add_test(NAME test_ina_sensor_task COMMAND ../test_ina_sensor_task/build/test_ina_sensor_task.elf)
add_test(NAME test_led_controller COMMAND ../test_led_controller/build/test_led_controller.elf)

# Unified Coverage Configuration
find_program(LCOV_PATH lcov REQUIRED)
find_program(GENHTML_PATH genhtml REQUIRED)

# The unified coverage report will be placed in host_test/coverage
set(UNIFIED_COVERAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/coverage")

# This target aggregates coverage data (.gcda files) from all test build directories
add_custom_target(unified_coverage
    COMMAND ${LCOV_PATH} --capture --directory .. --output-file unified_coverage.info --rc branch_coverage=1 --rc geninfo_unexecuted_blocks=1 --ignore-errors mismatch,inconsistent,source
    COMMAND ${LCOV_PATH} --extract unified_coverage.info "*/main/src/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    COMMAND ${LCOV_PATH} --remove unified_coverage_filtered.info "*/main/include/interfaces/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    COMMAND ${GENHTML_PATH} unified_coverage_filtered.info --output-directory ${UNIFIED_COVERAGE_DIR} --title "Solar Sensor Unified Coverage" --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,source
    COMMAND ${LCOV_PATH} --list unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty,source
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating unified coverage report from all host tests"
)

# Cleanup target to manually clear previous results if needed
add_custom_target(coverage_clean
    COMMAND find .. -name "*.gcda" -delete
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${UNIFIED_COVERAGE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Cleaning up coverage data"
)

# Helper target to build all tests at once
add_custom_target(build_all_tests
    COMMAND idf.py -C ../test_solar_sensor build
    COMMAND idf.py -C ../test_slow_sensors_task build
    COMMAND idf.py -C ../test_command_handler build
    COMMAND idf.py -C ../test_day_night_controller build
    COMMAND idf.py -C ../test_ota_controller build
    COMMAND idf.py -C ../test_solar_sensor_nvs build
    COMMAND idf.py -C ../test_ina_sensor_task build
    COMMAND idf.py -C ../test_led_controller build
    COMMENT "Building all test projects using idf.py"
)

# Unified target to build, clean, run all tests and generate coverage
add_custom_target(run_all_tests
    COMMAND ${CMAKE_COMMAND} --build . --target build_all_tests
    COMMAND ${CMAKE_COMMAND} --build . --target coverage_clean
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND ${CMAKE_COMMAND} --build . --target unified_coverage
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Full test cycle: build, clean, run, and coverage"
)
