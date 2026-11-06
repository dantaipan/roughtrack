#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

/** Finding out whether a newer RoughTrack is out, from its GitHub releases. Nothing is
    sent beyond an ordinary request for the latest release.
*/
namespace updates
{

inline constexpr auto latestReleaseUrl = "https://api.github.com/repos/dantaipan/roughtrack/releases/latest";

struct Release
{
    juce::String version;     // As tagged, such as "v0.10.0".
    juce::String notes;       // The release notes, in Markdown.
    juce::String pageUrl;
    juce::String downloadUrl; // The disk image, or the release page if there isn't one.
};

/** Compares version numbers part by part, so 0.10 is newer than 0.9. A leading "v" is ignored. */
bool isNewerVersion (const juce::String& candidate, const juce::String& current);

/** Reads GitHub's answer for the latest release, or nothing if it isn't one. */
std::optional<Release> parseLatestRelease (const juce::var& json);

/** Whether to tell the player about a release. Automatic checks leave out a version they
    chose to skip; asking from the menu always shows it.
*/
bool shouldOffer (const Release&, const juce::String& currentVersion, const juce::String& skippedVersion, bool isManual);

/** A stretch of release notes in one style, ready to show. */
struct NoteRun
{
    juce::String text;
    bool bold = false;
    bool heading = false;
};

/** Turns the Markdown of release notes into styled text. Headings, bullets, bold and links
    come through; images, and the download section and checksum that only make sense on the
    web page, are left out.
*/
std::vector<NoteRun> notesToRuns (const juce::String& markdown);
juce::String runsToText (const std::vector<NoteRun>&);

/** Asks GitHub for the latest release. Blocks for up to the timeout, so it's run off the
    message thread. On failure it says why in error.
*/
std::optional<Release> fetchLatestRelease (const juce::String& currentVersion, juce::String& error);

} // namespace updates
