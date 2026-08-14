idf_component_register(
    SRCS 
        "main.cpp"
        "test_ina_sensor_task.cpp"
        "../../../main/src/ina_sensor_task.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main/include/interfaces"
        "../../../main"
        "../../../components/smart-farm-common/include"
        "../../../components/smart-farm-common/include/interfaces"
        "../../../components/smart-farm-common/host_test/common"
        "../../../components/ina226_driver/include"
        "../../../components/ina226_driver/include/interfaces"
        "../../../components/ina226_driver/mocks"
        "../../../components/espnow_manager/include"
        "../../../components/espnow_manager/include/interfaces"
        "../../../components/time_manager/include"
        "../../../components/time_manager/include/interfaces"
        "../../../components/time_manager/host_test/mocks"
        "../../../components/idf_hals/include"
        "../../../components/idf_hals/include/interfaces"
        "../../../components/idf_hals/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        ina226_driver
        espnow_manager
        time_manager
        idf_hals
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
