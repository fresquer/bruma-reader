#!/usr/bin/env python3
"""Check the optional Bruma home through the real X4 simulator.
Build first: pio run -e simulator
Uses an isolated SD; no network or firmware upload is performed.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile

from library_catalog import make_epub

REPO = Path(__file__).resolve().parents[2]
QA = REPO / '.codex-build/home-dashboard-qa'
PROGRAM = REPO / '.pio/build/simulator/program'


def seed(root, theme=6, orientation=0, empty=False, long_titles=False):
    settings = root / '.crosspoint'
    settings.mkdir(parents=True, exist_ok=True)
    (settings / 'settings.json').write_text(json.dumps(dict(
        language='ES', uiTheme=theme, uiThemeSchemaVersion=3, orientation=orientation,
        showStatsAfterReading=0, removeReadBooksFromRecents=1, displayHeader=1)))
    books = []
    if not empty:
        for index, (path, title, author, pct, done) in enumerate([
            ('piranesi.epub', 'Piranesi', 'Susanna Clarke', 42, False),
            ('jardin.epub', 'El jardín de tinta', 'José García', 100, True),
            ('isla.epub', 'La isla del tesoro', 'R. L. Stevenson', 18, False),
        ]):
            if long_titles:
                title = 'Un título de libro muy largo con acentos y muchas palabras para probar el espacio'
                author = 'Una autora con un nombre extraordinariamente largo'
            make_epub(root, path, title, author)
            timestamp = 1791743400 - index * 86400
            books.append(dict(bookId=f'home-{index}', path='/' + path, knownPaths=['/' + path],
                title=title, author=author, coverBmpPath='', chapterTitle='',
                totalReadingMs=8280000 if index == 0 else 3000000, sessions=8,
                lastSessionMs=1440000, firstReadAt=timestamp - 864000,
                lastReadAt=timestamp, completedAt=timestamp if done else 0,
                lastProgressPercent=pct, chapterProgressPercent=0, completed=done,
                readingDays=[dict(dayOrdinal=20737-index, readingMs=1440000)]))
    (settings / 'state.json').write_text(json.dumps(dict(
        openEpubPath='/piranesi.epub' if books else '', lastKnownValidTimestamp=1791743400,
        showBootScreen=False)))
    (settings / 'reading_stats.json').write_text(json.dumps(dict(
        formatVersion=6, readingDays=[], legacyReadingDays=[], sessionLog=[], books=books)))
    # Deliberately exclude the finished book: the dashboard must still show it.
    (settings / 'recent.json').write_text(json.dumps(dict(formatVersion=2,
        books=[books[0], books[2]] if books else [])))
    return books


def run(root, name, script, shots, required=()):
    env = os.environ.copy()
    env.update(CROSSPOINT_SIM_SD=str(root), CROSSPOINT_SIM_INPUT_SCRIPT=script,
        CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{QA / (name + "-" + label + ".bmp")}' for ms, label in shots))
    proc = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=30)
    log = proc.stdout.decode(errors='replace')
    (QA / (name + '.log')).write_text(log)
    assert proc.returncode == 0, log[-2500:]
    for activity in required:
        assert 'Entering activity: ' + activity in log, (name, activity, log[-2500:])
    assert 'OOM' not in log and 'Failed to' not in log, log[-2500:]
    for _, label in shots:
        assert (QA / (name + '-' + label + '.bmp')).is_file()
    return log


def main():
    QA.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='home-sd-', dir=QA) as folder:
        root = Path(folder)
        initial = seed(root)
        run(root, 'stats', '1600:DOWN;1900:DOWN;2200:DOWN;2800:ENTER;4000:BACK;5000:QUIT',
            [(1200, 'home'), (2500, 'focus'), (3600, 'opened'), (4600, 'returned')], ['Home', 'ReadingStats'])
        data = json.loads((root / '.crosspoint/reading_stats.json').read_text())['books']
        assert [(b['path'], b['totalReadingMs'], b['lastProgressPercent'], b['completed']) for b in data] == [
            (b['path'], b['totalReadingMs'], b['lastProgressPercent'], b['completed']) for b in initial]
        # Selecting the first recent row opens the finished book, absent from Recents.
        run(root, 'finished', '1600:DOWN;2200:ENTER;3400:QUIT', [(2800, 'opened')], ['EpubReader'])
        seed(root)
        log = run(root, 'clock', '1400:DOWN;1700:DOWN;2000:DOWN;2300:DOWN;2600:DOWN;2900:DOWN;'
            '3400:ENTER;4700:BACK;5600:BACK;6800:QUIT',
            [(3200, 'focus'), (4300, 'wifi'), (5200, 'cancelled'), (6300, 'returned')], ['ClockSync', 'WifiSelection'])
        assert 'Handling result for popped activity' in log
        seed(root)
        run(root, 'library', '1300:DOWN;1600:DOWN;1900:DOWN;2200:DOWN;2800:ENTER;4000:QUIT',
            [(3500, 'opened')], ['Library'])
        seed(root)
        run(root, 'apps', '1200:DOWN;1500:DOWN;1800:DOWN;2100:DOWN;2400:DOWN;3000:ENTER;4200:QUIT',
            [(3600, 'opened')], ['Apps'])
        seed(root)
        run(root, 'shortcuts', '1000:DOWN;1300:DOWN;1600:DOWN;1900:DOWN;2200:DOWN;2500:DOWN;'
            '2800:DOWN;3400:ENTER;4600:QUIT', [(3200, 'page'), (4000, 'opened')], ['SyncDay'])
        seed(root)
        run(root, 'selections', '1600:DOWN;2200:DOWN;2800:DOWN;3400:DOWN;4000:DOWN;4600:DOWN;5300:QUIT',
            [(1200, 'current'), (1900, 'finished'), (2500, 'unfinished'), (3100, 'stats'),
             (3700, 'library'), (4300, 'apps'), (4900, 'clock')], ['Home'])
        seed(root, orientation=1)
        run(root, 'reader-orientation', '2400:QUIT', [(1800, 'home')], ['Home'])
        seed(root, long_titles=True)
        run(root, 'long', '1400:DOWN;2400:QUIT', [(1800, 'home')], ['Home'])
        empty_root = root / 'empty-sd'
        seed(empty_root, empty=True)
        run(empty_root, 'empty', '1400:DOWN;2000:ENTER;3200:QUIT', [(1100, 'home')], ['Library'])
        seed(root, theme=0)
        run(root, 'legacy', '2300:QUIT', [(1700, 'home')], ['Home'])
    print('PASS: dashboard stats/return, finished-book opening, clock cancellation/return, Library, Apps, '
          'extra shortcuts, reader orientation, long titles, empty library and legacy theme. Artifacts:', QA)


if __name__ == '__main__':
    main()
