/* SPDX-License-Identifier: GPL-2.0-only */
/* Admission of the hardware-agnostic RTX 4090 port, on the real retained core against the fake engine.
 * The port applies the Linux module's checks and pins nothing but the GPU (10de:2684), the presence and exact size
 * of the five 610.43.03 asset files (their content is NOT hash-checked: the hardware authenticates the signed
 * images) and the card's own parsable VBIOS. Tables and groups:
 *   - GENERIC: other boards/cards that the Linux gate admits. The whole transaction must SUCCEED.
 *   - REFUSE:  states the Linux gate refuses (or drift since discovery). Zero GPU writes, nothing leaked.
 *   - ASSET CONTENT: a flipped byte in any asset is accepted (hashes are only logged).
 *   - ASSET STRUCTURE: wrong size, NULL data, swapped names stay refused with zero writes. */
#define main transaction_fixture_main
#include "test_efi_transaction.c"
#undef main

static struct pci_dev Device;
static UINT64 Mask;
static VOID defaults(void) {
  fx_defaults();test_entry_ss0=0x15732127;test_entry_ss1=5;
}
static VOID fresh(void (*pre)(void)) {
  defaults();fx.allow_prepare_failure=TRUE;if(pre)pre();
  setup(SUCCESS);
  Mask=0xffffffffffffULL;
  Device=(struct pci_dev){.vendor=0x10de,.device=0x2684,
    .dev={.dma_mask=&Mask,.coherent_dma_mask=0xfffffffffULL},.pm_cap=(int)Gu.pm_cap,.refs=1};
}
/* want: the exact negative errno the retained core must return (0 = any failure). The reason matters: a blanket
   failure elsewhere must not satisfy a refusal case. */
static VOID rejected(const char *name,struct gpu_unlock_610_shared *shared,int want) {
  int r=gpu_unlock_610_unlock(&Device,shared);
  CHECK(r!=0 && (!want || r==want));
  CHECK(!Gu.started && !fake.mmio_writes && !fake.gsp_starts && !fake.sec2_starts && !fake.flrs);
  CHECK(!fake.live_maps && !fake.live_buffers && !Gu.allocations && !fake.pool_live);
  CHECK(!fake.master_enables && !fake.pci_writes);printf("guard refused %s (%d) PASS\n",name,r);
}
/* ---- GENERIC: pre-discovery knobs for a different board / card -------------------------------------------- */
static VOID g_subsystem(void){fx.subsystem=0x40691043;}               /* another AIB subsystem */
static VOID g_slot_a(void){fx.bus=0x2a;fx.dev=3;fx.fun=0;}            /* behind another root port */
static VOID g_slot_b(void){fx.seg=1;fx.bus=0x40;fx.dev=0;fx.fun=0;}   /* another PCI segment */
static VOID g_bar0_a(void){fx.bar0=0xfc000000U;}
static VOID g_bar0_b(void){fx.bar0=0x84000000U;}                      /* the AD104 reference board's BAR0 */
static VOID g_bar_others(void){fx.cfg_xor_at=0x18;fx.cfg_xor=0x0f;}   /* BAR1 high dword moved: not checked */
static VOID g_bar3(void){fx.cfg_xor_at=0x20;fx.cfg_xor=0x01;}         /* BAR3 high dword moved */
static VOID g_ss_1(void){test_entry_ss1=1;}
static VOID g_ss_7(void){test_entry_ss1=7;}
static VOID g_ss_0(void){test_entry_ss0=0;test_entry_ss1=0;}
static VOID g_ss0_high(void){test_entry_ss0=0x9f732127;}              /* high nibbles, not 88888888 */
static VOID g_vram_48g(void){fx.fb_mib=0xbff4;}                       /* 48 GiB modified board */
static VOID g_vram_odd(void){fx.fb_mib=0x5ff0;}
static VOID g_vram_small(void){fx.fb_mib=0x3ff4;}                     /* 16 GiB, FB layout is top-relative */
static VOID g_gfw_plm(void){fx.gfw_plm=0x0001;}                       /* only level0 matters */
static VOID g_gfw_plm_b(void){fx.gfw_plm=0x9b8f;}
static VOID g_gfw_boot(void){fx.gfw_boot=0x00ff;}                     /* low byte 0xff = boot completed */
static VOID g_gfw_cpuctl(void){fx.gfw_cpuctl=0x50;}                   /* halted bit plus others */
static VOID g_feat_plm(void){fx.feat_plm=0xfffffe0f;}                 /* some other closed PLM */
static VOID g_readout(void){fx.readout0=0x1;fx.readout1=0x00a10040;}  /* other speed-select units */
static VOID g_wpr2_lo(void){fx.wpr2_lo=0;}
static VOID g_chip_rev(void){fx.chip=0x192000a0;fx.class_rev=0x030000a0;} /* another stepping */
static VOID g_prom_tail(void){fx.prom_xor_at=0xfffff;fx.prom_xor=0xff;}   /* VBIOS bytes outside FWSEC differ */
static VOID g_prom_fwsec_sig(void){fx.prom_xor_at=0x43e68;fx.prom_xor=0xff;} /* still parses; the fuse path owns the check */
static VOID g_bme_set(void){fx.cfg_xor_at=4;fx.cfg_xor=4;}            /* firmware left bus master on */
static VOID p_bme_clear(void){config[4]&=(UINT8)~4;}                  /* what release.c does after the claim */
/* Capability chains of other boards. The captured chain is PM 0x60 -> MSI 0x68 -> PCIe 0x78 -> vendor 0xb4; 0xe0..0xfc
   is free. GuCheckConfig only searches the list, so none of these may stop the transaction. */
