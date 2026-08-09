idf_component_register(
    SRCS 
        "main.cpp"
        "test_solar_sensor.cpp"
        "../../../main/src/solar_sensor.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main"
        "../../../components/smart-farm-common/host_test/common"
        "../../../components/idf_hals/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        espnow_manager
        wifi_manager
        idf_hals
        ota_manager
        time_manager
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()