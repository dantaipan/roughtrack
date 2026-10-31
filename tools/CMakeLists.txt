add_executable(rifftrack_bench
    Bench.cpp)

target_link_libraries(rifftrack_bench PRIVATE
    rifftrack_engine
    juce::juce_recommended_warning_flags)
