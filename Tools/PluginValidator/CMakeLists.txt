# PluginValidator: pluginval, auval or aaxval over the bundles and folders on its
# command line. It knows nothing of this build's plugins, so point it at
# <build>/VST3, <build>/AU, <build>/AAX or a single bundle.
add_executable(PluginValidator Main.cpp)
target_link_libraries(PluginValidator PRIVATE MakeASoundPluginval)
set_makeasound_target_settings(PluginValidator)

# The --help text is data, not code: embedded so the binary carries it with no
# file to ship. The ResEmbed runtime key is the file's basename, so the files
# must keep their names. Each paragraph is one line, which the parser wraps.
find_package(ResEmbed REQUIRED)
res_embed_add(PluginValidator
        FILES     Resources/Description.txt
                  Resources/Epilogue.txt
        NAMESPACE PluginValidatorHelp
        CATEGORY  PluginValidatorHelp)
