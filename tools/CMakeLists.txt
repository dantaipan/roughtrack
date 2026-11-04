add_executable(roughtrack_bench
    Bench.cpp
    BuildDrumKit.cpp
    NativeViews.mm
    ../app/source/PluginWindow.cpp)

target_include_directories(roughtrack_bench PRIVATE ../app/source)

target_link_libraries(roughtrack_bench PRIVATE
    roughtrack_engine
    juce::juce_recommended_warning_flags)
