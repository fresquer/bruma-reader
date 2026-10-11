# Desktop Simulator

The firmware runs on the desktop through
[crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator):
a native PlatformIO build that swaps `lib/hal/` for a host HAL and paints the
e-ink framebuffer in an SDL2 window. It is the fastest way to check screens,
touch layouts, list conversions, and web-server routes without a device. It
does not model e-ink refresh, panel controllers, heap limits, deep sleep, or
OTA (firmware updates are stubbed out).

## Setup (once)

1. `brew install sdl2` (Linux: `libsdl2-dev libssl-dev`).
2. `platformio.ini` fetches the simulator from the fork
   `boydj/crosspoint-simulator`, branch `cpr-vcodex`. The upstream simulator
   only knows upstream's HAL; that branch adds the fork's `HalClock` UTC API,
   `HalGPIO::coldBootImpliesPowerButton`, `HalTiltSensor::readGyro`, plus host
   shims for `<Esp.h>`, `<esp_heap_caps.h>`, SdFat `common/FsDateTime.h`,
   `MD5Builder::getBytes`, the `esp_http_client` calls in `lib/KOReaderSync`,
   and the Wi-Fi channel/BSSID overloads. To work on the simulator itself,
   clone it beside this repo and point the env at the checkout from the
   gitignored `platformio.local.ini`:

   ```ini
   [env:simulator]
   lib_deps =
     ${env:simulator.lib_deps}
     simulator=symlink://../crosspoint-simulator
   ```

   Merge `crosspoint-reader/crosspoint-simulator` `main` into that branch when
   pulling firmware updates; keep the fork's additions small and additive.
3. Put EPUBs in `fs_/books/` (gitignored). That directory is the simulated SD
   card root; `fs_/.crosspoint/` holds the simulated caches and settings.

## Build and run

```bash
pio run -e simulator_x4_pro -t run_simulator   # X4 Pro: touch, Home key, frontlight
pio run -e simulator -t run_simulator          # X4 (buttons only)
pio run -e simulator_x3 -t run_simulator       # X3 landscape, tilt sensor
```

On Windows, run the simulator inside WSL (native Windows is not supported by
crosspoint-simulator). Install `libsdl2-dev` and `libssl-dev`, then use the GCC
profile:

```bash
pio run -e simulator_x4_pro_wsl -t run_simulator
```

Keys: arrows = side/front buttons, Return = Confirm, Escape = Back, P = Power,
S = sleep, H = Home key (X4 Pro), mouse = tap and swipe on touch profiles.

Simulator builds report the version `<base>.sim-<sha>` and do not touch the
release or dev counters (`scripts/git_branch.py`).

## Scripted checks and screenshots

The binary accepts a timed input script and screenshot schedule, which makes
screen reviews repeatable and needs no desktop automation permissions:

```bash
mkdir -p qa-artifacts
CROSSPOINT_SIM_INPUT_SCRIPT='2000:TAP:240,530;3200:SWIPE:400,10,400,300,300;4400:HOME:100;5400:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='2700:qa-artifacts/apps.bmp;3900:qa-artifacts/frontlight.bmp' \
  .pio/build/simulator_x4_pro/program
```

Actions: `BACK`, `ENTER`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `POWER`, `SLEEP`,
`HOME`, `QUIT`, `TAP:x,y[,hold]`, `SWIPE:x1,y1,x2,y2[,ms]`. Coordinates are
logical display pixels. Screenshots are BMP at the host's drawable resolution
(2x on Retina); convert with `sips -s format png`. `CROSSPOINT_SIM_FREE_HEAP`
and `CROSSPOINT_SIM_MAX_ALLOC_HEAP` fake low-memory conditions. The web server
binds `http://127.0.0.1:8080/` (`CROSSPOINT_SIM_HTTP_PORT` to move it).

## EPUB image grayscale regression

Run `python3 test/simulator/epub_image_grayscale.py /absolute/path/to/program`
with an X4 simulator binary. It generates a synthetic EPUB and isolated SDs,
then checks actual screenshot pixels across ten boots: text AA on/off, light
and dark modes, decoded and cached images, and a text-only page. It also checks
that disabling text AA keeps the paragraph monochrome without losing image
grays. The script reports its artifact directory; `--output-parent` can place
it under the gitignored `artifacts/`. It uses only the Python standard library.
This checks composition, not physical panel tones or refresh waveforms.

## When the simulator stops compiling

- A missing `Hal*` method or a fork-only ESP-IDF include belongs in the fork's
  simulator branch (see the simulator's `FORKING.md`); keep stubs one-line and
  additive so merging upstream simulator changes stays trivial.
- A host-portability problem in shared code (for example libc++ needing a
  complete type where GCC did not) is fixed in this repo.
- Do not edit `.pio/libdeps/*/simulator`; PlatformIO discards it.

## Bruma Library checks

On macOS, `./bin/run-simulator.sh` builds and opens the X4 simulator. Use
`./bin/run-simulator.sh --no-build` to reopen the current build immediately.
The simulated SD is `fs_/`; books may be placed in any visible subfolder.
Arrow keys navigate, Enter opens/selects, hold Enter opens the Library menu,
and Escape goes back. To use a different SD directory, set `CROSSPOINT_SIM_SD`
to its absolute path. The simulator writes settings/caches inside that directory,
so use a copy rather than a mounted real device card.

The Library feature includes an SD catalog scenario suite under `test/library/`.
It compiles the production catalog and natural comparator against a host storage
adapter that can inject read, write and directory enumeration failures:

```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test --target library_catalog_test
ctest --test-dir build/test -R '^library_catalog$' --output-on-failure
```

The suite covers nested folders, supported extensions, metadata reuse, missing
metadata, copies, all four orders, recent-history seeding, additions/deletions,
cancellation, low memory and interrupted catalog/history writes. Its 501-book
fixture uses a 10,240-byte catalog index; ordering temporarily needs another
index of the same capacity. Visible book strings and filesystem/parser buffers
are additional allocations, so this figure is not total firmware RAM usage.

Use Python 3.10–3.14 and the pioarduino PlatformIO Core for device builds. Both
`default` and `gh_release` accept `VCODEX_RELEASE_DRY_RUN=1` for verification
without advancing their counters. Local macOS package pins belong in
`platformio.local.ini`, not committed firmware configuration.

### Remaining reading time checks

After building `simulator`, run `python3 test/simulator/reading_time.py`. The
script creates temporary EPUB, TXT, Markdown and XTC fixtures, measures five
manual page advances, verifies restart persistence and layout invalidation,
and changes Book to Chapter through the real status-bar settings popup. It also
checks old settings, hidden time and both XTC overlay positions. Logs and BMP
screenshots stay in `.codex-build/reading-time-qa/`; the personal `fs_/` SD is
not modified. The existing `reader_regressions_test` includes the pure estimator
tests for interval bounds, pauses, skips, counter wrap and rounding.
