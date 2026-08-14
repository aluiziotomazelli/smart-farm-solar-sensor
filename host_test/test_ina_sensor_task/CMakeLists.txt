# host_test/test_ina_sensor_task/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_ina_sensor_task)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_ina_sensor_task 
    "*/main/src/ina_sensor_task.cpp"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_ina_sensor_task 
    "*/main/include/interfaces/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_ina_sensor_task test_ina_sensor_task.elf)
