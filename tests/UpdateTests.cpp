#include "Updates.h"

#include <catch2/catch_test_macros.hpp>

using namespace updates;

namespace
{

/** GitHub's answer for v0.9.0, cut down to the fields that matter. */
const char* const latestReleaseJson = R"({
  "html_url": "https://github.com/dantaipan/roughtrack/releases/tag/v0.9.0",
  "tag_name": "v0.9.0",
  "name": "RoughTrack 0.9.0",
  "draft": false,
  "prerelease": false,
  "assets": [
    { "name": "RoughTrack-0.9.0.dmg",
      "browser_download_url": "https://github.com/dantaipan/roughtrack/releases/download/v0.9.0/RoughTrack-0.9.0.dmg" }
  ],
  "body": "Hear grooves before placing them.\n\n## Download\n\n**[RoughTrack-0.9.0.dmg](https://example.com/x.dmg)**: open it.\n\n## New\n\n![The timeline](https://example.com/timeline.png)\n\n- **Preview grooves.** Rest on a groove in [the menu](https://example.com).\n- **Snap** to `bars`.\n\nSHA-256 of the disk image: `8e87`"
})";

} // namespace

TEST_CASE ("Versions compare by number, not as text")
{
    CHECK (isNewerVersion ("v0.10.0", "0.9.0"));
    CHECK (isNewerVersion ("0.9.1", "0.9.0"));
    CHECK (isNewerVersion ("v1.0", "0.9.9"));
    CHECK_FALSE (isNewerVersion ("v0.9.0", "0.9.0"));
    CHECK_FALSE (isNewerVersion ("0.9", "0.9.0"));
    CHECK_FALSE (isNewerVersion ("v0.8.1", "0.9.0"));
}

TEST_CASE ("GitHub's answer gives the version, notes and disk image")
{
    const auto release = parseLatestRelease (juce::JSON::parse (latestReleaseJson));

    REQUIRE (release.has_value());
    CHECK (release->version == "v0.9.0");
    CHECK (release->pageUrl == "https://github.com/dantaipan/roughtrack/releases/tag/v0.9.0");
    CHECK (release->downloadUrl == "https://github.com/dantaipan/roughtrack/releases/download/v0.9.0/RoughTrack-0.9.0.dmg");
    CHECK (release->notes.startsWith ("Hear grooves"));
}

TEST_CASE ("Without a disk image, Download goes to the release page")
{
    auto json = juce::JSON::parse (latestReleaseJson);
    json.getDynamicObject()->setProperty ("assets", juce::Array<juce::var>());

    const auto release = parseLatestRelease (json);

    REQUIRE (release.has_value());
    CHECK (release->downloadUrl == release->pageUrl);
}

TEST_CASE ("Anything that isn't a finished release is ignored")
{
    // Such as GitHub's answer when it's limiting requests.
    CHECK_FALSE (parseLatestRelease (juce::JSON::parse (R"({ "message": "API rate limit exceeded" })")).has_value());
    CHECK_FALSE (parseLatestRelease (juce::JSON::parse ("<html>Not found</html>")).has_value());
    CHECK_FALSE (parseLatestRelease ({}).has_value());

    auto draft = juce::JSON::parse (latestReleaseJson);
    draft.getDynamicObject()->setProperty ("prerelease", true);
    CHECK_FALSE (parseLatestRelease (draft).has_value());
}

TEST_CASE ("A skipped version is only offered when asked for")
{
    Release release;
    release.version = "v0.10.0";

    CHECK (shouldOffer (release, "0.9.0", {}, false));
    CHECK_FALSE (shouldOffer (release, "0.9.0", "v0.10.0", false));
    CHECK (shouldOffer (release, "0.9.0", "v0.10.0", true));

    // A newer one than the skipped version is offered again.
    release.version = "v0.11.0";
    CHECK (shouldOffer (release, "0.9.0", "v0.10.0", false));

    // Nothing is offered that isn't newer.
    release.version = "v0.9.0";
    CHECK_FALSE (shouldOffer (release, "0.9.0", {}, true));
}

TEST_CASE ("Release notes read cleanly in the app")
{
    const auto release = parseLatestRelease (juce::JSON::parse (latestReleaseJson));
    REQUIRE (release.has_value());

    const auto runs = notesToRuns (release->notes);
    const auto text = runsToText (runs);

    CHECK (text == juce::String::fromUTF8 ("Hear grooves before placing them.\n\nNew\n\xe2\x80\xa2 Preview grooves. Rest on a groove in the menu.\n\xe2\x80\xa2 Snap to bars."));

    // The download section, the picture and the checksum are for the web page.
    CHECK_FALSE (text.contains ("Download"));
    CHECK_FALSE (text.contains ("timeline"));
    CHECK_FALSE (text.contains ("SHA-256"));

    const auto heading = std::find_if (runs.begin(), runs.end(), [] (const NoteRun& run) { return run.heading; });
    REQUIRE (heading != runs.end());
    CHECK (heading->text == "New\n");

    const auto bold = std::find_if (runs.begin(), runs.end(), [] (const NoteRun& run) { return run.bold && ! run.heading; });
    REQUIRE (bold != runs.end());
    CHECK (bold->text == "Preview grooves.");
}
