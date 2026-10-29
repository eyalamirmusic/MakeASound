# MakeASoundScanProtocol: the scan worker protocol the example apps speak
# (--makeasound-scan in, one MAKEASOUND_SCAN: line out, ScanWorkerExit codes),
# and the sweep options of their command lines. Example code over
# MakeASoundHosting, which has no protocol of its own: a product with its own
# scanner writes its own instead of linking this.
find_package(eacp REQUIRED)

add_library(MakeASoundScanProtocol STATIC ScanArgs.cpp ScanWorker.cpp)

target_include_directories(MakeASoundScanProtocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(MakeASoundScanProtocol
        PUBLIC MakeASoundHosting
        PRIVATE eacp-core)

set_makeasound_target_settings(MakeASoundScanProtocol)
