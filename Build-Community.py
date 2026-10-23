#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/package an SDK community mod; never replace the official core."""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parent
SDK_REF='3dcaf8f7331ea94135d7161f653edfaf906d5ea6'
SDK_URL='https://github.com/Kyle-Hosman/az-mods-sdk.git'

def run(args,cwd=ROOT):
    subprocess.run([str(x) for x in args],cwd=cwd,check=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build',action='store_true',help='Package an existing ARM64 build; validation still runs')
    args=parser.parse_args()
    git=shutil.which('git')
    if not git and os.name=='nt':
        candidate=Path(os.environ.get('ProgramFiles',r'C:\Program Files'))/'Git/cmd/git.exe'
        if candidate.is_file():git=str(candidate)
    if not git:raise RuntimeError('Git fehlt. Git for Windows installieren oder den Git-Pfad ergaenzen.')
    sdk=ROOT/'.azmod-sdk'
    if not sdk.exists():
        run([git,'init',str(sdk)])
        run([git,'remote','add','origin',SDK_URL],sdk)
        run([git,'fetch','--depth','1','origin',SDK_REF],sdk)
        run([git,'checkout','--detach','FETCH_HEAD'],sdk)
    actual=subprocess.check_output([git,'rev-parse','HEAD'],cwd=sdk,text=True,encoding='utf-8',errors='replace').strip()
    if actual!=SDK_REF:raise RuntimeError('Der SDK-Ordner hat nicht den festgelegten Commit. Bitte .azmod-sdk umbenennen und erneut starten.')
    pack=[sys.executable,str(sdk/'tools/azmod-pack')]
    if not args.no_build:
        run(['docker','info','--format','{{.OSType}}'])
        run(pack+['build',str(ROOT)])
    run(pack+['validate',str(ROOT)])
    run(pack+['package',str(ROOT),'--github','MiuxH/az-waveform-height',
              '--tag','miuxh-wave-height-0.1.0'])
    print('\nCommunity-Paket: '+str(ROOT/'dist/miuxh-wave-height-0.1.0.azmod'))
    print('Kein Core und keine Firmware wurden ersetzt. README-DE.md beschreibt den Test.')

if __name__=='__main__':
    try:main()
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:
        print('ABGEBROCHEN: '+str(error),file=sys.stderr);sys.exit(1)
