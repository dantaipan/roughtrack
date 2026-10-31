add_executable(rifftrack_tests
    AudioDevicesTests.cpp
    AudioEngineTests.cpp
    ChannelStripTests.cpp
    ClipTests.cpp
    CrashRecoveryTests.cpp
    DropoutDetectorTests.cpp
    InsertTests.cpp
    LatencyCalibratorTests.cpp
    MetronomeTests.cpp
    PluginScannerTests.cpp
    SessionTests.cpp
    TakeRoundTripTests.cpp
    TimelineViewTests.cpp
    ../app/source/TimelineView.cpp)

target_include_directories(rifftrack_tests PRIVATE ../app/source)

target_link_libraries(rifftrack_tests PRIVATE
    rifftrack_engine
    Catch2::Catch2WithMain)

target_compile_definitions(rifftrack_tests PRIVATE
    RIFFTRACK_BENCH_PATH="$<TARGET_FILE:rifftrack_bench>"
    RIFFTRACK_NAM_EXAMPLES="${neuralampmodelercore_SOURCE_DIR}/example_models")

add_dependencies(rifftrack_tests rifftrack_bench)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(rifftrack_tests)
