add_library(rifftrack_engine STATIC
    source/AmpModel.cpp
    source/AudioDevices.cpp
    source/AudioEngine.cpp
    source/ChannelStrip.cpp
    source/Clip.cpp
    source/DropoutDetector.cpp
    source/InsertChain.cpp
    source/LatencyCalibrator.cpp
    source/Metronome.cpp
    source/MicrophoneAccess.mm
    source/PluginLibrary.cpp
    source/PluginScanner.cpp
    source/Session.cpp
    source/TakeWriter.cpp
    source/TrackPlayer.cpp)

target_include_directories(rifftrack_engine PUBLIC include)

target_link_libraries(rifftrack_engine
    PUBLIC
        rifftrack_juce
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,rifftrack_nam>"
        "-framework Accelerate"
        "-framework AVFoundation"
    PRIVATE
        juce::juce_recommended_warning_flags)
