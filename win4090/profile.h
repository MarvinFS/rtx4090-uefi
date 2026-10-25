/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
#include "vendor/vbios_610.h"
BOOLEAN GuAd104AssetsValid(VOID);
BOOLEAN GuAd104FwsecSane(const struct gpu_unlock_fwsec *);
/* Prepare is read-only on the GPU. It validates the card, assets and cold state, parses
 * the FWSEC image from the card's own VBIOS and creates the immutable selector latch.
 * No driver release here. */
EFI_STATUS GuAd104Prepare(VOID);
BOOLEAN GuAd104ColdEntry(VOID);
VOID GuProfileLogRegisters(VOID);
