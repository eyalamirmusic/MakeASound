eacp_add_webview_app(Demo
        SOURCES     Main.cpp
        API         Api::DemoApi
        API_HEADER  Types.h
        WEB_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/web
        BUNDLE_ID   com.eyalamir.makeasound.demo
        BUNDLE_NAME "MakeASound Demo"
        CATEGORY    DemoWeb
        REACT)

target_link_libraries(Demo PRIVATE MakeASound)
target_link_libraries(DemoSchema_Codegen PRIVATE MakeASound)
set_makeasound_warnings(Demo)
