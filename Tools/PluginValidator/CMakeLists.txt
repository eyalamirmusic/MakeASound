# PluginValidator: pluginval over the bundles and folders on its command line. It
# knows nothing of this build's plugins, so point it at <build>/VST3 or a .vst3.
add_executable(PluginValidator Main.cpp)
target_link_libraries(PluginValidator PRIVATE MakeASoundPluginval)
set_makeasound_target_settings(PluginValidator)
