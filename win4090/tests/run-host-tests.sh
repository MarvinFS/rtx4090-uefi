#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Host tests for the hardware-agnostic RTX 4090 UEFI port: real retained core + compat + port.c + profile.c against a
# fake PCI/MMIO/DMA engine, with the captured card's real PROM, UEFI PCI config and AD102 assets (other boards are fixture
# knobs). No hardware access. Usage:
#   tests/run-host-tests.sh [PROM] [ASSETS_DIR] [SURVEY_TXT]
# PROM (a read-only dump of your card's VBIOS window, not shipped) is required, as an argument or as $GU_PROM.
# SURVEY_TXT (the log of an earlier read-only board survey) is optional, as an argument or as $GU_SURVEY. Output goes to $OUT (default: a fresh mktemp -d), never into the source tree.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
prom=${1:-${GU_PROM:?pass the PROM dump as the first argument or set GU_PROM}}
assets=${2:-/usr/lib/firmware/gpu-unlock/610.43.03}
survey=${3:-${GU_SURVEY:-}}
out=${OUT:-$(mktemp -d)}
mkdir -p "$out"
cc=${CC:-gcc}
# GU_AD104_HOST_TEST (hosted only, port.h refuses it in firmware) opens the write gates against the fake engine and
# makes port.h define GU_PROFILE_BUILD; GU_ENTRY_LATCHED_SS1_FULL is the one selector policy of this port.
# -fshort-wchar: the UEFI headers assume 2-byte wchar_t (MinGW has it, glibc does not). -Wno-format: UINTN is
# long long and glibc size_t is long (same width on x86-64), so %zu on UINTN warns although it is correct.
flags="-std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-misleading-indentation -Wno-format -fshort-wchar \
 -D__KERNEL__ -DGU_UEFI_PORT -DGU_AD104_HOST_TEST -DGU_ENTRY_LATCHED_SS1_FULL \
 -I$root/compat -I$root/vendor/edk2/Include -I$root/vendor/edk2/Include/X64"
srcs="port compat checks profile sha256 vendor/unlock_610 vendor/prep_610 vendor/layout_610 vendor/vbios_610 vendor/gfw_eval"
files=""
for s in $srcs; do files="$files $root/$s.c"; done
# Compile both, run them side by side (each takes minutes: 84 MB asset walks per scenario), then report.
for t in efi_transaction guards; do
  $cc $flags "$here/test_$t.c" $files -o "$out/test-$t"
done
pids=""
for t in efi_transaction guards; do
  # Only the transaction test cross-checks the survey text dump against CONFIG.BIN.
  args="$prom $assets"
  [ "$t" = efi_transaction ] && [ -n "$survey" ] && [ -r "$survey" ] && args="$args $survey"
  "$out/test-$t" $args > "$out/$t.log" 2>&1 &
  eval "pid_$t=$!"
done
rc=0
for t in efi_transaction guards; do
  eval "pid=\$pid_$t"
  if wait "$pid"; then
    tail -n 2 "$out/$t.log"
  else
    tail -n 40 "$out/$t.log"; echo "FAIL: test_$t (full log: $out/$t.log)"; rc=1
  fi
done
[ $rc -eq 0 ] && echo "ALL HOST TESTS PASS (logs in $out)"
exit $rc
