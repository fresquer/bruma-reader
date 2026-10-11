#!/usr/bin/env python3
"""Exercise offline font installation through the native simulator.
Usage: python3 test/simulator/font_install.py --font /path/to/Family_14.cpfont
Uses a valid converted font fixture, isolated temporary SDs and no network.
"""
import argparse
import json
from pathlib import Path
import struct
import tempfile

from library_catalog import run

REPO = Path(__file__).resolve().parents[2]
QA = REPO / '.codex-build/font-install-qa'
PICK = ('1000:DOWN;1300:DOWN;1600:DOWN;1900:DOWN;2300:ENTER;2700:ENTER;'
        '3000:DOWN;3300:DOWN;3600:DOWN;4000:ENTER;4600:ENTER;5200:ENTER;')


def seed(root, font, existing=None, browser=False, filename='LocalFont_14.cpfont'):
    config = root / '.crosspoint'
    config.mkdir()
    incoming = root / 'incoming'
    incoming.mkdir()
    source = incoming / filename
    source.write_bytes(font)
    settings = dict(language='ES', uiTheme=6, uiThemeSchemaVersion=3,
                    settingsShortcut=0, settingsShortcutOrder=0, libraryShortcutOrder=18,
                    # Old saved flashcards preferences must not resurrect its route.
                    flashcardsShortcut=0, flashcardsShortcutVisible=1)
    for name in ['syncDay', 'browseFiles', 'library', 'readingStats', 'readingHeatmap',
                 'readingProfile', 'achievements', 'ifFound', 'recentBooks', 'bookmarks',
                 'favorites', 'dictionary', 'fileTransfer', 'screenClean', 'sleep', 'opdsBrowser']:
        settings[name + 'Shortcut'] = 1
    if browser:
        settings.update(settingsShortcut=1, browseFilesShortcut=0, browseFilesShortcutOrder=0)
    (config / 'settings.json').write_text(json.dumps(settings))
    (config / 'state.json').write_text(json.dumps(dict(openEpubPath='', showBootScreen=False)))
    target = root / '.fonts/LocalFont/LocalFont_14.cpfont'
    if existing is not None:
        target.parent.mkdir(parents=True)
        target.write_bytes(existing)
    return source, target


