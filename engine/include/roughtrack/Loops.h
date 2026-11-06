#pragma once

#include "roughtrack/Clip.h"
#include "roughtrack/Drums.h"

namespace roughtrack
{

/** Where a clip set to loop stops: where the next clip on its track starts, or the
    song's end if none does, but never before the clip's own end. The next clip is the
    nearest later start, whatever its place in the list. In the track's own units.
*/
juce::int64 findLoopEnd (const ClipList&, int index, juce::int64 songEnd);

/** The same for a drum part, in bars. */
int findLoopEnd (const DrumPartList&, int index, int songEndBar);

/** What plays: each looping clip followed by copies of itself up to its loop end, the
    last one cut short. Copies keep the clip's place in the list, so whatever played
    over the clip plays over its copies too. Nothing in the result loops.
*/
ClipList expandLoops (const ClipList&, juce::int64 songEnd);

/** What plays on a drum track. A part already repeats its groove when it's longer than
    it, so a looping part just runs on to its loop end.
*/
DrumPartList expandLoops (const DrumPartList&, int songEndBar);

/** Turns a looping clip into the copies it was playing, up to a new end, and stops it
    looping. Used when its end is dragged, so what you hear doesn't change.
*/
ClipList bakeLoop (const ClipList&, int index, juce::int64 newEnd);

} // namespace roughtrack
