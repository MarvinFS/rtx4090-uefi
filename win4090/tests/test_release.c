/* SPDX-License-Identifier: GPL-2.0-only */
/* Host tests for release.c (display-driver handoff) against a fake UEFI core on the surveyed
 * X870E / RTX 4090 topology. release.c is #included so Plan can be reset between scenarios.
 * Nothing here touches hardware: the "GPU" is a PciIo mock; every config/MMIO write is counted
 * and must stay at zero (the handoff never writes the GPU). */
#include "../port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#undef EPROTO
#undef EBADMSG
#undef EOVERFLOW
#undef EOPNOTSUPP
#undef ETIMEDOUT
#undef EALREADY
#pragma GCC diagnostic ignored "-Wunused-function"
#include "fake_efi_graph.h"

/* ---- stubs for boot_io.c / profile.c ---- */
static BOOLEAN log_open,verbose,cold_result;
static int silenced,silence_calls,unsilence_calls,cold_calls,cold_fail_on_call;
BOOLEAN GuBootLogOpen(VOID){return log_open;}
VOID GuBootSilence(VOID){silenced=1;silence_calls++;trace_ev(EV_SILENCE);}
VOID GuBootUnsilence(VOID){silenced=0;unsilence_calls++;trace_ev(EV_UNSILENCE);}
VOID GuLog(const char *f,...){
  trace_ev(EV_LOG);
  if(verbose){va_list a;va_start(a,f);vfprintf(stderr,f,a);va_end(a);}
}
VOID GuLogText(const char *t){trace_ev(EV_LOG);if(verbose)fputs(t,stderr);}
#include "../release.c"
BOOLEAN GuAd104ColdEntry(VOID){cold_calls++;return cold_result&&cold_calls!=cold_fail_on_call;}

static unsigned Checks,Scenarios;static const char *Name;
#define CHECK(x) do{Checks++;if(!(x)){fprintf(stderr,"FAIL [%s] line %d: %s\n",Name,__LINE__,#x);exit(1);}}while(0)

/* ---- scenario setup ---- */
static void gu_setup(void){
  GuInit(&FgBs,&Pc.io);
  Gu.image=H(H_IMG);Gu.handle=H(H_GPU);Gu.seg=0;Gu.bus=1;Gu.dev=0;Gu.fun=0;Gu.located=TRUE;
  Gu.bar_base=0xdd000000ULL;Gu.bar_size=0x1000000ULL;
  Gu.pm_cap=0x60;Gu.pcie_cap=0x78;Gu.pcie_version=2;Gu.msi_cap=0x68;Gu.msix_cap=0;
  Gu.profile_verified=Gu.prom_verified=TRUE;
}
static void begin(const char *name){
  Name=name;Scenarios++;
  fg_defaults();pc_init();fg_build();memset(&Plan,0,sizeof(Plan));
  log_open=TRUE;silenced=0;silence_calls=unsilence_calls=0;cold_calls=0;cold_fail_on_call=0;cold_result=TRUE;
  gu_setup();
}
/* Change the topology after begin() (config switches in Fg.cfg). */
static void rebuild(void){fg_build();Gu.handle=H(H_GPU);}
static EFI_STATUS pre(void){return GuRelPreflight(&FgSt,H(H_IMG),H(H_GPU));}

/* ---- observations of the fake ---- */
static int pci_opens(UINT32 mask){
  FG_PROTO *p=fg_find(H(H_GPU),&G_PCI);int n=0;
  for(int i=0;i<p->nopens;i++)if(p->opens[i].attr&mask)n++;
  return n;
}
static BOOLEAN claim_held(void){
  FG_PROTO *p=fg_find(H(H_GPU),&G_PCI);
  for(int i=0;i<p->nopens;i++)
    if(p->opens[i].attr==EFI_OPEN_PROTOCOL_EXCLUSIVE)
      return p->opens[i].agent==H(H_IMG)&&!p->opens[i].controller&&p->opens[i].count==1&&pci_opens(EFI_OPEN_PROTOCOL_EXCLUSIVE)==1&&!pci_opens(EFI_OPEN_PROTOCOL_BY_DRIVER);
  return FALSE;
}
static int gop_under_gpu(void){
  int n=0,len=dp_prefix(Fg.path[H_GPU]);
  for(int i=0;i<H_COUNT;i++){
    FG_PROTO *g=fg_find(H(i),&G_GOP);
    if(!g||!fg_find(H(i),&G_PATH)||!((EFI_GRAPHICS_OUTPUT_PROTOCOL *)g->iface)->Mode->FrameBufferSize)continue;
    if(dp_prefix(Fg.path[i])>len&&!memcmp(Fg.path[i],Fg.path[H_GPU],(size_t)len))n++;
  }
  return n;
}
static BOOLEAN no_writes_issued(void){return !Pc.pci_writes&&!Pc.mem_writes&&!Pc.bm_enables&&!Gu.mmio_writes;}
static BOOLEAN no_gpu_writes(void){return no_writes_issued()&&!(cmd()&4);}
static BOOLEAN tpl_ok(void){return !Fg.tpl_violations&&Fg.tpl==(int)TPL_APPLICATION;}
/* No logging callback between the last owner snapshot and DisconnectController. */
static void quiet_window(void){
  int d=-1,p=-1;
  for(int i=0;i<Fg.ntrace;i++)if(Fg.trace[i]==EV_DISCONNECT)d=i;
  CHECK(d>0);
  for(int i=d-1;i>=0;i--)if(Fg.trace[i]==EV_PPH){p=i;break;}
  CHECK(p>0);
  for(int i=p;i<d;i++)CHECK(Fg.trace[i]!=EV_LOG);
  BOOLEAN intent=FALSE;for(int i=0;i<p;i++)if(Fg.trace[i]==EV_LOG)intent=TRUE;
  CHECK(intent); /* durable intent logged before the window opens */
}
/* A refused preflight changes nothing and leaves release refusing too. */
static void refused_clean(EFI_STATUS got,EFI_STATUS want){
  if(got!=want)fprintf(stderr,"status mismatch [%s]: got %#llx want %#llx\n",Name,(unsigned long long)got,(unsigned long long)want);
  CHECK(got==want);
  CHECK(!Plan.ready&&!Plan.mutated&&Fg.disconnect_calls==0&&Fg.connect_calls==0&&Fg.exclusive_opens==0&&Fg.close_calls==0);
  CHECK(!Gu.owned&&!GuRelMutated()&&!silenced&&no_writes_issued()&&tpl_ok());
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==0&&!silenced);
}
/* The tail common to every healthy path: put the driver back and check the card is ready for Windows. */
static void recover_and_ready(int children){
  BOOLEAN gop=FALSE;
  CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop);
  CHECK(!Gu.owned&&!claim_held()&&pci_opens(EFI_OPEN_PROTOCOL_EXCLUSIVE)==0);
  CHECK(Fg.driver_bound&&pci_opens(EFI_OPEN_PROTOCOL_BY_DRIVER)==1&&gop_under_gpu()==children);
  CHECK(!silenced&&unsilence_calls==1);
  CHECK(!GuRelReleased());
  CHECK(GuRelReadyForWindows(FALSE));
  CHECK(no_gpu_writes()&&tpl_ok());
}

