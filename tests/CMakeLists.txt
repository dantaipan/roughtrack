add_executable(roughtrack_tests
    AudioDevicesTests.cpp
    AudioEngineTests.cpp
    BounceTests.cpp
    ChannelStripTests.cpp
    ClipTests.cpp
    ConsoleTests.cpp
    CrashRecoveryTests.cpp
    DropoutDetectorTests.cpp
    DrumTests.cpp
    InsertTests.cpp
    LatencyCalibratorTests.cpp
    MetronomeTests.cpp
    MeterTests.cpp
    MidiTests.cpp
    PluginScannerTests.cpp
    SessionTests.cpp
    TakeRoundTripTests.cpp
    TimelineViewTests.cpp
    TunerTests.cpp
    ../app/source/ConsoleLookAndFeel.cpp
    ../app/source/MasterStrip.cpp
    ../app/source/TimelineView.cpp
    ../app/source/TrackStrip.cpp
    ../app/source/TunerRibbon.cpp
    ../app/source/VuMeter.cpp)

target_include_directories(roughtrack_tests PRIVATE ../app/source)

target_link_libraries(roughtrack_tests PRIVATE
    roughtrack_engine
    Catch2::Catch2WithMain)

target_compile_definitions(roughtrack_tests PRIVATE
    ROUGHTRACK_BENCH_PATH="$<TARGET_FILE:roughtrack_bench>"
    ROUGHTRACK_NAM_EXAMPLES="${neuralampmodelercore_SOURCE_DIR}/example_models")

add_dependencies(roughtrack_tests roughtrack_bench)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(roughtrack_tests)
