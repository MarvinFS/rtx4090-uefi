#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Assembles the Windows install package folder (nothing is installed anywhere). Lists are written with printf: a
# POSIX sh echo turns the backslash in GPU4090-ASSETS\booter... into a backspace.
# Usage: ./make-package.sh <outdir> [assets-dir]   (assets default: /usr/lib/firmware/gpu-unlock/610.43.03)
set -eu
cd "$(dirname "$0")"
OUT=${1:?outdir}; ASSETS=${2:-/usr/lib/firmware/gpu-unlock/610.43.03}
grep -E '^[0-9a-f]{64}  ' NVIDIA-FILES.md | (cd "$ASSETS" && sha256sum -c --quiet -) ||
  { echo "firmware files in $ASSETS do not match NVIDIA-FILES.md" >&2; exit 1; }
python3 build.py --zig "${ZIG:-zig-0.14.1}" --out "$OUT/.build" >/dev/null
rm -rf "$OUT/pkg"; mkdir -p "$OUT/pkg/GPU4090-ASSETS"
for f in UNLOCK4090 UNLOCK4090M; do cp "$OUT/.build/$f.EFI" "$OUT/pkg/"; done
for f in booter_load_ad102.bin v67-ad102-610.43.03-four-plm.bin gsp_ga10x.bin gsp_boot_ad102.bin gsp_boot_desc_ad102.bin; do
  cp "$ASSETS/$f" "$OUT/pkg/GPU4090-ASSETS/"; printf '%s\n' "GPU4090-ASSETS\\$f"; done > "$OUT/pkg/ASSETS.LIST"
cp package/*.ps1 package/*.cmd package/before-after.png benchmark.py compare.py THIRD_PARTY_NOTICES.md UPSTREAM-WINDOWS-NOTICES.md NVIDIA-FILES.md LICENSE "$OUT/pkg/"
# tools/mma_bench.exe is built on Windows (CUDA 13.1 + MSVC, see tools/build-mma_bench.bat); nvcc cannot target Windows from Linux.
cp -r LICENSES package/tools "$OUT/pkg/"
# The user README is the root one; files from win4090/ and win4090/package/ sit at the package top level.
sed -e 's|](win4090/package/|](|g' -e 's|](win4090/|](|g' ../README.md > "$OUT/pkg/README.md"
grep -o '](\([^):#]*\))' "$OUT/pkg/README.md" | sed 's/^](\(.*\))$/\1/' | while read -r l; do
  [ -e "$OUT/pkg/$l" ] || { echo "README.md links to $l, which is not in the package" >&2; exit 1; }; done
( cd "$OUT/pkg" && find . -type f ! -name SHA256SUMS | sed 's|^\./||' | sort | xargs sha256sum > SHA256SUMS )
echo "package: $OUT/pkg ($(du -sh "$OUT/pkg" | cut -f1))"
