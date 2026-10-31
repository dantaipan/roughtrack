add_executable(rifftrack_tests
    AudioDevicesTests.cpp
    AudioEngineTests.cpp
    CrashRecoveryTests.cpp
    DropoutDetectorTests.cpp
    LatencyCalibratorTests.cpp
    MetronomeTests.cpp
    TakeRoundTripTests.cpp)

target_link_libraries(rifftrack_tests PRIVATE
    rifftrack_engine
    Catch2::Catch2WithMain)

target_compile_definitions(rifftrack_tests PRIVATE
    RIFFTRACK_BENCH_PATH="$<TARGET_FILE:rifftrack_bench>")

add_dependencies(rifftrack_tests rifftrack_bench)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(rifftrack_tests)
