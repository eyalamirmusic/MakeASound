add_executable(Example Example.cpp)
target_link_libraries(Example PRIVATE MakeASound)

set_makeasound_warnings(Example)
