idf_component_register(
    SRCS 
        "main.cpp"
        "test_led_controller.cpp"
        "../../../main/src/led_controller.cpp"
    INCLUDE_DIRS 
        "."
        "../../mocks"
        "../../../main/include"
        "../../../main/include/interfaces"
        "../../../main"
        "../../../components/idf_hals/include"
        "../../../components/idf_hals/include/interfaces"
        "../../../components/idf_hals/mocks"
    REQUIRES 
        gtest
        idf_hals
        WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PROJ_HOST_TEST)
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
