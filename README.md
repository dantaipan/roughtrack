# RiffTrack

A recording console for guitarists on macOS, in the spirit of a Portastudio. Start with one track and add more as you need them. RiffTrack is a working name.

## Build

Requires macOS 13+, Xcode, CMake and Ninja (`brew install cmake ninja`).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The app is built to `build/debug/app/RiffTrack_artefacts/Debug/RiffTrack.app`. Builds are signed with your Apple Development certificate if you have one, and ad-hoc otherwise. Pass `-DRIFFTRACK_CODESIGN_IDENTITY=...` to choose another identity.

## Sessions

Each session is a folder in `~/Music/RiffTrack`, holding its takes, `session.xml` and `inserts.xml`. It saves automatically, and the folder can be moved.

Editing never changes a recording. Takes are always dry, and clips only say which part of a take plays and when. A new recording plays over what's already there, so punching in replaces only the part it covers. Press **Record** during playback to punch in at that moment, and **Stop** to end the take.

A new session starts with one guitar track. **+ Track** adds more, up to 24. Double-click a track's name to rename it, and right-click it to delete the track. Deleting a track leaves its recordings in the session folder.

On the timeline, drag a clip to move it and drag its edges to trim it. Right-click for Split Here, Delete Clip, and Add Take at Playhead, which brings back any take in the session folder. Command-Z undoes, Shift-Command-Z redoes, Space plays and stops, and Delete removes the selected clip.

## Console

**Console** (or the X key) swaps the timeline for a full mixing desk: every track as a channel with its input, polarity, plugins, pan, Arm, Mon, mute, solo, fader and VU meter, plus the master fader. Beside the timeline, tracks shrink to a short row, and they're the same controls in both views.

An armed track's meter shows what's arriving at its input, whether or not Mon is on, because that's what gets recorded. Set your level on the interface so the needle sits around 0, which is a sine peaking at -18 dBFS. The light on a meter means a level near full scale, and it stays on until you click it. **Ø** flips a track's polarity in the mixer and leaves the take as recorded.

**Tuner** opens a strobe tuner across the top of the window. It reads the input of the first armed audio track, or of the first audio track if none is armed. The bands drift right when a string is sharp, left when it's flat, and stand still in tune. Play one string at a time: a chord has no single pitch, and an open E chord reads as E1.

## Amps and effects

Each track's **Channel** button opens its amp, cabinet, EQ, compressor and sends. The signal runs amp and cabinet, plugin inserts, EQ, compressor, fader, then the sends. **Effects** holds the reverb and delay that every track shares. None of this adds latency, and takes always stay dry, so any of it can change after recording.

RiffTrack doesn't ship amp captures. Load your own `.nam` files, including A2, and any `.wav` or `.aiff` cabinet IR. Sessions remember captures and IRs by their full path, so moving those files leaves a session reporting them as missing.

CPU depends on the capture. On an M4 at 48 kHz, a standard capture takes about 5% of a core and an A2 capture about 3%, while the largest (32-channel) captures take about 19%, which limits a session to two or three of them at any buffer size. The status bar shows CPU live and turns amber above 80%.

## Checking real hardware

`rifftrack_bench` runs the checks that need a real device. It's built to `build/<preset>/tools/`.

```sh
rifftrack_bench soak 1800 --device "Scarlett 2i2 4th Gen" --buffer 64 --record
rifftrack_bench overdub-check --device "Scarlett 2i2 4th Gen" --buffer 64
rifftrack_bench scan-plugins
rifftrack_bench plugin-check --buffer 64 --name TONEX
rifftrack_bench plugin-window VST3 NeuralAmpModeler
rifftrack_bench amp-check capture.nam --instances 4
rifftrack_bench soak 60 --amp capture.nam --cabinet cabinet.wav --full-channel
rifftrack_bench tuner-check take.wav
```

`overdub-check` plays test sweeps, so turn your monitors down and connect output 1 to input 1 with a cable first.

`scan-plugins` lists every installed VST3 and Audio Unit, each loaded in its own process. `plugin-check` runs third-party effects through the engine with no device. It loads them with default settings, so its timings don't show the cost of a loaded amp model. `plugin-window` opens an editor the way the app does and prints its native views.

`RiffTrack --snapshot <session folder> <png file> [console] [timeline] [tuner]` renders the real window on a session and quits, so the layout can be checked without anyone at the screen.

## Drums

Choose **Drums** under **+ Track**, then right-click its lane and use **Add Groove Here**. Grooves are real drummers' performances played through a sampled kit, so they follow the session tempo without any time-stretching. They're counted in bars: drag a part to move it, or drag its right edge past the end of the groove to loop it. **Add Fill Here** drops a fill over the bars it covers, replacing the groove underneath. The BPM on each groove and fill is the tempo the drummer played it at. Everything plays at the session tempo, and the menus list what was recorded within 5% of it first, because that sounds most natural. Parts are saved with the session and undo like clips. Changing the tempo moves parts in time, and recorded audio stays where it is, so set the tempo first.

`resources/drums` is generated. `tools/build_grooves.py` cuts `grooves.xml` from the Groove MIDI Dataset, and `rifftrack_bench build-kit` mixes DRSKit to stereo. See [CREDITS](resources/drums/CREDITS.md).

## MIDI

Choose **MIDI** under **+ Track**. A new track starts on the General MIDI synth built into macOS; click its instrument button to pick any AU or VST3 instrument, or to open its window. Every connected keyboard is used, and the status bar lists them. Arm the track to play it, and press Record to capture a take. Takes are standard `.mid` files in the session folder, and clips move, trim, split and undo like audio, snapping to sixteenth notes. Like drums, they follow the tempo.

Right-click a MIDI clip and choose **Quantize** to tighten it to quarters, eighths, sixteenths or triplets. This writes a new take beside the original, so the performance as played is never changed. Undo goes straight back, and the original stays under **Add Take at Playhead**.

## Layout

- `engine/`: audio engine library, with no UI code
- `app/`: the macOS application
- `tests/`: Catch2 tests for the engine
- `tools/`: `rifftrack_bench`, for checks on real hardware

## Credits

- [JUCE](https://juce.com), under AGPLv3
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore), MIT, with [Eigen](https://eigen.tuxfamily.org) (MPL 2.0) and [nlohmann/json](https://github.com/nlohmann/json) (MIT)
- [Catch2](https://github.com/catchorg/Catch2), BSL 1.0, for tests
- DRSKit by Lars Muldjord and Bent Bisballe Nyeng, CC BY 4.0. Drum samples provided by [DrumGizmo.org](https://www.drumgizmo.org)
- [Groove MIDI Dataset](https://magenta.withgoogle.com/datasets/groove) by Gillick, Roberts, Rob and Bansal, CC BY 4.0

## License

AGPL-3.0-or-later. See [LICENSE](LICENSE). RiffTrack uses [JUCE](https://juce.com) under its AGPLv3 option.