static VOID cap_at(UINT8 *c,UINTN at,UINT8 id,UINT8 next,UINT16 control){c[at]=id;c[at+1]=next;c[at+2]=(UINT8)control;c[at+3]=(UINT8)(control>>8);}
static VOID p_unknown_tail(UINT8 *c){c[0xb5]=0xe0;cap_at(c,0xe0,0x09,0,0x1009);}      /* vendor capability after the last */
static VOID p_unknown_head(UINT8 *c){c[0x34]=0xe0;cap_at(c,0xe0,0x0a,0x60,0);}        /* unknown id at the head of the chain */
static VOID p_unknown_long(UINT8 *c){c[0xb6]=0x80;}                                  /* vendor capability 'longer' than the area, overlapping */
static VOID p_unknown_many(UINT8 *c){c[0xb5]=0xe0;cap_at(c,0xe0,0x10,0xe4,0);cap_at(c,0xe4,0x7f,0xe8,0xffff);cap_at(c,0xe8,0x09,0,0x0003);}
static VOID p_msi_on(UINT8 *c){c[0x6a]|=1;}                                          /* MSI enabled by the firmware */
static VOID p_msix_on(UINT8 *c){c[0xb5]=0xe0;cap_at(c,0xe0,0x11,0,0x8003);}           /* MSI-X present and enabled */
static VOID p_no_pm(UINT8 *c){c[0x34]=0x68;}                                         /* no power-management capability */
static VOID p_pcie3(UINT8 *c){c[0x7a]=(UINT8)((c[0x7a]&0xf0)|3);}                     /* PCIe capability version 3 */
static VOID p_pcie_port(UINT8 *c){c[0x7a]=(UINT8)((c[0x7a]&0x0f)|0x40);}              /* another device/port type */
static VOID p_rom_on(UINT8 *c){c[0x30]|=1;}                                          /* expansion ROM enabled */
static VOID p_duplicates(UINT8 *c) {                                                 /* later copies are never consulted */
  c[0xb5]=0xe0;
  cap_at(c,0xe0,0x01,0xe8,0);c[0xe4]=3;                                              /* second PM: D3 */
  cap_at(c,0xe8,0x10,0xf0,0x0002);c[0xec]=c[0xed]=c[0xee]=c[0xef]=0;                  /* second PCIe: no FLR */
  cap_at(c,0xf0,0x11,0xf4,0x0001);cap_at(c,0xf4,0x11,0,0x0002);                       /* two MSI-X: the first at 0xf0 wins */
}
static VOID g_cap_unknown_tail(void){fx.cfg_patch=p_unknown_tail;}
static VOID g_cap_unknown_head(void){fx.cfg_patch=p_unknown_head;}
static VOID g_cap_unknown_long(void){fx.cfg_patch=p_unknown_long;}
static VOID g_cap_unknown_many(void){fx.cfg_patch=p_unknown_many;}
static VOID g_cap_msi_on(void){fx.cfg_patch=p_msi_on;}
static VOID g_cap_msix_on(void){fx.cfg_patch=p_msix_on;}
static VOID g_cap_no_pm(void){fx.cfg_patch=p_no_pm;}
static VOID g_cap_pcie3(void){fx.cfg_patch=p_pcie3;}
static VOID g_cap_pcie_port(void){fx.cfg_patch=p_pcie_port;}
static VOID g_cap_rom_on(void){fx.cfg_patch=p_rom_on;}
static VOID g_cap_duplicates(void){fx.cfg_patch=p_duplicates;}
static VOID g_everything(void){                                       /* a quite different board altogether */
  g_subsystem();fx.bus=0x0c;fx.dev=2;fx.fun=0;fx.bar0=0xfb000000U;test_entry_ss0=0x35142736;test_entry_ss1=3;
  fx.fb_mib=0xbff4;fx.gfw_plm=0x9b8f;fx.feat_plm=0xffffff0f;fx.readout1=0x00a10040;
}
static const struct {const char *name;void (*knobs)(void);} Generic[]={
  {"subsystem",g_subsystem},{"slot behind another root port",g_slot_a},{"slot on another PCI segment",g_slot_b},
  {"BAR0 0xfc000000",g_bar0_a},{"BAR0 0x84000000",g_bar0_b},{"BAR1 address",g_bar_others},{"BAR3 address",g_bar3},
  {"entry SS1=1",g_ss_1},{"entry SS1=7",g_ss_7},{"entry SS0=SS1=0",g_ss_0},{"entry SS0 high nibbles",g_ss0_high},
  {"VRAM 48 GiB",g_vram_48g},{"VRAM 0x5ff0",g_vram_odd},{"VRAM 16 GiB",g_vram_small},
  {"GFW PLM 0x1",g_gfw_plm},{"GFW PLM 0x9b8f",g_gfw_plm_b},{"GFW boot 0xff",g_gfw_boot},{"GFW cpuctl 0x50",g_gfw_cpuctl},
  {"feature PLM 0xfffffe0f",g_feat_plm},{"readout fields",g_readout},{"WPR2 lo 0",g_wpr2_lo},
  {"chip stepping a0",g_chip_rev},{"VBIOS tail bytes",g_prom_tail},{"VBIOS FWSEC signature bytes",g_prom_fwsec_sig},
  {"everything different",g_everything},
  {"extra vendor capability after the chain",g_cap_unknown_tail},{"unknown capability id at the chain head",g_cap_unknown_head},
  {"vendor capability overlapping the area",g_cap_unknown_long},{"several unknown capabilities",g_cap_unknown_many},
  {"MSI enabled",g_cap_msi_on},{"MSI-X present and enabled",g_cap_msix_on},{"no PM capability",g_cap_no_pm},
  {"PCIe capability version 3",g_cap_pcie3},{"another PCIe device/port type",g_cap_pcie_port},
  {"expansion ROM enabled",g_cap_rom_on},{"duplicated capabilities, first wins",g_cap_duplicates}};
