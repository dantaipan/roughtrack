include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)

# SOURCE_SUBDIR points nowhere so only the sources arrive: the project's own
# CMakeLists builds its tools, and has no library target to link.
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG v0.6.0
    GIT_SHALLOW TRUE
    GIT_SUBMODULES Dependencies/eigen
    SOURCE_SUBDIR no-cmake-project)

# RoughStomp, only for the look the two apps share (ui/rs_ui/Family.h), which
# the app compiles in. SOURCE_SUBDIR points nowhere and submodules are
# skipped, so none of its plugins or its own copy of JUCE come along. To pick
# up a change to the shared look, push it to RoughStomp and move this pin.
FetchContent_Declare(RoughStomp
    GIT_REPOSITORY https://github.com/dantaipan/roughstomp.git
    GIT_TAG c8ce401b6a7b4711da890d1d014a73ac63fdb63b
    GIT_SUBMODULES ""
    SOURCE_SUBDIR no-cmake-project)

FetchContent_MakeAvailable(JUCE Catch2 NeuralAmpModelerCore RoughStomp)

# JUCE modules are INTERFACE libraries whose sources compile into every target
# that links them. Compiling them once here stops the engine, app and tests each
# carrying a copy, which would otherwise collide at link time.
add_library(roughtrack_juce STATIC)

target_link_libraries(roughtrack_juce
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

target_compile_definitions(roughtrack_juce
    PUBLIC
        DONT_SET_USING_JUCE_NAMESPACE=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)

# Linking the modules PRIVATE keeps their sources out of consumers, so their
# include paths and definitions have to be re-exported by hand.
target_include_directories(roughtrack_juce
    INTERFACE $<TARGET_PROPERTY:roughtrack_juce,INCLUDE_DIRECTORIES>)

target_compile_definitions(roughtrack_juce
    INTERFACE $<TARGET_PROPERTY:roughtrack_juce,COMPILE_DEFINITIONS>)

# NAM's model types register themselves from static objects that nothing refers
# to, so consumers link the whole archive or they'd be dropped.
file(GLOB roughtrack_nam_sources CONFIGURE_DEPENDS
    ${neuralampmodelercore_SOURCE_DIR}/NAM/*.cpp
    ${neuralampmodelercore_SOURCE_DIR}/NAM/wavenet/*.cpp)

add_library(roughtrack_nam STATIC ${roughtrack_nam_sources})

target_include_directories(roughtrack_nam SYSTEM
    PUBLIC
        ${neuralampmodelercore_SOURCE_DIR}
        ${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen
        ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann)

target_compile_definitions(roughtrack_nam PUBLIC NAM_SAMPLE_FLOAT)
