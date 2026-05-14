eacp_add_webview_app(Demo
        SOURCES     Main.cpp
        WEB_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/web
        BUNDLE_ID   com.eyalamir.makeasound.demo
        BUNDLE_NAME "MakeASound Demo"
        NAMESPACE   WebResources
        CATEGORY    DemoWeb)

target_link_libraries(Demo PRIVATE MakeASound)
set_makeasound_warnings(Demo)
