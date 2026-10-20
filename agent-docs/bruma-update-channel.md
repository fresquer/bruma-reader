# Bruma update channel and preview 2

This change implements BR-002, BR-009 (controls/presentation only) and BR-005.
The first preview was reported working by the user; this new candidate has only
build, native and simulator validation. Publication is authorized for preview 2;
physical installation and OTA on the X4 remain pending.

## Install the first candidate from SD

The installed .39 preview still queries the inherited updater. Copy the local
`bruma-1.6.0.40.bin` candidate to the SD card, safely eject it, then use
Settings > System > SD Card Firmware Update (Ajustes > Sistema > Actualización de
firmware desde SD). Select the file and confirm the displayed version. Keep
power available and allow the update/reboot to finish. Existing books, settings,
reading history and catalog formats are retained; no reset is needed. Back up
the SD including `.crosspoint` before the physical test.

The SD filename is `bruma-<version>.bin`, without the inherited suffix. It is
byte-identical to the canonical release asset; the embedded version is unchanged. This is
an X4 / ESP32-C3 application image, not a merged USB/bootloader image. This
procedure does not modify partitions, bootloader or eFuses.

## Independent distribution

- Pages manifest: `https://fresquer.github.io/bruma-reader/firmware/bruma/manifest.json`
- Raw fallback: `https://raw.githubusercontent.com/fresquer/bruma-reader/master/docs/firmware/bruma/manifest.json`
- Required identity: `project: bruma-reader`; channel: `preview` or `stable`.
- Versioned, immutable payload: `docs/firmware/bruma/bruma-<version>.bin`
  (the .39 seed retains its original asset name).
- The old manifest and Auto Flash remain separate and unchanged. Never use the
  inherited synchronization workflow to distribute Bruma OTA.

The channel starts from the verified published .39 seed and advances to the
verified published .40 preview after its release. It never mirrors a local
candidate as if it were a published release.
Only strictly newer versions are offered; a .40 candidate must not offer .39.
Errors fetching/validating both manifests remain errors, not "up to date".

## Publishing and synchronizing

1. Publish a Bruma preview or stable release in `fresquer/bruma-reader`, with
   `bruma-<version>.bin`, `<tag>.json`, `<tag>-firmware-budget.json` and `.md`.
2. Run **Sync Bruma preview OTA** manually. Leave `tag` empty to select the
   highest complete published firmware version, including previews; optional
   explicit tags still cannot downgrade the current channel. Drafts, fonts and
   releases missing any required asset are excluded.
3. The script verifies repository URLs, published sizes/digests, metadata,
   C3/X4 markers, image checksum and SHA trailer. It writes the binary first and
   the manifest last, then the workflow commits only the independent directory
   and requests the existing branch-based Pages build.
4. Verify deployment of both Pages files and the Raw manifest, then perform a
   physical OTA test from a lower Bruma version already containing this updater.

Local read-only-source synchronization can be rehearsed outside the checkout:

```sh
python3 scripts/sync_bruma_firmware.py --project-dir /path/to/temporary-output
```

## Library and Apps controls

Library: upper side button selects the previous order; lower selects the next.
Hold does not repeat. Front navigation, long-press paging and Confirm to open
remain available. Hold Confirm for refresh, filters and reading state. The
selected book is retained by path when reordering; the filter stays active.
Empty libraries can remember an order, and scanning disables changes. A failed
sort retains the old setting and shows an error. Dates and time groups are
explicitly deferred; the catalog format is unchanged.

Apps always contains all 17 registered applications, including OPDS without
servers. Home shortcut preferences and global ordering fields remain intact.
The shortcut controls mean "Home + Apps" or "Apps only". Returning from an app
retains the selected application and viewport; the explicit Home gesture still
returns Home. Flashcards remains retired.

## Local verification

- Native CTest suite, including real OTA implementation with mocked transport,
  partition and flash operations: identity, fallback, old/equal versions,
  interrupted transfer, incorrect digest/chip/board and inactive-slot writes.
- Python release-channel regression suite: preview/stable selection, drafts,
  incomplete releases, source URLs, corruption, immutable payloads, metadata,
  downgrade refusal and payload-before-manifest ordering.
- `test/simulator/bruma_controls.py`: EN/ES sort controls, held-once, selection,
  restart, paging, empty library, shortcut independence and OPDS without servers;
  all applications open/return from a later page.
- `test/simulator/library_states.py`: filters, manual/automatic states and
  reader return. `library_catalog.py --large`: 506 books, metadata, persistent
  order and low-memory failure; native LibraryStore injects SD read/write faults.
- `default` and `gh_release` dry-run builds and the release budget gate.

Physical side-button behavior, e-ink timing and real Wi-Fi installation remain
pending on the X4. This document does not claim those checks have been run.

Payload TLS uses the inherited low-memory compatibility transport; certificate
validation protects the manifest and its SHA-256 authenticates the payload.
The public C3 asset is now `bruma-<version>.bin`; metadata/budget filenames
retain the release tag. Legacy .39 assets remain readable by the synchronizer.
