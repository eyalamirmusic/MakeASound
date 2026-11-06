# PluginScanner: the console scanner. A sweep, and the worker it relaunches itself
# as, in one binary, which other apps can ship as their scanner. A plain
# executable, not an app bundle: the linker's ad hoc signature is all it needs to
# load third-party bundles here. See Docs/PluginScanning.md for shipping one.
add_executable(PluginScanner Main.cpp)

makeasound_enable_hosting(PluginScanner)
target_link_libraries(PluginScanner PRIVATE MakeASoundScanProtocol eacp-core)
set_makeasound_target_settings(PluginScanner)

# The --help text is data, not code: embedded so the binary carries it with no
# file to ship. The ResEmbed runtime key is the file's basename, so the files
# must keep their names.
find_package(ResEmbed REQUIRED)
res_embed_add(PluginScanner
        FILES     Resources/Description.txt
                  Resources/Epilogue.txt
        NAMESPACE PluginScannerHelp
        CATEGORY  PluginScannerHelp)

# A bare executable has no bundle for AppKit to read LSUIElement from, so the
# process, and every worker, would flash a Dock tile before main() could hide it.
# macOS reads an Info.plist embedded in __TEXT,__info_plist as the dictionary.
if (APPLE)
    set(scanner_info_plist "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")

    target_link_options(PluginScanner PRIVATE
            "LINKER:-sectcreate,__TEXT,__info_plist,${scanner_info_plist}")
    set_property(TARGET PluginScanner APPEND PROPERTY
            LINK_DEPENDS "${scanner_info_plist}")
endif ()