static VOID must_succeed(const char *name,void (*knobs)(void),void (*post)(void)) {  /* post: after discovery */
  fresh(knobs);fx.allow_prepare_failure=FALSE;
  CHECK(fx.prepare==EFI_SUCCESS);
  if(post)post();
  struct gpu_unlock_610_shared shared={0};
  CHECK(!gpu_unlock_610_load_shared(&Device,&shared));
  int result=gpu_unlock_610_unlock(&Device,&shared);gpu_unlock_610_release_shared(&shared);
  CHECK(!result && GuSafeToReturn() && Gu.started && Gu.quiesced && !Gu.fault && !Gu.retained);
  CHECK(fake.gsp_starts==1 && fake.sec2_starts==1 && fake.flrs==1);
  CHECK(!fake.live_maps && !fake.live_buffers && !Gu.allocations && !fake.pool_live);
  CHECK(registers[SS0/4]==0x88888888 && registers[SS1/4]==8 && registers[PLM/4]==0xffffffffU && !registers[0x823818/4]);
  UINTN ss1_writes=0;
  for(UINTN i=0;i<fake.trace_count;i++)if(!fake.write_trace[i][0] && fake.write_trace[i][2]==SS1){CHECK(fake.write_trace[i][3]==8);ss1_writes++;}
  CHECK(ss1_writes==1);
  CHECK(!memcmp(config+0x10,&fx.bar0,4) && !(config[4]&4)); /* PCI state restored after the FLR, BME clear */
  CHECK(!EFI_ERROR(GuClearMaster()));
  /* GuRestorePci never re-enables expansion ROM decoding (it writes the saved ROM base with bit 0 cleared), so a board
     that left it enabled ends with it disabled; the BARs, command and the rest were verified by the core itself. */
  if(fx.cfg_patch==p_rom_on)printf("NOTE expansion ROM enable bit after the restore: %u (saved config had 1; port.c never re-enables ROM decode)\n",config[0x30]&1);
  printf("guard generic %s: transaction SUCCEEDED PASS\n",name);
  dispose_host_fixture();
}
/* ---- REFUSE ---------------------------------------------------------------------------------------------- */
enum where {AT_PREPARE,AT_TRANSACTION,AT_DRIFT};
static VOID r_plm_open(void){fx.feat_plm=0xffffffffU;}                /* already unlocked */
static VOID r_wpr2_up(void){fx.wpr2_hi=0x27feff0;}                    /* driver used the card since power-on */
static VOID r_ss1_unlocked(void){test_entry_ss1=8;}
static VOID r_ss0_unlocked(void){test_entry_ss0=0x88888888;}
static VOID r_bme(void){fx.cfg_xor_at=4;fx.cfg_xor=4;}                /* bus master already on at entry */
static VOID r_vendor(void){fx.cfg_xor_at=0;fx.cfg_xor=1;}
static VOID r_device(void){fx.cfg_xor_at=2;fx.cfg_xor=1;}             /* 10de:2685 */
static VOID r_class(void){fx.cfg_xor_at=0xb;fx.cfg_xor=0x03;}         /* not a display controller */
static VOID r_flr_pending(void){fx.cfg_xor_at=0x81;fx.cfg_xor=0x80;}   /* DevCtl FLR bit already set */
static VOID r_gfw_running(void){fx.gfw_cpuctl=0;}                     /* GFW not halted */
static VOID r_gfw_boot(void){fx.gfw_boot=0x03fe;}                     /* boot not completed */
static VOID r_gfw_plm(void){fx.gfw_plm=0x8b8e;}                       /* PLM level0 clear */
static VOID r_rom_sig(void){fx.prom_xor_at=0;fx.prom_xor=0xff;}       /* ROM header unparsable */
static VOID r_fwsec_desc(void){fx.prom_xor_at=0x43e3c;fx.prom_xor=0xff;}
static VOID r_ucode_table(void){fx.prom_xor_at=0x95be8;fx.prom_xor=0xff;}
static VOID d_slot_moved(void){fx.bus=0x44;}                          /* GetLocation differs from discovery */
static VOID d_bar0_moved(void){config[0x10]^=0x10;}
static VOID d_chip_gone(void){registers[0]=0xffffffffU;}
static VOID d_plm_open(void){registers[PLM/4]=0xffffffffU;}
static VOID d_ss1_changed(void){registers[SS1/4]^=1;}
static VOID d_ss0_changed(void){registers[SS0/4]^=0x100;}
static VOID d_fuse3(void){registers[0x8241e0/4]=3;}                   /* unsupported FWSEC fuse version */
static VOID d_rom_sig(void){((UINT8 *)registers)[0x300000]^=0xff;}
static VOID d_fwsec_desc(void){((UINT8 *)registers)[0x300000+0x43e3c]^=0xff;}
static VOID d_ucode_table(void){((UINT8 *)registers)[0x300000+0x95be8]^=0xff;}
static BOOLEAN no_display(void){return FALSE;}
static VOID d_display(void){Gu.display_check=no_display;}
static VOID d_nodisplaycb(void){Gu.display_check=NULL;}
static VOID d_not_owned(void){Gu.owned=FALSE;}
static VOID d_not_armed(void){Gu.armed=FALSE;}
static VOID d_second(void){Gu.transaction_attempted=TRUE;}
static VOID d_wrong_dev(void){Device.device=0x2704;}                  /* AD103: Ada but not this card */
static VOID d_wrong_vendor(void){Device.vendor=0x1002;}
static const struct {const char *name;enum where where;void (*pre)(void),(*post)(void);int err;} Refuse[]={
  {"PLM already open (unlocked)",AT_PREPARE,r_plm_open,NULL,-ENODEV},{"WPR2 up",AT_PREPARE,r_wpr2_up,NULL,-ENODEV},
  {"SS1 == 8",AT_PREPARE,r_ss1_unlocked,NULL,-ENODEV},{"SS0 == 88888888",AT_PREPARE,r_ss0_unlocked,NULL,-ENODEV},
  {"bus master still set at the claim step",AT_TRANSACTION,r_bme,NULL,-EBUSY},{"wrong vendor id",AT_PREPARE,r_vendor,NULL,-ENODEV},
  {"wrong device id 2685",AT_PREPARE,r_device,NULL,-ENODEV},{"not a display class",AT_PREPARE,r_class,NULL,-ENODEV},
  {"FLR already initiated in DevCtl",AT_TRANSACTION,r_flr_pending,NULL,-EIO},
  {"GFW not halted",AT_TRANSACTION,r_gfw_running,NULL,-ETIMEDOUT},{"GFW boot not complete",AT_TRANSACTION,r_gfw_boot,NULL,-ETIMEDOUT},
  {"GFW PLM level0 clear",AT_TRANSACTION,r_gfw_plm,NULL,-ETIMEDOUT},
  {"VBIOS ROM header corrupt",AT_PREPARE,r_rom_sig,NULL,-ENODEV},{"FWSEC descriptor corrupt",AT_PREPARE,r_fwsec_desc,NULL,-ENODEV},
  {"ucode table corrupt",AT_PREPARE,r_ucode_table,NULL,-ENODEV},
  {"slot differs from discovery",AT_DRIFT,NULL,d_slot_moved,-ENODEV},{"BAR0 moved after discovery",AT_DRIFT,NULL,d_bar0_moved,-ENODEV},
  {"chip unreadable",AT_DRIFT,NULL,d_chip_gone,-ENODEV},{"PLM opened after discovery",AT_DRIFT,NULL,d_plm_open,-ENODEV},
  {"SS1 changed after discovery",AT_DRIFT,NULL,d_ss1_changed,-ENODEV},{"SS0 changed after discovery",AT_DRIFT,NULL,d_ss0_changed,-ENODEV},
  {"unsupported FWSEC fuse version",AT_DRIFT,NULL,d_fuse3,-ERANGE},
  {"ROM header corrupted after discovery",AT_DRIFT,NULL,d_rom_sig,-EINVAL},
  {"FWSEC descriptor corrupted after discovery",AT_DRIFT,NULL,d_fwsec_desc,-EOPNOTSUPP},
  {"ucode table corrupted after discovery",AT_DRIFT,NULL,d_ucode_table,-EOPNOTSUPP},
  {"display check false",AT_DRIFT,NULL,d_display,-ENODEV},{"no display check",AT_DRIFT,NULL,d_nodisplaycb,-ENODEV},
  {"not owned",AT_DRIFT,NULL,d_not_owned,-ENODEV},{"not armed",AT_DRIFT,NULL,d_not_armed,-ENODEV},
  {"second attempt flag",AT_DRIFT,NULL,d_second,-EALREADY},
  {"wrong device in the API",AT_DRIFT,NULL,d_wrong_dev,-ENODEV},{"wrong vendor in the API",AT_DRIFT,NULL,d_wrong_vendor,-ENODEV}};
