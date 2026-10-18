#!/usr/bin/env python3
# mkicons.py - Workbench icons of the TinyDev distribution
#
# Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
#
# The icon writer (classic 4 colour icons, GlowIcons, NewIcons) and the
# motifs drawer, readme, license, install and tiles come from
# amiga-devkit/tools/amigaicons.py (git submodule). This file adds the
# motifs of TinyDev, of Scout and of the Scout parsers and registers them
# there:
#
#   tinydev    the editor window: line numbers, coloured code, the minimap
#   scout      a page of code under a magnifying glass
#   parser_<x> a chip with the label of the parser's language (parser())
#
# Run directly (or "make icons") to write sample icons of all styles to
# icons/<Style>/ and a preview picture icons/preview.png.

import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'amiga-devkit', 'tools'))
import amigaicons as ai  # noqa: E402

from amigaicons import STYLES, WBDRAWER, WBTOOL, WBPROJECT, write_icon  # noqa: E402,F401

BLACK, WHITE, TEXT = ai.BLACK, ai.WHITE, ai.TEXT
BLUE, GREY, PAPER, RED, GREEN = ai.BLUE, ai.GREY, ai.PAPER, ai.RED, ai.GREEN
INK = ((90, 96, 112), (40, 44, 56))           # chip body
COMMENT = (150, 154, 166)
KEYWORD, STRING, PREPRO = BLUE[1], GREEN[1], RED[1]

# Letters of the language labels: 5 x 7 for GlowIcons and NewIcons,
# 3 x 5 for the classic icons. Add a letter here for a new parser.
GLYPHS = {
    'C': ('.###.', '#...#', '#....', '#....', '#....', '#...#', '.###.'),
    'S': ('.####', '#....', '#....', '.###.', '....#', '....#', '####.'),
}
SMALL = {
    'C': ('.##', '#..', '#..', '#..', '.##'),
    'S': ('###', '#..', '###', '..#', '###'),
}


def glyph(c, x, y, letter, colour, small=False):
    """ a letter of GLYPHS (or SMALL) with its upper left corner at x, y """
    for dy, row in enumerate((SMALL if small else GLYPHS)[letter]):
        for dx, p in enumerate(row):
            if p == '#':
                if isinstance(c, ai.Canvas):
                    c.rect(x + dx, y + dy, x + dx, y + dy, colour)
                else:
                    c.put(x + dx, y + dy, colour)


# Code as it is shown in the editor: rows of (indent, ((length, kind), ...)).
# kinds: 'p' preprocessor, 'k' keyword, 't' text, 's' string, 'c' comment.
CODE = ((0, ((8, 'p'), (7, 't'))),
        (0, ((3, 'k'), (8, 't'))),
        (0, ((1, 't'),)),
        (3, ((3, 'k'), (5, 't'), (6, 's'))),
        (3, ((11, 'c'),)),
        (3, ((6, 'k'), (2, 't'))),
        (0, ((1, 't'),)))
GLOW_KIND = {'p': PREPRO, 'k': KEYWORD, 't': TEXT, 's': STRING, 'c': COMMENT}


def code_rows(width):
    """ the CODE segments as (row, x0, x1, kind) within 'width' columns """
    out = []
    for row, (indent, parts) in enumerate(CODE):
        x = indent
        for n, kind in parts:
            if x < width:
                out.append((row, x, min(x + n - 1, width - 1), kind))
            x += n + 1
    return out


# ------------------------------------------------------ classic pictures ---
# pens: 0 grey, 1 black, 2 white, 3 blue

