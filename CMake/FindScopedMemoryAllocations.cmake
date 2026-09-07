# Test-only dependency. AllocationsChecker is an INTERFACE target whose sources are
# compiled into whatever links it, interposing malloc/new for that binary — so it
# belongs to the test executable and must never reach the library or the apps.
#
# The interposition needs dlsym(RTLD_NEXT, ...), which Windows lacks, so this module
# is only pulled in on Apple and Linux and the suites that need it are left out of
# the build elsewhere - see Tests/CMakeLists.txt.
#
# To point at a local checkout instead of fetching, configure with
# -DCPM_ScopedMemoryAllocations_SOURCE="Path_To_ScopedMemoryAllocations".

include(CPM)

CPMAddPackage("gh:eyalamirmusic/ScopedMemoryAllocations#main")
