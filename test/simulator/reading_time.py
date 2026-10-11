#!/usr/bin/env python3
"""Real X4 UI/timing/persistence checks; build the simulator before running.
Screenshots and logs stay in ignored .codex-build/reading-time-qa/.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

REPO = Path(__file__).resolve().parents[2]
QA = REPO / '.codex-build/reading-time-qa'


def epub(path):
    title = 'Un título de capítulo muy largo para comprobar el espacio de la barra'
    with zipfile.ZipFile(path, 'w') as book:
        book.writestr('mimetype', 'application/epub+zip')
        book.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                      '<rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/>'
                      '</rootfiles></container>')
        book.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" '
                      'xmlns:dc="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="id">'
                      '<metadata><dc:identifier id="id">bruma-timing-fixture</dc:identifier>'
                      f'<dc:title>{title}</dc:title><dc:language>es</dc:language></metadata>'
                      '<manifest><item id="a" href="a.xhtml" media-type="application/xhtml+xml"/>'
                      '<item id="b" href="b.xhtml" media-type="application/xhtml+xml"/></manifest>'
                      '<spine><itemref idref="a"/><itemref idref="b"/></spine></package>')
        para = '<p>' + 'La lectura abre puertas y deja tiempo para pensar. ' * 12 + '</p>'
        for chapter in ('a', 'b'):
            book.writestr(chapter + '.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head>'
                          f'<title>{title}</title></head><body><h1>{title}</h1>{para * 35}</body></html>')


def xtc(path, chapters):
    count = 10
    chapter_data = b''
    if chapters:
        for name, start, end in [('Capítulo uno', 1, 5), ('Capítulo dos', 6, 10)]:
            chapter_data += name.encode().ljust(80, b'\0') + struct.pack('<HH', start, end) + b'\0' * 12
    table = 56 + 128 + len(chapter_data)
    data_offset = table + count * 16
    bitmap = b'\xff' * 48000
    page = struct.pack('<IHHBBIQ', 0x00475458, 480, 800, 0, 0, len(bitmap), 0) + bitmap
    header = struct.pack('<IBBHBBBBIQQQQII', 0x00435458, 1, 0, count, 0, 0, 0, int(chapters),
                         0, 0, table, data_offset, 0, 184 if chapters else 0, 0)
    entries = b''.join(struct.pack('<QIHH', data_offset + i * len(page), len(page), 480, 800)
                       for i in range(count))
    path.write_bytes(header + b'Lectura XTC de prueba\0'.ljust(128, b'\0') + chapter_data + entries + page * count)


def settings(root, **changes):
    path = root / '.crosspoint/settings.json'
    path.parent.mkdir(exist_ok=True)
    data = json.loads(path.read_text()) if path.exists() else dict(language='ES', showStatsAfterReading=0,
                  libraryShortcutOrder=0, appsHubOrder=1, statusBarTitle=0, statusBarBattery=1,
                  statusBarChapterPageCount=1, statusBarBookProgressPercentage=1)
    data.update(changes)
    path.write_text(json.dumps(data))


def run(root, name, script, shots, expected=None):
    env = os.environ.copy()
    env.update(CROSSPOINT_SIM_SD=str(root), CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{QA / (name + "-" + shot + ".bmp")}' for ms, shot in shots))
    proc = subprocess.run([str(REPO / '.pio/build/simulator/program')], cwd=REPO, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    log = proc.stdout.decode(errors='replace')
    (QA / (name + '.log')).write_text(log)
    assert proc.returncode == 0, log[-3000:]
    assert 'Entering activity: ' + (expected or ('EpubReader' if name.startswith('epub') else
           'XtcReader' if name.startswith('xtc') else 'TxtReader')) in log, log[-3000:]
    assert 'Page load retry limit' not in log and 'Failed to stream page' not in log, log[-3000:]
    return log


def stats(root):
    data = json.loads((root / '.crosspoint/reading_stats.json').read_text())
    assert data['formatVersion'] == 6
    assert len(data['books']) == 1
    return data['books'][0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--formats-only", action="store_true", help="Skip the already checked EPUB learning phase")
    args = parser.parse_args()
    QA.mkdir(parents=True, exist_ok=True)
    if not args.formats_only:
        with tempfile.TemporaryDirectory(prefix='time-', dir=QA) as temporary:
            root = Path(temporary)
            epub(root / 'Lectura.epub')
            settings(root)  # Old settings intentionally omit the new field.
            run(root, 'epub-learning', '1500:ENTER;2500:ENTER;4500:ENTER;12500:RIGHT;18500:RIGHT;'
                '24500:RIGHT;30500:RIGHT;36500:RIGHT;41000:BACK;43000:QUIT',
                [(10000, 'provisional'), (39000, 'learned')])
            first = stats(root)
            assert first['readingPace']['samples'] >= 5, first['readingPace']
            assert 5000 <= first['readingPace']['pageMs'] < 12000, first['readingPace']
            assert first['totalReadingMs'] > 30000
            assert json.loads((root / '.crosspoint/settings.json').read_text())['statusBarReadingTime'] == 1
            (QA / 'epub-saved-stats.json').write_text(json.dumps(first, indent=2))
            run(root, 'epub-restart', '1500:ENTER;4000:LEFT;4500:RIGHT;6500:BACK;8000:QUIT', [(5500, 'book')])
            second = stats(root)
            assert second['readingPace'] == first['readingPace'], (first, second)
            settings(root, statusBarReadingTime=2, orientation=1)
            run(root, 'epub-landscape', '1500:ENTER;5000:BACK;6500:QUIT', [(4000, 'chapter')])
            third = stats(root)
            assert third['readingPace']['samples'] == 0, third['readingPace']
            assert third['readingPace']['layout'] != first['readingPace']['layout']
            assert third['totalReadingMs'] >= second['totalReadingMs']
    for kind in ('txt', 'md', 'xtc-chapters', 'xtc-no-chapters'):
        with tempfile.TemporaryDirectory(prefix='time-', dir=QA) as temporary:
            root = Path(temporary)
            if kind.startswith('xtc'):
                xtc(root / 'Lectura.xtc', kind == 'xtc-chapters')
                settings(root, statusBarReadingTime=2, xtcStatusBarMode=1)
            else:
                (root / ('Lectura.' + kind)).write_text(('Una línea de texto para leer. ' * 15 + '\n\n') * 40)
                settings(root, statusBarReadingTime=2)
            run(root, kind, '1500:ENTER;2500:ENTER;4500:ENTER;18000:RIGHT;22000:BACK;24000:QUIT',
                [(14000, 'chapter'), (20000, 'next')])
            first = stats(root)
            assert first['readingPace']['samples'] == 1, first['readingPace']
            assert first['readingPace']['layout'] != 0
            settings(root, statusBarReadingTime=0, xtcStatusBarMode=2, orientation=1)
            run(root, kind + '-hidden', '1500:ENTER;5000:BACK;6500:QUIT', [(4000, 'hidden')])
            second = stats(root)
            if kind.startswith('xtc'):
                assert second['readingPace'] == first['readingPace']
            else:
                assert second['readingPace']['samples'] == 0
            assert second['totalReadingMs'] >= first['totalReadingMs']
    with tempfile.TemporaryDirectory(prefix='time-', dir=QA) as temporary:
        root = Path(temporary)
        settings(root, settingsShortcutOrder=0, libraryShortcutOrder=9, statusBarReadingTime=1)
        run(root, 'settings-preview', '1500:ENTER;2300:ENTER;3000:UP;3800:ENTER;4400:UP;'
            '5000:ENTER;5600:DOWN;6200:ENTER;7200:BACK;7600:BACK;8000:BACK;9000:QUIT',
            [(4800, 'book'), (5800, 'options'), (6700, 'chapter')], expected='StatusBarSettings')
        assert json.loads((root / '.crosspoint/settings.json').read_text())['statusBarReadingTime'] == 2
    print('Reading time: TXT/MD/XTC learning, layouts and settings passed' if args.formats_only else
          'Reading time: automatic learning, restart, old settings, layout reset, EPUB/TXT/MD/XTC modes passed')


if __name__ == '__main__':
    main()
