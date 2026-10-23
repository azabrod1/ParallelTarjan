#!/usr/bin/env python3
"""Assemble README.md from docs/*.md parts and the generated results table."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
parts = [open(os.path.join(HERE, 'docs', p)).read() for p in
         ('00_intro.md', '10_results.md', '20_models_termination.md', '30_models_susp_cycle.md',
          '40_counterexamples.md', '50_limits.md')]
table = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'scripts', 'results_table.py')],
                       capture_output=True, text=True, check=True).stdout
parts[1] = parts[1].replace('RESULTS_TABLE', table.rstrip('\n'))
open(os.path.join(HERE, 'README.md'), 'w').write('\n'.join(p.rstrip('\n') + '\n' for p in parts))
