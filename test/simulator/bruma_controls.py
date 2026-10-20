#!/usr/bin/env python3
"""Real simulator checks for direct Library sorting and the complete Apps catalog.

Uses only isolated SD fixtures. Artifacts stay in .codex-build/bruma-controls-qa.
Build the simulator first; no physical device is connected or flashed.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from library_catalog import make_epub

REPO = Path(__file__).resolve().parents[2]
QA = REPO / '.codex-build/bruma-controls-qa'
PROGRAM = REPO / '.pio/build/simulator/program'
APPS = ['library', 'browseFiles', 'syncDay', 'settings', 'readingStats', 'readingHeatmap',
        'readingProfile', 'achievements', 'ifFound', 'recentBooks', 'bookmarks', 'favorites',
        'dictionary', 'fileTransfer', 'screenClean', 'sleep', 'opdsBrowser']


def settings(root, language='EN', home=False):
    store = root / '.crosspoint'
    store.mkdir(parents=True, exist_ok=True)
    data = dict(language=language, uiTheme=3, showStatsAfterReading=0,
                appsHubShortcutOrder=2, libraryOrder=0, libraryFilter=0)
    for index, app in enumerate(APPS):
        data[app + 'Shortcut'] = 0 if home else 1
        data[app + 'ShortcutVisible'] = 0
        data[app + 'ShortcutOrder'] = index + 3
    data.update(libraryShortcut=0, libraryShortcutVisible=1, libraryShortcutOrder=1,
                opdsBrowserShortcutOrder=0)
    (store / 'settings.json').write_text(json.dumps(data))


def run(root, name, script, shots):
    env = os.environ.copy()
    env.update(CROSSPOINT_SIM_SD=str(root), CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{time}:{QA / (name + "-" + label + ".bmp")}' for time, label in shots))
    proc = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=50)
    log = proc.stdout.decode(errors='replace')
    (QA / (name + '.log')).write_text(log)
    assert proc.returncode == 0, log[-3000:]
    assert 'OOM' not in log and 'Fatal' not in log, log[-3000:]
    for _, label in shots:
        assert (QA / (name + '-' + label + '.bmp')).exists()
    return log


def check_app_returns():
    # All registered applications open and return to the same page/selection.
    # Exercise them from page two, with every Home shortcut hidden.
    for app in APPS:
        with tempfile.TemporaryDirectory(prefix='return-', dir=QA) as directory:
            root = Path(directory)
            settings(root)
            path = root / '.crosspoint/settings.json'
            data = json.loads(path.read_text())
            data['appsHubShortcutOrder'] = 17
            data[app + 'ShortcutOrder'] = 15
            for index, other in enumerate(other for other in APPS if other != app):
                data[other + 'ShortcutOrder'] = index if index < 15 else index + 1
            # OPDS is Home-hidden; library remains the only Home entry before Apps.
            path.write_text(json.dumps(data))
            moves = ';'.join(f'{2600 + index * 170}:RIGHT' for index in range(15))
            log = run(root, 'return-' + app,
                      '1200:DOWN;1900:ENTER;' + moves + ';6100:ENTER;8500:BACK;10400:QUIT',
                      [(5800, 'before'), (10000, 'after')])
            # Child activities pop; replacement destinations recreate Apps.
            assert ('Popped from activity stack' in log or log.count('Entering activity: Apps') == 2), app
            assert (QA / ('return-' + app + '-before.bmp')).read_bytes() == (QA / ('return-' + app + '-after.bmp')).read_bytes(), app

def check_save_failure():
    with tempfile.TemporaryDirectory(prefix='save-failure-', dir=QA) as directory:
        root = Path(directory)
        settings(root)
        for i in range(3):
            make_epub(root, f'book{i}.epub', f'Book {i}', f'Author {3-i}')
        (root / '.crosspoint/settings.json.tmp').mkdir()
        (root / '.crosspoint/settings.json.tmp/blocked').write_text('Simulated unwritable SD target')
        before = (root / '.crosspoint/settings.json').read_bytes()
        log = run(root, 'sort-save-failure',
                  '1200:ENTER;2300:ENTER;5000:DOWN;6500:QUIT', [(6000, 'error')])
        assert re.findall(r'Order=(\d+)', log) == ['0'], log[-2500:]
        assert (root / '.crosspoint/settings.json').read_bytes() == before


def main():
    QA.mkdir(parents=True, exist_ok=True)
    for language in ('EN', 'ES'):
        with tempfile.TemporaryDirectory(prefix='controls-', dir=QA) as directory:
            root = Path(directory)
            settings(root, language)
            for i in range(28):
                make_epub(root, f'books/book{i:02}.epub', f'Book {i:02}', f'Author {27-i:02}')
            log = run(root, language + '-library',
                '1200:ENTER;2300:ENTER;6000:RIGHT;6600:DOWN;7400:DOWN;8200:DOWN;9000:DOWN;'
                '9800:UP:1700;12500:ENTER;16500:BACK;19000:QUIT',
                [(5700, 'title'), (7100, 'author'), (7900, 'last-read'), (8700, 'added'),
                 (9500, 'title-again'), (12000, 'held-once'), (18000, 'reader-return')])
            assert re.findall(r'Order=(\d+)', log) == ['1', '2', '3', '0', '3'], log[-4000:]
            assert log.count('Entering activity: Library') == 2, log[-3000:]
            assert 'Entering activity: EpubReader' in log
            saved = json.loads((root / '.crosspoint/settings.json').read_text())
            assert saved['libraryOrder'] == 3 and saved['libraryFilter'] == 0
            stats = json.loads((root / '.crosspoint/reading_stats.json').read_text())
            assert any(book['path'].endswith('book01.epub') for book in stats['books']), stats
            # Restart retains order. A front-button hold pages once, never changes sort.
            restart = run(root, language + '-restart',
                '1200:ENTER;2200:RIGHT:1500;4300:QUIT', [(4000, 'paged')])
            assert 'Order=' not in restart
            assert json.loads((root / '.crosspoint/settings.json').read_text())['libraryOrder'] == 3
        with tempfile.TemporaryDirectory(prefix='empty-', dir=QA) as directory:
            root = Path(directory)
            settings(root, language)
            log = run(root, language + '-empty', '1200:ENTER;2200:UP;3100:DOWN;4000:QUIT', [(3600, 'empty')])
            assert re.findall(r'Order=(\d+)', log) == ['3', '0']
        hashes = []
        for home in (False, True):
            with tempfile.TemporaryDirectory(prefix='apps-', dir=QA) as directory:
                root = Path(directory)
                settings(root, language, home)
                name = language + ('-apps-home' if home else '-apps-hidden')
                log = run(root, name, '1200:DOWN;1900:ENTER;3200:ENTER;5000:BACK;6400:QUIT',
                          [(2700, 'catalog'), (4400, 'opds'), (5900, 'returned')])
                assert log.count('Entering activity: Apps') == 2 and 'Entering activity: OpdsServerList' in log, log[-3000:]
                assert (QA / (name + '-catalog.bmp')).read_bytes() == (QA / (name + '-returned.bmp')).read_bytes(), name
                # Identical catalog pixels despite different shortcut locations.
                hashes.append(hashlib.sha256((QA / (name + '-catalog.bmp')).read_bytes()).hexdigest())
        assert hashes[0] == hashes[1], (language, hashes)
    check_save_failure()
    if '--controls-only' not in sys.argv:
        check_app_returns()
    print('PASS: EN/ES direct order, wrap, held-once, selection/reader return, restart, paging, empty catalog and Apps independence/OPDS. Artifacts:', QA)


if __name__ == '__main__':
    if '--returns-only' in sys.argv:
        QA.mkdir(parents=True, exist_ok=True)
        check_app_returns()
        print('PASS: all 17 Apps return with selection and page preserved')
    else:
        main()
