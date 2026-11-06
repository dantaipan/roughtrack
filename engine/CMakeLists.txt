add_library(roughtrack_engine STATIC
    source/AmpModel.cpp
    source/AudioDevices.cpp
    source/AudioEngine.cpp
    source/ChannelStrip.cpp
    source/Clip.cpp
    source/DropoutDetector.cpp
    source/DrumKit.cpp
    source/Drums.cpp
    source/DrumSampler.cpp
    source/Grid.cpp
    source/InsertChain.cpp
    source/LatencyCalibrator.cpp
    source/Loops.cpp
    source/Metronome.cpp
    source/MicrophoneAccess.mm
    source/Midi.cpp
    source/MidiSequencer.cpp
    source/PluginLibrary.cpp
    source/PluginScanner.cpp
    source/Session.cpp
    source/TakeWriter.cpp
    source/TrackInstrument.cpp
    source/TrackPlayer.cpp
    source/Tuner.cpp)

target_include_directories(roughtrack_engine PUBLIC include)

target_link_libraries(roughtrack_engine
    PUBLIC
        roughtrack_juce
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,roughtrack_nam>"
        "-framework Accelerate"
        "-framework AVFoundation"
    PRIVATE
        juce::juce_recommended_warning_flags)
