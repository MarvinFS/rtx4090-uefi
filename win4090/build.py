#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build the RTX 4090 pre-boot unlock EFI images with zig 0.14.1 (Linux, macOS or Windows).
No GPU access, no installation. Images (reproducible, PE-checked):
  UNLOCK4090.EFI    product: unlocks automatically after a 3 s countdown (Esc: menu)
  UNLOCK4090M.EFI   same code, but the countdown ends in stock Windows (U / menu entry 1 to unlock)
  DRYRUN4090.EFI    all GPU write gates closed, display driver untouched
  RELTEST4090.EFI   display driver release + reconnect test, still no GPU write
Usage: python build.py [--zig zig] [--out build]"""
import argparse, hashlib, importlib.util, json, os, subprocess, sys
from pathlib import Path

SOURCES = ['app', 'boot_io', 'release', 'port', 'compat', 'checks', 'profile', 'sha256', 'runtime', 'format',
           'vendor/unlock_610', 'vendor/prep_610', 'vendor/layout_610', 'vendor/vbios_610', 'vendor/gfw_eval']
COMMON = ['-target', 'x86_64-uefi', '-std=c11', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
          '-mno-red-zone', '-fshort-wchar', '-Os', '-g0', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
          '-Wno-misleading-indentation', '-D__KERNEL__', '-DGU_UEFI_PORT', '-DGU_ENTRY_LATCHED_SS1_FULL',
          '-Icompat', '-Ivendor/edk2/Include', '-Ivendor/edk2/Include/X64']
VARIANTS = [('UNLOCK4090', 'auto', ['-DGU_AD104_TRANSACTION']),
            ('UNLOCK4090M', 'manual', ['-DGU_AD104_TRANSACTION', '-DGU_MANUAL']),
            ('DRYRUN4090', 'dryrun', ['-DGU_4090_DRYRUN']),
            ('RELTEST4090', 'reltest', ['-DGU_4090_DRYRUN', '-DGU_4090_RELCHECK'])]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--zig', default=os.environ.get('ZIG', 'zig'))
    ap.add_argument('--out', default='build')
    a = ap.parse_args()
    root = Path(__file__).resolve().parent
    os.chdir(root)
    out = Path(a.out)
    (out / 'obj').mkdir(parents=True, exist_ok=True)
    zig_version = subprocess.check_output([a.zig, 'version'], text=True).strip()
    if zig_version != '0.14.1':
        print(f'warning: tested with zig 0.14.1, found {zig_version}', file=sys.stderr)
    spec = importlib.util.spec_from_file_location('pecheck', root / 'tools' / 'pe_format.py')
    pe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pe)
    report = {}
    for name, ident, defines in VARIANTS:
        objs = []
        for s in SOURCES:
            o = out / 'obj' / f'{name}-{s.replace("/", "_")}.o'
            subprocess.run([a.zig, 'cc', *COMMON, *defines, f'-DGU_BUILD_ID="{ident}"', '-c', f'{s}.c', '-o', str(o)], check=True)
            objs.append(str(o))
        efi = out / f'{name}.EFI'
        # -s: no debug directory; /Brepro: no wall-clock PE timestamp, so the image is byte-for-byte reproducible
        subprocess.run([a.zig, 'cc', '-target', 'x86_64-uefi', '-nostdlib', '-s', '-Wl,/Brepro', *objs, '-o', str(efi)], check=True)
        for extra in (out / f'{name}.pdb', out / f'{name}.lib'):
            extra.unlink(missing_ok=True)
        data = efi.read_bytes()
        pe.validate_pe(data)
        report[efi.name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    report['zig'] = zig_version
    (out / 'build-result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
