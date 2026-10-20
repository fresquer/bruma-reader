"""Mirror published Bruma C3 firmware into its independent preview OTA channel.

No local build is accepted as input. Run manually after publishing a release.
"""
from __future__ import annotations

import argparse
from functools import reduce
import hashlib
import json
import operator
import os
from pathlib import Path
import re
import struct
from urllib.parse import quote

try:
    from .sync_autoflash_firmware import (
        C3_TARGET, download_bytes, firmware_tag_sort_key, request_json, verify_firmware, write_atomic,
    )
except ImportError:
    from sync_autoflash_firmware import (
        C3_TARGET, download_bytes, firmware_tag_sort_key, request_json, verify_firmware, write_atomic,
    )

REPO = "fresquer/bruma-reader"
PROJECT = "bruma-reader"
PAGES_ROOT = "https://fresquer.github.io/bruma-reader/firmware/bruma/"


def release_assets(release: dict) -> dict[str, dict] | None:
    if release.get("draft") or not release.get("published_at"):
        return None
    tag = str(release.get("tag_name", ""))
    if firmware_tag_sort_key(tag) is None:
        return None
    assets = {asset.get("name"): asset for asset in release.get("assets", []) if asset.get("state") == "uploaded"}
    image = f"bruma-{tag.removesuffix('-cpr-vcodex')}.bin"
    if image not in assets:
        image = f"{tag}.bin"  # Published preview .39 used the inherited filename.
    wanted = [image, f"{tag}.json", f"{tag}-firmware-budget.json", f"{tag}-firmware-budget.md"]
    return {name: assets[name] for name in wanted} if all(name in assets for name in wanted) else None


def select_release(releases: list[dict]) -> dict:
    candidates = [release for release in releases if release_assets(release)]
    if not candidates:
        raise ValueError("No published Bruma firmware release with all four C3 assets")
    return max(candidates, key=lambda release: (
        firmware_tag_sort_key(release["tag_name"]), not release.get("prerelease", False), release["published_at"],
    ))


def fetch_asset(asset: dict, tag: str) -> bytes:
    expected = f"https://github.com/{REPO}/releases/download/{quote(tag, safe='')}/{quote(asset['name'], safe='')}"
    if asset.get("browser_download_url") != expected:
        raise ValueError("Asset URL does not belong to this Bruma release")
    data = download_bytes(expected)
    if len(data) != asset.get("size"):
        raise ValueError("Published asset size mismatch")
    digest = asset.get("digest")
    if digest and digest != "sha256:" + hashlib.sha256(data).hexdigest():
        raise ValueError("Published asset digest mismatch")
    return data


