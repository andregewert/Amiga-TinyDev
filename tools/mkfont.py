#!/usr/bin/env python3
"""Build a monospaced Amiga bitmap font from a glyph text file.

Usage: mkfont.py <name> <fonts drawer> <glyphs.txt>[:<size>] ...

Writes <fonts drawer>/<name>.font (struct FontContentsHeader listing every
size) and <fonts drawer>/<name>/<size> for each glyph file. A size is the
height of the file's cells unless given after a colon: a larger one gets
empty rows below the descenders, so only the line height grows. The format
of the glyph file is described in fonts/Holstein/glyphs.txt.

A font file is a hunk executable (as diskfont.library loads it with
LoadSeg()): one code hunk starting with "moveq #-1,d0; rts", then
struct DiskFontHeader with its struct TextFont, the character bitmap (one
strip, all glyphs side by side, word aligned rows) and tf_CharLoc, followed
by the relocations of the four pointers in it (see diskfont/diskfont.h and
graphics/text.h of the NDK).
"""
import os
import struct
import sys

XSIZE = 8            # width of every glyph
HEIGHT = 16          # rows of a glyph in the text file, unless "!height" says otherwise
BASELINE = 11        # row of the baseline (tf_Baseline), unless "!baseline" says otherwise
LO, HI = 32, 255     # tf_LoChar, tf_HiChar

FPF_DISKFONT = 0x02
FPF_DESIGNED = 0x40
NT_FONT = 12
DFH_ID = 0x0F80
FCH_ID = 0x0F00
HUNK_HEADER, HUNK_CODE, HUNK_RELOC32, HUNK_END = 0x3F3, 0x3E9, 0x3EC, 0x3F2


def read_glyphs(path):
    """Read a glyph text file: (dict code -> list of row strings, height, baseline)."""
    glyphs, cur = {}, None
    height, baseline = HEIGHT, BASELINE
    with open(path, encoding='ascii') as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if not line or line.startswith(';'):
                continue
            if line.startswith('!'):
                key, _, value = line[1:].partition(' ')
                if key == 'height':
                    height = int(value)
                elif key == 'baseline':
                    baseline = int(value)
                else:
                    sys.exit(f'{path}:{n}: unknown setting {key}')
            elif line.startswith('='):
                cur = int(line[1:].split()[0])
                if not LO <= cur <= HI or cur in glyphs:
                    sys.exit(f'{path}:{n}: bad or repeated code {cur}')
                glyphs[cur] = []
            elif cur is not None and len(line) == XSIZE and set(line) <= set('.#'):
                glyphs[cur].append(line)
            else:
                sys.exit(f'{path}:{n}: not a row of {XSIZE} pixels')
    for code, rows in glyphs.items():
        if len(rows) != height:
            sys.exit(f'{path}: glyph {code} has {len(rows)} rows, not {height}')
    if not 0 < baseline < height:
        sys.exit(f'{path}: baseline {baseline} outside the cell')
    return glyphs, height, baseline


def default_glyph(height, baseline):
    """The box shown for codes without a glyph."""
    rows = ['.' * XSIZE] * height
    for r in range(max(baseline - 8, 0), baseline + 1):
        rows[r] = '.#######' if r in (max(baseline - 8, 0), baseline) else '.#.....#'
    return rows


def font_file(glyphs, height, baseline, name, ysize):
    """The hunk file of one size, as bytes."""
    pad = ['.' * XSIZE] * (ysize - height)
    order = list(range(LO, HI + 1)) + [None]          # None: the default glyph (last entry)
    slots, slot_of = [], {}
    for code in order[:-1]:
        if code in glyphs:
            slot_of[code] = len(slots)
            slots.append(glyphs[code] + pad)
    slot_of[None] = len(slots)
    slots.append(default_glyph(height, baseline) + pad)

    modulo = (len(slots) * XSIZE + 15) // 16 * 2       # bytes per row, even
    bitmap = bytearray(modulo * ysize)
    for s, rows in enumerate(slots):
        for y, row in enumerate(rows):
            for x, pixel in enumerate(row):
                if pixel == '#':
                    bit = s * XSIZE + x
                    bitmap[y * modulo + bit // 8] |= 0x80 >> (bit % 8)
    charloc = b''.join(struct.pack('>HH', slot_of.get(c, slot_of[None]) * XSIZE, XSIZE) for c in order)

    dfh = 4                                            # after moveq/rts
    dfh_name = dfh + 22                                # struct Node, FileID, Revision, Segment
    tf = dfh_name + 32                                 # MAXFONTNAME
    chardata = tf + 52                                 # sizeof(struct TextFont)
    charloc_off = chardata + len(bitmap)
    end = charloc_off + len(charloc)
    size = (end + 3) & ~3
    flags = FPF_DESIGNED | FPF_DISKFONT

    h = bytearray(b'\x70\xff\x4e\x75')                 # moveq #-1,d0 ; rts
    h += struct.pack('>IIBbI', 0, 0, NT_FONT, 0, dfh_name)             # dfh_DF
    h += struct.pack('>HHi', DFH_ID, 1, 0)                             # dfh_FileID, _Revision, _Segment
    h += name.encode('latin-1')[:31].ljust(32, b'\0')                  # dfh_Name
    h += struct.pack('>IIBbIIH', 0, 0, NT_FONT, 0, dfh_name, 0, end - tf)  # tf_Message
    h += struct.pack('>HBBHHHHBB', ysize, 0, flags, XSIZE, baseline, 1, 0, LO, HI)
    h += struct.pack('>IHIII', chardata, modulo, charloc_off, 0, 0)    # no CharSpace/CharKern: monospaced
    assert len(h) == chardata
    h += bitmap + charloc
    h += bytes(size - len(h))

    relocs = (dfh + 10, tf + 10, tf + 34, tf + 40)     # the two ln_Name, tf_CharData, tf_CharLoc
    out = struct.pack('>6I', HUNK_HEADER, 0, 1, 0, 0, size // 4)
    out += struct.pack('>II', HUNK_CODE, size // 4) + bytes(h)
    out += struct.pack('>III', HUNK_RELOC32, len(relocs), 0) + b''.join(struct.pack('>I', r) for r in relocs)
    out += struct.pack('>II', 0, HUNK_END)
    return out


def contents_file(name, sizes):
    """<name>.font: struct FontContentsHeader and a struct FontContents per size."""
    out = struct.pack('>HH', FCH_ID, len(sizes))
    for s in sizes:
        out += f'{name}/{s}'.encode('latin-1').ljust(256, b'\0')
        out += struct.pack('>HBB', s, 0, FPF_DESIGNED | FPF_DISKFONT)
    return out


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__.split('\n\n')[1])
    name, drawer = sys.argv[1:3]
    files = {}
    for arg in sys.argv[3:]:
        path, _, size = arg.partition(':')
        glyphs, height, baseline = read_glyphs(path)
        size = int(size) if size else height
        if size < height or size in files:
            sys.exit(f'{arg}: size {size} smaller than the cells or given twice')
        files[size] = font_file(glyphs, height, baseline, name, size)
    os.makedirs(os.path.join(drawer, name), exist_ok=True)
    for size, data in files.items():
        with open(os.path.join(drawer, name, str(size)), 'wb') as f:
            f.write(data)
    with open(os.path.join(drawer, name + '.font'), 'wb') as f:
        f.write(contents_file(name, sorted(files)))


if __name__ == '__main__':
    main()
