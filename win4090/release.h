/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
/* Read-only: pin the GPU handle path, its one NVIDIA GOP driver, the GOP children and
   their console consumers, the console handles, BAR0 and PCI config. Logs what it
   learned. Never stops a driver. */
EFI_STATUS GuRelPreflight(EFI_SYSTEM_TABLE *,EFI_HANDLE,EFI_HANDLE);
/* Stop the NVIDIA GOP driver once (the UEFI core cascades through the console stack),
   take the exclusive PciIo claim, restore only cleared IO/MEM decode and re-verify
   config, BAR0 and the cold registers. No GPU register write. */
EFI_STATUS GuRelRelease(VOID);
/* Display_check hook for the retained core: released state still consistent. */
BOOLEAN GuRelReleased(VOID);
/* Close the claim and reconnect the NVIDIA GOP driver for Windows. A GOP that does
   not come back is reported in *Gop; the caller decides (it is not a GPU fault). */
EFI_STATUS GuRelFinish(BOOLEAN *Gop);
/* Same reconnect without a transaction behind it. Only valid while no GPU register
   write, config write, DMA or FLR has happened (release test, aborted release). */
EFI_STATUS GuRelRecover(BOOLEAN *Gop);
/* Fresh unlogged checks right before StartImage. Unlocked selects the expected
   register state (TRUE after a transaction, FALSE after a release test). */
BOOLEAN GuRelReadyForWindows(BOOLEAN Unlocked);
BOOLEAN GuRelMutated(VOID);
