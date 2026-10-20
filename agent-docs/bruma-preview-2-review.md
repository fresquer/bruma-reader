# Bruma preview 2 review — 20 October 2026

Scope: BR-002 (own OTA), BR-009 (direct library controls/presentation), BR-005
(complete Apps), and BR-001's download name. No changes to bootloader,
partitions, eFuses, persistent catalog format or settings schema.

## Review findings resolved

- Publishing still expected a vCodex-named binary despite the local SD alias.
  The packager, release workflow, budget metadata and release gate now agree on
  one `bruma-1.6.0.40.bin`. Compatible tag/version fields remain intact.
- OTA and channel sync now support both the .39 filename and Bruma's new name.
  Regression coverage verifies the new image name against its metadata/budget.
- Manifest URL validation also rejects path syntax in a version, preventing
  an otherwise matching version/URL pair from escaping the channel directory.
- Apps destinations that replace the activity now return to Apps; Back release
  is consumed after apps that exit on press. Selection is restored by app ID,
  including after settings reorder, and pagination remains intact.
- A failed library sort or settings save restores the prior order and reports
  an error. Existing native SD failure cases and a blocked save fixture cover
  recovery while preserving the settings/catalog files.

## Verification

296 native CTest cases, including the OTA implementation with mocked network
and flash. The Python channel suite covers both naming schemes, previews,
drafts, metadata, digest, payload-before-manifest and immutable paths.
EN/ES simulator controls, all 17 Apps return paths from page two, 506 books,
filters/states and low-memory/SD failure checks. Default and gh_release builds
plus the release budget gate. clang-format and git diff --check.

Targeted cppcheck reports one existing performance suggestion in unmodified
BookmarkStore.h (`getFilePath` returns by value), not a changed-code defect.

## Physical and transport limits

The user reported the first preview working; preview 2 itself still requires
physical X4 testing. Certificate validation protects the update manifest. The
inherited low-memory payload HTTPS compatibility transport does not separately
validate certificates; the verified manifest SHA-256 authenticates every
payload byte before activation. This behavior is preserved to avoid regressing
the previously verified low-memory installation path; it is documented in the
installation guide rather than described as certificate-verified payload TLS.

Keep this a prerelease. Install the transition from .39 via SD. Real Wi-Fi OTA
requires a subsequent higher published Bruma version containing this updater.
The inherited Auto Flash is separate and remains untouched.
