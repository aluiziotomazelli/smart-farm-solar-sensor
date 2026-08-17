idf_component_register(
    SRCS 
        "main.cpp"
        "test_solar_sensor.cpp"
        "../../../main/src/solar_sensor.cpp"
        "../../../main/src/ina_sensor_task.cpp"
        "../../../main/src/command_handler.cpp"
        "../../../main/src/day_night_controller.cpp"
        "../../../main/src/ota_controller.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main/include/interfaces"
        "../../../main"
        "../../../components/smart-farm-common/include"
        "../../../components/smart-farm-common/include/interfaces"
        "../../../components/smart-farm-common/host_test/common"
        "../../../components/idf_hals/mocks"
        "../../../components/ina226_driver/include"
        "../../../components/ina226_driver/include/interfaces"
        "../../../components/ina226_driver/mocks"
        "../../../components/espnow_manager/include"
        "../../../components/espnow_manager/include/interfaces"
        "../../../components/ota_manager/include"
        "../../../components/ota_manager/include/interfaces"
        "../../../components/ota_manager/host_test/mocks"
        "../../../components/time_manager/include"
        "../../../components/time_manager/include/interfaces"
        "../../../components/time_manager/host_test/mocks"
        "../../../components/wifi_manager/include"
        "../../../components/wifi_manager/include/interfaces"
        "../../../components/battery_monitor/include"
        "../../../components/battery_monitor/include/interfaces"
        "../../../components/battery_monitor/host_test/mocks"
        "../../../components/ds18b20_driver/include"
        "../../../components/ds18b20_driver/include/interfaces"
        "../../../components/ds18b20_driver/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        ina226_driver
        espnow_manager
        ota_manager
        time_manager
        wifi_manager
        battery_monitor
        ds18b20_driver
        idf_hals
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()