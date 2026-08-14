idf_component_register(
    SRCS 
        "main.cpp"
        "test_solar_sensor_nvs.cpp"
        "../../../main/src/solar_sensor_nvs.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main/include/interfaces"
        "../../../main"
        "../../../components/smart-farm-common/include"
        "../../../components/smart-farm-common/include/interfaces"
        "../../../components/smart-farm-common/host_test/common"
    REQUIRES 
        gtest
        smart-farm-common
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
