#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native hook fixtures: no firmware bytes or hard-coded host address mappings."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--cc',default=os.environ.get('CC','gcc'))
p.add_argument('--passes',nargs='+',default=['plain','asan'])
args=p.parse_args()
root=Path(__file__).resolve().parents[1]
for name in args.passes:
    flags={'plain':[], 'asan':['-fsanitize=address,undefined'], 'tsan':['-fsanitize=thread']}[name]
    with tempfile.TemporaryDirectory() as directory:
        binary=Path(directory)/'native-test'
        subprocess.run([args.cc,'-std=gnu11','-O1','-Wall','-Wextra','-Werror',
                        '-fno-omit-frame-pointer',*flags,'-I'+str(root),
                        str(root/'tests/native.c'),'-pthread','-o',str(binary)],check=True)
        env=dict(os.environ)
        if name=='asan':env['ASAN_OPTIONS']='detect_leaks=0'
        print('== '+name,flush=True)
        subprocess.run([str(binary)],check=True,env=env)
