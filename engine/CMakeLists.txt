add_library(rifftrack_engine STATIC
    source/AudioDevices.cpp
    source/AudioEngine.cpp
    source/DropoutDetector.cpp
    source/LatencyCalibrator.cpp
    source/Metronome.cpp
    source/MicrophoneAccess.mm
    source/TakeWriter.cpp
    source/TrackPlayer.cpp)

target_include_directories(rifftrack_engine PUBLIC include)

target_link_libraries(rifftrack_engine
    PUBLIC
        rifftrack_juce
        "-framework Accelerate"
        "-framework AVFoundation"
    PRIVATE
        juce::juce_recommended_warning_flags)
