#!/usr/bin/env python3
"""Print a Markdown results table: one row per run.sh configuration, with its constants and
the verdict/state counts parsed from results/<config>.out (and results/<config>.time)."""
import os, re, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def runs():
    txt = open(os.path.join(HERE, 'run.sh')).read()
    block = txt[txt.index('RUNS=('):txt.index('\n)\n')]
    for line in block.splitlines():
        m = re.match(r'^\s+"([^"]*)"\s*$', line)
        if m:
            f = m.group(1).split()
            yield f[0], f[1], f[2], len(f) > 3 and f[3] == 'long'

SKIP = {'NULL', 'NoRecheck', 'BlindResume', 'CheckThenStore', 'RelaxedEntry', 'NoConsensus',
        'NoSecondPass', 'WalkBeforePublish', 'IncBeforeEnqueue', 'IncBeforeTake', 'CountBlockFirst'}

def constants(name):
    txt = open(os.path.join(HERE, name + '.cfg')).read()
    out = []
    for m in re.finditer(r'^\s+(\w+) = (.+)$', txt, re.M):
        k, v = m.group(1), m.group(2).strip()
        if k in SKIP and v == 'FALSE':
            continue
        if k in SKIP and v == 'TRUE' and k not in ('IncBeforeEnqueue', 'IncBeforeTake', 'CountBlockFirst'):
            out.append('**%s**' % k); continue
        if k in ('IncBeforeEnqueue', 'IncBeforeTake', 'CountBlockFirst'):
            if v == 'FALSE':
                out.append('**%s=FALSE**' % k)
            continue
        if k == 'Workers' or k == 'Searches' or k == 'Cells':
            v = str(v.count(',') + 1)
        out.append('%s=%s' % (k, v))
    props = []
    m = re.search(r'^INVARIANTS (.*)$', txt, re.M)
    inv = m.group(1).split() if m else []
    m = re.search(r'^PROPERTIES (.*)$', txt, re.M)
    if m:
        props = m.group(1).split()
    sym = 'SYMMETRY' in txt
    return ', '.join(out), inv, props, sym

def result(name):
    p = os.path.join(HERE, 'results', name + '.out')
    if not os.path.exists(p):
        return None
    t = open(p).read()
    if 'No error has been found' in t:
        v = 'PASS'
    else:
        m = re.search(r'^Error: (Invariant (\w+) is violated|Temporal properties were violated)', t, re.M)
        v = ('FAIL ' + (m.group(2) if m.group(2) else 'liveness')) if m else 'INCOMPLETE'
    m = re.findall(r'^(\d+) states generated, (\d+) distinct states found', t, re.M)
    gen, dist = (m[-1] if m else ('-', '-'))
    m = re.search(r'depth of the complete state graph search is (\d+)', t)
    depth = m.group(1) if m else '-'
    m = re.search(r'with (\d+) workers', t)
    w = m.group(1) if m else '?'
    tp = os.path.join(HERE, 'results', name + '.time')
    secs = open(tp).read().strip() if os.path.exists(tp) else '?'
    return v, int(gen) if gen != '-' else '-', int(dist) if dist != '-' else '-', depth, secs, w

def fmt(n):
    return '{:,}'.format(n) if isinstance(n, int) else n

print('| Config | Constants | Checked | Expected | Result | Distinct states | Depth | Time (s) | TLC workers |')
print('| --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: |')
for name, mod, expect, long in runs():
    consts, inv, props, sym = constants(name)
    checked = ' '.join(inv + props) + (' (symmetry)' if sym else '')
    r = result(name)
    if r is None:
        print('| `%s`%s | %s | %s | %s | not run | | | | |' % (name, ' (long)' if long else '', consts, checked, expect))
        continue
    v, gen, dist, depth, secs, w = r
    ok = (expect == 'pass' and v == 'PASS') or (expect.startswith('fail') and v.startswith('FAIL'))
    print('| `%s`%s | %s | %s | %s | %s%s | %s | %s | %s | %s |' % (
        name, ' (long)' if long else '', consts, checked, expect, v, '' if ok else ' **UNEXPECTED**',
        fmt(dist), depth, secs, w))
