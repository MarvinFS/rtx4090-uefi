# Where the five firmware files come from

The unlock reads five files from `GPU4090-ASSETS\`. Four are NVIDIA's own files, and the fifth is generated. None of them is in this repository. The release zip takes them from the `gpu-unlock-firmware` package, built with [gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada/tree/491925c1079435e260137b9f3526066e764de830) v1.1.1 (commit `491925c1079435e260137b9f3526066e764de830`, recipe in `packaging/arch/PKGBUILD`) and installed in `/usr/lib/firmware/gpu-unlock/610.43.03/`. That build downloads two archives and generates one file. The values below come from that commit, read on 2026-10-25.

## gsp_ga10x.bin: NVIDIA's driver firmware, repackaged by CachyOS

`gsp_ga10x.bin` (84,277,400 bytes) is a byte-for-byte copy of the file in CachyOS's package of the NVIDIA driver:

- package: `nvidia-utils-610.43.03-1-x86_64_v3.pkg.tar.zst`
- downloaded from: `https://archive.cachyos.org/archive/cachyos-v3/nvidia-utils-610.43.03-1-x86_64_v3.pkg.tar.zst`
- package SHA-256: `e17e7c92eeffa178b0fe1f40eb17a47095f8fc3ce4a77bf115cf5bd5a4df86e9`
- path inside the package: `usr/lib/firmware/nvidia/610.43.03/gsp_ga10x.bin`

gpu-unlock-ada's `firmware/generate_v67.py` takes the file out of the package after it checks the package's SHA-256, its version `610.43.03-1`, and the file's own SHA-256. It is NVIDIA's file, unchanged. CachyOS only packages it. Its license is [LICENSES/NVIDIA-Driver.txt](LICENSES/NVIDIA-Driver.txt), which is byte-identical to that package's `usr/share/licenses/nvidia-utils/LICENSE` (SHA-256 `c692fb14ad4f499c6aee1b9e144e6905148c420e6225b17cb2937366c1e507ad`, checked 2026-10-25).

## booter_load_ad102.bin, gsp_boot_ad102.bin, gsp_boot_desc_ad102.bin: decoded from NVIDIA's open kernel module source

gpu-unlock-ada's `firmware/extract_boot_assets.py --chip ad102` decodes `booter_load_ad102.bin` (57,672 bytes), `gsp_boot_ad102.bin` (36,864 bytes) and `gsp_boot_desc_ad102.bin` (84 bytes) from the AD102 bindata files `g_bindata_kgspGetBinArchiveBooterLoadUcode_AD102.c` and `g_bindata_kgspGetBinArchiveGspRmBoot_AD102.c` in NVIDIA's open kernel module source:

- archive: `NVIDIA-kernel-module-source-610.43.03.tar.xz`
- downloaded from: `https://download.nvidia.com/XFree86/NVIDIA-kernel-module-source/NVIDIA-kernel-module-source-610.43.03.tar.xz`
- archive SHA-256: `7e118923c7a23edc36114d63273a46e3e04e9af98695a42203e7ac2dfe9fc1dc`

That source is licensed GPL-2.0 OR MIT. The NVIDIA MIT notice is [LICENSES/NVIDIA-MIT.txt](LICENSES/NVIDIA-MIT.txt).

## v67-ad102-610.43.03-four-plm.bin: generated, not an NVIDIA file

gpu-unlock-ada's `firmware/generate_v67.py` builds `v67-ad102-610.43.03-four-plm.bin` (64,000 bytes) from constants in the script alone: the 32-bit word 0x0000019C repeated, with 25 words patched at fixed offsets. The file contains no bytes from NVIDIA's files. The script adapts the construction from TOTORONG's [AD103_win_unlocker](https://huggingface.co/TOTORONG/AD103_win_unlocker) (`source/firmware-tools/ad102_payload.py`, revision `51df9c17ec89eb8486df52cc9079dde4bd1f9974`).

## Checksums

`make-package.sh` checks these before it copies the files into the package, and stops if any file differs.

```
6ee05b20e1dc244c08c052f36ef28469b8d09f4955d1fe33e48a179e11a7fd49  booter_load_ad102.bin
44c36b2d6b3424e53e18f3e55db85c833655de071ce66ce42749588a4a7dc8cb  v67-ad102-610.43.03-four-plm.bin
572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a  gsp_ga10x.bin
45d6335a8185d6ff13e8d1484cea73f68d3d13aa1c19dc080ed1c3482dd5bac0  gsp_boot_ad102.bin
83f8f3f1f5f9496e0ea8fc7103c2beb64d0b6de2f5ffe725f13514de5a95fa42  gsp_boot_desc_ad102.bin
```
