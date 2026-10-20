#!/usr/bin/env python3
"""Exercise real EPUB metadata and Library navigation in the X4 simulator.

Build first: pio run -e simulator
Run: python3 test/simulator/library_catalog.py
Optional: --large adds 500 books to the real simulated SD scan.
Screenshots and logs are written under the ignored .codex-build/library-qa/.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
from xml.sax.saxutils import escape

REPO = Path(__file__).resolve().parents[2]


def make_epub(root, path, title, author):
    output = root / path
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w') as book:
        book.writestr('mimetype', 'application/epub+zip')
        book.writestr('META-INF/container.xml', '<container version="1.0" '
                      'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>'
                      '<rootfile full-path="OEBPS/content.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        book.writestr('OEBPS/content.opf', '<package xmlns="http://www.idpf.org/2007/opf" '
                      'xmlns:dc="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="id">'
                      f'<metadata><dc:identifier id="id">{escape(str(path))}</dc:identifier>'
                      f'<dc:title>{escape(title)}</dc:title><dc:creator>{escape(author)}</dc:creator>'
                      '<dc:language>es</dc:language></metadata><manifest>'
                      '<item id="c" href="chapter.xhtml" media-type="application/xhtml+xml"/>'
                      '</manifest><spine><itemref idref="c"/></spine></package>')
        book.writestr('OEBPS/chapter.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head>'
                      f'<title>{escape(title)}</title></head><body><h1>{escape(title)}</h1>'
                      '<p>Esta es una prueba de lectura de Bruma Reader. La biblioteca reúne '
                      'libros de distintas carpetas.</p></body></html>')


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def catalog(path):
    data = path.read_bytes()
    header = struct.unpack_from('<8I', data)
    assert header[0:2] == (0x42524c42, 1)
    assert checksum(data[:28]) == header[7]
    records = {}
    offset = 32
    for _ in range(header[2]):
        fields = struct.unpack_from('<10I', data, offset)
        end = offset + 40 + sum(fields[:3])
        assert checksum(data[offset:end]) == struct.unpack_from('<I', data, end)[0]
        pos = offset + 40
        strings = []
        for size in fields[:3]:
            strings.append(data[pos:pos + size].decode('utf-8'))
            pos += size
        records[strings[0]] = strings[1:]
        offset = end + 4
    assert offset == len(data)
    return records


def run(root, qa, name, script, screenshots=(), timeout=30, low_memory=False):
    env = os.environ.copy()
    env.update(CROSSPOINT_SIM_SD=str(root), CROSSPOINT_SIM_INPUT_SCRIPT=script)
    env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{at}:{qa / filename}' for at, filename in screenshots)
    if low_memory:
        env.update(CROSSPOINT_SIM_FREE_HEAP='16000', CROSSPOINT_SIM_MAX_ALLOC_HEAP='16000')
    log_path = qa / f'{name}.log'
    with log_path.open('w') as log:
        subprocess.run([str(REPO / '.pio/build/simulator/program')], cwd=REPO, env=env,
                       stdout=log, stderr=subprocess.STDOUT, timeout=timeout, check=True)
    return log_path.read_text()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--large', action='store_true')
    args = parser.parse_args()
    qa = REPO / '.codex-build/library-qa'
    qa.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='library-sd-', dir=REPO / '.codex-build') as directory:
        root = Path(directory)
        (root / '.crosspoint').mkdir()
        (root / '.crosspoint/settings.json').write_text(json.dumps({
            'language': 'ES', 'showStatsAfterReading': 0, 'libraryShortcutOrder': 0,
            'appsHubShortcutOrder': 1,
        }))
        make_epub(root, 'Novelas/a.epub', 'El jardín de tinta', 'José García')
        make_epub(root, 'Ensayo/Subcarpeta/b.EPUB', 'Bruma 2', 'Ana Martín')
        make_epub(root, 'Otra copia/a.epub', 'El jardín de tinta', 'José García')
        make_epub(root, 'Ensayo/cafe.epub', 'Café', 'Lé')
        make_epub(root, '.oculta/ignorado.epub', 'Oculto', 'Nadie')
        make_epub(root, 'System Volume Information/ignorado.epub', 'Sistema', 'Nadie')
        (root / 'Notas').mkdir()
        (root / 'Notas/Apuntes.md').write_text('# Apuntes\nUn texto de prueba.')
        (root / 'dañado.epub').write_text('not a zip')
        (root / 'portada.png').write_text('ignored')
        if args.large:
            for i in range(500):
                make_epub(root, f'Shelf{i % 7}/book{i}.epub', f'Book {i}', f'Author {i % 20}')
        menu_at = 45000 if args.large else 6000
        script = (f'1500:ENTER;2500:ENTER;{menu_at}:ENTER:1200;'
                  f'{menu_at + 2000}:BACK;{menu_at + 2500}:DOWN;{menu_at + 2900}:LEFT:1200;{menu_at + 4700}:QUIT')
        first = run(root, qa, 'scan', script, [
            (1200, 'home.bmp'), (2200, 'create.bmp'),
            (menu_at - 500, 'library.bmp'), (menu_at + 1800, 'menu.bmp'),
            (menu_at + 2800, 'author-order.bmp'), (menu_at + 4400, 'last-page.bmp'),
        ], timeout=65)
        books = catalog(root / '.crosspoint/library.bin')
        assert len(books) == (506 if args.large else 6), len(books)
        assert books['/Ensayo/Subcarpeta/b.EPUB'] == ['Bruma 2', 'Ana Martín']
        assert books['/Ensayo/cafe.epub'] == ['Café', 'Lé']
        assert books['/dañado.epub'] == ['dañado', '']
        assert books['/Notas/Apuntes.md'] == ['Apuntes', '']
        assert not any('.oculta' in path or 'System Volume Information' in path for path in books)
        settings = json.loads((root / '.crosspoint/settings.json').read_text())
        assert settings['libraryOrder'] == 1, settings['libraryOrder']
        assert 'Entering activity: Reader' not in first, 'Holding Confirm unexpectedly opened a book'

        # Restart must preserve Author order. Open the first real book and return.
        second = run(root, qa, 'reader-return', '1500:ENTER;2500:ENTER;6500:BACK;8000:QUIT', [
            (2100, 'author-restart.bmp'), (5000, 'reader.bmp'), (7400, 'returned.bmp'),
        ])
        assert 'Entering activity: EpubReader' in second, second
        assert second.count('Entering activity: Library') >= 2, second
        assert (root / '.crosspoint/library-open.bin').stat().st_size > 44
        assert json.loads((root / '.crosspoint/settings.json').read_text())['libraryOrder'] == 1

        # Catalog load refuses unsafe allocations at low heap, without rewriting SD state.
        before = (root / '.crosspoint/library.bin').read_bytes()
        low = run(root, qa, 'low-memory', '1500:DOWN;2000:ENTER;3000:ENTER;4500:QUIT', [(4000, 'low-memory.bmp')], low_memory=True)
        assert 'Entering activity: Library' in low, low
        assert (root / '.crosspoint/library.bin').read_bytes() == before
        print(f'Simulator: {len(books)} books, real EPUB metadata, menu, saved order, reader return and low memory passed')
        print(f'Screenshots and logs: {qa}')


if __name__ == '__main__':
    main()
