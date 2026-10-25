/* SPDX-License-Identifier: GPL-2.0-only */
/* Control-flow test of app.c itself (#included below), compiled once per build mode by
 * run-boot-app-tests.sh: product, GU_MANUAL, GU_4090_DRYRUN, GU_4090_DRYRUN+GU_4090_RELCHECK.
 * Everything app.c calls is stubbed except GuCheckConfig (the real checks.c judges the real GPU
 * config of the development machine, taken from the firmware survey). Each scenario records the
 * exact call order as a trace and asserts it, plus the failure policy:
 *   before any release         -> log why, 3 s note, stock Windows
 *   released, nothing written  -> put the driver back, verify, stock Windows (else halt)
 *   any GPU write              -> containment and halt, Windows is never started
 *   a returned Windows image after an unlock -> halt, never a second attempt. */
#include "fake_efi_fs.h"
#include "../app.c"
#include <setjmp.h>

#if defined(GU_4090_RELCHECK)
#define MODE 3
#define MODE_NAME "release test"
#elif defined(GU_4090_DRYRUN)
#define MODE 2
#define MODE_NAME "dry run"
#elif defined(GU_MANUAL)
#define MODE 1
#define MODE_NAME "product, manual"
#else
#define MODE 0
#define MODE_NAME "product"
#endif

static unsigned Checks;static const char *Case="";
static void dump(void);
#define CHECK(x) do{Checks++;if(!(x)){fprintf(stderr,"app FAIL [%s] %s:%d case=%s: %s\n",MODE_NAME,__FILE__,__LINE__,Case,#x);dump();exit(1);}}while(0)

/* ------------------------------------------------------------ scenario */
typedef struct {
  const char *name;
  EFI_STATUS open_s,win_s;           /* GuBootOpen / GuBootLoadWindows */
  int sb;                            /* SB_* */
  int nogpu,twogpus,cfg_bad,bar_bad,bme,slot,pcifail;
  EFI_STATUS assets_s,prep_s,pre_s,menu_s;
  int choice;                        /* 0: whatever the countdown default is, 1: unlock, 2: windows */
  EFI_STATUS rel_s;int rel_mutates;
  EFI_STATUS recover_s;int recover_nogop;
  int ready0_bad,ready1_bad;
  int load_rc,tx_rc,tx_write,tx_retained;
  EFI_STATUS clear_s;int unsafe;
  EFI_STATUS fin_s;int fin_nogop;
  int close_fail_at,start_ok;
  const char *exp[4];                /* expected trace per MODE; NULL: not applicable in that mode */
  unsigned stall3;                   /* bit MODE set: expect the 3 s "skipped" pause (and its note) in that build */
  const char *note;                  /* substring expected on the console */
  int ret_check;EFI_STATUS ret;      /* expected return value (when the app returns) */
} SC;
enum {SB_OFF,SB_ABSENT,SB_ON,SB_ERR,SB_BADSIZE};
#define ALL(x) {x,x,x,x}
#define PRE "open win reg assets prep pre menu "
#define STOCK "close start unload "
#define SKIPTAIL "close start unload "
#define OKTX "rel load tx free clear fin close ready1 start halt "
#define RECOVERED "recover close ready0 close start unload "

