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

## Checking real hardware

`rifftrack_bench` runs the checks that need a real device. It's built to `build/<preset>/tools/`.

```sh
rifftrack_bench soak 1800 --device "Scarlett 2i2 4th Gen" --buffer 64 --record
rifftrack_bench overdub-check --device "Scarlett 2i2 4th Gen" --buffer 64
```

`overdub-check` plays test sweeps, so turn your monitors down and connect output 1 to input 1 with a cable first.

## Layout

- `engine/`: audio engine library, with no UI code
- `app/`: the macOS application
- `tests/`: Catch2 tests for the engine
- `tools/`: `rifftrack_bench`, for checks on real hardware

## License

AGPL-3.0-or-later. See [LICENSE](LICENSE). RiffTrack uses [JUCE](https://juce.com) under its AGPLv3 option.
