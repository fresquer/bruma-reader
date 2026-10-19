import contextlib
import io
import tempfile
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts import sync_autoflash_firmware as sync

from scripts.sync_autoflash_firmware import C3_TARGET, build_manifest, pages_firmware_url


class AutoflashManifestTest(unittest.TestCase):
    def test_installed_ota_uses_redirect_free_pages_copy(self):
        tag = "1.6.0.33-cpr-vcodex"
        release_url = f"https://github.com/franssjz/cpr-vcodex/releases/download/{tag}/{tag}.bin"
        release = {
            "tag_name": tag,
            "published_at": "2026-09-15T00:00:00Z",
        }
        image = {
            "asset": {"name": f"{tag}.bin", "updated_at": "2026-09-15T00:00:00Z"},
            "downloadUrl": release_url,
            "size": 6_391_952,
            "sha256": "a" * 64,
        }

        manifest = build_manifest("franssjz/cpr-vcodex", release, {C3_TARGET.key: image})

        self.assertEqual(
            manifest["downloadUrl"],
            "https://franssjz.github.io/cpr-vcodex/firmware/firmware.bin",
        )
        self.assertEqual(manifest["devices"]["x4"]["downloadUrl"], release_url)
        self.assertEqual(manifest["devices"]["x3"]["downloadUrl"], release_url)

    @unittest.skipUnless(sys.version_info >= (3, 10), "Release sync requires Python 3.10+")
    def test_sync_preserves_bruma_preview_while_updating_inherited_tools(self):
        preview = "1.6.0.39-cpr-vcodex"
        stable = "1.6.0.99-cpr-vcodex"
        landing = f'<a href="https://github.com/fresquer/bruma-reader/releases/tag/{preview}">Download preview</a>'
        release = {
            "tag_name": stable,
            "assets": [{"name": f"{stable}.bin", "browser_download_url": f"https://github.com/franssjz/cpr-vcodex/releases/download/{stable}/{stable}.bin"}],
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "docs/assets").mkdir(parents=True)
            (root / "docs/index.html").write_text(landing)
            (root / "docs/assets/site.js").write_text(preview)
            with patch.object(sync, "fetch_latest_firmware_release", return_value=release), \
                 patch.object(sync, "download_bytes", return_value=b"test-image"), \
                 patch.object(sync, "verify_firmware"), \
                 patch.dict("os.environ", {"GITHUB_ENV": ""}), \
                 contextlib.redirect_stdout(io.StringIO()):
                sync.sync_autoflash("franssjz/cpr-vcodex", root, None)
            self.assertEqual((root / "docs/index.html").read_text(), landing)
            self.assertEqual((root / "docs/assets/site.js").read_text(), stable)
            self.assertTrue((root / "docs/firmware/manifest.json").exists())

    def test_pages_url_rejects_invalid_repository_name(self):
        with self.assertRaises(ValueError):
            pages_firmware_url("cpr-vcodex")


if __name__ == "__main__":
    unittest.main()