def pic_tinydev():
    c = ai.Canvas(40, 24)
    c.rect(1, 1, 38, 22, 2)
    c.frame(1, 1, 38, 22, 1, 1)
    c.rect(2, 2, 37, 4, 3)                    # title bar
    c.rect(2, 5, 5, 21, 0)                    # line numbers
    for y in range(7, 21, 2):
        c.rect(3, y, 4, y, 1)
    c.rect(32, 5, 32, 21, 1)                  # minimap
    c.rect(33, 5, 37, 21, 0)
    c.frame(33, 8, 37, 13, 2, 2)              # its viewport
    for row, x0, x1, kind in code_rows(24):
        y = 7 + 2 * row
        c.rect(7 + x0, y, 7 + x1, y, 3 if kind in 'pk' else 1)
        c.rect(34 + x0 // 6, 6 + 2 * row, 34 + x1 // 6, 6 + 2 * row, 3 if kind in 'pk' else 1)
    return c


def pic_scout():
    c = ai.CLASSIC['readme']()
    c.rect(5, 9, 24, 21, 2)                   # clear the lower lines
    for i, y in enumerate((9, 11, 13)):
        c.rect(7, y, (18, 14, 20)[i], y, 1 if i != 1 else 3)
    # the magnifying glass: ring, glass, handle
    cx, cy, r = 21, 14, 6
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if d <= r + 0.3:
                c.rect(x, y, x, y, 1 if d > r - 1.2 else 2)
    c.rect(17, 13, 24, 14, 3)                 # a line, enlarged
    for i in range(5):
        c.rect(25 + i, 19 + i, 27 + i, 19 + i, 1)
    return c


def classic_parser(letter):
    def pic():
        c = ai.Canvas(36, 24)
        for y in range(5, 20, 3):             # pins
            c.rect(4, y, 7, y + 1, 1)
            c.rect(28, y, 31, y + 1, 1)
        c.rect(8, 2, 27, 21, 1)               # body
        c.rect(16, 2, 19, 3, 0)               # notch
        c.rect(12, 8, 23, 16, 3)              # label
        glyph(c, 16, 10, letter, 2, small=True)
        return c
    return pic


# --------------------------------------------- GlowIcons motifs ---

def m_tinydev(s, c):
    frame = s.line(GREY[1])
    c.rect(1, 3, 40, 35, frame)                       # window
    c.rect(2, 4, 39, 8, s.ramp(*BLUE)(2, 4, 39, 8))   # title bar
    c.rect(3, 5, 5, 7, WHITE)
    c.rect(2, 9, 7, 34, s.ramp(*GREY)(2, 9, 7, 34))   # line numbers
    for y in range(12, 33, 3):
        c.line(4, y, 5, y, GREY[1])
    c.rect(8, 9, 8, 34, frame)
    c.rect(9, 9, 32, 34, s.ramp(WHITE, PAPER[1])(9, 9, 32, 34))     # code
    c.rect(33, 9, 33, 34, frame)
    c.rect(34, 9, 39, 34, s.ramp(*GREY)(34, 9, 39, 34))             # minimap
    c.rect(34, 13, 39, 21, mix_light(GREY[0]))                      # its viewport
    c.line(34, 13, 39, 13, GREY[1])
    c.line(34, 21, 39, 21, GREY[1])
    for row, x0, x1, kind in code_rows(22):
        c.line(10 + x0, 12 + 3 * row, 10 + x1, 12 + 3 * row, GLOW_KIND[kind])
    for row, x0, x1, kind in code_rows(22):
        y = 11 + 2 * row + (row > 3)
        c.line(35 + x0 // 4, y, 35 + x1 // 5, y, GLOW_KIND[kind])
    for row, x0, x1, kind in code_rows(22)[:6]:
        y = 26 + row
        c.line(35 + x0 // 4, y, 35 + x1 // 6, y, GLOW_KIND[kind])


def ring(c, cx, cy, r, colour):
    """ a circle line of radius r """
    for y in range(cy - r - 1, cy + r + 2):
        for x in range(cx - r - 1, cx + r + 2):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if r - 0.6 < d <= r + 0.4:
                c.put(x, y, colour)


def mix_light(colour):
    return ai.mix(colour, WHITE, 0.6)


def m_scout(s, c):
    sheet = [(4, 2), (23, 2), (30, 9), (30, 35), (4, 35)]
    c.poly(sheet, s.ramp(*PAPER)(4, 2, 30, 35), s.line(GREY[1]))
    c.poly([(23, 2), (23, 9), (30, 9)], GREY[0], s.line(GREY[1]))
    for row, x0, x1, kind in code_rows(18):
        c.line(8 + x0, 8 + 3 * row, 8 + x1, 8 + 3 * row, GLOW_KIND[kind])
    # the magnifying glass over the middle, a keyword enlarged in it
    cx, cy, r = 24, 20, 9
    glass = ai.mix(WHITE, BLUE[0], 0.25)
    c.disc(cx, cy, r, s.ramp(WHITE, glass)(cx - r, cy - r, cx + r, cy + r), s.line(GREY[1]))
    for y in (cy - 1, cy, cy + 1):
        c.line(cx - 6, y, cx - 1, y, KEYWORD)
    for y in (cy - 1, cy, cy + 1):
        c.line(cx + 1, y, cx + 5, y, TEXT)
    c.line(cx - 5, cy - 6, cx - 2, cy - 7, WHITE)     # glint
    # the ring and the handle
    ring(c, cx, cy, r, s.line(GREY[1]))
    ring(c, cx, cy, r - 1, GREY[1])
    handle = [(30, 27), (33, 25), (41, 33), (38, 36)]
    c.poly(handle, s.ramp((150, 96, 60), (80, 46, 24))(30, 25, 41, 36), (60, 34, 16))


def glow_parser(letter, colour):
    def m(s, c):
        for y in range(9, 31, 4):                     # pins
            c.rect(3, y, 8, y + 1, s.ramp(*GREY)(3, y, 8, y + 1))
            c.rect(33, y, 38, y + 1, s.ramp(*GREY)(33, y, 38, y + 1))
        c.rect(8, 4, 33, 35, s.line(INK[1]))
        c.rect(9, 5, 32, 34, s.ramp(*INK)(9, 5, 32, 34))
        for y in range(5, 9):                         # notch, half a circle
            for x in range(17, 25):
                if (x - 20.5) ** 2 + (y - 5) ** 2 <= 10:
                    c.put(x, y, INK[1])
        c.disc(13, 9, 1, ai.mix(INK[0], WHITE, 0.3))  # pin 1 mark
        c.rect(13, 13, 28, 28, s.line(colour[1]))     # language label
        c.rect(14, 14, 27, 27, s.ramp(*colour)(14, 14, 27, 27))
        glyph(c, 18, 17, letter, WHITE)
        glyph(c, 19, 17, letter, WHITE)               # bold
    return m


# ------------------------------------------------------- NewIcons motifs ---

N_CODE = {'p': ai.N_RED, 'k': ai.N_YELLOW, 't': WHITE, 's': ai.N_GREEN, 'c': ai.N_G170}


def n_tinydev(c, sel):
    """ the monitor with line numbers and coloured, indented code;
        selected: scrolled on by one line, the cursor line highlighted     """
    def screen(c, box, u0, u1, t0, t1, sel):
        rows = code_rows(u1 - u0 - 4)
        shift = 1 if sel else 0
        for row, x0, x1, kind in rows:
            r = row - shift
            if r < 0:
                continue
            t = t0 + 1 + 2 * r
            if t > t1:
                continue
            box.face_line(c, t, u0, u0 + 1, ai.N_G170)            # line number
            box.face_line(c, t, u0 + 3 + x0 // 2 * 2, u0 + 3 + x1 // 2 * 2 + 1, N_CODE[kind])
        if sel:
            box.face_line(c, t0 + 1 + 2 * 3, u0 + 3, u1, ai.N_LBLUE)
    ai.n_monitor(c, screen, sel)


def n_scout(c, sel):
    """ a sheet of code with a magnifying glass on it;
        selected: the glass has moved on, further down the page            """
    s = ai.n_sheet(c, False)
    d = 4 if sel else 0
    cx, cy = 22 + d // 2, 21 + d
    c.disc(cx, cy, 7, ai.checker(WHITE, ai.N_LBLUE), BLACK)
    ring(c, cx, cy, 6, ai.N_G136)
    c.put(cx - 3, cy - 3, WHITE)
    c.put(cx - 2, cy - 4, WHITE)
    for i in range(6):                                # handle
        for w in range(3):
            c.put(cx + 5 + i + w, cy + 5 + i, BLACK if w in (0, 2) else ai.N_TAN)
    c.put(cx + 11, cy + 11, BLACK)
    c.put(cx + 12, cy + 11, BLACK)
    for y in (cy - 1, cy):
        for x in range(cx - 4, cx):
            c.put(x, y, ai.N_YELLOW)


def ni_parser(letter, colour):
    def n(c, sel):
        """ a chip on its platform, pins along its long side, the language
            label in front; selected: the chip is pressed into the platform
            (its pins get shorter) and its LED lights up                    """
        d = 2 if sel else 0                           # how far it is pressed in
        box = ai.Box(6, 17 + d, 9, 5, 3)
        # the size of the original block; no shadow: the chip stands on its pins
        ai.platform(c, 4, 29, 11, 5, box, shadow=0)
        box.draw(c, ai.N_G51, ai.N_G102, ai.N_G136, edge=ai.N_G102)
        # Pins only along the long side, like a DIP chip (the short end at
        # the left has none): 4 pins, 2 wide and 5 long (d shorter when
        # pressed in: the feet stay on the platform), evenly spread.
        first = 2 * box.b + 2                         # first column of the long side + 1
        # The pins of the hidden long side lie 2b columns to the left and b
        # rows higher; only their ends below the body show, darker.
        free = (None, ai.N_GREEN, ai.N_G51)
        for u in range(first, first + 4 * 5, 5):
            # outline left, 2 metal columns (in the shade), outline right
            for du, metal in ((-1, BLACK), (0, ai.N_G136), (1, ai.N_G102), (2, BLACK)):
                x = box.x0 + u + du - 2 * box.b
                y0 = box.bottom(u + max(0, min(du, 1))) - box.b
                below = box.bottom(x - box.x0)        # lower outline of the body here
                foot = y0 - d + 6
                for y in range(y0 + 1, foot + 1):
                    if y > below and c.p[y][x] in free:
                        c.put(x, y, BLACK if y == foot else metal)
        for u in range(first, first + 4 * 5, 5):
            for du, metal in ((0, ai.N_G204), (1, ai.N_G136)):
                x, y = box.x0 + u + du, box.bottom(u + du)
                foot = y - d + 6
                for yy in range(y + 1, foot):
                    c.put(x, yy, metal)
                c.put(x, foot, BLACK)
            c.put(box.x0 + u - 1, box.bottom(u - 1) + 1, BLACK)   # outline left
            c.put(box.x0 + u + 2, box.bottom(u + 2) + 1, BLACK)   # and right
        led = (box.x0 + 8, box.ytop(8) + 3)
        c.put(led[0], led[1], ai.N_YELLOW if sel else ai.N_G102)
        c.put(led[0] + 1, led[1], ai.N_YELLOW if sel else ai.N_G102)
        # the label: a straight badge like the file format labels, at the
        # lower right below the platform, where it hides no pin
        x, y = 28, 30
        c.rect(x, y, x + 10, y + 10, BLACK)
        c.rect(x + 1, y + 1, x + 9, y + 9, colour)
        glyph(c, x + 3, y + 2, letter, WHITE)
    return n


def parser(letter, colour=BLUE, ni_colour=ai.N_BLUE):
    """ registers the motif of a Scout parser, role 'parser_<letter>' """
    role = 'parser_' + letter.lower()
    ai.register(role, classic_parser(letter), glow_parser(letter, colour), ni_parser(letter, ni_colour))
    return role


ai.register('tinydev', pic_tinydev, m_tinydev, n_tinydev)
ai.register('scout', pic_scout, m_scout, n_scout)
PARSER_C = parser('C')


# ---------------------------------------------------------------- samples ---

ROLES = ('drawer', 'tinydev', 'scout', PARSER_C, 'readme', 'license')

SAMPLES = (('Drawer', 'drawer', WBDRAWER, {}),
           ('TinyDev', 'tinydev', WBTOOL, {'stack': 32768}),
           ('TinyDev-Scout', 'scout', WBTOOL, {'stack': 16384}),
           ('c_parser', PARSER_C, WBTOOL, {'stack': 16384}),
           ('ReadMe', 'readme', WBPROJECT, {'default_tool': 'SYS:Utilities/MultiView'}),
           ('License', 'license', WBPROJECT, {'default_tool': 'SYS:Utilities/MultiView'}))


if __name__ == '__main__':
    ai.write_samples(os.path.join(ROOT, 'icons'), SAMPLES, ROLES)
