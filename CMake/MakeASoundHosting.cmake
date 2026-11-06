# makeasound_enable_hosting(<target>)
#
# Turns a target that already exists, an app or a plugin, into one that hosts
# third-party plugins. It links MakeASoundHosting and, on macOS, seals an
# executable's exports.
#
# Why seal: Mach-O binds ordinary symbols per image, but C++ weak definitions
# (template instantiations, inline functions, vtables, typeinfo) are coalesced
# across every image in the process onto the first copy dyld sees, which is the
# host's. A plugin built from different revisions of a library the host also
# links (EA::Vector<Something>, Miro, eacp) would then call the host's copy of
# an inline function whose layout it does not share. The mangled name encodes the
# type's name, never its layout, so nothing catches it. An executable that exports
# nothing keeps its definitions to itself, and nothing outside a host has any
# business calling in. A plugin bundle is already sealed by its own export list
# (see MakeASoundPlugin.cmake), so only executables get the flag here.
#
# Nothing else is needed. The VST3 SDK's moduleinit.cpp, which references the
# moduleHandle a plugin's entry file defines, is an object of its own in vst3sdk
# that no host references, so a host executable needs no stand-in for it.
function(makeasound_enable_hosting target)
    if (NOT TARGET MakeASoundHosting)
        message(FATAL_ERROR "makeasound_enable_hosting(${target}): MakeASoundHosting "
                "does not exist. It needs MAKEASOUND_BUILD_HOSTING and "
                "MAKEASOUND_BUILD_PLUGIN, on macOS or Windows.")
    endif ()

    target_link_libraries(${target} PRIVATE MakeASoundHosting)

    get_target_property(target_type ${target} TYPE)

    if (APPLE AND target_type STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE "LINKER:-no_exported_symbols")
    endif ()
endfunction()
