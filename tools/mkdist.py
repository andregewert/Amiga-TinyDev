#!/usr/bin/env python3
# mkdist.py - builds the Aminet archive TinyDev.lha from the built tree
#
# Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
#
# Creates dist/TinyDev/ with the installer script, the programs, classes,
# catalogs and help pages from bin/ (laid out as on the Amiga, so it runs from the archive),
# the templates packed as LhA archives (Templates/<Id>.lha, for Sprout),
# the documentation (README.md converted to ISO-8859-1), the sources and
# icons (classic icons next to the files, complete GlowIcons and NewIcons
# sets in Icons/, see mkicons.py), then packs it with lha. The common parts
# come from amiga-devkit/tools/distlib.py. Run "make dist".

import os, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import ROOT, PARSER_C, PARSER_DOS, PARSER_MAKE, PARSER_MD, PARSER_JSON, PARSER_MARKUP, PARSER_AREXX, PARSER_ASM, PARSER_E, PARSER_ACE, PARSER_PASCAL, PARSER_GUIDE, WBDRAWER, WBTOOL, WBPROJECT  # noqa: E402  (registers the motifs)
from distlib import copy, copytree, copy_devkit, latin1_copy, package_icons, pack  # noqa: E402

NAME = 'TinyDev'
DRAWER_ICON = NAME + '-Drawer'      # Icons/<Style>/: icon of the package drawer
DIST = os.path.join(ROOT, 'dist')
PKG = os.path.join(DIST, NAME)
MULTIVIEW = 'SYS:Utilities/MultiView'
MDTOHTML_SOURCES = ('mdtohtml.c', 'mdtree.c', 'mdtree.h', 'mdtree_dos.c', 'mdconv.c', 'mdconv.h',
                    'entity_stub.c', 'fileio.c', 'fileio.h')


