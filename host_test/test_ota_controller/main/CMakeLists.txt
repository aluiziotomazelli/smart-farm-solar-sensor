idf_component_register(
    SRCS 
        "main.cpp"
        "test_ota_controller.cpp"
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
        "../../../components/ota_manager/include"
        "../../../components/ota_manager/include/interfaces"
        "../../../components/ota_manager/host_test/mocks"
        "../../../components/idf_hals/include"
        "../../../components/idf_hals/include/interfaces"
        "../../../components/idf_hals/mocks"
    REQUIRES 
        gtest
        smart-farm-common
        ota_manager
        idf_hals
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