/* ================= healthy paths ================= */
static void s_release_test(int clear){
  begin(clear?"release test, driver Stop clears memory decode":"release test, decode kept");
  Fg.stop_clears_decode=clear;
  CHECK(pre()==EFI_SUCCESS);
  CHECK(Plan.ready&&Plan.owner.agent==H(H_NV)&&Plan.owner.image==H(H_NV)&&Plan.owner.child_count==1);
  CHECK(Plan.owner.version==0x70009&&Plan.owner.size==161696&&Plan.owner.base==(VOID *)FgNvCode);
  CHECK(Plan.owner.children[0].width==1920&&Plan.owner.children[0].height==1080&&Plan.owner.children[0].stride==2048);
  CHECK(Plan.owner.children[0].format==PixelBlueGreenRedReserved8BitPerColor&&Plan.owner.children[0].framebuffer==0xf000000000ULL&&Plan.owner.children[0].framebuffer_size==0x870000);
  CHECK(Plan.attributes==0x300&&(Plan.config[1]&0xffffU)==2); /* surveyed: cached IO|MEM, command MEM only */
  CHECK(!silenced&&Fg.disconnect_calls==0&&Fg.exclusive_opens==0&&Fg.driver_bound&&!Gu.owned&&!GuRelMutated());
  CHECK(no_gpu_writes()&&gop_under_gpu()==1&&tpl_ok()&&!GuRelReleased());
  CHECK(GuRelRelease()==EFI_SUCCESS);
  CHECK(Fg.disconnect_calls==1&&Fg.disc_ctrl==H(H_GPU)&&Fg.disc_drv==H(H_NV)&&Fg.disc_child==NULL);
  CHECK(silenced&&Gu.owned&&GuRelMutated()&&claim_held()&&!Fg.driver_bound&&gop_under_gpu()==0);
  CHECK(cold_calls==2&&Fg.exclusive_opens==1);
  CHECK(Fg.excl_section>0&&Fg.excl_section==Fg.excl_pph_section); /* snapshot and claim in one raised-TPL section */
  CHECK(tpl_ok());
  quiet_window();
  if(clear)CHECK(Pc.enable_n==1&&Pc.enable_args[0]==EFI_PCI_IO_ATTRIBUTE_MEMORY); /* memory only: never IO, never bus master */
  else CHECK(Pc.enable_n==0);
  CHECK(cmd()==2&&(Pc.attrs&(EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER))==EFI_PCI_IO_ATTRIBUTE_MEMORY);
  CHECK(no_gpu_writes()&&GuRelReleased());
  recover_and_ready(1);
  CHECK(Fg.connect_calls==1&&Fg.close_calls==1&&Fg.conn_ctrl==H(H_GPU)&&Fg.conn_drv[0]==H(H_NV)&&Fg.conn_drv[1]==NULL&&Fg.conn_recursive);
  CHECK(cmd()==2);
}
static void s_io_decode(void){
  begin("original command had IO and MEM decode: both restored, nothing else");
  cfg_set(1,0x00100003);Fg.stop_clears_decode=TRUE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  CHECK(Pc.enable_n==1&&Pc.enable_args[0]==(EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY)&&cmd()==3&&!(cmd()&4));
  recover_and_ready(1);
}
static void s_post_transaction(int devctl,int clear){
  begin(devctl?"post-transaction finish, driver Start rewrites device control":"post-transaction finish");
  Fg.stop_clears_decode=clear;Fg.start_rewrites_devctl=devctl;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  /* what the transaction leaves behind (SS0 88888888, SS1 8, PLM open, READOUT_1 0; one FLR, quiesced) */
  GuStarted();GuQuiesced();Gu.flrs=1;regs_unlocked();
  CHECK(GuSafeToReturn());
  BOOLEAN gop=FALSE;
  CHECK(GuRelFinish(&gop)==EFI_SUCCESS&&gop);
  CHECK(!Gu.owned&&!claim_held()&&Fg.driver_bound&&gop_under_gpu()==1&&Fg.connect_calls==1&&!silenced&&unsilence_calls==1);
  CHECK(Fg.conn_drv[0]==H(H_NV)&&Fg.conn_drv[1]==NULL&&Fg.conn_recursive);
  CHECK(Pc.pci_writes==0&&Pc.mem_writes==0&&Pc.bm_enables==0&&!(cmd()&4)&&tpl_ok());
  CHECK(GuRelReadyForWindows(TRUE)&&!GuRelReadyForWindows(FALSE));
  /* the final gate is exactly the Linux-verified state */
  reg_set(0x823820,5);CHECK(!GuRelReadyForWindows(TRUE));regs_unlocked();
  reg_set(0x82381c,0x15732127);CHECK(!GuRelReadyForWindows(TRUE));regs_unlocked();
  reg_set(0x823804,0xffffff8f);CHECK(!GuRelReadyForWindows(TRUE));regs_unlocked();
  reg_set(0x823818,0x410080);CHECK(!GuRelReadyForWindows(TRUE));regs_unlocked();
  CHECK(GuRelReadyForWindows(TRUE));
  CHECK(GuRelRecover(&gop)==EFI_NOT_READY&&!gop); /* no recovery once the GPU was started */
}
static void s_no_driver(void){
  begin("no firmware driver, no display children, display on the iGPU");
  Fg.cfg.driver=0;Fg.cfg.nchild=0;Fg.cfg.igpu_gop=1;rebuild();
  CHECK(!Fg.driver_bound&&pci_opens(EFI_OPEN_PROTOCOL_BY_DRIVER)==0);
  CHECK(pre()==EFI_SUCCESS&&Plan.owner.agent==NULL&&Plan.owner.child_count==0);
  CHECK(GuRelRelease()==EFI_SUCCESS);
  CHECK(Fg.disconnect_calls==0&&GuRelMutated()&&Gu.owned&&claim_held()&&Pc.enable_n==0&&cold_calls==2&&silenced);
  CHECK(GuRelReleased());
  BOOLEAN gop=FALSE;
  CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop);
  CHECK(Fg.connect_calls==0&&!Gu.owned&&!claim_held()&&!Fg.driver_bound&&!silenced&&gop_under_gpu()==0);
  CHECK(GuRelReadyForWindows(FALSE)&&no_gpu_writes()&&tpl_ok());
}
static void s_two_children(void){
  begin("two display children");
  Fg.cfg.nchild=2;rebuild();
  CHECK(pre()==EFI_SUCCESS&&Plan.owner.child_count==2&&gop_under_gpu()==2);
  CHECK(Plan.owner.children[0].framebuffer!=Plan.owner.children[1].framebuffer);
  CHECK(GuRelRelease()==EFI_SUCCESS&&gop_under_gpu()==0&&Fg.disconnect_calls==1&&claim_held());
  recover_and_ready(2);
}
static void s_any_slot(void){
  begin("any slot, any BAR0 address, any subsystem");
  Pc.bus=0x41;Pc.dev=3;Pc.fun=0;Pc.bar_base=0xfc000000ULL;cfg_set(4,0xfc000000U);cfg_set(11,0x40b110de);cfg_set(2,0x030000a2);
  Gu.bus=0x41;Gu.dev=3;Gu.fun=0;Gu.bar_base=0xfc000000ULL;Fg.cfg.root_dev=3;Fg.cfg.root_fn=1;rebuild();
  CHECK(pre()==EFI_SUCCESS&&Plan.owner.child_count==1&&GuRelRelease()==EFI_SUCCESS&&claim_held());
  recover_and_ready(1);
}
static void s_other_gop(int mode){
  begin(mode==1?"a GOP on another device is ignored":"a blt-only GOP on another device is ignored");
  Fg.cfg.igpu_gop=mode;rebuild();
  CHECK(pre()==EFI_SUCCESS&&Plan.owner.child_count==1);
  CHECK(GuRelRelease()==EFI_SUCCESS&&gop_under_gpu()==0);
  recover_and_ready(1);
}
static void s_volatile_status(void){
  begin("volatile status bits are ignored");
  Fg.stop_perturbs_status=TRUE;
  CHECK(pre()==EFI_SUCCESS);
  cfg_set(1,cfg_get(1)|0x40000000U);cfg_set(32,cfg_get(32)|0x00080000U); /* RW1C error bits move between preflight and release */
  CHECK(GuRelRelease()==EFI_SUCCESS&&GuRelReleased());
  recover_and_ready(1);
}
/* ---- firmware bus master (the Linux module's "claim: cleared firmware BME") ---- */
static void s_bme_entry(int cached,int clear,int finish){
  static char name[160];
  snprintf(name,sizeof(name),"BME set at entry%s%s, %s",cached?" (and cached)":"",clear?", driver Stop clears decode":"",finish?"release -> Finish":"release -> Recover");
  begin(name);
  cfg_set(1,0x00100006U);if(cached)Pc.attrs=0x700;Fg.stop_clears_decode=clear;
  CHECK(pre()==EFI_SUCCESS&&Plan.bme_entry&&(Plan.config[1]&4)&&Plan.attributes==(cached?0x700ULL:0x300ULL));
  CHECK(GuRelRelease()==EFI_SUCCESS);
  CHECK(Pc.disable_n==1&&Pc.disable_args[0]==EFI_PCI_IO_ATTRIBUTE_BUS_MASTER); /* bus master only, once */
  CHECK(cmd()==2&&!(Pc.attrs&EFI_PCI_IO_ATTRIBUTE_BUS_MASTER)&&(Pc.attrs&EFI_PCI_IO_ATTRIBUTE_MEMORY));
  CHECK(!(Plan.config[1]&4)&&!(Plan.attributes&EFI_PCI_IO_ATTRIBUTE_BUS_MASTER)); /* baselines are BME-clear now */
  if(clear)CHECK(Pc.enable_n==1&&Pc.enable_args[0]==EFI_PCI_IO_ATTRIBUTE_MEMORY); else CHECK(Pc.enable_n==0);
  CHECK(Fg.disconnect_calls==1&&claim_held()&&GuRelReleased()&&no_gpu_writes()&&tpl_ok());
  if(!finish){recover_and_ready(1);return;}
  GuStarted();GuQuiesced();Gu.flrs=1;regs_unlocked();
  BOOLEAN gop=FALSE;
  CHECK(GuRelFinish(&gop)==EFI_SUCCESS&&gop&&GuRelReadyForWindows(TRUE)&&no_gpu_writes()&&cmd()==2);
}
static void s_bme_no_driver(void){
  begin("BME set at entry, no firmware driver bound");
  Fg.cfg.driver=0;Fg.cfg.nchild=0;rebuild();cfg_set(1,0x00100006U);
  CHECK(pre()==EFI_SUCCESS&&Plan.bme_entry&&Plan.owner.agent==NULL);
  CHECK(GuRelRelease()==EFI_SUCCESS&&Fg.disconnect_calls==0);
  CHECK(Pc.disable_n==1&&Pc.disable_args[0]==EFI_PCI_IO_ATTRIBUTE_BUS_MASTER&&cmd()==2&&claim_held());
  { BOOLEAN gop=FALSE;
    CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop&&Fg.connect_calls==0&&!Gu.owned&&!claim_held()&&!Fg.driver_bound);
    CHECK(GuRelReadyForWindows(FALSE)&&no_gpu_writes()&&tpl_ok()); }
}
static void s_bme_cached_only(void){
  begin("only the cached attribute has bus master");
  Pc.attrs=0x700;
  CHECK(pre()==EFI_SUCCESS&&!Plan.bme_entry&&Plan.attributes==0x700);
  CHECK(GuRelRelease()==EFI_SUCCESS&&Pc.disable_n==1&&!(Pc.attrs&EFI_PCI_IO_ATTRIBUTE_BUS_MASTER)&&cmd()==2);
  recover_and_ready(1);
  begin("no bus master anywhere: nothing is cleared");
  CHECK(pre()==EFI_SUCCESS&&!Plan.bme_entry&&GuRelRelease()==EFI_SUCCESS&&Pc.disable_n==0&&Pc.enable_n==0);
  recover_and_ready(1);
  begin("BME at entry but already clear by release time: nothing to clear, baselines clear");
  cfg_set(1,0x00100006U);
  CHECK(pre()==EFI_SUCCESS&&Plan.bme_entry);
  cmd_set(2); /* firmware or another agent cleared it */
  CHECK(GuRelRelease()==EFI_SUCCESS&&Pc.disable_n==0&&!(Plan.config[1]&4)&&cmd()==2);
  recover_and_ready(1);
}
static void s_bme_failures(void){
  begin("BME clear: Attributes(Disable) fails");
  cfg_set(1,0x00100006U);Fg.disable_status=EFI_DEVICE_ERROR;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_DEVICE_ERROR);
  CHECK(Pc.disable_n==1&&Gu.owned&&claim_held()&&GuRelMutated()&&(cmd()&4)&&(Plan.config[1]&4)&&Plan.bme_entry);
  CHECK(Pc.pci_writes==0&&Pc.mem_writes==0&&Pc.bm_enables==0&&Gu.mmio_writes==0);
  Fg.disable_status=EFI_SUCCESS;
  { BOOLEAN gop=FALSE; /* the caller recovers: the driver goes back, bus master as the firmware had it */
    CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop&&Fg.driver_bound&&!Gu.owned&&GuRelReadyForWindows(FALSE)&&(cmd()&4)); }
  begin("BME clear: Disable reports success but bus master is still set");
  cfg_set(1,0x00100006U);Fg.disable_noop=TRUE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_COMPROMISED_DATA);
  CHECK(Pc.disable_n==1&&Gu.owned&&claim_held()&&(cmd()&4)&&(Plan.config[1]&4));
  { BOOLEAN gop=FALSE;CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop&&GuRelReadyForWindows(FALSE)); }
  begin("BME clear: command register cleared but the cached attribute still has bus master");
  cfg_set(1,0x00100006U);Pc.attrs=0x700;Fg.disable_keeps_cached_bm=TRUE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_COMPROMISED_DATA&&Gu.owned&&Pc.disable_n==1&&Plan.attributes==0x700);
  begin("BME clear: another config dword changes during the clear");
  cfg_set(1,0x00100006U);Fg.disable_moves_dword=15;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_COMPROMISED_DATA&&Gu.owned&&claim_held()&&Pc.disable_n==1&&!(cmd()&4));
  { BOOLEAN gop=FALSE;CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop); }
  begin("BME clear: a BAR dword changes during the clear");
  cfg_set(1,0x00100006U);Fg.disable_moves_dword=6;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_COMPROMISED_DATA&&Gu.owned);
  { BOOLEAN gop=FALSE;CHECK(GuRelRecover(&gop)==EFI_NOT_READY&&gop&&!GuRelReadyForWindows(FALSE)); } /* BAR change: caller halts */
}
static void s_bme_driver_reconnect(void){
  begin("driver sets BME again on reconnect, firmware had it at entry: accepted (Recover)");
  cfg_set(1,0x00100006U);Fg.start_sets_bme=TRUE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS&&cmd()==2);
  { BOOLEAN gop=FALSE;
    CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop&&(cmd()&4)&&GuRelReadyForWindows(FALSE));
    CHECK(Pc.pci_writes==0&&Pc.mem_writes==0&&Pc.bm_enables==0&&Gu.mmio_writes==0&&Pc.disable_n==1); }
  begin("driver sets BME again on reconnect, firmware had it at entry: accepted (Finish)");
  cfg_set(1,0x00100006U);Fg.start_sets_bme=TRUE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  GuStarted();GuQuiesced();Gu.flrs=1;regs_unlocked();
  { BOOLEAN gop=FALSE;CHECK(GuRelFinish(&gop)==EFI_SUCCESS&&gop&&(cmd()&4)&&GuRelReadyForWindows(TRUE)); }
  begin("driver sets BME on reconnect, firmware did not have it at entry: refused");
  Fg.start_sets_bme=TRUE;
  CHECK(pre()==EFI_SUCCESS&&!Plan.bme_entry&&GuRelRelease()==EFI_SUCCESS);
  { BOOLEAN gop=FALSE;CHECK(GuRelRecover(&gop)==EFI_NOT_READY&&gop&&!GuRelReadyForWindows(FALSE)); }
  begin("driver sets BME on reconnect, no BME at entry, Finish: refused");
  Fg.start_sets_bme=TRUE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  GuStarted();GuQuiesced();Gu.flrs=1;regs_unlocked();
  { BOOLEAN gop=FALSE;CHECK(GuRelFinish(&gop)==EFI_NOT_READY&&gop&&!GuRelReadyForWindows(TRUE)); }
  begin("ReadyForWindows: BME behind our back is judged by the entry state");
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  { BOOLEAN gop=FALSE;CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop&&GuRelReadyForWindows(FALSE)); }
  cmd_set(cmd()|4U);CHECK(!GuRelReadyForWindows(FALSE));
  begin("ReadyForWindows: BME tolerated when the firmware had it");
  cfg_set(1,0x00100006U);
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  { BOOLEAN gop=FALSE;CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop); }
  cmd_set(cmd()|4U);CHECK(GuRelReadyForWindows(FALSE));
  begin("the driver's Stop sets bus master although the firmware had none: cleared at the claim");
  Fg.stop_sets_bme=TRUE;
  CHECK(pre()==EFI_SUCCESS&&!Plan.bme_entry&&GuRelRelease()==EFI_SUCCESS&&Pc.disable_n==1&&cmd()==2);
  recover_and_ready(1);
  begin("bus master appears between preflight and release (not at entry): refused before the stop");
  CHECK(pre()==EFI_SUCCESS);cmd_set(cmd()|4U);
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==0&&!GuRelMutated()&&!silenced&&!Gu.owned&&Pc.disable_n==0);
}
static void s_released_check(void){
  begin("GuRelReleased tracks the released state");
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS&&GuRelReleased());
  fg_child(1,ChildAdr[1]);CHECK(!GuRelReleased()); /* a GOP of ours came back while claimed */
  begin("GuRelReleased: console replaced");
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS&&GuRelReleased());
  FgSt.ConOut=&FgOutOther;CHECK(!GuRelReleased());
  begin("GuRelReleased: config drift");
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS&&GuRelReleased());
  cfg_set(6,cfg_get(6)^1U);CHECK(!GuRelReleased());
  begin("GuRelReleased: fault latch");
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS&&GuRelReleased());
  Gu.fault=TRUE;CHECK(!GuRelReleased());
}

