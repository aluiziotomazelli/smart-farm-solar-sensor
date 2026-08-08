cmake_minimum_required(VERSION 3.16)

# Define the project name
set(PROJECT_NAME "solar_sensor")

# Set default sdkconfig profiles for development if not explicitly passed
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.esp32c3;sdkconfig.defaults.dev")
endif()

# Add paths for components (submodules and shared components)
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/idf_hals"
    "${CMAKE_CURRENT_LIST_DIR}/components/smart-farm-common"
    "${CMAKE_CURRENT_LIST_DIR}/components/espnow_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/wifi_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/ota_manager"
    "${CMAKE_CURRENT_LIST_DIR}/components/time_manager"
    

)

# Include the ESP-IDF framework
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project
project(${PROJECT_NAME})
