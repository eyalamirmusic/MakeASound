eacp_add_webview_app(Synth
        SOURCES     Main.cpp Synth.h AudioProcessor.h Types.h
        API         Api::SynthApi
        API_HEADER  Types.h
        WEB_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/web
        BUNDLE_ID   com.eyalamir.makeasound.synth
        BUNDLE_NAME "MakeASound Synth"
        CATEGORY    SynthWeb
        REACT)

target_link_libraries(Synth PRIVATE MakeASound)
target_link_libraries(SynthSchema_Codegen PRIVATE MakeASound)
set_makeasound_warnings(Synth)
