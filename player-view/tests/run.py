#!/usr/bin/env python3
import argparse,subprocess,tempfile,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',default='cc');p.add_argument('--passes',nargs='+',default=['plain','asan']);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 for mode in a.passes:
  flags=[] if mode=='plain' else ['-fsanitize='+('thread' if mode=='tsan' else 'address,undefined'),'-fno-omit-frame-pointer']
  exe=Path(t)/mode
  subprocess.run([a.cc,'-O1','-g','-Wall','-Wextra','-Wno-address','-Werror=implicit-function-declaration',*flags,str(root/'tests/native.c'),str(root/'render.c'),'-lm','-lpthread','-o',str(exe)],check=True)
  env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0'
  subprocess.run([str(exe)],check=True,env=env)
