# Changes from the Latence.app AD104 / MSI port (2026-10-25)

Retarget to the GeForce RTX 4090 (AD102, 10de:2684) as a board-independent module.

- Removed every machine pin: subsystem, bus/device/function, BAR addresses, config dword pins, VRAM size, PROM hash,
  FWSEC geometry, the exact cold-register table (and the idea of learning more of it), cold SS1, the Windows ESP and home
  partition identities, the MSI USB/console graph code, driver code hashes and sizes.
- Admission is the Linux module's: PCI ID and capability chain, asset SHA-256 pins, FWSEC parsed from the VBIOS at run
  time (bounds only), GFW halted / PLM level 0 / boot complete, feature PLM not already open, WPR2 down.
  Retained UEFI safety: fault latch, DMA retention, one-shot SS0/SS1 selector latch with the SS1_FULL policy.
- `GuCheckConfig` is a tolerant capability finder, not a validator: it needs the PCI ID, an endpoint header, memory decode, a PCIe capability with FLR support and D0 if a PM capability exists (what the Linux module needs); unknown capabilities, MSI state, PCIe version, overlaps, the ROM-enable bit etc. are skipped, never refused. Firmware assets are only checked for presence and exact size.
- `port.c` `GuEnableMaster` judges memory decode and bus master only; the firmware PciIo caches an IO attribute the
  command register does not have (found on the surveyed board, would have refused the transaction).
- New `release.c` (generic display-driver handoff), `boot_io.c` (run-time volume and Windows discovery, log ring,
  3 s countdown, two-entry Esc menu), `app.c` (new flow and failure policy), `build.sh`, `make-package.sh`, `package/`.
- The VBIOS is never written; see ../README.md.

## Fast Startup and sleep (2026-10-25)

- `Install-Gpu4090Unlock.ps1` turns Windows Fast Startup off (`HiberbootEnabled=0`) after the boot entry is verified.
  It stores the old value in `install.json` before the change. `-KeepFastStartup` opts out. If a Group Policy forces
  Fast Startup on, the installer warns and leaves the policy alone.
- `Uninstall-Gpu4090Unlock.ps1` restores the stored value, unless it was changed after the install. Records from
  v0.1.0 have no stored value, so nothing is restored for them.
- `Test-Gpu4090Unlock.ps1` (`Check.cmd`) reports the Fast Startup state. It fails when the installer turned Fast Startup
  off and something turned it back on, and it explains a stock benchmark result after sleep.
- Measured on the reference PC: S3 sleep returns the card to stock. Before the sleep, FP16 and BF16 with FP32 accumulate
  ran at 333 / 332 TF/s against 334 for FP16 accumulate. After a 31 second sleep they ran at 165 / 165 against 333.
  Resume does not go through the firmware boot path, so the app does not run again.
- New Pester cases in `tests/windows/Gpu4090Common.Tests.ps1` use a scratch HKCU key.
