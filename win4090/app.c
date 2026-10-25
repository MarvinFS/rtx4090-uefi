/* SPDX-License-Identifier: GPL-2.0-only */
/* RTX 4090 (NVIDIA AD102, 10de:2684) pre-boot unlock, run before Windows Boot Manager.
 * Automatic: preflight, 3 second countdown (Esc: menu with "unlock" or "Windows without
 * the mod"), the Linux-proven firmware transaction, then Windows. Board, slot, BAR address,
 * display stack, Windows disk and the folder this image sits in are all discovered at run time.
 * Failure before any GPU write: log why and start stock Windows. Failure after one: contain the
 * card and stop with a power-off message (a full power-off returns the card to stock). */
#include "port.h"
#include "checks.h"
#include "profile.h"
#include "release.h"
#include "boot_io.h"
#include "compat/compat.h"
#include "vendor/unlock_610.h"
#include <Protocol/PciIo.h>
#include <IndustryStandard/Acpi10.h>

#ifndef GU_BUILD_ID
#define GU_BUILD_ID "dev"
#endif
#ifndef GU_ENTRY
#define GU_ENTRY EfiMain
#endif
/* The product build unlocks when the countdown ends; GU_MANUAL (and every dry-run build)
   starts stock Windows then and unlocks only on U / menu entry 1. */
#if defined(GU_AD104_TRANSACTION) && !defined(GU_MANUAL)
#define GU_DEFAULT_UNLOCK TRUE
#else
#define GU_DEFAULT_UNLOCK FALSE
#endif
#define GU_COUNTDOWN_SECONDS 3U
#define LOG(...) do{if(GuBootLogOpen())GuLog(__VA_ARGS__);}while(0)

static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID GlobalGuid={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};
static EFI_SYSTEM_TABLE *Table;

/* Exactly one 10de:2684 anywhere in the system; its slot and BAR0 are read, not assumed. */
static EFI_STATUS Discover(EFI_HANDLE image) {
  EFI_BOOT_SERVICES *b=Table->BootServices;EFI_HANDLE *h=NULL,target=NULL;
  EFI_PCI_IO_PROTOCOL *io=NULL;UINTN count=0,found=0;
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&count,&h);
  if(s==EFI_SUCCESS&&(!h||!count||count>1024))s=EFI_UNSUPPORTED;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_PCI_IO_PROTOCOL *p=NULL;UINT32 id=0;
    if(b->HandleProtocol(h[i],&PciGuid,(VOID **)&p)!=EFI_SUCCESS||!p||!p->Pci.Read)continue;
    if(p->Pci.Read(p,EfiPciIoWidthUint32,0,1,&id)!=EFI_SUCCESS)continue;
    if(id==GU_BOARD_ID){found++;target=h[i];io=p;}
  }
  if(h)b->FreePool(h);
  LOG("PCI scan: 10de:2684 functions found=%zu status=%#llx\n",(size_t)found,(unsigned long long)s);
  if(s!=EFI_SUCCESS)return s;
  if(found!=1||!io)return found?EFI_UNSUPPORTED:EFI_NOT_FOUND;
  Gu.pci=io;Gu.handle=target;Gu.image=image;
  UINT32 c[64];GU_CAPS caps;
  if(!io->GetLocation||!io->GetBarAttributes)return EFI_UNSUPPORTED;
  s=io->GetLocation(io,&Gu.seg,&Gu.bus,&Gu.dev,&Gu.fun);
  if(s==EFI_SUCCESS)s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,c);
  if(s!=EFI_SUCCESS||!GuCheckConfig(c,Gu.seg,Gu.bus,Gu.dev,Gu.fun,&caps))return EFI_UNSUPPORTED;
  Gu.located=TRUE;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=NULL;UINT64 support=0;
  s=io->GetBarAttributes(io,0,&support,(VOID **)&d);
  if(s==EFI_SUCCESS&&(!d||d->Desc!=0x8a||d->Len!=43||d->ResType||d->AddrTranslationOffset||d->AddrLen!=GU_BAR0_BYTES||
     d->AddrRangeMin>0xffffffffULL||(d->AddrRangeMin&(GU_BAR0_BYTES-1))||(UINT32)d->AddrRangeMin!=(c[4]&~0xfU)))s=EFI_UNSUPPORTED;
  if(s==EFI_SUCCESS){Gu.bar_base=d->AddrRangeMin;Gu.bar_size=d->AddrLen;}
  if(d)b->FreePool(d);
  if(s!=EFI_SUCCESS)return s;
  Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;
  LOG("GPU at %zu:%zu:%zu.%zu subsystem=%#x class_rev=%#x BAR0=%#llx pm=%#x pcie=%#x v%u msi=%#x msix=%#x\n",
    Gu.seg,Gu.bus,Gu.dev,Gu.fun,c[11],c[2],(unsigned long long)Gu.bar_base,
    (unsigned)caps.pm,(unsigned)caps.pcie,(unsigned)caps.pcie_version,(unsigned)caps.msi,(unsigned)caps.msix);
  return EFI_SUCCESS;
}
static VOID FreeAssets(VOID) {
  for(UINTN i=0;i<5;i++){GuFree((VOID *)GuAssets[i].firmware.data);GuAssets[i].firmware.data=NULL;GuAssets[i].firmware.size=0;}
}
/* Nothing was written to the GPU (or the display was put back): start stock Windows. If the image
   comes back, its status goes to the firmware, which then tries its next boot option. */
