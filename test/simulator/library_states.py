#!/usr/bin/env python3
"""Real X4 library filters, manual states, empty view and reader return checks."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

from library_catalog import make_epub, checksum
from reading_time import epub

REPO = Path(__file__).resolve().parents[2]
QA = REPO / '.codex-build/library-states-qa'


def run(root, name, script, shots):
    env = os.environ.copy()
    env.update(CROSSPOINT_SIM_SD=str(root), CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{QA / (name + "-" + shot + ".bmp")}' for ms, shot in shots))
    result = subprocess.run([str(REPO / '.pio/build/simulator/program')], cwd=REPO, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
    text = result.stdout.decode(errors='replace')
    (QA / (name + '.log')).write_text(text)
    assert result.returncode == 0, text[-3000:]
    assert 'Entering activity: Library' in text, text[-3000:]
    assert 'Could not clear' not in text
    return text


def state_events(root):
    data = (root / '.crosspoint/library-state.bin').read_bytes()
    result = {}
    offset = 0
    while offset < len(data):
        fields = struct.unpack_from('<10I', data, offset)
        end = offset + 40 + sum(fields[:3])
        assert checksum(data[offset:end]) == struct.unpack_from('<I', data, end)[0]
        assert fields[7] == 1 and fields[8] <= 3
        result[data[offset + 40:offset + 40 + fields[0]].decode()] = fields[8]
        offset = end + 4
    return result


def main():
    QA.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='states-', dir=QA) as tmp:
        root = Path(tmp)
        (root / '.crosspoint').mkdir()
        (root / 'a.txt').write_text(('Una lectura de prueba para volver al mismo listado. ' * 10 + '\n\n') * 20)
        epub(root / 'b.epub')
        with zipfile.ZipFile(root / 'b.epub') as book:
            contents = [(entry.filename, book.read(entry)) for entry in book.infolist()]
        with zipfile.ZipFile(root / 'b.epub', 'w') as book:
            for name, content in contents:
                content = content.replace(b'Un t\xc3\xadtulo de cap\xc3\xadtulo muy largo para comprobar el espacio de la barra', b'B pending')
                book.writestr(name, content)
        make_epub(root, 'c.epub', 'C finished', 'Autor')
        settings_path = root / '.crosspoint/settings.json'
        settings_path.write_text(json.dumps(dict(language='ES', uiTheme=3, libraryShortcutOrder=0,
                appsHubOrder=1, showStatsAfterReading=0, libraryFilter=0, libraryOrder=0)))
        seeded_books = [dict(bookId='1' * 32, path='/a.txt', knownPaths=['/a.txt'], title='a', author='',
                            lastProgressPercent=25, totalReadingMs=30000, completed=False, readingDays=[]),
                        dict(bookId='2' * 32, path='/c.epub', knownPaths=['/c.epub'], title='C finished', author='Autor',
                            lastProgressPercent=100, totalReadingMs=0, completed=True, readingDays=[])]
        (root / '.crosspoint/reading_stats.json').write_text(json.dumps(dict(formatVersion=6,
                   readingDays=[], legacyReadingDays=[], sessionLog=[], books=seeded_books)))
        first = run(root, 'manual-filters',
            '1500:ENTER;2500:ENTER;4500:ENTER:1200;6200:UP;6700:ENTER;7400:ENTER:1200;'
            '9000:UP;9500:ENTER;10200:ENTER:1200;11800:UP;12300:UP;12800:ENTER;'
            '14000:ENTER:1200;15600:UP;16100:ENTER;17200:ENTER;18100:UP;18600:UP;19100:ENTER;20400:QUIT',
            [(4200, 'all'), (7000, 'marked'), (9800, 'unmarked'), (13400, 'finished'),
             (16500, 'empty'), (19800, 'pending')])
        assert 'Entering activity: Reader' not in first
        assert json.loads(settings_path.read_text())['libraryFilter'] == 2
        assert state_events(root) == {'/a.txt': 2, '/c.epub': 2}
        assert (root / '.crosspoint/library.bin').exists()
        before = {book['path']: (book['lastProgressPercent'], book['totalReadingMs'], book.get('sessions', 0), book['completed'])
                  for book in json.loads((root / '.crosspoint/reading_stats.json').read_text())['books']}
        assert before['/a.txt'] == (25, 30000, 0, False), before
        assert before['/c.epub'] == (100, 0, 0, True), before  # Manual library state changes no reading statistics.
        second = run(root, 'restart-reader-return',
            '1500:ENTER;2500:ENTER;7000:BACK;9500:ENTER;10500:UP;11000:UP;11500:UP;12000:ENTER;'
            '13000:ENTER;17000:BACK;18500:QUIT',
            [(2100, 'pending'), (8500, 'empty-after-open'), (12600, 'reading'), (17700, 'returned')])
        assert 'Entering activity: EpubReader' in second, second[-2000:]
        assert 'Entering activity: TxtReader' in second, second[-2000:]
        assert second.count('Entering activity: Library') == 3
        assert json.loads(settings_path.read_text())['libraryFilter'] == 1
        assert state_events(root) == {'/a.txt': 2, '/c.epub': 2}
        settings = json.loads(settings_path.read_text())
        settings['uiTheme'] = 6  # The same library from the new Bruma Home dashboard.
        settings_path.write_text(json.dumps(settings))
        final = run(root, 'bruma-auto-finish',
            '1500:DOWN;1900:DOWN;2300:DOWN;2600:DOWN;3000:ENTER;3500:DOWN;4000:DOWN;4500:ENTER;'
            '8000:RIGHT;10000:BACK;13000:QUIT', [(11000, 'returned')])
        assert 'Entering activity: EpubReader' in final, final[-2000:]
        assert state_events(root)['/c.epub'] == 0  # Real completion restores automatic Finished.
        assert json.loads(settings_path.read_text())['libraryFilter'] == 1
    print('Library states: filters, persistence, manual undo, empty views, reader return and automatic completion passed')


if __name__ == '__main__':
    main()
