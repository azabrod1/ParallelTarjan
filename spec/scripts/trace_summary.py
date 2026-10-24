#!/usr/bin/env python3
"""Condense a TLC counterexample: print each step's action and the variables it changed."""
import re, sys

def parse(path):
    txt = open(path).read()
    parts = re.split(r'\n(?=State \d+:|\d+: Back to state)', txt)
    out = []
    for p in parts[1:]:
        head = p.split('\n')[0]
        d = {}
        cur = None
        for line in p.split('\n')[1:]:
            m = re.match(r'^/\\ (\w+) = (.*)$', line)
            if m:
                cur = m.group(1); d[cur] = m.group(2)
            elif cur and line.startswith('   '):
                d[cur] += ' ' + line.strip()
            elif not line.strip():
                break
        out.append((head, d))
    return out

def main():
    path = sys.argv[1]
    keys = sys.argv[2].split(',') if len(sys.argv) > 2 else None
    prev = {}
    for head, d in parse(path):
        m = re.match(r'(State \d+|\d+: Back to state[^<]*): <(\w+)', head)
        label = (m.group(1) + ' ' + m.group(2)) if m else head[:70]
        if 'Back to state' in head:
            print(head.strip()); continue
        changed = {k: v for k, v in d.items() if prev.get(k) != v and (keys is None or k in keys)}
        if not prev:
            changed = {k: v for k, v in d.items() if keys is None or k in keys}
        print(label)
        for k, v in changed.items():
            print('    %s = %s' % (k, v))
        prev = d

if __name__ == '__main__':
    main()