static EFI_STATUS StockWindows(const char *why,EFI_STATUS reason) {
  if(why){
    GuBootNote("\nUnlock skipped (%s, status %#llx). Starting Windows without the mod.\n",why,(unsigned long long)reason);
    Table->BootServices->Stall(3000000);
  }
  FreeAssets();
  (VOID)GuBootClose();
  EFI_STATUS s=GuBootWindows();
  GuBootUnloadWindows();
  return s;
}
EFI_STATUS EFIAPI GU_ENTRY(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
  if(!image||!t||!t->BootServices||!t->RuntimeServices||!t->RuntimeServices->GetVariable)return EFI_INVALID_PARAMETER;
  Table=t;GuInit(t->BootServices,NULL);Gu.image=image;
  const char *stage="log";GU_CHOICE choice;
  EFI_STATUS s=GuBootOpen(image,t);
  if(s!=EFI_SUCCESS) {
    /* No log, no GPU interaction: just keep the machine booting. */
    GuBootSay("RTX 4090 unlock: cannot open the log on this volume; skipping.\n");
    (VOID)GuBootLoadWindows();
    return StockWindows(NULL,0);
  }
  GuLog("RTX 4090 unlock build=%s profile=%s\n",GU_BUILD_ID,
#ifdef GU_STOCK_PROFILE_NAME
    GU_STOCK_PROFILE_NAME
#else
    "none"
#endif
    );
  GuBootNote("RTX 4090 unlock (%s)\n",GU_BUILD_ID);
  /* Stage Windows first: every skip below (Secure Boot included) then ends in a stock Windows start. */
  stage="windows-image";s=GuBootLoadWindows();
  if(s!=EFI_SUCCESS) {
    /* Cannot hand over to Windows ourselves: leave the GPU alone and let the firmware boot normally. */
    GuBootNote("Cannot stage the Windows boot manager (%#llx); not touching the GPU.\n",(unsigned long long)s);
    (VOID)GuBootClose();return s;
  }
  {
    UINT8 secure=1;UINTN n=1;stage="secure-boot";
    s=t->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,NULL,&n,&secure);
    /* An unsigned image only runs with Secure Boot off; a firmware without the variable has none. */
    if(s==EFI_NOT_FOUND)s=EFI_SUCCESS;
    else if(s==EFI_SUCCESS&&(n!=1||secure))s=EFI_SECURITY_VIOLATION;
    if(s!=EFI_SUCCESS)goto skip;
  }
  stage="discover";s=Discover(image);if(s!=EFI_SUCCESS||Gu.fault)goto skip;
  GuProfileLogRegisters();if(Gu.fault){s=EFI_DEVICE_ERROR;goto skip;}
  stage="assets";GuBootSay("Loading firmware assets...\n");s=GuBootLoadAssets();if(s!=EFI_SUCCESS||Gu.fault)goto skip;
  stage="cold-state";s=GuAd104Prepare();if(s!=EFI_SUCCESS||Gu.fault)goto skip;
  stage="display-preflight";s=GuRelPreflight(t,image,Gu.handle);if(s!=EFI_SUCCESS||Gu.fault)goto skip;
  GuBootNote("Checks passed.\n");
  stage="menu";s=GuBootMenu(GU_DEFAULT_UNLOCK,GU_COUNTDOWN_SECONDS,&choice);
  if(s!=EFI_SUCCESS)choice=GU_CHOICE_WINDOWS;
  GuLog("choice: %s\n",choice==GU_CHOICE_UNLOCK?"unlock":"windows without the mod");
  if(choice!=GU_CHOICE_UNLOCK)return StockWindows(NULL,0);
#if defined(GU_4090_DRYRUN) && !defined(GU_4090_RELCHECK)
  GuBootNote("DRY RUN build: no display release and no GPU write. Starting Windows.\n");
  return StockWindows(NULL,0);
