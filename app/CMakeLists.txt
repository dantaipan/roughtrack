juce_add_gui_app(RoughTrack
    PRODUCT_NAME "RoughTrack"
    COMPANY_NAME "Dan Mullins"
    BUNDLE_ID "com.danmullins.roughtrack"
    ICON_BIG "${CMAKE_SOURCE_DIR}/resources/icon/icon-macos-1024.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "RoughTrack records from your audio interface inputs."
    PLIST_TO_MERGE "<plist><dict><key>NSDocumentsFolderUsageDescription</key><string>Plugins you load in RoughTrack, such as TONEX and AmpliTube, keep their presets and tones in your Documents folder.</string></dict></plist>")

juce_add_binary_data(RoughTrackAssets SOURCES "${CMAKE_SOURCE_DIR}/resources/icon/icon-macos-1024.png")

target_sources(RoughTrack PRIVATE
    source/AboutWindow.cpp
    source/ChannelWindow.cpp
    source/ConsoleLookAndFeel.cpp
    source/ConsolePanel.cpp
    source/GroovePreview.cpp
    source/Main.cpp
    source/MainComponent.cpp
    source/MasterStrip.cpp
    source/ParameterPanel.cpp
    source/PluginWindow.cpp
    source/TimelineView.cpp
    source/ToolbarButtons.cpp
    source/TrackStrip.cpp
    source/TunerRibbon.cpp
    source/Updates.cpp
    source/UpdateWindow.cpp
    source/VuMeter.cpp)

target_compile_definitions(RoughTrack PRIVATE
    ROUGHTRACK_VERSION="${PROJECT_VERSION}")

target_link_libraries(RoughTrack PRIVATE
    RoughTrackAssets
    roughtrack_engine
    juce::juce_recommended_warning_flags)

# Plugin hosting only works under Hardened Runtime with these entitlements, so
# every build runs signed with them instead of finding out at release time.
# Without an Apple Development certificate this falls back to ad-hoc signing.
execute_process(COMMAND security find-identity -v -p codesigning
    OUTPUT_VARIABLE roughtrack_identities
    ERROR_QUIET)

if(roughtrack_identities MATCHES "Apple Development")
    set(roughtrack_default_identity "Apple Development")
else()
    set(roughtrack_default_identity "-")
endif()

set(ROUGHTRACK_CODESIGN_IDENTITY "${roughtrack_default_identity}" CACHE STRING "codesign identity for local builds")

# Notarization needs Apple's secure timestamp, which local builds skip to stay quick and offline.
if(ROUGHTRACK_CODESIGN_IDENTITY MATCHES "Developer ID")
    set(roughtrack_timestamp "--timestamp")
else()
    set(roughtrack_timestamp "--timestamp=none")
endif()

# JUCE's icon holds only two sizes, so the full hand-drawn set replaces it.
file(GLOB roughtrack_icon_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/icon/AppIcon.iconset/*.png")
set(roughtrack_icon "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns")

add_custom_command(OUTPUT "${roughtrack_icon}"
    COMMAND iconutil -c icns -o "${roughtrack_icon}" "${CMAKE_SOURCE_DIR}/resources/icon/AppIcon.iconset"
    DEPENDS ${roughtrack_icon_files}
    VERBATIM)

add_custom_target(RoughTrackIcon DEPENDS "${roughtrack_icon}")
add_dependencies(RoughTrack RoughTrackIcon)

# The copies below only run when the app links, so the drums and icon have to trigger that.
file(GLOB roughtrack_drum_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/drums/*")
set_property(TARGET RoughTrack APPEND PROPERTY LINK_DEPENDS ${roughtrack_drum_files} "${roughtrack_icon}")

add_custom_command(TARGET RoughTrack POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/drums"
        "$<TARGET_BUNDLE_CONTENT_DIR:RoughTrack>/Resources/Drums"
    COMMAND "${CMAKE_COMMAND}" -E copy "${roughtrack_icon}" "$<TARGET_BUNDLE_CONTENT_DIR:RoughTrack>/Resources/AppIcon.icns"
    COMMAND codesign --force --options runtime ${roughtrack_timestamp}
        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/Entitlements.plist"
        --sign "${ROUGHTRACK_CODESIGN_IDENTITY}"
        "$<TARGET_BUNDLE_DIR:RoughTrack>"
    VERBATIM)
