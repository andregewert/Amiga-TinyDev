#!/usr/bin/env python3
# mkdist.py - builds the Aminet archive TinyDev.lha from the built tree
#
# Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
#
# Creates dist/TinyDev/ with the programs, classes, catalogs and help
# pages from bin/ (laid out as on the Amiga, so it runs from the archive),
# the documentation (README.md converted to ISO-8859-1), the sources and
# icons (classic icons next to the files, complete GlowIcons and NewIcons
# sets in Icons/, see mkicons.py), then packs it with lha. The common parts
# come from amiga-devkit/tools/distlib.py. Run "make dist".

import os, shutil, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import ROOT, PARSER_C, WBDRAWER, WBTOOL, WBPROJECT  # noqa: E402  (registers the motifs)
from distlib import copy, copytree, copy_devkit, latin1_copy, package_icons, pack  # noqa: E402

NAME = 'TinyDev'
DRAWER_ICON = NAME + '-Drawer'      # Icons/<Style>/: icon of the package drawer
DIST = os.path.join(ROOT, 'dist')
PKG = os.path.join(DIST, NAME)
MULTIVIEW = 'SYS:Utilities/MultiView'


def drawers(top):
    """ the drawers below top, relative to PKG, parents first """
    out = []
    for dirpath, dirnames, _ in os.walk(top):
        dirnames.sort()
        out.extend(os.path.relpath(os.path.join(dirpath, d), PKG) for d in dirnames)
    return out


def main():
    b = lambda *p: os.path.join(ROOT, *p)
    shutil.rmtree(DIST, ignore_errors=True)
    os.makedirs(PKG)

    copy(b('package', NAME + '.readme'), os.path.join(PKG, NAME + '.readme'))
    copy(b('LICENSE'), os.path.join(PKG, 'LICENSE'))
    # bin/ is laid out as on the Amiga: programs, classes, catalogs, help
    for f in ('TinyDev', 'TinyDev-Scout', 'HelpViewer', os.path.join('parsers', 'c_parser')):
        copy(b('bin', f), os.path.join(PKG, f))
    for d in ('Classes', 'Catalogs', 'Help'):
        copytree(b('bin', d), os.path.join(PKG, d))

    docs = os.path.join(PKG, 'Docs')
    latin1_copy(b('README.md'), os.path.join(docs, 'README.md'))
    # what the README shows or links to, at the same relative paths
    copy(b('screenshot.png'), os.path.join(docs, 'screenshot.png'))
    copy(b('icons', 'preview.png'), os.path.join(docs, 'icons', 'preview.png'))
    copy(b('LICENSE'), os.path.join(docs, 'LICENSE'))

    # sources in the same layout as the repository, so "make" works there
    src = os.path.join(PKG, 'Source')
    for d in ('src', 'include', 'sfd', 'catalogs', 'tests', 'tests-host', 'scripts', 'tools', 'package'):
        copytree(b(d), os.path.join(src, d), ignore=('__pycache__',))
    for f in ('Makefile', 'README.md', 'LICENSE', 'AGENTS.md', 'CLAUDE.md', 'TECHNICAL.md', 'FUTURE.md',
              'screenshot.png'):
        copy(b(f), os.path.join(src, f))
    copy(b('icons', 'preview.png'), os.path.join(src, 'icons', 'preview.png'))
    copytree(b('external', 'regex'), os.path.join(src, 'external', 'regex'), ignore=('.git',))
    copytree(b('external', 'html_gadget', 'include'), os.path.join(src, 'external', 'html_gadget', 'include'))
    copy(b('external', 'html_gadget', 'LICENSE'), os.path.join(src, 'external', 'html_gadget', 'LICENSE'))
    copy_devkit(os.path.join(src, 'amiga-devkit'))

    # icons: classic ones next to the files, every style also in Icons/<Style>/
    icons = [('TinyDev', 'tinydev', WBTOOL,
              dict(stack=32768, tooltypes=('(TOOLBAR=BOTH)', '(TOOLBARFRAMES)', '(NOSPLASH)'))),
             ('TinyDev-Scout', 'scout', WBTOOL, dict(stack=16384)),
             ('parsers/c_parser', PARSER_C, WBTOOL, dict(stack=16384)),
             ('HelpViewer', 'helpviewer', WBTOOL,
              dict(stack=32768, tooltypes=('(TTF)', '(FONTSET=Vera)', '(SIZE=12)'))),
             (NAME + '.readme', 'readme', WBPROJECT, dict(default_tool=MULTIVIEW)),
             ('LICENSE', 'license', WBPROJECT, dict(default_tool=MULTIVIEW)),
             ('Docs/README.md', 'readme', WBPROJECT, dict(default_tool=MULTIVIEW))]
    shown = ['Docs', 'Icons', 'Source', 'parsers', 'Classes', 'Catalogs', 'Help']
    for top in ('Classes', 'Catalogs', 'Help'):
        shown += drawers(os.path.join(PKG, top))
    for d in shown:
        icons.append((d, 'drawer', WBDRAWER, {}))
    # The icon of the package drawer has a name of its own in Icons/<Style>/:
    # the drawer is called like the program, whose icon is TinyDev.info.
    package_icons(PKG, NAME, icons, DRAWER_ICON)

    pack(DIST, NAME)


if __name__ == '__main__':
    main()
