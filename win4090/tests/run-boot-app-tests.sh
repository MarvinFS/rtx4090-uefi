#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Host tests for the boot/app layer of the RTX 4090 UEFI port (no hardware, hosted gcc):
#   test_boot_io.c   boot_io.c against a fake EFI environment (volumes, boot variables, console, image loading),
#                    using the real device paths and Boot0000 bytes from the firmware survey
#   test_app_flow.c  app.c itself, once per build mode (product, GU_MANUAL, GU_4090_DRYRUN, DRYRUN+RELCHECK), all
#                    collaborators stubbed, call order and failure policy asserted for every stage
# Usage: tests/run-boot-app-tests.sh [ASSETS_DIR]   (the five AD102 assets, read only)
# Output goes to $OUT (default: a fresh mktemp -d), never into the source tree.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
assets=${1:-/usr/lib/firmware/gpu-unlock/610.43.03}
out=${OUT:-$(mktemp -d)}
mkdir -p "$out"
cc=${CC:-gcc}
# -fshort-wchar: the UEFI headers assume 2-byte wchar_t. -Wno-format: UINTN is long long, glibc size_t is long
# (same width on x86-64), so %zu on UINTN warns although it is correct.
common="-std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-misleading-indentation -Wno-format -fshort-wchar \
 -D__KERNEL__ -DGU_UEFI_PORT -DGU_ENTRY_LATCHED_SS1_FULL -I$root/compat -I$root/vendor/edk2/Include -I$root/vendor/edk2/Include/X64"
core="port compat checks profile sha256 format vendor/unlock_610 vendor/prep_610 vendor/layout_610 vendor/vbios_610 vendor/gfw_eval"
files=""
for s in $core; do files="$files $root/$s.c"; done
rc=0
run() { # name  log-tail-lines  command...
  name=$1; shift
  if "$@" > "$out/$name.log" 2>&1; then tail -n 2 "$out/$name.log"
  else tail -n 40 "$out/$name.log"; echo "FAIL: $name (full log: $out/$name.log)"; rc=1; fi
}
# boot_io.c is #included by its test and linked with the core (hosted test mode: writes allowed for the core's own tests).
$cc $common -DGU_AD104_HOST_TEST "$here/test_boot_io.c" $files -o "$out/test-boot-io"
run boot-io "$out/test-boot-io" "$assets"
# app.c: one executable per build mode; only checks.c is real, everything else app.c calls is a stub.
for mode in "product:-DGU_AD104_TRANSACTION" "manual:-DGU_AD104_TRANSACTION -DGU_MANUAL" \
            "dryrun:-DGU_4090_DRYRUN" "reltest:-DGU_4090_DRYRUN -DGU_4090_RELCHECK"; do
  name=${mode%%:*}; defs=${mode#*:}
  # shellcheck disable=SC2086
  $cc $common $defs -DGU_ENTRY=AppEntry -DGU_BUILD_ID=\"test\" "$here/test_app_flow.c" "$root/checks.c" -o "$out/test-app-$name"
  run "app-$name" "$out/test-app-$name"
done
[ $rc -eq 0 ] && echo "ALL BOOT/APP HOST TESTS PASS (logs in $out)"
exit $rc