/* GuCheckConfig / GuTargetConfig directly. GuCheckConfig is a tolerant capability finder: it refuses a wrong ID, a
   non-display class or bridge header, memory decode off, no capability list, a list it cannot walk (loop, misaligned,
   outside 0x40..0xfc), a first PM capability not in D0, no PCIe capability, PCIe version 0 and no FLR support. The
   rest of the list is searched, never judged, and the FIRST PM / PCIe / MSI / MSI-X capability wins. */
static VOID config_unit(void) {
  UINT32 c[64];GU_CAPS caps;
  fx_defaults();GuInit(&bs,&pci);Gu.seg=0;Gu.bus=0x2a;Gu.dev=3;Gu.fun=0;Gu.located=TRUE;
  #define FRESH() memcpy(c,CapturedConfig,sizeof(c))
  #define BYTE(o) (((UINT8 *)c)[o])
  #define OK() CHECK(GuCheckConfig(c,0,1,0,0,&caps))
  #define REFUSED() do{CHECK(!GuCheckConfig(c,0,1,0,0,&caps));CHECK(!caps.pm && !caps.pcie && !caps.msi && !caps.msix);}while(0)
  FRESH();OK();CHECK(caps.pm==0x60 && caps.pcie==0x78 && caps.pcie_version==2 && caps.msi==0x68 && !caps.msix);
  /* location is not pinned: any segment/bus/device/function */
  CHECK(GuCheckConfig(c,3,0x7f,31,7,&caps));
  /* subsystem, revision, BAR addresses, the vendor capability at 0xb4 and dword 0xc8 are not pinned */
  FRESH();c[11]=0x40691043;c[2]=(c[2]&~0xffU)|0xa0;c[4]=0xfc000000U;c[6]^=0x0f;c[8]^=0x01;c[0xc8/4]=0;OK();
  /* GuTargetConfig: same card in the slot discovery found it, any subsystem/revision */
  CHECK(GuTargetConfig(c,0,0x2a,3,0));
  CHECK(!GuTargetConfig(c,0,0x2a,3,1) && !GuTargetConfig(c,0,0x2b,3,0) && !GuTargetConfig(c,1,0x2a,3,0) && !GuTargetConfig(c,0,0x2a,4,0));
  Gu.located=FALSE;CHECK(GuTargetConfig(c,0,1,0,0));Gu.located=TRUE;
  /* ACCEPTED: board and firmware details outside the transaction's needs */
  FRESH();BYTE(4)|=4;OK();CHECK(GuTargetConfig(c,0,0x2a,3,0));                         /* bus master left set */
  FRESH();BYTE(0x30)|=1;OK();                                                         /* expansion ROM enabled */
  FRESH();BYTE(0x81)|=0x80;OK();                                                      /* FLR bit already set: GuSavePci/GuFlr refuse it later */
  FRESH();BYTE(0x6a)|=1;OK();CHECK(caps.msi==0x68);                                    /* MSI enabled */
  FRESH();BYTE(0x6a)|=0x80;BYTE(0x6a)&=(UINT8)~1;OK();                                 /* 64-bit/other MSI flavours */
  FRESH();BYTE(0xb5)=0xe0;BYTE(0xe0)=0x11;BYTE(0xe1)=0;BYTE(0xe2)=3;BYTE(0xe3)=0x80;OK(); /* MSI-X present and enabled */
  CHECK(caps.msix==0xe0 && caps.pm==0x60 && caps.pcie==0x78);
  FRESH();BYTE(0x7a)=(UINT8)((BYTE(0x7a)&0xf0)|3);OK();CHECK(caps.pcie_version==3);    /* PCIe capability version 3 */
  FRESH();BYTE(0x7a)=(UINT8)((BYTE(0x7a)&0xf0)|0xf);OK();CHECK(caps.pcie_version==0xf); /* any version >= 1 */
  FRESH();for(UINT8 t=0;t<16;t++){BYTE(0x7a)=(UINT8)((BYTE(0x7a)&0x0f)|(t<<4));OK();}   /* any device/port type */
  FRESH();BYTE(0x34)=0x68;OK();CHECK(caps.pm==0 && caps.pcie==0x78 && caps.msi==0x68);   /* no PM capability at all */
  FRESH();BYTE(0xb5)=0xe0;BYTE(0xe0)=0x7f;BYTE(0xe1)=0xe4;BYTE(0xe2)=0xff;BYTE(0xe3)=0xff;
  BYTE(0xe4)=0x09;BYTE(0xe5)=0;BYTE(0xe6)=0xf0;OK();                                  /* unknown ids, any length, in any order */
  CHECK(caps.pm==0x60 && caps.pcie==0x78 && caps.msi==0x68 && !caps.msix);
  FRESH();BYTE(0x34)=0xe0;BYTE(0xe0)=0x0a;BYTE(0xe1)=0x60;OK();CHECK(caps.pm==0x60);    /* unknown id first */
  FRESH();BYTE(0xb6)=0x80;OK();                                                       /* vendor length overlapping everything */
  FRESH();BYTE(0xb6)=0;OK();                                                          /* ... or zero */
  FRESH();BYTE(0x68)=0x09;BYTE(0x6a)=0x30;OK();CHECK(caps.msi==0 && caps.pm==0x60);    /* MSI replaced by a vendor capability: none found */
  /* first capability of each kind wins; later copies are not judged */
  FRESH();BYTE(0xb5)=0xe0;
  BYTE(0xe0)=0x01;BYTE(0xe1)=0xe8;BYTE(0xe4)=3;                                        /* second PM in D3 */
  BYTE(0xe8)=0x10;BYTE(0xe9)=0xf0;BYTE(0xea)=0x02;BYTE(0xeb)=0;c[0xec/4]=0;            /* second PCIe without FLR, version 2 */
  BYTE(0xf0)=0x05;BYTE(0xf1)=0xf4;                                                    /* second MSI */
  BYTE(0xf4)=0x11;BYTE(0xf5)=0xf8;BYTE(0xf8)=0x11;BYTE(0xf9)=0;                        /* two MSI-X */
  OK();CHECK(caps.pm==0x60 && caps.pcie==0x78 && caps.msi==0x68 && caps.msix==0xf4);
  /* ... and the first one is judged: a first PM in D3 is refused even if a later one is D0, a first PCIe without FLR even
     if a later one has it */
  FRESH();BYTE(0x64)|=3;BYTE(0xb5)=0xe0;BYTE(0xe0)=0x01;BYTE(0xe1)=0;BYTE(0xe4)=0;REFUSED();
  FRESH();BYTE(0x7f)&=(UINT8)~0x10;BYTE(0xb5)=0xe0;BYTE(0xe0)=0x10;BYTE(0xe1)=0;BYTE(0xe2)=2;c[0xe4/4]=0x10000000U;REFUSED();
  /* REFUSED */
  FRESH();c[0]^=1;REFUSED();CHECK(!GuTargetConfig(c,0,0x2a,3,0));                      /* vendor */
  FRESH();c[0]^=0x10000;REFUSED();CHECK(!GuTargetConfig(c,0,0x2a,3,0));                /* device 2685 */
  FRESH();BYTE(0x0b)=0x02;REFUSED();CHECK(!GuTargetConfig(c,0,0x2a,3,0));              /* class */
  FRESH();BYTE(0x0e)=0x01;REFUSED();                                                  /* bridge header */
  FRESH();BYTE(4)&=(UINT8)~2;REFUSED();                                               /* memory decode off */
  FRESH();BYTE(0x06)&=(UINT8)~0x10;REFUSED();                                         /* status: no capability list */
  FRESH();BYTE(0x34)=0;REFUSED();                                                     /* no capability pointer, hence no PCIe */
  FRESH();BYTE(0x64)|=3;REFUSED();                                                    /* PM present, not D0 */
  FRESH();BYTE(0x69)=0xb4;REFUSED();                                                  /* PCIe capability skipped: none */
  FRESH();BYTE(0x69)=0x60;REFUSED();                                                  /* capability loop */
  FRESH();BYTE(0x69)=0x79;REFUSED();                                                  /* misaligned pointer */
  FRESH();BYTE(0x69)=0x30;REFUSED();                                                  /* pointer below 0x40 */
  FRESH();BYTE(0x69)=0xfd;REFUSED();                                                  /* pointer above 0xfc */
  FRESH();BYTE(0x7a)&=0xf0;REFUSED();                                                 /* PCIe capability version 0 */
  FRESH();BYTE(0x7f)&=(UINT8)~0x10;REFUSED();                                         /* no FLR capability */
  #undef FRESH
  #undef BYTE
  #undef OK
  #undef REFUSED
  printf("config gates: capability list searched not judged (unknown/duplicate/MSI/MSI-X/PCIe 3+/no PM/ROM/BME/FLR-pending accepted; first of each kind wins); wrong id/class/header, MEM decode, unwalkable list, PM not D0, no PCIe/FLR refused PASS\n");
}
/* ---- ASSETS: size-only admission ---------------------------------------------------------------------------- */
static UINTN flip_asset,flip_at;
static VOID k_flip(void){((UINT8 *)GuAssets[flip_asset].firmware.data)[flip_at]^=0x01;}
static unsigned count_of(const char *needle) {
  unsigned n=0;for(const char *at=fake.log;(at=strstr(at,needle));at+=strlen(needle))n++;return n;
}
/* A flipped byte in any asset is NOT refused by admission: only the hardware authenticates the signed images, and the
   payload runs only in the volatile transaction. The simulation proceeds all the way; Prepare logs the hashes and says
   DIFFERS for the changed (hashed) file. The flipped byte is the last one, restored afterwards - except in the 84-byte
   boot descriptor, which is a structure rather than payload: every word of it is range-checked or must be zero, so its
   last byte is flipped in a separate case below and here bit 0 of the (unused) code-length word at 0x34 is flipped. */
