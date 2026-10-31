juce_add_gui_app(RiffTrack
    PRODUCT_NAME "RiffTrack"
    COMPANY_NAME "Dan Mullins"
    BUNDLE_ID "com.danmullins.rifftrack"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "RiffTrack records from your audio interface inputs.")

target_sources(RiffTrack PRIVATE
    source/Main.cpp
    source/MainComponent.cpp
    source/TrackStrip.cpp)

target_compile_definitions(RiffTrack PRIVATE
    RIFFTRACK_VERSION="${PROJECT_VERSION}")

target_link_libraries(RiffTrack PRIVATE
    rifftrack_engine
    juce::juce_recommended_warning_flags)

# Plugin hosting only works under Hardened Runtime with these entitlements, so
# every build runs signed with them instead of finding out at release time.
# Without an Apple Development certificate this falls back to ad-hoc signing.
execute_process(COMMAND security find-identity -v -p codesigning
    OUTPUT_VARIABLE rifftrack_identities
    ERROR_QUIET)

if(rifftrack_identities MATCHES "Apple Development")
    set(rifftrack_default_identity "Apple Development")
else()
    set(rifftrack_default_identity "-")
endif()

set(RIFFTRACK_CODESIGN_IDENTITY "${rifftrack_default_identity}" CACHE STRING "codesign identity for local builds")

add_custom_command(TARGET RiffTrack POST_BUILD
    COMMAND codesign --force --options runtime --timestamp=none
        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/Entitlements.plist"
        --sign "${RIFFTRACK_CODESIGN_IDENTITY}"
        "$<TARGET_BUNDLE_DIR:RiffTrack>"
    VERBATIM)