/* ================= refusals before any release ================= */
static void s_refuse_preflight(void){
  begin("refuse: a second BY_DRIVER holder on PciIo");
  fg_open(H(H_GPU),&G_PCI,H(H_OTHER),H(H_GPU),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  refused_clean(pre(),EFI_ACCESS_DENIED);
  begin("refuse: an EXCLUSIVE opener present at preflight");
  fg_open(H(H_GPU),&G_PCI,H(H_OTHER),NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE,1);
  refused_clean(pre(),EFI_ACCESS_DENIED);
  begin("refuse: unrelated non-passive record (driver bound)");
  fg_open(H(H_GPU),&G_PATH,H(H_OTHER),H(H_GPU),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  refused_clean(pre(),EFI_ACCESS_DENIED);
  begin("refuse: unrelated non-passive record (no driver bound)");
  Fg.cfg.driver=0;Fg.cfg.nchild=0;rebuild();
  fg_open(H(H_GPU),&G_PATH,H(H_OTHER),H(H_GPU),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  refused_clean(pre(),EFI_ACCESS_DENIED);
  begin("refuse: PciIo BY_DRIVER holder with OpenCount 2");
  fg_open(H(H_GPU),&G_PCI,H(H_NV),H(H_GPU),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  refused_clean(pre(),EFI_ACCESS_DENIED);
  begin("refuse: the driver agent is the console handle");
  { /* a pathological firmware where the BY_DRIVER agent is the console */
    FG_PROTO *p=fg_find(H(H_GPU),&G_PCI);fg_remove_opens(p,H(H_NV),EFI_OPEN_PROTOCOL_BY_DRIVER);
    fg_open(H(H_GPU),&G_PCI,H(H_CON),H(H_GPU),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  }
  refused_clean(pre(),EFI_ACCESS_DENIED);
  begin("refuse: BY_CHILD child that is not a GOP");
  Fg.cfg.nchild=0;rebuild();
  { int i=fg_free_slot();memset(&Fg.h[i],0,sizeof(Fg.h[i]));
    int len=dp_prefix(Fg.path[H_GPU]);memcpy(Fg.path[i],Fg.path[H_GPU],(size_t)len);dp_end(Fg.path[i],dp_adr(Fg.path[i],len,0x80010320U));
    fg_add_proto(i,&G_PATH,Fg.path[i]);fg_open(H(H_GPU),&G_PATH,H(H_NV),H(i),EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER,1); }
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: BY_CHILD GOP whose path does not descend from the GPU");
  Fg.cfg.nchild=0;Fg.cfg.igpu_gop=1;rebuild();
  fg_open(H(H_GPU),&G_PATH,H(H_NV),H(H_IGOP),EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER,1);
  refused_clean(pre(),EFI_ACCESS_DENIED);
  begin("refuse: BY_CHILD GOP with an insane mode");
  Fg.cfg.nchild=1;rebuild();FgInfo[0].PixelsPerScanLine=100; /* narrower than the width */
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: BY_CHILD record names the GPU itself");
  Fg.cfg.nchild=0;rebuild();fg_open(H(H_GPU),&G_PATH,H(H_NV),H(H_GPU),EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER,1);
  refused_clean(pre(),EFI_ACCESS_DENIED);
  begin("refuse: memory decode not cached in the PciIo attributes");
  Pc.attrs=0x100;
  refused_clean(pre(),EFI_NOT_READY);
  begin("refuse: memory decode off in the command register");
  cfg_set(1,0x00100000U);
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: wrong device id");
  cfg_set(0,0x268510deU);
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: not a display class device");
  cfg_set(2,0x060000a1U);
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: discovery slot differs from where the card answers");
  Gu.bus=7;
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: BAR0 is not 16 MiB");
  Pc.bar_len=0x2000000ULL;
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: BAR0 base differs from the discovered one");
  Pc.bar_base=0xde000000ULL;
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: discovery did not locate the card");
  Gu.located=FALSE;
  refused_clean(pre(),EFI_INVALID_PARAMETER);
  begin("refuse: no console input");
  FgSt.ConIn=NULL;
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("refuse: console handles missing");
  FgSt.ConsoleOutHandle=NULL;
  refused_clean(pre(),EFI_UNSUPPORTED);
  begin("preflight twice");
  CHECK(pre()==EFI_SUCCESS&&pre()==EFI_ALREADY_STARTED);
}
static void s_release_preconditions(void){
  begin("release: preconditions");
  CHECK(GuRelRelease()==EFI_NOT_READY); /* no preflight */
  CHECK(pre()==EFI_SUCCESS);
  Gu.profile_verified=FALSE;CHECK(GuRelRelease()==EFI_NOT_READY);Gu.profile_verified=TRUE;
  Gu.prom_verified=FALSE;CHECK(GuRelRelease()==EFI_NOT_READY);Gu.prom_verified=TRUE;
  Gu.fault=TRUE;CHECK(GuRelRelease()==EFI_NOT_READY);Gu.fault=FALSE;
  Gu.started=TRUE;CHECK(GuRelRelease()==EFI_NOT_READY);Gu.started=FALSE;
  CHECK(Fg.disconnect_calls==0&&!GuRelMutated()&&silence_calls==0);
  begin("release: log closed");
  CHECK(pre()==EFI_SUCCESS);log_open=FALSE;
  CHECK(GuRelRelease()==EFI_DEVICE_ERROR&&Fg.disconnect_calls==0&&!GuRelMutated()&&!silenced&&!Gu.owned);
  begin("release: cold entry fails before the release");
  CHECK(pre()==EFI_SUCCESS);cold_fail_on_call=1;
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==0&&!GuRelMutated()&&!silenced&&!Gu.owned&&Fg.exclusive_opens==0);
  begin("release: cold entry fails after the release (registers moved)");
  CHECK(pre()==EFI_SUCCESS);cold_fail_on_call=2;
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==1&&GuRelMutated()&&Gu.owned&&claim_held());
  recover_and_ready(1);
}
static void s_changed_before_release(void){
  begin("abort: another EXCLUSIVE opener appears after preflight");
  CHECK(pre()==EFI_SUCCESS);fg_open(H(H_GPU),&G_PCI,H(H_OTHER),NULL,EFI_OPEN_PROTOCOL_EXCLUSIVE,1);
  CHECK(GuRelRelease()==EFI_ACCESS_DENIED&&Fg.disconnect_calls==0&&!GuRelMutated()&&!silenced&&!Gu.owned&&Plan.ready);
  begin("abort: driver image base changes after preflight");
  CHECK(pre()==EFI_SUCCESS);FgImage.ImageBase=FgNvCode+16;
  CHECK(GuRelRelease()==EFI_ABORTED&&Fg.disconnect_calls==0&&!GuRelMutated()&&!silenced&&!Gu.owned&&Fg.exclusive_opens==0);
  begin("abort: driver binding version changes after preflight");
  CHECK(pre()==EFI_SUCCESS);FgBind.Version=0x70008;
  CHECK(GuRelRelease()==EFI_ABORTED&&Fg.disconnect_calls==0&&!GuRelMutated());
  begin("abort: a second display child appears after preflight");
  CHECK(pre()==EFI_SUCCESS);fg_child(1,ChildAdr[1]);
  CHECK(GuRelRelease()==EFI_ABORTED&&Fg.disconnect_calls==0&&!GuRelMutated()&&!silenced);
  begin("abort: the display child's framebuffer moves after preflight");
  CHECK(pre()==EFI_SUCCESS);FgMode[0].FrameBufferBase+=0x1000;
  CHECK(GuRelRelease()==EFI_ABORTED&&Fg.disconnect_calls==0&&!GuRelMutated());
  begin("abort: console output replaced after preflight");
  CHECK(pre()==EFI_SUCCESS);FgSt.ConOut=&FgOutOther;
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==0&&!GuRelMutated()&&!silenced&&Plan.ready);
  begin("abort: console output handle replaced after preflight");
  CHECK(pre()==EFI_SUCCESS);FgSt.ConsoleOutHandle=H(H_OTHER);
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==0&&!GuRelMutated());
  begin("abort: key reader replaced after preflight");
  CHECK(pre()==EFI_SUCCESS);FgIn.ReadKeyStroke=fg_key_read2;
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==0&&!GuRelMutated());
  begin("abort: console input removed after preflight");
  CHECK(pre()==EFI_SUCCESS);FgSt.ConIn=NULL;
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==0&&!GuRelMutated());
  begin("abort: config changed after preflight");
  CHECK(pre()==EFI_SUCCESS);cfg_set(15,cfg_get(15)^1U);
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==0&&!GuRelMutated());
  begin("stray GOP under the GPU that no BY_CHILD record explains: refused after the stop, claim held");
  CHECK(pre()==EFI_SUCCESS);
  { int i=fg_free_slot();memset(&Fg.h[i],0,sizeof(Fg.h[i]));
    int len=dp_prefix(Fg.path[H_GPU]);memcpy(Fg.path[i],Fg.path[H_GPU],(size_t)len);dp_end(Fg.path[i],dp_adr(Fg.path[i],len,0x80010360U));
    fg_gop_data(5,0xf010000000ULL,0x870000);fg_add_proto(i,&G_PATH,Fg.path[i]);fg_add_proto(i,&G_GOP,&FgGop[5]); }
  CHECK(GuRelRelease()==EFI_NOT_READY&&Fg.disconnect_calls==1&&GuRelMutated()&&Gu.owned&&claim_held()&&!GuRelReleased());
}