static UINTN flip_offset(UINTN i){return i==4?0x34:GuAssets[i].firmware.size-1;}
static VOID assets_flipped_accepted(void) {
  static const char *names[]={"booter_load_ad102","v67-ad102-610.43.03-four-plm","gsp_ga10x","gsp_boot_ad102","gsp_boot_desc_ad102"};
  for(UINTN i=0;i<5;i++) {
    char label[96];snprintf(label,sizeof(label),"asset %s with a flipped byte",names[i]);
    flip_asset=i;flip_at=flip_offset(i);
    k_flip();                                     /* before discovery, so Prepare logs the difference */
    must_succeed(label,NULL,NULL);
    k_flip();                                     /* restore */
    CHECK(count_of("reference=DIFFERS")==(i==2?0u:1u) && count_of("reference=match")==(i==2?4u:3u));
    CHECK(count_of("(not hashed)")==1);             /* gsp_ga10x (84 MB) is never hashed at boot */
  }
  /* and after the preload, with the flip between load_shared and unlock */
  for(UINTN i=0;i<5;i++) {
    fresh(NULL);fx.allow_prepare_failure=FALSE;CHECK(fx.prepare==EFI_SUCCESS);
    struct gpu_unlock_610_shared shared={0};
    CHECK(!gpu_unlock_610_load_shared(&Device,&shared));
    flip_asset=i;flip_at=flip_offset(i);k_flip();
    int result=gpu_unlock_610_unlock(&Device,&shared);gpu_unlock_610_release_shared(&shared);
    k_flip();
    CHECK(!result && GuSafeToReturn() && Gu.started && Gu.quiesced && !Gu.fault && !Gu.retained && fake.flrs==1);
    CHECK(registers[SS0/4]==0x88888888 && registers[SS1/4]==8);
    printf("guard asset %u flipped between preload and unlock: ACCEPTED PASS\n",(unsigned)i);
    dispose_host_fixture();
  }
}
/* The descriptor's last byte is a required-zero word. Admission still accepts the file (size, name, data), the
   descriptor parser stops the transaction before the claim, so nothing is written. */
