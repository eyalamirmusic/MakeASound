add_executable(Synth
        Main.cpp
        Synth.h
        AudioProcessor.h
        SynthUI.h)
target_link_libraries(Synth PRIVATE MakeASound eacp-webview)

set_makeasound_warnings(Synth)

res_embed_add(Synth
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/web
        NAMESPACE WebResources
        CATEGORY SynthWeb)

set(BUNDLE_ID "com.eyalamir.makeasound.synth")

set_target_properties(Synth PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "MakeASound Synth"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})
