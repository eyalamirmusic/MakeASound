eacp_add_webview_app(Synth
        SOURCES     Main.cpp Synth.h AudioProcessor.h SynthUI.h
        WEB_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/web
        BUNDLE_ID   com.eyalamir.makeasound.synth
        BUNDLE_NAME "MakeASound Synth"
        NAMESPACE   WebResources
        CATEGORY    SynthWeb)

target_link_libraries(Synth PRIVATE MakeASound)
set_makeasound_warnings(Synth)
