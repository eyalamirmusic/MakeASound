add_executable(Demo Main.cpp)
target_link_libraries(Demo PRIVATE MakeASound eacp-webview)

set_makeasound_warnings(Demo)

res_embed_add(Demo
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/web
        NAMESPACE WebResources
        CATEGORY DemoWeb)

set(BUNDLE_ID "com.eyalamir.makeasound.demo")

set_target_properties(Demo PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "MakeASound Demo"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})
