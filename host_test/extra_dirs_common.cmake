# host_test/extra_dirs_common.cmake
# Common setup for host-based test projects in smart-farm-solar-sensor.

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

list(APPEND EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}/components/smart-farm-common"
    "${PROJECT_ROOT}/components/smart-farm-common/host_test/common"
    "${PROJECT_ROOT}/components/espnow_manager"
    "${PROJECT_ROOT}/components/wifi_manager"
    "${PROJECT_ROOT}/components/idf_hals"
    "${PROJECT_ROOT}/components/ota_manager"
    "${PROJECT_ROOT}/components/time_manager"
    "${PROJECT_ROOT}/components/power_control"
    "${PROJECT_ROOT}/components/ina226_driver"
    "${PROJECT_ROOT}/components/ds18b20_driver"
    "${PROJECT_ROOT}/components/battery_monitor"
    "${PROJECT_ROOT}/host_test/mocks"
    "${PROJECT_ROOT}/host_test/gtest"

    "$ENV{IDF_PATH}/tools/mocks/driver"
    "$ENV{IDF_PATH}/tools/mocks/esp_wifi"
    "$ENV{IDF_PATH}/tools/mocks/esp_netif"
    "$ENV{IDF_PATH}/tools/mocks/esp_event"
    "$ENV{IDF_PATH}/tools/mocks/lwip"
)

set(COMPONENTS 
    "main"
    "smart-farm-common"
    "espnow_manager"
    "ota_manager"
    "time_manager"
    "power_control"
    "ina226_driver"
    "ds18b20_driver"
    "battery_monitor"
    "udp_logger"
    "wifi_manager"
    "esp_timer"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
