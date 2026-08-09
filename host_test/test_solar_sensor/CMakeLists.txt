# host_test/test_solar_sensor/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# List extra component directories so IDF can find our components.
list(APPEND EXTRA_COMPONENT_DIRS
    "../../components/smart-farm-common"
    "../../components/smart-farm-common/host_test/common"
    "../../components/espnow_manager"
    "../../components/wifi_manager"
    "../../components/idf_hals"
    "../../components/ota_manager"
    "../../components/time_manager"
    "../../components/power_control"
    "../../components/ina226_driver"
    "../mocks"
    "../gtest"
    
    "$ENV{IDF_PATH}/tools/mocks/driver"  # Official ESP-IDF driver mocks
    "$ENV{IDF_PATH}/tools/mocks/esp_wifi"  # Official ESP-IDF driver mocks
    "$ENV{IDF_PATH}/tools/mocks/esp_netif"
    "$ENV{IDF_PATH}/tools/mocks/esp_event"
    "$ENV{IDF_PATH}/tools/mocks/lwip"
)

set(COMPONENTS 
    "main"
    "smart-farm-common"
    "espnow_manager"
    "idf_hals"
    "ota_manager"
    "time_manager"
    "power_control"
    "ina226_driver"
    "udp_logger"
    "wifi_manager"
    "esp_timer"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(test_solar_sensor)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_solar_sensor 
    "*/main/src/*"
)

# Set the patterns to remove from the binary
set(REMOVE_PATTERNS_test_solar_sensor 
    "*/main/include/*"
    "*host_test/*"
    "*build/*"
)

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_solar_sensor test_solar_sensor.elf)