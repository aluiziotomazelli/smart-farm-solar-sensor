# host_test/test_solar_sensor_nvs/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include("../extra_dirs_common.cmake")

project(test_solar_sensor_nvs)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_solar_sensor_nvs 
    "*/main/src/solar_sensor_nvs.cpp"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_solar_sensor_nvs 
    "*/main/include/interfaces/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_solar_sensor_nvs test_solar_sensor_nvs.elf)