static VOID desc_last_byte(void) {
  flip_asset=4;flip_at=GuAssets[4].firmware.size-1;
  k_flip();fresh(NULL);fx.allow_prepare_failure=FALSE;CHECK(fx.prepare==EFI_SUCCESS);
  struct gpu_unlock_610_shared shared={0};
  CHECK(!gpu_unlock_610_load_shared(&Device,&shared));
  rejected("boot descriptor with a corrupt required-zero word",&shared,-EINVAL);
  CHECK(Gu.transaction_attempted);                /* it got past admission: not refused for the file's content */
  gpu_unlock_610_release_shared(&shared);k_flip();dispose_host_fixture();
}
/* Structure stays pinned: exact size, data present, the right file under the right name. */
enum {BAD_SIZE_MINUS,BAD_SIZE_PLUS,BAD_NULL,BAD_SWAP,BAD_KINDS};
static GU_ASSET saved_assets[5];
static VOID bad_apply(unsigned kind,UINTN i,UINTN j) {
  memcpy(saved_assets,GuAssets,sizeof(saved_assets));
  if(kind==BAD_SIZE_MINUS)GuAssets[i].firmware.size--;
  else if(kind==BAD_SIZE_PLUS)GuAssets[i].firmware.size++;
  else if(kind==BAD_NULL)GuAssets[i].firmware.data=NULL;
  else {const char *t=GuAssets[i].name;GuAssets[i].name=GuAssets[j].name;GuAssets[j].name=t;}
}
static VOID bad_restore(void){memcpy(GuAssets,saved_assets,sizeof(saved_assets));}
static unsigned assets_structure_refused(void) {
  static const char *kinds[]={"size-1","size+1","NULL data","swapped names"};
  unsigned cases=0;
  for(unsigned kind=0;kind<BAD_KINDS;kind++)
    for(UINTN i=0;i<5;i++) {
      UINTN j=(i+1)%5;
      if(kind==BAD_SWAP && i>=4)continue;           /* pairs (0,1) (1,2) (2,3) (3,4) */
      char label[96];snprintf(label,sizeof(label),"asset %u %s",(unsigned)i,kinds[kind]);
      /* (a) damaged before discovery: Prepare refuses (EFI_CRC_ERROR) and so does the preload */
      bad_apply(kind,i,j);fresh(NULL);
      CHECK(fx.prepare==EFI_CRC_ERROR && !Gu.profile_verified);
      struct gpu_unlock_610_shared shared={0};
      CHECK(gpu_unlock_610_load_shared(&Device,&shared)==-ENODEV);
      CHECK(!Gu.started && !fake.mmio_writes && !fake.live_maps && !fake.pci_writes && !fake.master_enables);
      bad_restore();dispose_host_fixture();
      /* (b) damaged after discovery and preload: the unlock admission refuses with zero writes */
      fresh(NULL);fx.allow_prepare_failure=FALSE;CHECK(fx.prepare==EFI_SUCCESS);
      struct gpu_unlock_610_shared loaded={0};
      CHECK(!gpu_unlock_610_load_shared(&Device,&loaded));
      bad_apply(kind,i,j);rejected(label,&loaded,-ENODEV);bad_restore();
      gpu_unlock_610_release_shared(&loaded);dispose_host_fixture();
      cases++;
    }
  return cases;
}
int main(int argc,char **argv) {
  CHECK(argc==3);load_fixture_assets(argv[1],argv[2]);config_unit();
  for(unsigned i=0;i<sizeof(Generic)/sizeof(Generic[0]);i++)must_succeed(Generic[i].name,Generic[i].knobs,NULL);
  /* The board firmware may leave bus master set: discovery and Prepare accept it, the release step clears it (post),
     then the transaction runs - the Linux module's order. The same fixture with the bit still set is the refusal
     "bus master still set at the claim step" below. */
  must_succeed("bus master left set by firmware, cleared by the release",g_bme_set,p_bme_clear);
  for(unsigned i=0;i<sizeof(Refuse)/sizeof(Refuse[0]);i++) {
    fresh(Refuse[i].pre);
    if(Refuse[i].where==AT_PREPARE)CHECK(fx.prepare!=EFI_SUCCESS && !Gu.profile_verified);
    else CHECK(fx.prepare==EFI_SUCCESS);
    if(Refuse[i].post)Refuse[i].post();
    struct gpu_unlock_610_shared shared={0};
    int loaded=gpu_unlock_610_load_shared(&Device,&shared);
    if(loaded) {  /* refused already while loading: nothing may have happened */
      CHECK(loaded==Refuse[i].err);
      CHECK(!Gu.started && !fake.mmio_writes && !fake.live_maps && !fake.pci_writes && !fake.master_enables);
      printf("guard refused %s (at load) PASS\n",Refuse[i].name);
    } else {
      rejected(Refuse[i].name,&shared,Refuse[i].err);gpu_unlock_610_release_shared(&shared);
    }
    dispose_host_fixture();
  }
  assets_flipped_accepted();desc_last_byte();
  unsigned structure=assets_structure_refused();
  /* A second attempt after a real success: nothing more may be written, mapped or reset. */
  fresh(NULL);fx.allow_prepare_failure=FALSE;CHECK(fx.prepare==EFI_SUCCESS);
  {
    struct gpu_unlock_610_shared shared={0};
    CHECK(!gpu_unlock_610_load_shared(&Device,&shared) && !gpu_unlock_610_unlock(&Device,&shared));
    gpu_unlock_610_release_shared(&shared);
    UINTN writes=fake.mmio_writes,maps=fake.maps,flrs=fake.flrs,gsp=fake.gsp_starts,sec2=fake.sec2_starts;
    struct gpu_unlock_610_shared again={0};
    int loaded=gpu_unlock_610_load_shared(&Device,&again);
    CHECK((loaded || gpu_unlock_610_unlock(&Device,&again)!=0));
    CHECK(fake.mmio_writes==writes && fake.maps==maps && fake.flrs==flrs && fake.gsp_starts==gsp && fake.sec2_starts==sec2);
    if(!loaded)gpu_unlock_610_release_shared(&again);
    printf("guard refused second attempt after success PASS\n");
  }
  dispose_host_fixture();
  for(UINTN i=0;i<5;i++)free((VOID *)GuAssets[i].firmware.data);
  printf("%u+1 generic boards unlock (%u of them capability-chain variants), 10 flipped assets accepted, %u refusals and %u asset-structure cases (x2) with zero GPU writes: PASS; all hardware operations simulated.\n",
    (unsigned)(sizeof(Generic)/sizeof(Generic[0])),11u,(unsigned)(sizeof(Refuse)/sizeof(Refuse[0])+1),structure);return 0;
}
