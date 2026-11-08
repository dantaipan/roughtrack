# RoughTrack

A simplified multitrack DAW for macOS, built with JUCE. README.md covers
using, building and releasing it.

## Layout

- `engine/`: audio engine, sessions, plugin hosting (no GUI)
- `app/`: the GUI (`app/source`)
- `tests/`: Catch2 tests (`roughtrack_tests`)
- `tools/`: `roughtrack_bench` and the release script

## The family look (shared with RoughStomp)

RoughTrack and RoughStomp (github.com/dantaipan/roughstomp, the guitar rig
plugin used here as an insert) share one look: the colours, buttons, menus,
console knobs and capped faders.

- It lives in RoughStomp: `ui/include/rs_ui/Family.h` and `ui/src/Family.cpp`.
  `cmake/Dependencies.cmake` fetches RoughStomp at a pinned commit (only those
  files are used; none of its plugins or its JUCE), and the app and tests
  compile `Family.cpp` in.
- `app/source/ConsoleLookAndFeel.h` is only an alias: `console::` colours are
  `rs::ui::family`, and `ConsoleLookAndFeel` is `rs::ui::FamilyLookAndFeel`.
- To change the shared look, change it in RoughStomp, push, then move the
  pin (`GIT_TAG`) in `cmake/Dependencies.cmake`, and check the
  `RoughTrack --snapshot` pictures (see README). Don't add a local copy here.
- RoughTrack-only colours and drawing (track colours, the VU meters, the
  transport buttons) stay in RoughTrack's own sources.

## Working here

- Build with the CMake presets (`cmake --preset release`,
  `cmake --build --preset release`); run `roughtrack_tests` before finishing
  a change, and use `--snapshot` to check GUI changes
