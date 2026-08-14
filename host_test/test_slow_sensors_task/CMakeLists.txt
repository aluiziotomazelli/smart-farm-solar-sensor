# host_test/test_slow_sensors_task/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_slow_sensors_task)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_slow_sensors_task 
    "*/main/src/slow_sensors_task.cpp"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_slow_sensors_task 
    "*/main/include/interfaces/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_slow_sensors_task test_slow_sensors_task.elf)
