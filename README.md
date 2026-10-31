# RiffTrack

A fixed 7-track recording console for guitarists on macOS, in the spirit of a Portastudio. RiffTrack is a working name.

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

Editing never changes a recording. Takes are always dry, and clips only say which part of a take plays and when. A new recording plays over what's already there, so punching in replaces only the part it covers.

On the timeline, drag a clip to move it and drag its edges to trim it. Right-click for Split Here, Delete Clip, and Add Take at Playhead, which brings back any take in the session folder. Command-Z undoes, Shift-Command-Z redoes, Space plays and stops, and Delete removes the selected clip.

## Amps and effects

Each track's **Channel** button opens its amp, cabinet, EQ, compressor and sends. The signal runs amp and cabinet, plugin inserts, EQ, compressor, fader, then the sends. **Effects** holds the reverb and delay that every track shares. None of this adds latency, and takes always stay dry, so any of it can change after recording.

RiffTrack doesn't ship amp captures. Load your own `.nam` files, including A2, and any `.wav` or `.aiff` cabinet IR. Sessions remember captures and IRs by their full path, so moving those files leaves a session reporting them as missing.

CPU depends on the capture. On an M4 at 48 kHz, a standard capture takes about 5% of a core and an A2 capture about 3%, while the largest (32-channel) captures take about 19%, which limits a session to two or three of them at any buffer size. The status bar shows CPU live and turns orange above 80%.

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
```

`overdub-check` plays test sweeps, so turn your monitors down and connect output 1 to input 1 with a cable first.

`scan-plugins` lists every installed VST3 and Audio Unit, each loaded in its own process. `plugin-check` runs third-party effects through the engine with no device. It loads them with default settings, so its timings don't show the cost of a loaded amp model. `plugin-window` opens an editor the way the app does and prints its native views.

`RiffTrack --snapshot <session folder> <png file>` renders the real window on a session and quits, so the layout can be checked without anyone at the screen.

## Layout

- `engine/`: audio engine library, with no UI code
- `app/`: the macOS application
- `tests/`: Catch2 tests for the engine
- `tools/`: `rifftrack_bench`, for checks on real hardware

## Credits

- [JUCE](https://juce.com), under AGPLv3
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore), MIT, with [Eigen](https://eigen.tuxfamily.org) (MPL 2.0) and [nlohmann/json](https://github.com/nlohmann/json) (MIT)
- [Catch2](https://github.com/catchorg/Catch2), BSL 1.0, for tests

## License

AGPL-3.0-or-later. See [LICENSE](LICENSE). RiffTrack uses [JUCE](https://juce.com) under its AGPLv3 option.