def validate_image(data: bytes, version: str) -> None:
    """ESP image structure, C3 descriptor, XOR checksum, SHA trailer and board tag."""
    if (len(data) < 288 or data[0] != 0xE9 or not 1 <= data[1] <= 16
            or struct.unpack_from("<H", data, 12)[0] != 5
            or struct.unpack_from("<I", data, 28)[0] < 256
            or struct.unpack_from("<I", data, 32)[0] != 0xABCD5432):
        raise ValueError("Invalid ESP32-C3 application image")
    raw_version = data[48:80]
    if b"\0" not in raw_version or raw_version.split(b"\0", 1)[0].decode("ascii") != version:
        raise ValueError("Embedded image version differs from release metadata")
    pos, checksum = 24, 0xEF
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError("Truncated segment header")
        length = struct.unpack_from("<I", data, pos + 4)[0]
        pos += 8
        if pos + length > len(data):
            raise ValueError("Truncated segment")
        checksum = reduce(operator.xor, data[pos:pos + length], checksum)
        pos += length
    checksum_pos = (pos // 16 + 1) * 16 - 1
    if checksum_pos >= len(data) or data[checksum_pos] != checksum:
        raise ValueError("ESP image checksum mismatch")
    digest_end = checksum_pos + 33
    if data[23] != 1 or digest_end > len(data):
        raise ValueError("ESP image SHA-256 trailer missing")
    if hashlib.sha256(data[:checksum_pos + 1]).digest() != data[checksum_pos + 1:digest_end]:
        raise ValueError("ESP image SHA-256 trailer mismatch")
    markers = re.findall(rb"CROSSPOINT-BOARD-V1:([^;]+);", data)
    if markers != [b"x4"]:
        raise ValueError("Image does not identify the supported X4/C3 board")


def sync_bruma(project_dir: Path, token: str | None = None, tag: str | None = None) -> dict:
    if tag:
        release = request_json(f"https://api.github.com/repos/{REPO}/releases/tags/{quote(tag, safe='')}", token)
        if not isinstance(release, dict) or not release_assets(release):
            raise ValueError("Requested tag is not a complete published Bruma firmware release")
    else:
        releases = []
        page = 1
        while True:
            batch = request_json(f"https://api.github.com/repos/{REPO}/releases?per_page=100&page={page}", token)
            if not isinstance(batch, list):
                raise ValueError("Invalid GitHub releases response")
            releases.extend(batch)
            if len(batch) < 100:
                break
            page += 1
        release = select_release(releases)
    tag = release["tag_name"]
    assets = release_assets(release)
    image_asset = next(asset for name, asset in assets.items() if name.endswith(".bin"))
    firmware = fetch_asset(image_asset, tag)
    verify_firmware(C3_TARGET, image_asset, firmware)
    metadata = json.loads(fetch_asset(assets[f"{tag}.json"], tag))
    budget = json.loads(fetch_asset(assets[f"{tag}-firmware-budget.json"], tag))
    version = tag.removesuffix("-cpr-vcodex")
    if any(metadata.get(key) != value for key, value in {
        "board": "x4", "environment": "gh_release", "version": version,
        "applicationVersion": version, "artifactName": image_asset["name"], "firmwareBytes": len(firmware),
    }.items()):
        raise ValueError("Release metadata does not match C3 firmware")
    if (budget.get("board") != "x4" or budget.get("chipFamily") != "ESP32-C3"
            or budget.get("environment") != "gh_release" or budget.get("firmwareBytes") != len(firmware)
            or budget.get("artifactName") != image_asset["name"]):
        raise ValueError("Release budget does not match C3 firmware")
    validate_image(firmware, version)
    directory = project_dir / "docs/firmware/bruma"
    manifest_path = directory / "manifest.json"
    if manifest_path.exists():
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        previous_key = firmware_tag_sort_key(previous.get("version", ""))
        if previous.get("project") != PROJECT or previous_key is None:
            raise ValueError("Existing channel manifest has an invalid project or version")
        if previous_key > firmware_tag_sort_key(tag):
            raise ValueError("Refusing to downgrade the Bruma channel")
    image_path = directory / image_asset["name"]
    if image_path.exists() and image_path.read_bytes() != firmware:
        raise ValueError("Immutable published image has changed; publish a new version instead")
    manifest = {
        "project": PROJECT,
        "channel": "preview" if release.get("prerelease") else "stable",
        "version": tag,
        "downloadUrl": PAGES_ROOT + image_asset["name"],
        "size": len(firmware),
        "sha256": hashlib.sha256(firmware).hexdigest(),
        "source": {"repo": REPO, "tag": tag, "asset": image_asset["name"], "publishedAt": release["published_at"]},
    }
    # The payload is versioned and immutable; the manifest is the last write.
    write_atomic(image_path, firmware)
    write_atomic(manifest_path, (json.dumps(manifest, indent=2) + "\n").encode())
    print(f"Bruma {manifest['channel']}: {tag} ({len(firmware)} bytes)")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, default=Path.cwd())
    parser.add_argument("--tag", help="Explicit published tag; downgrades remain forbidden")
    args = parser.parse_args()
    try:
        sync_bruma(args.project_dir.resolve(), os.environ.get("GITHUB_TOKEN"), args.tag)
    except (ValueError, KeyError, OSError, RuntimeError) as error:
        parser.exit(1, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