static const SC Scs[]={
 {.name="no log: skip everything, stock Windows",.open_s=EFI_WRITE_PROTECTED,.exp=ALL("open win " STOCK),.note="cannot open the log",.ret_check=1,.ret=EFI_ABORTED},
 {.name="no log and no Windows: nothing to start",.open_s=EFI_WRITE_PROTECTED,.win_s=EFI_NOT_FOUND,.exp=ALL("open win " STOCK),.ret_check=1,.ret=EFI_NOT_READY},
 {.name="Windows cannot be staged: the GPU is not touched",.win_s=EFI_NOT_FOUND,.exp=ALL("open win close "),.note="Cannot stage",.ret_check=1,.ret=EFI_NOT_FOUND},
 {.name="Secure Boot on",.sb=SB_ON,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (secure-boot,"},
 {.name="Secure Boot variable unreadable",.sb=SB_ERR,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (secure-boot,"},
 {.name="Secure Boot variable of the wrong size",.sb=SB_BADSIZE,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (secure-boot,"},
 {.name="Secure Boot variable absent is fine",.sb=SB_ABSENT,.choice=2,.exp=ALL(PRE STOCK)},
 {.name="no GPU",.nogpu=1,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (discover,"},
 {.name="two GPUs",.twogpus=1,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (discover,"},
 {.name="GPU config rejected (memory decode off)",.cfg_bad=1,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (discover,"},
 {.name="BAR0 descriptor of the wrong size",.bar_bad=1,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (discover,"},
 {.name="BAR0 descriptor disagrees with config space",.bar_bad=2,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (discover,"},
 {.name="BAR0 above 4 GiB",.bar_bad=3,.exp=ALL("open win " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (discover,"},
 {.name="firmware left bus master set: app.c does not care",.bme=1,.choice=2,.exp=ALL(PRE STOCK)},
 {.name="GPU in another slot with another BAR0",.slot=1,.choice=2,.exp=ALL(PRE STOCK)},
 {.name="a function whose config cannot be read is skipped",.pcifail=1,.choice=2,.exp=ALL(PRE STOCK)},
 {.name="assets fail",.assets_s=EFI_COMPROMISED_DATA,.exp=ALL("open win reg assets " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (assets,"},
 {.name="cold-state check fails",.prep_s=EFI_NOT_READY,.exp=ALL("open win reg assets prep " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (cold-state,"},
 {.name="display preflight fails",.pre_s=EFI_ACCESS_DENIED,.exp=ALL("open win reg assets prep pre " SKIPTAIL),.stall3=0xF,.note="Unlock skipped (display-preflight,"},
 {.name="menu: Windows without the mod",.choice=2,.exp=ALL(PRE STOCK)},
 {.name="menu fails: treated as Windows without the mod",.menu_s=EFI_DEVICE_ERROR,.exp=ALL(PRE STOCK)},
 {.name="countdown expires: the build's default",.choice=0,.tx_write=1,
  .exp={PRE OKTX,PRE STOCK,PRE STOCK,PRE STOCK}},
 {.name="unlock chosen, everything works",.choice=1,.tx_write=1,.exp={PRE OKTX,PRE OKTX,PRE STOCK,PRE "rel recover close ready0 start halt "}},
 {.name="unlock, Windows image returns with success: still halts, one attempt",.choice=1,.tx_write=1,.start_ok=1,.exp={PRE OKTX,PRE OKTX,PRE STOCK,PRE "rel recover close ready0 start halt "}},
 {.name="release refused before the driver was touched",.choice=1,.rel_s=EFI_ACCESS_DENIED,
  .exp={PRE "rel " STOCK,PRE "rel " STOCK,PRE STOCK,PRE "rel " STOCK},.stall3=0xB,.note="Unlock skipped (display-release,"},
 {.name="release failed after the driver was stopped: display put back, stock Windows",.choice=1,.rel_s=EFI_DEVICE_ERROR,.rel_mutates=1,
  .exp={PRE "rel " RECOVERED,PRE "rel " RECOVERED,PRE STOCK,PRE "rel " RECOVERED}},
 {.name="release failed and the display cannot be recovered: halt",.choice=1,.rel_s=EFI_DEVICE_ERROR,.rel_mutates=1,.recover_s=EFI_DEVICE_ERROR,
  .exp={PRE "rel recover close halt ",PRE "rel recover close halt ",PRE STOCK,PRE "rel recover close halt "}},
 {.name="release failed, recovery reports no GOP: halt",.choice=1,.rel_s=EFI_DEVICE_ERROR,.rel_mutates=1,.recover_nogop=1,
  .exp={PRE "rel recover close halt ",PRE "rel recover close halt ",PRE STOCK,PRE "rel recover close halt "}},
 {.name="release failed, display back but final check false: halt",.choice=1,.rel_s=EFI_DEVICE_ERROR,.rel_mutates=1,.ready0_bad=1,
  .exp={PRE "rel recover close ready0 close halt ",PRE "rel recover close ready0 close halt ",PRE STOCK,PRE "rel recover close ready0 close halt "}},
 {.name="transaction: shared assets fail to load (no write)",.choice=1,.load_rc=-5,
  .exp={PRE "rel load free clear " RECOVERED,PRE "rel load free clear " RECOVERED,PRE STOCK,NULL}},
 {.name="transaction fails before the first write: display put back, stock Windows",.choice=1,.tx_rc=-5,
  .exp={PRE "rel load tx free clear " RECOVERED,PRE "rel load tx free clear " RECOVERED,PRE STOCK,NULL}},
 {.name="transaction fails after a write: containment, halt, never Windows",.choice=1,.tx_rc=-5,.tx_write=1,
  .exp={PRE "rel load tx free clear clear close halt ",PRE "rel load tx free clear clear close halt ",PRE STOCK,NULL}},
 {.name="transaction fails after a write with retained DMA: same",.choice=1,.tx_rc=-5,.tx_write=1,.tx_retained=1,
  .exp={PRE "rel load tx free clear clear close halt ",PRE "rel load tx free clear clear close halt ",PRE STOCK,NULL}},
 {.name="transaction ok but containment fails: halt",.choice=1,.tx_write=1,.clear_s=EFI_DEVICE_ERROR,
  .exp={PRE "rel load tx free clear clear close halt ",PRE "rel load tx free clear clear close halt ",PRE STOCK,NULL}},
 {.name="transaction ok but not safe to return: halt",.choice=1,.tx_write=1,.unsafe=1,
  .exp={PRE "rel load tx free clear clear close halt ",PRE "rel load tx free clear clear close halt ",PRE STOCK,NULL}},
 {.name="reconnect fails after a successful transaction: halt",.choice=1,.tx_write=1,.fin_s=EFI_DEVICE_ERROR,
  .exp={PRE "rel load tx free clear fin clear close halt ",PRE "rel load tx free clear fin clear close halt ",PRE STOCK,NULL}},
 {.name="GOP does not come back after a successful transaction: halt",.choice=1,.tx_write=1,.fin_nogop=1,
  .exp={PRE "rel load tx free clear fin clear close halt ",PRE "rel load tx free clear fin clear close halt ",PRE STOCK,NULL}},
 {.name="log close fails after the transaction: halt",.choice=1,.tx_write=1,.close_fail_at=1,
  .exp={PRE "rel load tx free clear fin close clear close halt ",PRE "rel load tx free clear fin close clear close halt ",PRE STOCK,NULL}},
 {.name="final verification false after the transaction: halt",.choice=1,.tx_write=1,.ready1_bad=1,
  .exp={PRE "rel load tx free clear fin close ready1 clear close halt ",PRE "rel load tx free clear fin close ready1 clear close halt ",PRE STOCK,NULL}},
 {.name="release test: recovery fails (retried once from the failure handler, then halt)",.choice=1,.recover_s=EFI_DEVICE_ERROR,
  .exp={NULL,NULL,PRE STOCK,PRE "rel recover recover close halt "}},
 {.name="release test: recovery without GOP",.choice=1,.recover_nogop=1,
  .exp={NULL,NULL,PRE STOCK,PRE "rel recover recover close halt "}},
 {.name="release test: final check false",.choice=1,.ready0_bad=1,
  .exp={NULL,NULL,PRE STOCK,PRE "rel recover close ready0 recover close ready0 close halt "}},
 {.name="release test: log close fails",.choice=1,.close_fail_at=1,
  .exp={NULL,NULL,PRE STOCK,PRE "rel recover close recover close ready0 close start unload "}},
};

/* ------------------------------------------------------------ the world */
static const SC *Sc;
static char Trace[2048],Console[16384];
static unsigned Halted,Starts,LogWhileClosed,ClearCalls,Nclose,TxCalls,RelCalls;
static BOOLEAN LogOpen,Staged,Mutated,AssetsLoaded;
static BOOLEAN MenuDef;static UINTN MenuSecs;static unsigned MenuCalls;
static UINTN PreSeg,PreBus,PreDev,PreFun;static UINT64 PreBar;static BOOLEAN PreLocated;static EFI_HANDLE PreHandle,GpuHandle;
static jmp_buf Escape;static EFI_HANDLE Image;static int ImageObj;
static void T(const char *t){strcat(Trace,t);strcat(Trace," ");}
static void con(const char *t){size_t n=strlen(Console);if(n+strlen(t)<sizeof Console-1)strcat(Console,t);}
static void dump(void){fprintf(stderr,"--- trace: %s\n--- console: %s\n",Trace,Console);}

GU_PLATFORM Gu;
VOID GuInit(EFI_BOOT_SERVICES *b,EFI_PCI_IO_PROTOCOL *p){memset(&Gu,0,sizeof Gu);Gu.bs=b;Gu.pci=p;}
VOID GuLog(const char *f,...){(void)f;if(!LogOpen)LogWhileClosed++;}
VOID GuLogText(const char *t){(void)t;}
VOID *GuAlloc(size_t n){void *p=NULL;f_alloc(EfiLoaderData,n,&p);if(p)memset(p,0,n);return p;}
VOID GuFree(VOID *p){if(p&&!Gu.retained)f_free(p);}
BOOLEAN GuSafeToReturn(VOID){
  if(Sc->unsafe)return FALSE;
  return !Gu.retained&&!Gu.allocations&&!Gu.dma_count&&!Gu.master_attributes_pending&&(!Gu.started||(Gu.quiesced&&!Gu.fault));
}
EFI_STATUS GuClearMaster(VOID){
#if GU_PREFLIGHT_ONLY
  CHECK(!"GuClearMaster must not be reached in a build that cannot write");
#endif
  T("clear");ClearCalls++;return Sc->clear_s;
}
GU_ASSET GuAssets[5];

EFI_STATUS GuBootOpen(EFI_HANDLE i,EFI_SYSTEM_TABLE *t){T("open");CHECK(i==Image&&t==&FSt);if(Sc->open_s)return Sc->open_s;LogOpen=TRUE;return EFI_SUCCESS;}
EFI_STATUS GuBootLoadWindows(VOID){T("win");if(Sc->win_s)return Sc->win_s;Staged=TRUE;return EFI_SUCCESS;}
VOID GuBootUnloadWindows(VOID){T("unload");Staged=FALSE;}
EFI_STATUS GuBootWindows(VOID){
  T("start");if(!Staged)return EFI_NOT_READY;
  Staged=FALSE;Starts++;return Sc->start_ok?EFI_SUCCESS:EFI_ABORTED;
}
EFI_STATUS GuBootClose(VOID){T("close");Nclose++;if(Sc->close_fail_at&&(int)Nclose==Sc->close_fail_at){LogOpen=FALSE;return EFI_DEVICE_ERROR;}LogOpen=FALSE;return EFI_SUCCESS;}
BOOLEAN GuBootLogOpen(VOID){return LogOpen;}
VOID GuBootSay(const char *t){if(t)con(t);}
VOID GuBootNote(const char *f,...){char b[1024];va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);con(b);}
VOID GuBootSilence(VOID){}
VOID GuBootUnsilence(VOID){}
VOID GuBootHalt(VOID){T("halt");Halted++;longjmp(Escape,1);}
EFI_STATUS GuBootLoadAssets(VOID){
  T("assets");if(Sc->assets_s)return Sc->assets_s;
  for(int i=0;i<5;i++){VOID *p=GuAlloc(64);GuAssets[i].firmware.data=p;GuAssets[i].firmware.size=64;}
  AssetsLoaded=TRUE;return EFI_SUCCESS;
}
EFI_STATUS GuBootMenu(BOOLEAN def,UINTN secs,GU_CHOICE *c){
  T("menu");MenuCalls++;MenuDef=def;MenuSecs=secs;
  *c=Sc->choice==1?GU_CHOICE_UNLOCK:Sc->choice==2?GU_CHOICE_WINDOWS:(def?GU_CHOICE_UNLOCK:GU_CHOICE_WINDOWS);
  return Sc->menu_s;
}
VOID GuProfileLogRegisters(VOID){T("reg");}
EFI_STATUS GuAd104Prepare(VOID){T("prep");if(Sc->prep_s)return Sc->prep_s;Gu.profile_verified=Gu.prom_verified=TRUE;return EFI_SUCCESS;}
EFI_STATUS GuRelPreflight(EFI_SYSTEM_TABLE *t,EFI_HANDLE self,EFI_HANDLE pci){
  T("pre");CHECK(t==&FSt&&self==Image);
  PreSeg=Gu.seg;PreBus=Gu.bus;PreDev=Gu.dev;PreFun=Gu.fun;PreBar=Gu.bar_base;PreLocated=Gu.located;PreHandle=pci;
  CHECK(Gu.handle==pci&&Gu.image==self&&Gu.pci&&Gu.bar_size==GU_BAR0_BYTES&&Gu.profile_verified&&Gu.prom_verified);
  return Sc->pre_s;
}
EFI_STATUS GuRelRelease(VOID){
  T("rel");RelCalls++;if(Sc->rel_mutates||!Sc->rel_s)Mutated=TRUE;
  if(!Sc->rel_s)Gu.owned=TRUE;return Sc->rel_s;
}
BOOLEAN GuRelReleased(VOID){return TRUE;}
EFI_STATUS GuRelRecover(BOOLEAN *gop){T("recover");*gop=!Sc->recover_nogop;if(!Sc->recover_s)Gu.owned=FALSE;return Sc->recover_s;}
EFI_STATUS GuRelFinish(BOOLEAN *gop){
  T("fin");CHECK(Gu.owned&&Gu.quiesced&&Gu.flrs==1&&GuSafeToReturn());
  *gop=!Sc->fin_nogop;if(!Sc->fin_s)Gu.owned=FALSE;return Sc->fin_s;
}
BOOLEAN GuRelReadyForWindows(BOOLEAN unlocked){T(unlocked?"ready1":"ready0");return unlocked?!Sc->ready1_bad:!Sc->ready0_bad;}
BOOLEAN GuRelMutated(VOID){return Mutated;}

int gpu_unlock_610_load_shared(struct pci_dev *d,struct gpu_unlock_610_shared *sh){
  T("load");(void)sh;
  CHECK(d->vendor==0x10de&&d->device==0x2684&&d->pm_cap==Gu.pm_cap);
  CHECK(Gu.owned&&Gu.armed&&Gu.display_ok&&Gu.display_check==GuRelReleased&&Gu.profile_verified&&Gu.prom_verified&&!Gu.started);
  return Sc->load_rc;
}
int gpu_unlock_610_unlock(struct pci_dev *d,const struct gpu_unlock_610_shared *sh){
  T("tx");(void)d;(void)sh;TxCalls++;
  if(Sc->tx_write){Gu.started=TRUE;Gu.mmio_writes=7;Gu.quiesced=TRUE;if(!Sc->tx_rc)Gu.flrs=1;}
  if(Sc->tx_retained)Gu.retained=TRUE;
  return Sc->tx_rc;
}
void gpu_unlock_610_release_shared(struct gpu_unlock_610_shared *sh){(void)sh;T("free");}

/* --------------------------------------------- PCI functions (real survey config) */
static const UINT32 GpuCfg[64]={
  0x268410de,0x00100002,0x030000a1,0x00800010, 0xdd000000,0x0000000c,0x000000f0,0x0000000c,
  0x000000f8,0x0000f001,0x00000000,0x51041462, 0xde000000,0x00000060,0x00000000,0x000001ff,
  0x51041462,0,0,0, 0,0x00000001,0x0023d6ce,0, 0x48036801,0x00000008,0x00807805,0,
  0,0,0x0012b410,0x112c8de1, 0x00092930,0x00457904,0x10840042,0, 0,0,0,0x00070813,
  0x00001400,0x0180001e,0x001f0004,0, 0,0x01140009,0,0, 0,0,0x00050011,0x00b90000,
  0x00ba0000,0,0,0};
typedef struct {EFI_PCI_IO_PROTOCOL io;UINT32 cfg[64];UINTN seg,bus,dev,fun;UINT64 bar_min,bar_len;int read_fail;int bar_kind;} FPci;
static FPci Pci[6];static int PciN;
static EFI_STATUS EFIAPI p_read(EFI_PCI_IO_PROTOCOL *io,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN cnt,VOID *buf){
  FPci *p=(FPci *)io;if(p->read_fail)return EFI_DEVICE_ERROR;
  FCHECK(w==EfiPciIoWidthUint32&&!(off&3)&&off+cnt*4<=256);memcpy(buf,(UINT8 *)p->cfg+off,cnt*4);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI p_loc(EFI_PCI_IO_PROTOCOL *io,UINTN *s,UINTN *b,UINTN *d,UINTN *f){FPci *p=(FPci *)io;*s=p->seg;*b=p->bus;*d=p->dev;*f=p->fun;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI p_bar(EFI_PCI_IO_PROTOCOL *io,UINT8 idx,UINT64 *sup,VOID **res){
  FPci *p=(FPci *)io;(void)sup;FCHECK(idx==0);
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d;EFI_STATUS s=f_alloc(EfiBootServicesData,sizeof *d+2,(VOID **)&d);if(s)return s;
  memset(d,0,sizeof *d+2);d->Desc=0x8a;d->Len=43;d->AddrSpaceGranularity=32;d->AddrRangeMin=p->bar_min;d->AddrLen=p->bar_len;
  d->AddrRangeMax=p->bar_min+p->bar_len-1;((UINT8 *)(d+1))[0]=0x79;*res=d;return EFI_SUCCESS;
}
static FPci *add_pci(UINT32 id,UINTN bus,UINTN dev) {
  FCHECK(PciN<6);FPci *p=&Pci[PciN++];memset(p,0,sizeof *p);p->cfg[0]=id;p->bus=bus;p->dev=dev;
  p->io.Pci.Read=p_read;p->io.GetLocation=p_loc;p->io.GetBarAttributes=p_bar;
  EFI_HANDLE h=fh_new();fh_add(h,&FG_Pci,&p->io);return p;
}
static FPci *add_gpu(const SC *s,UINTN bus,UINTN dev) {
  FPci *p=add_pci(0,bus,dev);memcpy(p->cfg,GpuCfg,sizeof GpuCfg);
  if(s->slot)p->cfg[4]=0xfc000000;
  if(s->bme)p->cfg[1]|=4;
  if(s->cfg_bad)p->cfg[1]&=~2U;
  p->bar_min=p->cfg[4]&~0xfULL;p->bar_len=GU_BAR0_BYTES;
  if(s->bar_bad==1)p->bar_len=0x2000000;
  if(s->bar_bad==2)p->bar_min+=0x1000000;
  if(s->bar_bad==3)p->bar_min=0x100000000ULL;
  return p;
}
static EFI_STATUS EFIAPI sb_err(CHAR16 *n,EFI_GUID *g,UINT32 *a,UINTN *s,VOID *d){(void)n;(void)g;(void)a;(void)s;(void)d;return EFI_DEVICE_ERROR;}

static unsigned count(const char *hay,const char *needle){unsigned n=0;for(const char *p=hay;(p=strstr(p,needle));p+=strlen(needle))n++;return n;}
static EFI_STATUS run(const SC *sc) {
  static volatile EFI_STATUS result;
  Sc=sc;fake_env_reset();PciN=0;memset(Trace,0,sizeof Trace);memset(Console,0,sizeof Console);
  Halted=Starts=LogWhileClosed=ClearCalls=Nclose=TxCalls=RelCalls=MenuCalls=0;
  LogOpen=Staged=Mutated=AssetsLoaded=FALSE;PreLocated=FALSE;PreHandle=NULL;PreBar=0;PreSeg=PreBus=PreDev=PreFun=0;
  memset(GuAssets,0,sizeof GuAssets);Image=(EFI_HANDLE)&ImageObj;
  add_pci(0x12345678,0,0);                                    /* some other device */
  if(sc->pcifail)add_pci(0x22ba10de,1,0)->read_fail=1;         /* a function that cannot be read */
  UINTN bus=sc->slot?0x0a:1,dev=sc->slot?3:0;
  FPci *g=NULL;
  if(!sc->nogpu)g=add_gpu(sc,bus,dev);
  if(sc->twogpus)add_gpu(sc,0x20,0);
  GpuHandle=NULL;for(int i=0;i<FHn;i++)if(g&&fh_get(&FH[i],&FG_Pci)==&g->io)GpuHandle=&FH[i];
  FBs.LocateHandleBuffer=f_locatebuffer;
  { UINT8 v=sc->sb==SB_ON; if(sc->sb==SB_OFF||sc->sb==SB_ON)fvar_set("SecureBoot",&v,1);
    if(sc->sb==SB_BADSIZE){UINT8 two[2]={0,0};fvar_set("SecureBoot",two,2);}
    if(sc->sb==SB_ERR)FRt.GetVariable=sb_err; }
  if(!setjmp(Escape))result=AppEntry(Image,&FSt);else result=(EFI_STATUS)0xdead0000dead;
  return result;
}

static void check_scenario(const SC *sc) {
  const char *want=sc->exp[MODE];if(!want)return;
  Case=sc->name;EFI_STATUS r=run(sc);
  CHECK(!strcmp(Trace,want));
  /* hard invariants, for every scenario */
  CHECK(TxCalls<=1&&RelCalls<=1&&Starts<=1&&count(Trace," load ")<=1);   /* one physical attempt, one Windows start */
  CHECK(LogWhileClosed==0);                                              /* logging with the log closed would fault the platform */
  CHECK(!!strstr(Trace,"halt ")==!!Halted);
  if(Gu.started||Gu.mmio_writes)CHECK(!strstr(Trace,"start ")||strstr(Trace,"ready1 start"));  /* Windows after a write only after the final check */
  if(strstr(Trace,"halt "))CHECK(r==(EFI_STATUS)0xdead0000dead);
  CHECK(FStallUs==(((sc->stall3>>MODE)&1)?3000000ULL:0));
  if(sc->note&&(!sc->stall3||((sc->stall3>>MODE)&1)))CHECK(strstr(Console,sc->note));
  if(sc->ret_check)CHECK(r==sc->ret);
  /* nothing leaks unless the app halted holding its assets */
  { unsigned live=(AssetsLoaded&&!strstr(Trace,"start "))?5:0;CHECK(FPoolLive==live); }
  if(strstr(Trace,"menu ")){CHECK(MenuCalls==1&&MenuSecs==3);
    CHECK(MenuDef==(MODE==0)); }                                         /* only the automatic product build defaults to unlock */
  if(strstr(Trace,"pre ")){
    CHECK(PreLocated&&PreHandle==GpuHandle);
    CHECK(PreBus==(sc->slot?0x0a:1)&&PreDev==(sc->slot?3U:0U)&&PreFun==0&&PreSeg==0);
    CHECK(PreBar==(sc->slot?0xfc000000ULL:0xdd000000ULL));
  }
  CHECK(FWatchdogCalls==0);                                              /* the watchdog is boot_io's business */
}
int main(void) {
  /* argument validation: nothing at all may be called */
  Case="invalid parameters";Sc=&Scs[0];
  fake_env_reset();memset(Trace,0,sizeof Trace);CHECK(AppEntry(NULL,&FSt)==EFI_INVALID_PARAMETER);
  CHECK(AppEntry((EFI_HANDLE)&ImageObj,NULL)==EFI_INVALID_PARAMETER);
  { EFI_SYSTEM_TABLE t=FSt;t.RuntimeServices=NULL;CHECK(AppEntry((EFI_HANDLE)&ImageObj,&t)==EFI_INVALID_PARAMETER);
    EFI_RUNTIME_SERVICES r=FRt;r.GetVariable=NULL;t=FSt;t.RuntimeServices=&r;CHECK(AppEntry((EFI_HANDLE)&ImageObj,&t)==EFI_INVALID_PARAMETER);
    t=FSt;t.BootServices=NULL;CHECK(AppEntry((EFI_HANDLE)&ImageObj,&t)==EFI_INVALID_PARAMETER); }
  CHECK(Trace[0]==0);
  unsigned ran=0;
  for(unsigned i=0;i<sizeof Scs/sizeof Scs[0];i++){if(Scs[i].exp[MODE])ran++;check_scenario(&Scs[i]);}
  printf("app flow [%s]: %u checks, %u scenarios PASS (all collaborators stubbed, real GuCheckConfig on the surveyed GPU config)\n",MODE_NAME,Checks,ran);
  return 0;
}
