#!/usr/bin/env python3
"""Build the C templates that come with TinyDev ("make check-templates").

Every template offering a compiler that is installed here is made with the
portable core of Sprout (build/tmpl_generate) and built as a user would
build it: a project with its own Makefile, a file template (CModule) by
compiling its .c files. It shows whether a template compiles and links, not
whether the result works on the Amiga.

A compiler that is not installed is skipped with a note, as vbcc and the
NDK are not needed for anything else. gcc is the cross compiler of this
host, not the 2.95.3 most Amiga users have; SAS/C cannot be run here.
CatComp only runs on the Amiga: the header is written here in its layout,
the catalogs with amiga-devkit/tools/catcomp.py.

Errors fail the check. Warnings are shown, except the known harmless ones
(known_warning()).
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

CATCOMP = 'amiga-devkit/tools/catcomp.py'


def known_warning(line, previous):
    """Tell whether a warning of vbcc is one of the known harmless ones."""
    if line.startswith('warning 53 ') and 'devices/timer.h' in line:
        return True     # a header of the NDK 3.2 itself
    return line.startswith('warning 153 ') and '(void)' in previous


def own_warnings(output):
    """Return the warning lines of a build that are not known ones."""
    lines = output.splitlines()
    found = []
    for i, line in enumerate(lines):
        if 'warning' not in line:
            continue
        if not known_warning(line, lines[i - 1] if i > 0 else ''):
            found.append(line)
    return found


def write_catcomp_header(cd, header):
    """Write the header CatComp would make of a .cd, as far as it is used."""
    lines = open(cd, encoding='latin-1').read().split('\n')
    texts = []
    i = 0
    while i < len(lines):
        m = re.match(r'(\w+)\s*\(.*\)\s*$', lines[i])
        i += 1
        if m is None:
            continue
        text = lines[i]
        i += 1
        while text.endswith('\\') and i < len(lines):
            text = text[:-1] + lines[i]
            i += 1
        texts.append((m[1], text))
    out = ['#ifdef CATCOMP_ARRAY', '#undef CATCOMP_NUMBERS', '#define CATCOMP_NUMBERS',
           '#undef CATCOMP_STRINGS', '#define CATCOMP_STRINGS', '#endif',
           '#ifdef CATCOMP_NUMBERS']
    out += ['#define %s %d' % (name, n) for n, (name, _) in enumerate(texts)]
    out += ['#endif', '#ifdef CATCOMP_STRINGS']
    out += ['#define %s_STR "%s"' % (name, text) for name, text in texts]
    out += ['#endif', '#ifdef CATCOMP_ARRAY',
            'struct CatCompArrayType { LONG cca_ID; STRPTR cca_Str; };',
            'static const struct CatCompArrayType CatCompArray[] = {']
    out += ['    {%s, (STRPTR)%s_STR},' % (name, name) for name, _ in texts]
    out += ['};', '#endif']
    with open(header, 'w', encoding='latin-1') as f:
        f.write('\n'.join(out) + '\n')


def prepare_catalogs(project):
    """Stand in for the CatComp steps of a Makefile; return an error or None."""
    drawer = os.path.join(project, 'Catalogs')
    if not os.path.isdir(drawer):
        return None
    for cd in sorted(f for f in os.listdir(drawer) if f.endswith('.cd')):
        stem = cd[:-3]
        path = os.path.join(drawer, cd)
        write_catcomp_header(path, os.path.join(project, stem + '_strings.h'))
        for language in sorted(os.listdir(drawer)):
            ct = os.path.join(drawer, language, stem + '.ct')
            if not os.path.isfile(ct):
                continue
            r = subprocess.run([sys.executable, CATCOMP, 'catalog', path, ct,
                                os.path.join(drawer, language, stem + '.catalog')],
                               capture_output=True, text=True, errors='replace')
            if r.returncode != 0:
                return r.stdout + r.stderr
    return None


def unreplaced(project):
    """Return text about placeholders left in what gets built, or None."""
    for folder, _, names in os.walk(project):
        for name in names:
            if name == 'Makefile' or name.endswith(('.c', '.h')):
                path = os.path.join(folder, name)
                left = re.findall(r'\{\$\w+[^}]*\}', open(path, encoding='latin-1').read())
                if left:
                    return '%s: unknown placeholder %s' % (path, left[0])
    return None


def values_for(meta, compiler):
    """Return the KEY=value arguments for a template."""
    args = ['AUTHOR=Test Author']
    for p in meta.get('placeholders', []):
        if p['key'] == 'CC':
            args.append('CC=' + compiler)
        elif p['key'] == 'NAME' and not p.get('default'):
            args.append('NAME=Foo')
        elif p.get('required') and not p.get('default') and p['key'] != 'AUTHOR':
            args.append(p['key'] + '=Foo')
    return args


def build(project, meta, tool):
    """Build a generated project; return (ok, output)."""
    if meta.get('kind') != 'file':
        r = subprocess.run(['make', 'CC=' + tool['cc']], cwd=project, env=tool['env'],
                           capture_output=True, text=True, errors='replace')
        return r.returncode == 0, r.stdout + r.stderr
    output, ok = '', True
    for name in sorted(os.listdir(project)):
        if name.endswith('.c'):
            r = subprocess.run(tool['cc'].split() + tool['flags'].split() +
                               ['-c', '-o', name[:-2] + '.o', name], cwd=project, env=tool['env'],
                               capture_output=True, text=True, errors='replace')
            output += r.stdout + r.stderr
            ok = ok and r.returncode == 0
    return ok, output


def clean_environment():
    """Return the environment without what the calling make passes on."""
    env = dict(os.environ)
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(name, None)
    return env


def find_compilers(a):
    """Return the installed compilers by the names used in the templates."""
    tools = {}
    vc = os.path.join(a.vbcc, 'bin', 'vc')
    if os.path.isfile(vc) and os.path.isdir(a.ndk):
        env = dict(clean_environment(), VBCC=a.vbcc)
        env['PATH'] = os.path.join(a.vbcc, 'bin') + os.pathsep + env['PATH']
        tools['vbcc'] = {'cc': '%s -I%s/targets/m68k-amigaos/include -I%s' % (vc, a.vbcc, a.ndk),
                         'flags': '-O1', 'env': env}
    else:
        print('note: vbcc skipped (%s or %s not found)' % (vc, a.ndk))
    if os.path.isfile(a.gcc):
        tools['gcc'] = {'cc': a.gcc, 'flags': '-O2 -noixemul', 'env': clean_environment()}
    else:
        print('note: gcc skipped (%s not found)' % a.gcc)
    return tools


def check(a, template, meta, compiler, tool):
    """Generate and build a template with a compiler; return the result text
    or None if there is nothing to build; print details of a failure."""
    project = os.path.join(a.work, '%s-%s' % (template, compiler))
    r = subprocess.run([a.generator, os.path.join(a.templates, template), project] +
                       values_for(meta, compiler), capture_output=True, text=True, errors='replace')
    problem = r.stdout + r.stderr if r.returncode != 0 else unreplaced(project) or prepare_catalogs(project)
    if problem:
        print('--- %s with %s\n%s' % (template, compiler, problem))
        return 'FAILED'
    names = os.listdir(project)
    buildable = any(n.endswith('.c') for n in names) if meta.get('kind') == 'file' else 'Makefile' in names
    if not buildable:
        return None
    ok, output = build(project, meta, tool)
    if not ok:
        print('--- %s with %s\n%s' % (template, compiler, output))
        return 'FAILED'
    warnings = own_warnings(output)
    if warnings:
        print('--- %s with %s\n%s' % (template, compiler, '\n'.join(warnings)))
        return 'ok, %d warning(s)' % len(warnings)
    return 'ok'


def main():
    """Run the check; the exit code is non-zero if a template failed."""
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--generator', required=True, help='the program tmpl_generate')
    p.add_argument('--templates', default='templates', help='drawer of the templates')
    p.add_argument('--work', required=True, help='drawer for the builds (emptied)')
    p.add_argument('--vbcc', required=True, help='where vbcc is installed')
    p.add_argument('--ndk', required=True, help='include drawer of the NDK')
    p.add_argument('--gcc', required=True, help='the gcc cross compiler')
    a = p.parse_args()

    tools = find_compilers(a)
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(a.work)
    failed = built = 0
    for template in sorted(os.listdir(a.templates)):
        path = os.path.join(a.templates, template, 'template.json')
        if not os.path.isfile(path):
            continue
        meta = json.load(open(path, encoding='latin-1'))
        choices = next((x.get('choices', []) for x in meta.get('placeholders', []) if x['key'] == 'CC'), None)
        if choices is None and meta.get('kind') != 'file':
            continue    # a project in another language
        for compiler, tool in tools.items():
            if choices is not None and compiler not in choices:
                continue
            result = check(a, template, meta, compiler, tool)
            if result is None:
                continue
            print('%-16s %-6s %s' % (template, compiler, result))
            built += 1
            failed += result == 'FAILED'
    print('%d built, %d failed' % (built, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
