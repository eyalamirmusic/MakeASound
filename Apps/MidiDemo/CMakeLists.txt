add_executable(MidiDemo MidiDemo.cpp)
target_link_libraries(MidiDemo PRIVATE MakeASound)

set_makeasound_warnings(MidiDemo)
