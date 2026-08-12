idf_component_register(
    SRCS 
        "main.cpp"
        "test_solar_sensor.cpp"
        "test_solar_sensor_nvs.cpp"
        "test_ina_sensor_task.cpp"
        "test_command_handler.cpp"
        "test_day_night_controller.cpp"
        "test_ota_controller.cpp"
        "../../../main/src/solar_sensor.cpp"
        "../../../main/src/solar_sensor_nvs.cpp"
        "../../../main/src/ina_sensor_task.cpp"
        "../../../main/src/command_handler.cpp"
        "../../../main/src/day_night_controller.cpp"
        "../../../main/src/ota_controller.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main"
        "../../../components/smart-farm-common/host_test/common"
        "../../../components/idf_hals/mocks"
        "../../../components/power_control/include"
        "../../../components/power_control/include/interfaces"
        "../../../components/ina226_driver/include"
        "../../../components/ina226_driver/include/interfaces"
        "../../../components/ina226_driver/mocks"
        "../../../components/battery_monitor/include"
        "../../../components/battery_monitor/include/interfaces"
        "../../../components/battery_monitor/host_test/mocks"
        "../../../components/ota_manager/host_test/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        espnow_manager
        wifi_manager
        idf_hals
        ota_manager
        time_manager
        power_control
        ina226_driver
        battery_monitor
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()