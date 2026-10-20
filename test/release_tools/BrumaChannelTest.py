import contextlib
import copy
from functools import reduce
import hashlib
import io
import json
import operator
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from scripts import sync_bruma_firmware as channel

ORIGINAL_WRITE_ATOMIC = channel.write_atomic


def release(version="1.6.0.42", preview=True):
    tag = version + "-cpr-vcodex"
    data = bytearray(1048576)
    data[:4] = bytes([0xE9, 1, 0, 0])
    struct.pack_into("<H", data, 12, 5)
    data[23] = 1
    struct.pack_into("<II", data, 24, 0x3C000020, len(data) - 32)
    struct.pack_into("<I", data, 32, 0xABCD5432)
    data[48:48 + len(version)] = version.encode()
    marker = b"CROSSPOINT-BOARD-V1:x4;"
    data[400:400 + len(marker)] = marker
    checksum = reduce(operator.xor, data[32:], 0xEF)
    data.extend(b"\0" * 15 + bytes([checksum]))
    data.extend(hashlib.sha256(data).digest())
    image = bytes(data)
    metadata = dict(board="x4", environment="gh_release", version=version,
                    applicationVersion=version, artifactName=tag + ".bin", firmwareBytes=len(image))
    payloads = {
        tag + ".bin": image,
        tag + ".json": json.dumps(metadata).encode(),
        tag + "-firmware-budget.json": json.dumps(dict(metadata, chipFamily="ESP32-C3")).encode(),
        tag + "-firmware-budget.md": b"budget report",
    }
    info = dict(tag_name=tag, prerelease=preview, draft=False, published_at="2026-10-20T10:00:00Z", assets=[])
    for name, payload in payloads.items():
        info["assets"].append(dict(name=name, state="uploaded", size=len(payload),
            digest="sha256:" + hashlib.sha256(payload).hexdigest(),
            browser_download_url=f"https://github.com/{channel.REPO}/releases/download/{tag}/{name}"))
    return info, payloads


class BrumaChannelTest(unittest.TestCase):
    def run_sync(self, root, info, payloads):
        with patch.object(channel, "request_json", return_value=[info]), \
             patch.object(channel, "download_bytes", side_effect=lambda url: payloads[url.rsplit("/", 1)[1]]), \
             contextlib.redirect_stdout(io.StringIO()):
            return channel.sync_bruma(root)

    def test_selects_highest_complete_firmware_including_previews(self):
        older, _ = release("1.6.0.40", False)
        newer, _ = release("1.6.0.42", True)
        draft, _ = release("1.6.0.99")
        draft["draft"] = True
        missing, _ = release("1.6.0.98")
        missing["assets"].pop()
        fonts = dict(tag_name="sd-fonts-m1", published_at="2026-10-20", assets=[])
        self.assertEqual(channel.select_release([draft, missing, fonts, older, newer]), newer)
        stable = copy.deepcopy(newer)
        stable["prerelease"] = False
        self.assertEqual(channel.select_release([newer, stable]), stable)

    def test_sync_writes_immutable_payload_before_manifest_and_keeps_legacy_untouched(self):
        info, payloads = release()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            legacy = root / "docs/firmware/manifest.json"
            legacy.parent.mkdir(parents=True)
            legacy.write_text("legacy channel")
            calls = []
            def write(path, data):
                calls.append(path.name)
                ORIGINAL_WRITE_ATOMIC(path, data)
            with patch.object(channel, "write_atomic", side_effect=write):
                manifest = self.run_sync(root, info, payloads)
            self.assertEqual(calls, [info["tag_name"] + ".bin", "manifest.json"])
            self.assertEqual(manifest["project"], "bruma-reader")
            self.assertEqual(manifest["channel"], "preview")
            self.assertEqual(legacy.read_text(), "legacy channel")
            self.assertEqual(self.run_sync(root, info, payloads), manifest)

    def test_named_bruma_binary_is_mirrored_with_matching_metadata(self):
        info, payloads = release()
        tag = info["tag_name"]
        name = "bruma-1.6.0.42.bin"
        payloads[name] = payloads.pop(tag + ".bin")
        info["assets"][0]["name"] = name
        info["assets"][0]["browser_download_url"] = f"https://github.com/{channel.REPO}/releases/download/{tag}/{name}"
        for suffix in (".json", "-firmware-budget.json"):
            key = tag + suffix
            metadata = json.loads(payloads[key])
            metadata["artifactName"] = name
            payloads[key] = json.dumps(metadata).encode()
            asset = next(asset for asset in info["assets"] if asset["name"] == key)
            asset["size"] = len(payloads[key])
            asset["digest"] = "sha256:" + hashlib.sha256(payloads[key]).hexdigest()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.run_sync(root, info, payloads)
            self.assertTrue(manifest["downloadUrl"].endswith("/" + name))
            self.assertEqual((root / "docs/firmware/bruma" / name).read_bytes(), payloads[name])

    def test_corrupt_digest_or_foreign_url_publishes_nothing(self):
        for fault in ["digest", "url", "chip", "version", "checksum"]:
            info, payloads = release()
            asset = info["assets"][0]
            name = asset["name"]
            if fault == "digest":
                asset["digest"] = "sha256:" + "0" * 64
            elif fault == "url":
                asset["browser_download_url"] = "https://example.com/firmware.bin"
            else:
                image = bytearray(payloads[name])
                image[{"chip": 12, "version": 48, "checksum": 1000}[fault]] ^= 1
                payloads[name] = bytes(image)
                asset["digest"] = "sha256:" + hashlib.sha256(image).hexdigest()
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                with self.assertRaises(ValueError):
                    self.run_sync(root, info, payloads)
                self.assertFalse((root / "docs/firmware/bruma/manifest.json").exists())

    def test_metadata_mismatch_downgrade_and_changed_immutable_payload_are_rejected(self):
        info, payloads = release()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.run_sync(root, info, payloads)
            manifest_path = root / "docs/firmware/bruma/manifest.json"
            before = manifest_path.read_bytes()
            older, old_payloads = release("1.6.0.41")
            with self.assertRaisesRegex(ValueError, "downgrade"):
                self.run_sync(root, older, old_payloads)
            image_path = manifest_path.parent / (info["tag_name"] + ".bin")
            image_path.write_bytes(b"different")
            with self.assertRaisesRegex(ValueError, "Immutable"):
                self.run_sync(root, info, payloads)
            self.assertEqual(manifest_path.read_bytes(), before)
        bad = copy.deepcopy(info)
        meta = next(asset for asset in bad["assets"] if asset["name"] == info["tag_name"] + ".json")
        payloads[meta["name"]] = payloads[meta["name"]].replace(b'"x4"', b'"x3"')
        meta["digest"] = "sha256:" + hashlib.sha256(payloads[meta["name"]]).hexdigest()
        with tempfile.TemporaryDirectory() as temporary, self.assertRaises(ValueError):
            self.run_sync(Path(temporary), bad, payloads)


if __name__ == "__main__":
    unittest.main()
