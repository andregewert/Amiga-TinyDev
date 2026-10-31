#!/usr/bin/env python3
"""Make the LhA archives of tests/test_sprout_lha.c (tests/data/sprout/).

The same files are packed with every header level and method jlha
(Debian: jlha-utils) can write; the test makes the expected contents with
the same rules as files() below. Run it again only when these change:

    python3 tools/mksprouttests.py
"""
import os
import shutil
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'tests', 'data', 'sprout')

# name of the archive: jlha options (header level, method)
VARIANTS = {
    'h0lh5.lha': '0o5',
    'h1lh5.lha': '1o5',
    'h2lh5.lha': '2o5',
    'h1lh6.lha': '1o6',
    'h2lh7.lha': '2o7',
    'h1lh0.lha': '1z',
}


def big():
    """ dir/sub/big.txt: lines from a linear congruential generator, larger
        than the 64 KB dictionary of -lh7-                                 """
    alpha = 'abcdefghijklmnopqrstuvwxyz'
    x = 12345
    lines = []
    for i in range(3000):
        x = (x * 1103515245 + 12345) & 0x7fffffff
        lines.append('%04d %05d %s\n' % (i, x % 100000, alpha[:(x >> 8) % 27]))
    data = ''.join(lines).encode('latin-1')
    assert len(data) > 65536
    return data


def files():
    """ the files of the archives, as (path, bytes); None for a drawer """
    return [
        ('hello.txt', b'Hello {$NAME}\r\nzweite Zeile \xe4\n'),
        ('bin.dat', bytes((i * 7 + 3) & 255 for i in range(1024))),
        ('empty.txt', b''),
        ('dir/empty', None),
        ('dir/sub/big.txt', big()),
    ]


def main():
    os.makedirs(OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for path, data in files():
            full = os.path.join(tmp, path)
            if data is None:
                os.makedirs(full, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'wb') as f:
                f.write(data)
        for name, opts in VARIANTS.items():
            arc = os.path.join(OUT, name)
            if os.path.exists(arc):
                os.remove(arc)
            subprocess.run(['jlha', 'a' + opts + 'q', arc, 'hello.txt', 'bin.dat', 'empty.txt', 'dir'],
                           cwd=tmp, check=True)
            print(name, os.path.getsize(arc), 'bytes')
    if shutil.which('lha'):
        subprocess.run(['lha', 'v', os.path.join(OUT, 'h2lh7.lha')], check=True)


if __name__ == '__main__':
    main()