/* ================= failures during and after the release ================= */
static void s_disconnect_failures(void){
  for(int clear=0;clear<2;clear++){
    begin(clear?"DisconnectController fails, no change, decode cleared elsewhere":"DisconnectController fails, no change");
    Fg.stop_clears_decode=clear;Fg.disc_mode=DISC_ERROR_NOCHANGE;
    CHECK(pre()==EFI_SUCCESS);
    CHECK(GuRelRelease()==EFI_DEVICE_ERROR);
    CHECK(Fg.disconnect_calls==1&&GuRelMutated()&&!Gu.owned&&Fg.exclusive_opens==0&&Fg.driver_bound&&silenced&&Gu.pci==&Pc.io);
    recover_and_ready(1); /* the caller can continue to stock Windows */
    CHECK(Fg.connect_calls==1&&Fg.close_calls==0);
  }
  for(int clear=0;clear<2;clear++){
    begin(clear?"DisconnectController fails after a partial stop (decode cleared)":"DisconnectController fails after a partial stop");
    Fg.stop_clears_decode=clear;Fg.disc_mode=DISC_ERROR_PARTIAL;
    CHECK(pre()==EFI_SUCCESS);
    CHECK(GuRelRelease()==EFI_DEVICE_ERROR);
    CHECK(Fg.disconnect_calls==1&&GuRelMutated()&&!Gu.owned&&Fg.exclusive_opens==0&&!Fg.driver_bound&&gop_under_gpu()==0&&silenced);
    recover_and_ready(1);
    CHECK(Fg.connect_calls==1&&cmd()==2);
  }
  begin("DisconnectController returns success but nothing stopped");
  Fg.disc_mode=DISC_SUCCESS_NOOP;
  CHECK(pre()==EFI_SUCCESS);
  CHECK(GuRelRelease()==EFI_ACCESS_DENIED);
  CHECK(Fg.disconnect_calls==1&&GuRelMutated()&&!Gu.owned&&Fg.exclusive_opens==0&&Fg.driver_bound&&pci_opens(EFI_OPEN_PROTOCOL_EXCLUSIVE)==0);
  recover_and_ready(1);
  begin("a leftover non-passive record on the GPU handle after the stop: claim refused");
  Fg.stop_leaves_record=TRUE;
  CHECK(pre()==EFI_SUCCESS);
  CHECK(GuRelRelease()==EFI_ACCESS_DENIED);
  CHECK(Fg.disconnect_calls==1&&GuRelMutated()&&!Gu.owned&&Fg.exclusive_opens==0&&pci_opens(EFI_OPEN_PROTOCOL_EXCLUSIVE)==0);
  recover_and_ready(1);
  begin("the driver replaces the PciIo interface when it stops");
  Fg.stop_replaces_pciio=TRUE;
  CHECK(pre()==EFI_SUCCESS);
  CHECK(GuRelRelease()==EFI_COMPROMISED_DATA&&!Gu.owned&&Fg.exclusive_opens==0&&Gu.pci==&Pc.io2);
}
static void s_drift_and_decode(void){
  begin("config drift beyond the decode bits after the stop: refused, claim held");
  Fg.stop_moves_dword=15;
  CHECK(pre()==EFI_SUCCESS);
  CHECK(GuRelRelease()==EFI_UNSUPPORTED);
  CHECK(Gu.owned&&claim_held()&&GuRelMutated()&&Pc.enable_n==0&&no_gpu_writes()&&!GuRelReleased());
  recover_and_ready(1); /* interrupt-line drift is not one of the compared fields once the driver is back */
  begin("BAR drift after the stop: refused, and recovery reports the BAR change");
  Fg.stop_moves_dword=6;
  CHECK(pre()==EFI_SUCCESS);
  CHECK(GuRelRelease()==EFI_UNSUPPORTED&&Gu.owned);
  { BOOLEAN gop=FALSE;
    CHECK(GuRelRecover(&gop)==EFI_NOT_READY&&gop); /* reconnected, but BARs differ: caller must halt */
    CHECK(!GuRelReadyForWindows(FALSE)); }
  begin("decode cannot be re-enabled: Attributes(Enable) fails");
  Fg.stop_clears_decode=TRUE;Fg.enable_status=EFI_DEVICE_ERROR;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_DEVICE_ERROR);
  CHECK(Pc.enable_n==1&&Gu.owned&&claim_held());
  Fg.enable_status=EFI_SUCCESS;
  recover_and_ready(1);
  begin("Attributes(Enable) claims success but changes nothing");
  Fg.stop_clears_decode=TRUE;Fg.enable_noop=TRUE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_COMPROMISED_DATA&&Pc.enable_n==1&&Gu.owned);
  begin("driver Start sets bus master: reconnect is refused");
  Fg.start_sets_bme=TRUE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  { BOOLEAN gop=FALSE;
    CHECK(GuRelRecover(&gop)==EFI_NOT_READY&&gop&&!GuRelReadyForWindows(FALSE));
    CHECK(Pc.pci_writes==0&&Pc.mem_writes==0&&Pc.bm_enables==0); }
}
static void s_reconnect_no_gop(void){
  begin("Recover: ConnectController succeeds but no GOP returns");
  Fg.connect_creates_children=FALSE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  { BOOLEAN gop=TRUE;
    CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&!gop);
    CHECK(Fg.driver_bound&&gop_under_gpu()==0&&silenced&&unsilence_calls==0&&!Gu.owned);
    CHECK(!GuRelReadyForWindows(FALSE)); }
  begin("Finish: ConnectController succeeds but no GOP returns");
  Fg.connect_creates_children=FALSE;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  GuStarted();GuQuiesced();Gu.flrs=1;regs_unlocked();
  { BOOLEAN gop=TRUE;
    CHECK(GuRelFinish(&gop)==EFI_SUCCESS&&!gop);
    CHECK(silenced&&unsilence_calls==0&&!GuRelReadyForWindows(TRUE)); }
  begin("Recover: ConnectController returns an error");
  Fg.connect_status=EFI_DEVICE_ERROR;
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  { BOOLEAN gop=TRUE;
    CHECK(GuRelRecover(&gop)==EFI_DEVICE_ERROR&&!gop&&!Gu.owned&&Fg.connect_calls==1&&!Fg.driver_bound&&silenced); }
  begin("Recover closes the claim before reconnecting (the driver cannot start under the exclusive open)");
  CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
  { BOOLEAN gop=FALSE;
    CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop&&Fg.close_calls==1&&Fg.driver_bound); }
}
static void s_recover_refused(void){
  for(int v=0;v<6;v++){
    static const char *names[]={"Recover refused after an MMIO write","Recover refused after an FLR","Recover refused once started",
      "Recover refused with DMA mapped","Recover refused with DMA allocations","Recover refused with bus master pending"};
    begin(names[v]);
    CHECK(pre()==EFI_SUCCESS&&GuRelRelease()==EFI_SUCCESS);
    switch(v){
      case 0:Gu.mmio_writes=1;break;case 1:Gu.flrs=1;break;case 2:Gu.started=TRUE;break;
      case 3:Gu.dma_count=1;break;case 4:Gu.allocations=(GU_DMA *)&FgDummy;break;default:Gu.master_attributes_pending=TRUE;
    }
    BOOLEAN gop=TRUE;
    CHECK(GuRelRecover(&gop)==EFI_NOT_READY&&!gop);
    CHECK(Fg.connect_calls==0&&Fg.close_calls==0&&Gu.owned&&claim_held()&&!Fg.driver_bound&&silenced);
  }
  begin("Recover refused before any release");
  { BOOLEAN gop=TRUE;CHECK(GuRelRecover(&gop)==EFI_NOT_READY&&!gop);CHECK(pre()==EFI_SUCCESS&&GuRelRecover(&gop)==EFI_NOT_READY&&!gop&&Fg.connect_calls==0); }
}
static void s_finish_preconditions(void){
  begin("Finish: preconditions and unreadable selectors");
  CHECK(pre()==EFI_SUCCESS);
  { BOOLEAN gop=TRUE;
    CHECK(GuRelFinish(&gop)==EFI_NOT_READY&&!gop); /* not released yet */
    CHECK(GuRelRelease()==EFI_SUCCESS);
    CHECK(GuRelFinish(&gop)==EFI_NOT_READY&&!gop&&Fg.connect_calls==0); /* nothing started */
    GuStarted();CHECK(GuRelFinish(&gop)==EFI_NOT_READY&&Fg.connect_calls==0); /* not quiesced */
    GuQuiesced();Gu.flrs=2;CHECK(GuRelFinish(&gop)==EFI_NOT_READY&&Fg.connect_calls==0);
    Gu.flrs=1;regs_unlocked();
    reg_set(0x82381c,0xffffffffU);CHECK(GuRelFinish(&gop)==EFI_DEVICE_ERROR&&Fg.connect_calls==0&&Gu.owned&&claim_held());
    reg_set(0x82381c,0xbadf5620U);CHECK(GuRelFinish(&gop)==EFI_DEVICE_ERROR&&Fg.connect_calls==0&&Gu.owned);
    reg_set(0x82381c,0x88888888U);reg_set(0x823820,0xffffffffU);CHECK(GuRelFinish(&gop)==EFI_DEVICE_ERROR&&Fg.connect_calls==0);
    regs_unlocked();
    log_open=FALSE;CHECK(GuRelFinish(&gop)==EFI_DEVICE_ERROR&&Fg.connect_calls==0&&Gu.owned);log_open=TRUE;
    Gu.fault=TRUE;CHECK(GuRelFinish(&gop)==EFI_NOT_READY&&Fg.connect_calls==0);Gu.fault=FALSE;
    CHECK(GuRelFinish(&gop)==EFI_SUCCESS&&gop&&Fg.connect_calls==1); }
}
static void s_ready_preconditions(void){
  begin("ReadyForWindows: preconditions");
  CHECK(!GuRelReadyForWindows(FALSE));
  CHECK(pre()==EFI_SUCCESS);CHECK(!GuRelReadyForWindows(FALSE)); /* not released */
  CHECK(GuRelRelease()==EFI_SUCCESS);CHECK(!GuRelReadyForWindows(FALSE)); /* claim still held */
  { BOOLEAN gop=FALSE;CHECK(GuRelRecover(&gop)==EFI_SUCCESS&&gop); }
  CHECK(GuRelReadyForWindows(FALSE));
  Gu.fault=TRUE;CHECK(!GuRelReadyForWindows(FALSE));Gu.fault=FALSE;
  FgSt.ConOut=&FgOutOther;CHECK(!GuRelReadyForWindows(FALSE));FgSt.ConOut=&FgOut;
  cfg_set(1,cfg_get(1)|4U);CHECK(!GuRelReadyForWindows(FALSE)); /* bus master set behind our back */
  cfg_set(1,cfg_get(1)&~4U);CHECK(GuRelReadyForWindows(FALSE));
  reg_set(0x823804,0xffffffffU);CHECK(!GuRelReadyForWindows(FALSE)); /* already open although no transaction ran */
  reg_set(0x823804,0xffffff8fU);
  FgSt.ConIn=NULL;CHECK(!GuRelReadyForWindows(FALSE));
}

int main(int argc,char **argv){
  verbose=argc>1&&!strcmp(argv[1],"-v");
  s_release_test(0);s_release_test(1);s_io_decode();
  s_post_transaction(0,0);s_post_transaction(1,1);
  s_bme_entry(0,0,0);s_bme_entry(1,0,0);s_bme_entry(0,1,0);s_bme_entry(1,1,0);s_bme_entry(0,0,1);s_bme_entry(1,1,1);
  s_bme_no_driver();s_bme_cached_only();s_bme_failures();s_bme_driver_reconnect();
  s_no_driver();s_two_children();s_any_slot();s_other_gop(1);s_other_gop(2);s_volatile_status();s_released_check();
  s_refuse_preflight();s_release_preconditions();s_changed_before_release();
  s_disconnect_failures();s_drift_and_decode();s_reconnect_no_gop();s_recover_refused();s_finish_preconditions();s_ready_preconditions();
  printf("release host tests: %u scenarios, %u checks PASS (fake UEFI core on the surveyed topology; no GPU write counted)\n",Scenarios,Checks);
  return 0;
}
