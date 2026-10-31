include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(JUCE Catch2)

# JUCE modules are INTERFACE libraries whose sources compile into every target
# that links them. Compiling them once here stops the engine, app and tests each
# carrying a copy, which would otherwise collide at link time.
add_library(rifftrack_juce STATIC)

target_link_libraries(rifftrack_juce
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

target_compile_definitions(rifftrack_juce
    PUBLIC
        DONT_SET_USING_JUCE_NAMESPACE=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)

# Linking the modules PRIVATE keeps their sources out of consumers, so their
# include paths and definitions have to be re-exported by hand.
target_include_directories(rifftrack_juce
    INTERFACE $<TARGET_PROPERTY:rifftrack_juce,INCLUDE_DIRECTORIES>)

target_compile_definitions(rifftrack_juce
    INTERFACE $<TARGET_PROPERTY:rifftrack_juce,COMPILE_DEFINITIONS>)
