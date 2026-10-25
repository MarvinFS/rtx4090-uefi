/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
typedef enum {GU_CHOICE_UNLOCK=1,GU_CHOICE_WINDOWS} GU_CHOICE;
/* Home volume = the volume this image was started from (any FAT volume: USB stick, a
   partition on any internal disk, or an ESP). Holds the log and the assets; nothing
   outside the app's own two directories is ever written. */
EFI_STATUS GuBootOpen(EFI_HANDLE,EFI_SYSTEM_TABLE *);
EFI_STATUS GuBootLoadAssets(VOID);
/* Finds the Windows boot manager at run time (the firmware's own "Windows Boot Manager"
   boot entry first, else the only volume holding \EFI\Microsoft\Boot\bootmgfw.efi),
   reads it, and LoadImages it. Read-only; content is logged, never pinned. */
EFI_STATUS GuBootLoadWindows(VOID);
VOID GuBootUnloadWindows(VOID);
/* Countdown with Esc menu. DefaultUnlock selects the action when the countdown expires. */
EFI_STATUS GuBootMenu(BOOLEAN DefaultUnlock,UINTN Seconds,GU_CHOICE *);
EFI_STATUS GuBootWindows(VOID);
EFI_STATUS GuBootClose(VOID);
/* Console only / console and log (when open). Both are no-ops on the console while silenced. */
VOID GuBootSay(const char *);
VOID GuBootNote(const char *Format,...);
VOID GuBootSilence(VOID);
VOID GuBootUnsilence(VOID);
VOID GuBootHalt(VOID);
/* Diagnostics log only while the log file is open (GuLog faults otherwise). */
BOOLEAN GuBootLogOpen(VOID);
