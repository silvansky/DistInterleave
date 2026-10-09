# Working on DistInterleave

## Scope and architecture

DistInterleave is a C++20 JUCE audio effect targeting VST3, AU on macOS, and a
custom standalone app. Read `README.md` for the controls, routing, and live cycle
semantics before changing behavior.

- `src/InterleaveEngine.h` is independent of JUCE. It captures full cycles in
  preallocated slots and alternates the selected count between two sources.
- `src/PluginProcessor.*` owns host parameters, bus negotiation, source routing,
  gain/mix smoothing, and state serialization.
- `src/PluginEditor.*` implements the editor. `src/LookAndFeel.*` provides the
  Varispeed-derived palette, typography, and control drawing.
- `src/Standalone.cpp` provides hardware routing and device settings. Keep its
  custom wrapper: JUCE's default standalone does not expose all sidechain inputs.
- `tests/EngineTests.cpp` tests DSP behavior; `tests/ProcessorTests.cpp` tests bus
  layouts, block-size independence, mixing, defaults, and state.
- `tools/AUSmoke.cpp` loads and renders the actual AU without installing it.
  `tools/Screenshot.cpp` renders the editor offscreen.

## Behavior to preserve

- A full cycle spans two sign changes, from rising crossing to rising crossing.
  Exact zero retains the previous sign. Discard an initial partial cycle.
- Cycles is an integer from 1 to 100, default 1. Count played cycles individually;
  do not require an entire multi-cycle group to fit in the capture buffer.
- Live sources keep arriving. Retain the latest completed waiting cycle and never
  overwrite the playing cycle. Preserve the documented two-second per-cycle
  fallback for silence, DC, and extremely slow signals.
- L/R defaults on stereo input, treats left/right as separate sources, and
  produces mono or identical stereo channels at every Mix setting. Its dry path
  is the average of input left and right.
- Mono main input always uses In/Sidechain, including when host automation sets
  Mode to L/R. The editor must show only the effective sidechain choice.
- Main input, sidechain, and output each support mono/stereo independently.
  An absent sidechain passes the main signal through; an enabled silent sidechain
  participates as silence.
- Channels defaults to Independent and is disabled in L/R. Left/Right detection
  uses the selected channel's boundaries for the whole stereo source.
- Gain is linear 0-2x after mixing, default 1x. Mix is linear 0-100%, default 100%.
  Do not add clipping, saturation, limiting, or normalization.
- Preserve existing parameter IDs (`mode`, `channels`, `cycles`, `gain`, `mix`)
  and saved-state compatibility. Document intentional changes to live latency,
  source selection, or dry/wet behavior.

## Real-time and code conventions

- Allocate audio storage in preparation, not in `processBlock` or engine capture
  and playback. Avoid audio-thread locks, I/O, logging, and large buffer copies.
- Read aliased main/sidechain inputs before writing output. In mono-to-stereo
  layouts the output right channel can share storage with the first sidechain
  channel in JUCE's process buffer.
- Use bounded work per sample and keep results independent of host block size.
- Follow the surrounding C++ style and use descriptive names. Keep DSP changes
  separate from presentation concerns. Use ASCII in UI string literals.
- Match the existing slate surface, cyan headings, green value arcs, and flat
  controls. Use palette/font helpers in `LookAndFeel.h`; keep the editor's fixed
  design coordinates and scale the surface when resizing.
- Keep controls accessible through typed values, double-click reset, shift-drag
  fine adjustment, and contextual footer help. Check disabled controls and mono layouts.

## Build and verify

JUCE is pinned in `CMakeLists.txt`. CMake fetches it automatically; an existing
checkout can be supplied through `JUCE_SOURCE_DIR`. Do not hard-code a developer's
absolute checkout path into tracked files.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
```

For DSP/routing changes, add meaningful regression coverage and run the engine
and processor tests. For target or wrapper changes, build the affected formats.
The AU test needs macOS audio-component service access; a restrictive sandbox can
block registration even when the bundle is valid.

Engine sanitizers can run without JUCE:

```sh
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Isrc tests/EngineTests.cpp -o /tmp/DistInterleaveEngineTests
/tmp/DistInterleaveEngineTests
```

For UI changes, render and inspect both stereo and mono previews:

```sh
cmake --build build --target DistInterleaveShot --parallel 6
./build/DistInterleaveShot docs/preview.png
./build/DistInterleaveShot docs/preview-mono.png mono
```

On macOS, the standalone UI smoke test opens and closes the window without
enabling hardware audio:

```sh
./build/DistInterleave_Standalone_artefacts/Release/DistInterleave.app/Contents/MacOS/DistInterleave --smoke-test
```

Documentation-only changes need link/command review, not an audio rebuild.
Never commit generated build trees, local dependency checkouts, plugin binaries,
credentials, or machine-specific CMake caches. Keep `README.md` current when
controls, routing, targets, or build commands change.

## Source provenance

Implement the interleave algorithm independently. Do not copy, translate, or link
CDP source code. The existing LookAndFeel files are reused under VarispeedDelay's
MIT option; preserve `LICENSE.txt`. JUCE and its bundled dependencies
retain their own licenses and notices.
