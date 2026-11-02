add_executable(rifftrack_bench
    Bench.cpp
    BuildDrumKit.cpp
    NativeViews.mm
    ../app/source/PluginWindow.cpp)

target_include_directories(rifftrack_bench PRIVATE ../app/source)

target_link_libraries(rifftrack_bench PRIVATE
    rifftrack_engine
    juce::juce_recommended_warning_flags)