def exercise(root, name, script, shots=()):
    log = run(root, QA, name, script, shots)
    assert 'Entering activity: FontInstall' in log
    assert 'OOM' not in log and '[FCS]' not in log
    return log


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--font', type=Path, required=True)
    args = parser.parse_args()
    font = args.font.read_bytes()
    assert font[:8] == b'CPFONT\0\0' and struct.unpack_from('<H', font, 8)[0] == 4
    QA.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='fonts-', dir=QA) as directory:
        base = Path(directory)
        def case(name):
            root = base / name
            root.mkdir()
            return root
        root = case('install')
        source, target = seed(root, font)
        log = exercise(root, 'font-install', PICK + '6100:BACK;6400:DOWN;6800:ENTER;7100:DOWN;'
                       '7400:DOWN;7800:ENTER;8500:QUIT',
                       [(2850, 'font-settings.bmp'), (4300, 'font-picker.bmp'),
                        (5900, 'font-success.bmp'), (7650, 'font-family.bmp')])
        assert target.read_bytes() == font and source.read_bytes() == font
        settings = json.loads((root / '.crosspoint/settings.json').read_text())
        assert settings['sdFontFamilyName'] == 'LocalFont' and settings['fontSize'] == 14
        # Normalization compacts numeric ranks after retiring an access. A legacy
        # rank of 18 must retain its position after the lower-ranked accesses,
        # rather than falling back to Library's initial rank of zero.
        assert all(settings[name + 'ShortcutOrder'] < settings['libraryShortcutOrder']
                   for name in ('settings', 'browseFiles', 'dictionary', 'fileTransfer', 'screenClean'))
        assert settings['libraryShortcutOrder'] < settings['sleepShortcutOrder']
        assert 'Loaded SD font family: LocalFont' in log
        modified = bytearray(font)
        modified[13] ^= 1  # Reserved header byte; valid, different file/hash.
        root = case('cancel')
        source, target = seed(root, modified, existing=font)
        exercise(root, 'font-cancel', PICK + '6200:ENTER;6900:QUIT', [(5800, 'font-replace.bmp')])
        assert target.read_bytes() == font and source.read_bytes() == modified
        root = case('replace')
        source, target = seed(root, modified, existing=font, filename='LocalFont_014.cpfont')
        exercise(root, 'font-replace', PICK + '6100:DOWN;6500:ENTER;7500:QUIT',
                 [(7100, 'font-replaced.bmp')])
        assert target.read_bytes() == modified and source.read_bytes() == modified
        assert not list(target.parent.glob('*.backup')) and not list(target.parent.glob('*.importing'))
        root = case('active-replace')
        source, target = seed(root, modified, existing=font)
        other_size = target.with_name('LocalFont_18.cpfont')
        other_size.write_bytes(font)
        config_path = root / '.crosspoint/settings.json'
        config = json.loads(config_path.read_text())
        config.update(sdFontFamilyName='LocalFont', fontSize=14, fontSizeSchemaVersion=2)
        config_path.write_text(json.dumps(config))
        log = exercise(root, 'font-active-replace', PICK + '6100:DOWN;6500:ENTER;7500:QUIT')
        assert target.read_bytes() == modified and other_size.read_bytes() == font
        assert 'Loaded SD font family: LocalFont' in log
        root = case('write-error')
        source, target = seed(root, modified, existing=font)
        target.with_name(target.name + '.importing').mkdir()
        exercise(root, 'font-write-error', PICK + '6100:DOWN;6500:ENTER;7500:QUIT',
                 [(7100, 'font-write-error.bmp')])
        assert target.read_bytes() == font and source.read_bytes() == modified
        for name, data, filename in [
            ('invalid', b'not a font', 'LocalFont_14.cpfont'),
            ('old-version', font[:8] + b'\x03\x00' + font[10:], 'LocalFont_14.cpfont'),
            ('bad-name', font, 'LocalFont.cpfont'),
            ('truncated', font[:40], 'LocalFont_14.cpfont'),
        ]:
            root = case(name)
            source, target = seed(root, data, existing=font, filename=filename)
            exercise(root, 'font-' + name, PICK + '6500:QUIT', [(5900, 'font-' + name + '.bmp')])
            assert target.read_bytes() == font and source.read_bytes() == data
        for length in (31, 32):
            root = case('family-length-' + str(length))
            family = 'A' * length
            source, target = seed(root, font, existing=font, filename=family + '_14.cpfont')
            if length == 31:
                exercise(root, 'font-family-31', PICK + '6100:BACK;6400:DOWN;6800:ENTER;7100:DOWN;'
                         '7400:DOWN;7800:ENTER;8500:QUIT')
                installed = root / '.fonts' / family / (family + '_14.cpfont')
                assert installed.read_bytes() == font
                assert json.loads((root / '.crosspoint/settings.json').read_text())['sdFontFamilyName'] == family
            else:
                exercise(root, 'font-family-32', PICK + '6500:QUIT')
                assert not (root / '.fonts' / family).exists()
            assert target.read_bytes() == font and source.read_bytes() == font
        root = case('browser')
        source, target = seed(root, font, browser=True)
        exercise(root, 'font-browser', '1000:DOWN;1300:DOWN;1600:DOWN;1900:DOWN;2300:ENTER;'
                 '3000:ENTER;3600:ENTER;4700:QUIT', [(4300, 'font-browser-success.bmp')])
        assert target.read_bytes() == font and source.read_bytes() == font
        root = case('empty')
        source, target = seed(root, font)
        source.unlink()
        log = exercise(root, 'font-empty', PICK.replace('5200:ENTER;', '') +
                       '5500:BACK;5900:BACK;6500:QUIT', [(5200, 'font-empty.bmp')])
        assert not target.exists() and 'Handling result for popped activity' in log
    print('PASS: offline font install/select, original preservation, replace/cancel, '
          'canonical filename, active family replacement, other-size preservation, SD write failure, '
          'invalid/old/truncated files, browser opening and empty picker cancellation.')


if __name__ == '__main__':
    main()