def pack_templates(dest):
    """ templates/<Id>/ (drawers in the repository and in bin/) as archives
        dest/<Id>.lha, as Sprout reads them: template.json and files/ at
        the top                                                          """
    tool = shutil.which('jlha') or 'lha'
    top = os.path.join(ROOT, 'templates')
    os.makedirs(dest, exist_ok=True)
    for tid in sorted(os.listdir(top)):
        tdir = os.path.join(top, tid)
        if not os.path.isfile(os.path.join(tdir, 'template.json')):
            continue
        parts = ['template.json'] + [p for p in ('template.info', 'files') if os.path.exists(os.path.join(tdir, p))]
        subprocess.run([tool, 'ao5q', os.path.join(dest, tid + '.lha')] + parts, cwd=tdir, check=True)


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
    copy(b('package', 'Install'), os.path.join(PKG, 'Install'))
    copy(b('LICENSE'), os.path.join(PKG, 'LICENSE'))
    # bin/ is laid out as on the Amiga: programs, classes, catalogs, help
    for f in ('TinyDev', 'TinyDev-Scout', 'HelpViewer', 'Sprout', 'mdtohtml', os.path.join('parsers', 'c_parser'),
              os.path.join('parsers', 'dos_parser'), os.path.join('parsers', 'make_parser'),
              os.path.join('parsers', 'markdown_parser'), os.path.join('parsers', 'json_parser'),
              os.path.join('parsers', 'markup_parser'), os.path.join('parsers', 'arexx_parser'),
              os.path.join('parsers', 'asm_parser'),
              os.path.join('parsers', 'e_parser'), os.path.join('parsers', 'ace_parser'), os.path.join('parsers', 'pascal_parser'),
              os.path.join('parsers', 'guide_parser')):
        copy(b('bin', f), os.path.join(PKG, f))
    for d in ('Classes', 'Catalogs', 'Help'):
        copytree(b('bin', d), os.path.join(PKG, d))
    pack_templates(os.path.join(PKG, 'Templates'))

    docs = os.path.join(PKG, 'Docs')
    latin1_copy(b('README.md'), os.path.join(docs, 'README.md'))
    # what the README shows or links to, at the same relative paths
    copy(b('screenshot.png'), os.path.join(docs, 'screenshot.png'))
    copy(b('logo.png'), os.path.join(docs, 'logo.png'))
    copy(b('icons', 'preview.png'), os.path.join(docs, 'icons', 'preview.png'))
    copy(b('LICENSE'), os.path.join(docs, 'LICENSE'))

    # sources in the same layout as the repository, so "make" works there
    src = os.path.join(PKG, 'Source')
    for d in ('src', 'include', 'sfd', 'catalogs', 'tests', 'scripts', 'tools', 'package', 'templates', 'helpsrc'):
        copytree(b(d), os.path.join(src, d), ignore=('__pycache__',))
    for f in ('Makefile', 'README.md', 'LICENSE', 'AGENTS.md', 'CLAUDE.md', 'TECHNICAL.md',
              'screenshot.png', 'logo.png'):
        copy(b(f), os.path.join(src, f))
    copy(b('icons', 'preview.png'), os.path.join(src, 'icons', 'preview.png'))
    copytree(b('external', 'regex'), os.path.join(src, 'external', 'regex'), ignore=('.git',))
    copytree(b('external', 'html_gadget', 'include'), os.path.join(src, 'external', 'html_gadget', 'include'))
    copy(b('external', 'html_gadget', 'LICENSE'), os.path.join(src, 'external', 'html_gadget', 'LICENSE'))
    copytree(b('external', 'openurl', 'developer', 'C', 'include'),
             os.path.join(src, 'external', 'openurl', 'developer', 'C', 'include'))
    copy(b('external', 'openurl', 'COPYING'), os.path.join(src, 'external', 'openurl', 'COPYING'))
    # mdtohtml: only what the Makefile compiles of the Kickdown project
    mdtools = os.path.join(src, 'external', 'mdtools')
    for f in MDTOHTML_SOURCES:
        copy(b('external', 'mdtools', 'src', f), os.path.join(mdtools, 'src', f))
    copy(b('external', 'mdtools', 'LICENSE'), os.path.join(mdtools, 'LICENSE'))
    for f in ('md4c.c', 'md4c.h', 'md4c-html.c', 'md4c-html.h', 'entity.h'):
        copy(b('external', 'mdtools', 'md4c', 'src', f), os.path.join(mdtools, 'md4c', 'src', f))
    copy(b('external', 'mdtools', 'md4c', 'LICENSE.md'), os.path.join(mdtools, 'md4c', 'LICENSE.md'))
    copy_devkit(os.path.join(src, 'amiga-devkit'))

    # icons: classic ones next to the files, every style also in Icons/<Style>/
    icons = [('Install', 'install', WBPROJECT,
              dict(default_tool='Installer', tooltypes=('APPNAME=' + NAME, 'MINUSER=AVERAGE'))),
             ('TinyDev', 'tinydev', WBTOOL,
              dict(stack=32768, tooltypes=('(TOOLBAR=BOTH)', '(TOOLBARFRAMES)', '(NOSPLASH)'))),
             ('TinyDev-Scout', 'scout', WBTOOL, dict(stack=16384)),
             ('parsers/c_parser', PARSER_C, WBTOOL, dict(stack=16384)),
             ('parsers/dos_parser', PARSER_DOS, WBTOOL, dict(stack=16384)),
             ('parsers/make_parser', PARSER_MAKE, WBTOOL, dict(stack=16384)),
             ('parsers/markdown_parser', PARSER_MD, WBTOOL, dict(stack=16384)),
             ('parsers/json_parser', PARSER_JSON, WBTOOL, dict(stack=16384)),
             ('parsers/markup_parser', PARSER_MARKUP, WBTOOL, dict(stack=16384)),
             ('parsers/arexx_parser', PARSER_AREXX, WBTOOL, dict(stack=16384)),
             ('parsers/asm_parser', PARSER_ASM, WBTOOL, dict(stack=16384)),
             ('parsers/e_parser', PARSER_E, WBTOOL, dict(stack=16384)),
             ('parsers/ace_parser', PARSER_ACE, WBTOOL, dict(stack=16384)),
             ('parsers/pascal_parser', PARSER_PASCAL, WBTOOL, dict(stack=16384)),
             ('parsers/guide_parser', PARSER_GUIDE, WBTOOL, dict(stack=16384)),
             ('HelpViewer', 'helpviewer', WBTOOL,
              dict(stack=32768, tooltypes=('(TTF)', '(FONTSET=Vera)', '(SIZE=10)'))),
             (NAME + '.readme', 'readme', WBPROJECT, dict(default_tool=MULTIVIEW)),
             ('LICENSE', 'license', WBPROJECT, dict(default_tool=MULTIVIEW)),
             ('Docs/README.md', 'readme', WBPROJECT, dict(default_tool=MULTIVIEW))]
    shown = ['Docs', 'Icons', 'Source', 'parsers', 'Classes', 'Catalogs', 'Help', 'Templates']
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
