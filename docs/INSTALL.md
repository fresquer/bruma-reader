# Install Bruma Reader on Xteink X4

Bruma is a fork of CPR-vCodex, built on CrossPoint Reader. Preview 2 adds the
independent Bruma update channel, direct library sort controls and the complete
Apps catalog. The first preview has a positive user report; this new preview
still requires physical X4 testing.

## Upgrade from vCodex or Bruma preview 1 using SD

1. Back up your SD card, including the hidden `.crosspoint` folder.
2. Download **`bruma-1.6.0.40.bin`** from [Bruma Preview 2](https://github.com/fresquer/bruma-reader/releases/tag/1.6.0.40-cpr-vcodex).
3. Copy it to the SD root, safely eject the card and insert it into the reader.
4. Open **Settings > System > SD Card Firmware Update**. In Spanish:
   **Ajustes > Sistema > Actualización de firmware desde SD**.
5. Choose the file, confirm and let installation/restart finish with sufficient power.

The inherited SD picker accepts `.bin` files and validates their contents;
it does not require a vCodex filename. The application version is `1.6.0.40`.
Books, settings, reading history and library formats are retained; no factory
reset is needed. This is the X4/ESP32-C3 application image. It does not rewrite
bootloader, partitions or eFuses and is not a merged USB image or X4 Pro image.

## Subsequent Wi-Fi updates

The .39 preview retains its inherited updater, so install .40 from SD first.
Starting with .40, update checks use only the Bruma manifest on GitHub Pages,
with the same manifest on GitHub Raw as fallback. A newer complete published
Bruma preview or stable release is offered; equal/older versions are not.
The confirmation identifies previews. Failed checks remain errors.

The manifest is fetched with certificate validation. The low-memory payload
transport inherited from working X4 releases uses HTTPS without separate
certificate validation; its bytes are authenticated against the SHA-256 from
the verified manifest. Size, image, chip and board checks must also pass before
the inactive app partition is selected. Real Wi-Fi installation of this preview
remains a physical-device test to complete.

## USB installation

The inherited Auto Flash page is not a Bruma installer. A dedicated Bruma web
installer has not been released. Do not write this application image at a
whole-flash offset or erase the entire device using a generic web flasher.
USB recovery capability depends on the particular device.
