#include "Updates.h"

namespace updates
{

namespace
{

constexpr int timeoutMs = 10000;

juce::Array<int> versionParts (const juce::String& version)
{
    juce::Array<int> parts;

    for (const auto& part : juce::StringArray::fromTokens (version.trim().trimCharactersAtStart ("vV"), ".", {}))
        parts.add (part.getIntValue());

    return parts;
}

/** Adds a line's text, with **bold** spans, [links](url) as their text, and images and
    `code` marks taken out.
*/
void addInline (std::vector<NoteRun>& runs, const juce::String& line)
{
    juce::String plain;
    auto bold = false;

    const auto flush = [&]
    {
        if (plain.isNotEmpty())
            runs.push_back ({ plain, bold, false });

        plain.clear();
    };

    for (int i = 0; i < line.length();)
    {
        if (line.substring (i).startsWith ("**"))
        {
            flush();
            bold = ! bold;
            i += 2;
            continue;
        }

        const auto isImage = line.substring (i).startsWith ("![");

        if (isImage || line[i] == '[')
        {
            const auto textStart = i + (isImage ? 2 : 1);
            const auto textEnd = line.indexOf (textStart, "](");
            const auto urlEnd = textEnd >= 0 ? line.indexOfChar (textEnd + 2, ')') : -1;

            if (urlEnd >= 0)
            {
                if (! isImage)
                    plain << line.substring (textStart, textEnd);

                i = urlEnd + 1;
                continue;
            }
        }

        if (line[i] != '`')
            plain << line[i];

        ++i;
    }

    flush();
}

} // namespace

bool isNewerVersion (const juce::String& candidate, const juce::String& current)
{
    const auto newer = versionParts (candidate);
    const auto older = versionParts (current);

    for (int i = 0; i < juce::jmax (newer.size(), older.size()); ++i)
    {
        const auto a = newer[i], b = older[i];

        if (a != b)
            return a > b;
    }

    return false;
}

std::optional<Release> parseLatestRelease (const juce::var& json)
{
    if (! json.isObject())
        return std::nullopt;

    Release release;
    release.version = json["tag_name"].toString();
    release.notes = json["body"].toString();
    release.pageUrl = json["html_url"].toString();
    release.downloadUrl = release.pageUrl;

    if (release.version.isEmpty() || release.pageUrl.isEmpty() || versionParts (release.version).isEmpty()
        || static_cast<bool> (json["draft"]) || static_cast<bool> (json["prerelease"]))
        return std::nullopt;

    if (const auto* assets = json["assets"].getArray())
        for (const auto& asset : *assets)
            if (asset["name"].toString().endsWithIgnoreCase (".dmg"))
                release.downloadUrl = asset["browser_download_url"].toString();

    return release;
}

bool shouldOffer (const Release& release, const juce::String& currentVersion, const juce::String& skippedVersion, bool isManual)
{
    if (! isNewerVersion (release.version, currentVersion))
        return false;

    return isManual || release.version != skippedVersion;
}

std::vector<NoteRun> notesToRuns (const juce::String& markdown)
{
    std::vector<NoteRun> runs;
    auto skippingSection = false;
    auto lastWasBlank = true;

    for (auto line : juce::StringArray::fromLines (markdown))
    {
        line = line.trimEnd();

        if (line.startsWith ("#"))
        {
            const auto title = line.trimCharactersAtStart ("#").trim();

            // The window has its own Download button, so the notes' download section repeats it.
            skippingSection = title.equalsIgnoreCase ("Download");

            if (! skippingSection)
            {
                if (! lastWasBlank)
                    runs.push_back ({ "\n" });

                runs.push_back ({ title + "\n", true, true });

                // What follows starts straight under the heading.
                lastWasBlank = true;
            }

            continue;
        }

        const auto trimmed = line.trimStart();

        if (skippingSection || trimmed.startsWith ("![") || trimmed.startsWithIgnoreCase ("SHA-256"))
            continue;

        if (trimmed.isEmpty())
        {
            if (! lastWasBlank)
                runs.push_back ({ "\n" });

            lastWasBlank = true;
            continue;
        }

        if (trimmed.startsWith ("- ") || trimmed.startsWith ("* "))
        {
            runs.push_back ({ juce::String::fromUTF8 ("\xe2\x80\xa2 ") });
            addInline (runs, trimmed.substring (2));
        }
        else
        {
            addInline (runs, trimmed);
        }

        runs.push_back ({ "\n" });
        lastWasBlank = false;
    }

    while (! runs.empty() && runs.back().text.trim().isEmpty())
        runs.pop_back();

    return runs;
}

juce::String runsToText (const std::vector<NoteRun>& runs)
{
    juce::String text;

    for (const auto& run : runs)
        text << run.text;

    return text;
}

std::optional<Release> fetchLatestRelease (const juce::String& currentVersion, juce::String& error)
{
    // GitHub asks every request to say what it comes from.
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: RoughTrack/" + currentVersion)
                             .withConnectionTimeoutMs (timeoutMs);

    const auto stream = juce::URL (latestReleaseUrl).createInputStream (options);

    if (stream == nullptr)
    {
        error = "Couldn't reach GitHub. Check your internet connection.";
        return std::nullopt;
    }

    const auto release = parseLatestRelease (juce::JSON::parse (stream->readEntireStreamAsString()));

    if (! release.has_value())
        error = "GitHub's answer didn't include a release.";

    return release;
}

} // namespace updates