#endif
  stage="display-release";
  GuBootNote("\nReleasing the display; the screen may freeze or go blank until Windows starts.\n");
  GuLog("Unlock chosen: one physical attempt, no retries\n");
  if(Gu.fault){s=EFI_DEVICE_ERROR;goto failed;}
  s=GuRelRelease();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  Gu.display_check=GuRelReleased;Gu.display_ok=TRUE;
#if GU_PREFLIGHT_ONLY
  {
    /* Release test: the display driver was stopped, claimed, restored; put it back, no GPU write. */
    BOOLEAN gop=FALSE;stage="release-test";
    s=GuRelRecover(&gop);GuLog("release test: status=%#llx gop=%u\n",(unsigned long long)s,gop);
    if(s!=EFI_SUCCESS||!gop||Gu.fault)goto failed;
    GuBootNote("Release test OK: driver stopped, claimed, restored and reconnected; GPU not written.\n");
    stage="close-log";s=GuBootClose();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    stage="final-verification";if(!GuRelReadyForWindows(FALSE)){s=EFI_NOT_READY;goto failed;}
    FreeAssets();stage="start-windows";s=GuBootWindows();
    GuBootHalt();return s==EFI_SUCCESS?EFI_ABORTED:s;
  }
#else
  {
    Gu.armed=TRUE;stage="shared-assets";
    UINT64 mask=0xffffffffffffULL;
    struct pci_dev dev={.vendor=0x10de,.device=0x2684,.dev={.dma_mask=&mask,.coherent_dma_mask=0xfffffffffULL},
      .current_state=PCI_D0,.pm_cap=Gu.pm_cap,.refs=1};
    struct gpu_unlock_610_shared shared={0};
    int result=gpu_unlock_610_load_shared(&dev,&shared);
    if(!result){stage="firmware-transaction";result=gpu_unlock_610_unlock(&dev,&shared);}
    gpu_unlock_610_release_shared(&shared);
    /* Also covers failures before GuStarted, including a bus-master enable that did not read back. */
    EFI_STATUS contained=GuClearMaster();
    LOG("Transaction result=%d containment=%#llx safe=%u retained=%u started=%u quiesced=%u flrs=%u DMA_live=%u\n",
      result,(unsigned long long)contained,GuSafeToReturn(),Gu.retained,Gu.started,Gu.quiesced,Gu.flrs,Gu.dma_count);
    if(result||Gu.fault||EFI_ERROR(contained)||!GuSafeToReturn()){s=EFI_DEVICE_ERROR;goto failed;}
    BOOLEAN gop=FALSE;stage="display-reconnect";
    s=GuRelFinish(&gop);if(s!=EFI_SUCCESS||!gop||Gu.fault)goto failed;
    /* The final pre-Windows check runs after the log closes: this line is not proof of a Windows start. */
    LOG("TRANSACTION OK: hardware readbacks passed, DMA released, display driver reconnected; final pre-Windows check follows unlogged\n");
    GuBootNote("Unlocked. Starting Windows.\n");
    stage="close-log";s=GuBootClose();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    stage="final-verification";if(!GuRelReadyForWindows(TRUE)){s=EFI_NOT_READY;goto failed;}
    FreeAssets();stage="start-windows";s=GuBootWindows();
    /* A returned Windows image is not a successful handoff; never fall back to another attempt. */
    GuBootHalt();return s==EFI_SUCCESS?EFI_ABORTED:s;
  }
#endif
skip:
  /* Preflight refused before anything was released or written. */
  if(s==EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  LOG("SKIP: stage=%s status=%#llx fault=%u\n",stage,(unsigned long long)s,Gu.fault);
  return StockWindows(stage,s);
failed:
  if(s==EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  LOG("STOP: stage=%s status=%#llx released=%u started=%u mmio_writes=%u retained=%u fault=%u\n",stage,(unsigned long long)s,
    GuRelMutated(),Gu.started,Gu.mmio_writes,Gu.retained,Gu.fault);
  if(Gu.started||Gu.mmio_writes||Gu.retained||Gu.allocations||Gu.master_attributes_pending) {
    /* The GPU was written: containment only, no recovery, no Windows. */
    if(Gu.started||Gu.master_attributes_pending)(VOID)GuClearMaster();
    (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
  }
  if(GuRelMutated()) {
    /* Display released, GPU untouched: put the driver back, verify, then stock Windows. */
    BOOLEAN gop=FALSE;EFI_STATUS r=GuRelRecover(&gop);
    LOG("recovery: status=%#llx gop=%u\n",(unsigned long long)r,gop);
    if(r==EFI_SUCCESS&&gop&&!Gu.fault) {
      (VOID)GuBootClose();
      if(GuRelReadyForWindows(FALSE)){GuBootNote("Display restored. Starting Windows without the mod.\n");return StockWindows(NULL,0);}
    }
    (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
  }
  return StockWindows(stage,s);
}
