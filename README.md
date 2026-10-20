<p align="center">
  <img src="docs/assets/bruma-reader-banner.png" alt="Bruma Reader: an open book unfolding into a misty landscape" width="100%" />
</p>

# Bruma Reader

**Your library. A moment of calm.**

Bruma Reader is reading firmware for the **Xteink X4**. It brings books from your SD card into one library, estimates your remaining reading time, and offers a home screen with your current book and reading statistics.

It is a fork of **[CPR-vCodex](https://github.com/franssjz/cpr-vcodex)**, itself based on **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)**. Bruma preserves that foundation and its reading, statistics, and customization tools. This repository is not the official CrossPoint project and is not affiliated with Xteink.

[User guide](USER_GUIDE.md) · [Simulator](agent-docs/simulator.md) · [SD fonts](docs/sd-card-fonts.md) · [Original documentation](README.cpr-vcodex.md)

## What Bruma adds

| Feature | What you can do |
|---|---|
| **Library** | Browse books from different SD folders in one paginated list, with titles and authors. |
| **Sorting and filters** | Use the side buttons to cycle the visible order: title, author, last read, or recently added; show all books, unread books, books in progress, or finished books. |
| **Reading states** | Mark a book as finished or undo that mark without changing its progress or reading time. |
| **Remaining reading time** | Show a book or chapter estimate in the reading status bar, or hide it. |
| **Bruma Home** | See your current book, recent reads, daily goal, and statistics, with shortcuts to Library and Apps. |
| **Complete Apps catalog** | Every registered app remains available, independently of Home shortcuts, including OPDS without configured servers. |
| **Bruma updates** | Receive newer published Bruma previews and releases through the separate Bruma OTA channel after installing .40 from SD. |
| **Fonts from the SD card** | Install `.cpfont` files without Wi-Fi, from Settings or the file browser. |

### All your books in one list

Library scans the SD card and its visible subfolders. It includes **EPUB, XTC, TXT, and Markdown**, skips images and hidden or system directories, and opens each book from its original location.

- On your first visit, choose **Create library**.
- After adding, changing, or deleting files, use **Update library** in the menu.
- Updates show progress and can be canceled. Errors or cancellation preserve the previous catalog.
- Your sort order, filter, and selection are preserved when you return from reading.
- Files without metadata use their filenames. Copies stored at different paths appear separately.
- Reading states follow progress and statistics, with manual overrides available.

The catalog is stored in `/.crosspoint/`. Updating it does not organize, move, or modify your books. **Browse Files** remains available in Apps for folder navigation. Library does not include text search or covers.

### Know how much is left

Status bar customization offers **Remaining time: Hidden / Book / Chapter**. The default is **Book**, and the same setting is available through the web interface.

Estimates appear as `~25 min` or `~1 h 20 min`. The estimator starts with a provisional pace of 60 seconds per page and learns from normal manual page advances. It ignores long pauses, jumps, backward navigation, and automatic page turns. Reading pace is saved per book.

This works with EPUB, XTC, TXT, and Markdown. For documents without chapters, Chapter represents the whole document. The estimate is a guide: page size and reading habits affect the result.

### A home screen built around reading

The optional **Bruma** theme brings together your current book, recent reads, reading time, daily goal, streak, and finished books. Select it in the interface theme settings. Other themes and shortcut customization remain available.

### Install fonts offline

1. Copy a compatible file into any SD folder, for example `Lexend_14.cpfont`.
2. Open **Settings > Reader > Install font from file**, or select the file in **Browse Files**.
3. Confirm replacement if that size is already installed.
4. Choose the family under **Settings > Reader > Font Family**.

The original file is preserved. Only that size is installed; other sizes in the family remain available. This uses the `.cpfont` format and does not convert TTF or OTF files. See the [font guide](docs/sd-card-fonts.md) for downloads and conversion instructions.

## What it keeps from CPR-vCodex

Bruma retains features from its base, including:

- Reading statistics, goals, streaks, heatmaps, session history, and achievements.
- **Sync Day**, which provides a reference date for daily statistics.
- SD-based StarDict dictionaries, EPUB highlights, and page marks.
- SD fonts, themes, reading settings, and custom sleep screens.
- File transfer and web settings, OPDS, and KOReader synchronization.
- Inherited maintenance and firmware update tools.

**Flashcards has been removed from Bruma** to keep the project focused on reading. Existing preferences for other shortcuts are preserved.

The [original CPR-vCodex documentation](README.cpr-vcodex.md) preserves its credits, technical history, and recovery notes. Its release and Auto Flash links belong to CPR-vCodex and do not necessarily include Bruma's additions.

## Download a preview build

[Bruma Reader — Preview 2](https://github.com/fresquer/bruma-reader/releases/tag/1.6.0.40-cpr-vcodex) provides the ESP32-C3 firmware binary, build metadata, and size-budget reports. This is a prerelease for testing. The first preview has an initial positive user report; this new preview still needs physical X4 validation.

Download **`bruma-1.6.0.40.bin`** from this repository. Install this transition preview from SD using the [Bruma installation guide](docs/INSTALL.md). It introduces a separate Bruma OTA channel for subsequent versions; the already installed .39 preview still has its inherited updater. The inherited Auto Flash remains separate and must not be used to install Bruma.

## Compatibility and status

Bruma development focuses on **Xteink X4 / ESP32-C3**. The base retains X3 panel support, but Bruma's additions require physical validation before that device can be considered tested.

**X4 Pro is not a distribution target.** CPR-vCodex's withdrawal of its binaries remains in effect.

Bruma's local review passed **296 host tests**, X4 simulator checks, and `default` and `gh_release` builds. Library was checked with more than 500 books, including storage errors and low-memory conditions. Speed, legibility, and the behavior of the new features still require validation on a physical X4.

Bruma is the name of this project. Internal identifiers, `/.crosspoint/` paths, and release-tag identifiers are retained for compatibility. The banner and this README do not change branding inside the firmware.

## Try it on your computer

Clone the repository with its submodules:

```bash
git clone --recurse-submodules https://github.com/fresquer/bruma-reader.git
cd bruma-reader
```

You need [pioarduino PlatformIO Core](https://github.com/pioarduino/platformio-core) and Python 3.10–3.14. Install SDL2 for the simulator; on macOS:

```bash
brew install sdl2
./bin/run-simulator.sh
```

Place your books in `fs_/`, which acts as the simulated SD card. Books can live in different subfolders. To reopen an existing build without compiling again:

```bash
./bin/run-simulator.sh --no-build
```

| Key | Action |
|---|---|
| Arrow keys | Navigate or turn pages, depending on the screen |
| Enter | Select or open |
| Hold Enter | Open the Library menu |
| Escape | Go back |
| P | Power button |
| S | Sleep |

On Linux, install `libsdl2-dev` and `libssl-dev`, then run `pio run -e simulator -t run_simulator`. On Windows, use WSL. The [simulator guide](agent-docs/simulator.md) covers configuration and screen checks.

The simulator lets you review navigation and layout. It does not reproduce physical e-ink refresh or validate firmware installation.

## Build and test

From the repository root, with submodules initialized:

```bash
VCODEX_RELEASE_DRY_RUN=1 pio run -e default -j 1
VCODEX_RELEASE_DRY_RUN=1 pio run -e gh_release -j 1
```

Dry-run mode prevents version counters from advancing. In PowerShell, set `$env:VCODEX_RELEASE_DRY_RUN = "1"` before running `pio`, then remove the variable when finished.

Host tests:

```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
cmake --build build/test
ctest --test-dir build/test --output-on-failure
```

Library and reader checks, after building the simulator:

```bash
python3 test/simulator/library_catalog.py --large
python3 test/simulator/library_states.py
python3 test/simulator/reading_time.py
python3 test/simulator/home_dashboard.py
```

See the [contributing guide](docs/contributing/README.md) and [AGENTS.md](AGENTS.md) for firmware constraints and development workflows. Local builds do not publish or install firmware by themselves.

## Documentation

- [User guide](USER_GUIDE.md)
- [Simulator and tests](agent-docs/simulator.md)
- [SD card fonts](docs/sd-card-fonts.md)
- [Dictionaries](docs/dictionary.md)
- [Web server](docs/webserver.md)
- [Translations](docs/i18n.md)
- [Base firmware stability audit](agent-docs/stability-audit-2026-09.md)
- [Inherited CPR-vCodex README](README.cpr-vcodex.md)
- [Inherited changelog](CHANGELOG.md)

## Origins, credits, and license

The project lineage is:

**[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) → [CPR-vCodex](https://github.com/franssjz/cpr-vcodex) → [Bruma Reader](https://github.com/fresquer/bruma-reader)**

Thanks to Dave Allie and the CrossPoint Reader contributors for the foundation, and to **franssjz** and the CPR-vCodex contributors for the additions Bruma builds upon.

Credits also remain with **zgredex** for Lyra Carousel, **erickosanchezj** for its CPR-vCodex adaptation, **Julia Nguyen (uxjulia)** and [CrossInk](https://github.com/uxjulia/CrossInk) for the highlighting work incorporated into the base, and **Which-Estimate4566** for the original CPR-vCodex logo. Bruma's new banner is an AI-generated illustration created for this project.

The code retains the [original MIT license](LICENSE), including its copyright notice. Third-party components, fonts, and other resources retain their own licenses.
