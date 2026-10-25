#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Host tests for release.c (display-driver handoff) against a fake UEFI core on the surveyed X870E / RTX 4090
# topology. No hardware access. Usage: tests/run-release-tests.sh [-v]     (-v: echo the app log lines)
# Output goes to $OUT (default: a fresh mktemp -d), never into the source tree.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
out=${OUT:-$(mktemp -d)}
mkdir -p "$out"
cc=${CC:-gcc}
# Same hosted flags as run-host-tests.sh. -fshort-wchar: the UEFI headers assume 2-byte wchar_t. -Wno-format: UINTN is
# long long and glibc size_t is long (same width on x86-64), so %zu on UINTN warns although it is correct.
flags="-std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-misleading-indentation -Wno-format -fshort-wchar \
 -D__KERNEL__ -DGU_UEFI_PORT -DGU_AD104_HOST_TEST -DGU_ENTRY_LATCHED_SS1_FULL \
 -I$root/compat -I$root/vendor/edk2/Include -I$root/vendor/edk2/Include/X64"
# release.c itself is #included by the test (its static state is reset between scenarios); port.c and sha256.c are the real ones.
$cc $flags "$here/test_release.c" "$root/port.c" "$root/sha256.c" -o "$out/test-release"
if "$out/test-release" "$@" > "$out/release.log" 2>&1; then
  tail -n 2 "$out/release.log"
  echo "ALL RELEASE TESTS PASS (log in $out)"
else
  tail -n 40 "$out/release.log"; echo "FAIL: test_release (full log: $out/release.log)"; exit 1
fi
