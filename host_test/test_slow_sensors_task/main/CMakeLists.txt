idf_component_register(
    SRCS 
        "main.cpp"
        "test_slow_sensors_task.cpp"
        "../../../main/src/slow_sensors_task.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main/include/interfaces"
        "../../../main"
        "../../../components/battery_monitor/include"
        "../../../components/battery_monitor/include/interfaces"
        "../../../components/battery_monitor/host_test/mocks"
        "../../../components/ds18b20_driver/include"
        "../../../components/ds18b20_driver/include/interfaces"
        "../../../components/ds18b20_driver/mocks"
        "../../../components/idf_hals/include"
        "../../../components/idf_hals/include/interfaces"
        "../../../components/idf_hals/mocks"
    REQUIRES 
        gtest
        battery_monitor
        ds18b20_driver
        idf_hals
        smart-farm-common
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
