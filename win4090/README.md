# win4090 - RTX 4090 pre-boot unlock (UEFI), developer notes

User-facing install and usage: [README.md](../README.md).

Derived from the gpu-unlock-ada `windows/ad104-msi` tree (Latence.app v0.5.4), which carries the retained
transaction core (`vendor/`) and UEFI platform layer (`port.c`, `compat.c`). Everything specific to that MSI
RTX 4070 Ti / Z490 machine was removed; see CHANGES.md. Upstream credits and licences are unchanged:
`THIRD_PARTY_NOTICES.md`, `UPSTREAM-WINDOWS-NOTICES.md`, `LICENSES/`, `provenance.json`.

| File | Role |
|---|---|
| `app.c` | flow: log, Secure Boot, stage Windows, discover the single 10de:2684, assets, cold-state check, display preflight, 3 s countdown / Esc menu, release, transaction, reconnect, Windows |
| `release.c` | display-driver handoff for any firmware: one `DisconnectController` of whatever holds the GPU's PciIo, exclusive claim, restore only cleared IO/MEM decode, `ConnectController` back. No-driver and no-display cards are valid |
| `boot_io.c` | home volume (wherever the image runs), log ring, assets, Windows boot manager discovery (BootOrder entry, else the only volume with bootmgfw.efi), countdown and menu |
| `profile.c` | admission: NVIDIA 610.43.03 asset SHA-256 pins, PCI ID and capability chain, FWSEC parsed from the card's own VBIOS, the Linux module's cold-state gate |
| `port.c`, `compat.c`, `checks.c` | platform layer: fault latch, DMA retention, BME / FLR / config gates, one-shot selector latch |
| `vendor/` | the Linux transaction core (prep, layout, vbios, gfw) |

## Build (no hardware access)

`python build.py [--zig zig] [--out build]` (Linux, macOS or Windows) builds with zig 0.14.1 and PE-checks four images: `UNLOCK4090.EFI` (product, automatic),
`UNLOCK4090M.EFI` (countdown ends in stock Windows), `DRYRUN4090.EFI` (all GPU gates closed, display untouched),
`RELTEST4090.EFI` (display release + reconnect test, still no GPU write). `./make-package.sh <outdir>` assembles
the Windows install folder with `SHA256SUMS`. It ships only `UNLOCK4090.EFI` and `UNLOCK4090M.EFI`, not the two test images.

Build modes (`port.h`): product `GU_AD104_TRANSACTION`; `GU_4090_DRYRUN`; `GU_4090_RELCHECK` (needs DRYRUN); `GU_MANUAL`.

## Tests

Host tests simulate PCI, MMIO, DMA, the UEFI core and the firmware engines; nothing touches hardware.
`tests/run-host-tests.sh` (retained core against this card's real PROM, config and assets),
`tests/run-release-tests.sh` (display handoff against a fake UEFI core with the surveyed topology),
`tests/run-boot-app-tests.sh` (boot I/O and the app's failure policy).
