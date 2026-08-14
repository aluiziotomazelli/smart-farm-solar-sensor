# main/CMakeLists.txt

# Fallback to secrets.example.hpp if secrets.hpp does not exist (e.g. on CI or fresh clone)
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/include/secrets.hpp")
    message(STATUS "secrets.hpp not found, creating from secrets.example.hpp")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/include/secrets.example.hpp"
                   "${CMAKE_CURRENT_LIST_DIR}/include/secrets.hpp" COPYONLY)
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "src/solar_sensor.cpp"
        "src/solar_sensor_nvs.cpp"
        "src/ina_sensor_task.cpp"
        "src/command_handler.cpp"
        "src/day_night_controller.cpp"
        "src/ota_controller.cpp"
        "src/slow_sensors_task.cpp"
        "src/led_controller.cpp"
    INCLUDE_DIRS
        "include"
        "include/interfaces"
        "."
    REQUIRES
        smart-farm-common
        espnow_manager
        wifi_manager
        ota_manager
        time_manager
        battery_monitor
        idf_hals
        ina226_driver
        ds18b20_driver
)