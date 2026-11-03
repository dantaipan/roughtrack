juce_add_gui_app(RiffTrack
    PRODUCT_NAME "RiffTrack"
    COMPANY_NAME "Dan Mullins"
    BUNDLE_ID "com.danmullins.rifftrack"
    ICON_BIG "${CMAKE_SOURCE_DIR}/resources/icon/icon-macos-1024.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "RiffTrack records from your audio interface inputs."
    PLIST_TO_MERGE "<plist><dict><key>NSDocumentsFolderUsageDescription</key><string>Plugins you load in RiffTrack, such as TONEX and AmpliTube, keep their presets and tones in your Documents folder.</string></dict></plist>")

juce_add_binary_data(RiffTrackAssets SOURCES "${CMAKE_SOURCE_DIR}/resources/icon/icon-macos-1024.png")

target_sources(RiffTrack PRIVATE
    source/AboutWindow.cpp
    source/ChannelWindow.cpp
    source/ConsoleLookAndFeel.cpp
    source/Main.cpp
    source/MainComponent.cpp
    source/MasterStrip.cpp
    source/ParameterPanel.cpp
    source/PluginWindow.cpp
    source/TimelineView.cpp
    source/ToolbarButtons.cpp
    source/TrackStrip.cpp
    source/TunerRibbon.cpp
    source/VuMeter.cpp)

target_compile_definitions(RiffTrack PRIVATE
    RIFFTRACK_VERSION="${PROJECT_VERSION}")

target_link_libraries(RiffTrack PRIVATE
    RiffTrackAssets
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

# Notarization needs Apple's secure timestamp, which local builds skip to stay quick and offline.
if(RIFFTRACK_CODESIGN_IDENTITY MATCHES "Developer ID")
    set(rifftrack_timestamp "--timestamp")
else()
    set(rifftrack_timestamp "--timestamp=none")
endif()

# JUCE's icon holds only two sizes, so the full hand-drawn set replaces it.
file(GLOB rifftrack_icon_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/icon/AppIcon.iconset/*.png")
set(rifftrack_icon "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns")

add_custom_command(OUTPUT "${rifftrack_icon}"
    COMMAND iconutil -c icns -o "${rifftrack_icon}" "${CMAKE_SOURCE_DIR}/resources/icon/AppIcon.iconset"
    DEPENDS ${rifftrack_icon_files}
    VERBATIM)

add_custom_target(RiffTrackIcon DEPENDS "${rifftrack_icon}")
add_dependencies(RiffTrack RiffTrackIcon)

# The copies below only run when the app links, so the drums and icon have to trigger that.
file(GLOB rifftrack_drum_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/drums/*")
set_property(TARGET RiffTrack APPEND PROPERTY LINK_DEPENDS ${rifftrack_drum_files} "${rifftrack_icon}")

add_custom_command(TARGET RiffTrack POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/drums"
        "$<TARGET_BUNDLE_CONTENT_DIR:RiffTrack>/Resources/Drums"
    COMMAND "${CMAKE_COMMAND}" -E copy "${rifftrack_icon}" "$<TARGET_BUNDLE_CONTENT_DIR:RiffTrack>/Resources/AppIcon.icns"
    COMMAND codesign --force --options runtime ${rifftrack_timestamp}
        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/Entitlements.plist"
        --sign "${RIFFTRACK_CODESIGN_IDENTITY}"
        "$<TARGET_BUNDLE_DIR:RiffTrack>"
    VERBATIM)
