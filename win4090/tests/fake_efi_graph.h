/* SPDX-License-Identifier: GPL-2.0-only */
/* Fake UEFI core (handle database, open-protocol records, Disconnect/ConnectController with the
 * UEFI core's cascade semantics, TPL) and a PciIo mock, for host tests of release.c.
 * The default topology is the one surveyed on the X870E Taichi (uefi-survey-run00/SURVEY.TXT):
 *   GPU handle: PciIo BY_DRIVER by the NVIDIA GOP driver (agent == its image handle,
 *   DriverBinding 0x70009, LoadedImage 161696 bytes, EfiBootServicesCode, DeviceHandle == GPU),
 *   DevicePath BY_CHILD driver -> child GOP handle, passive firmware opens, one GOP child
 *   (1920x1080 stride 2048 BGR fb 0xf000000000 size 0x870000) with GOP BY_DRIVER (graphics
 *   console), SimpleTextOut BY_DRIVER + BY_CHILD to the virtual console (splitter), EDID
 *   discovered BY_DRIVER (third agent); ConOut=ConIn=StdErr one virtual handle.
 * The caller includes the system headers (and undefines clashing errno macros) first. */
#pragma once
#pragma GCC diagnostic ignored "-Wunused-function"
#include <IndustryStandard/Acpi10.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>

enum {H_CORE,H_BRIDGE,H_GPU,H_NV,H_GFX,H_SPLIT,H_EDID,H_FW,H_CON,H_IGPU,H_IGOP,H_IMG,H_OTHER,H_FIRST_CHILD=16,H_COUNT=40};
enum {EV_LOG=1,EV_PPH,EV_OPENINFO,EV_OPEN_EXCL,EV_DISCONNECT,EV_CONNECT,EV_CLOSE,EV_SILENCE,EV_UNSILENCE};
typedef enum {DISC_OK,DISC_ERROR_NOCHANGE,DISC_ERROR_PARTIAL,DISC_SUCCESS_NOOP} FG_DISC;
/* How an output without a screen shows up: no GOP; no GOP and no device path; a blt-only GOP without a
   framebuffer; a blt-only GOP with a framebuffer address; a GOP without a mode. */
enum {DARK_NO_GOP=1,DARK_NO_PATH,DARK_BLT,DARK_BLT_FB,DARK_NO_MODE};
#define FG_PROTOS 12
#define FG_OPENS 12
typedef struct {EFI_HANDLE agent,controller;UINT32 attr,count;} FG_OPEN;
typedef struct {EFI_GUID guid;VOID *iface;FG_OPEN opens[FG_OPENS];int nopens;} FG_PROTO;
typedef struct {int live;FG_PROTO protos[FG_PROTOS];int nprotos;} FG_HANDLE;

static struct {
  FG_HANDLE h[H_COUNT];
  UINT8 path[H_COUNT][96];
  /* dark: bit k set = child k is an output without a screen of its own, of kind dark_kind (DARK_*) */
  struct {int root_dev,root_fn,nchild,driver,igpu_gop,dark,dark_kind;} cfg;
  /* behaviour switches */
  FG_DISC disc_mode;EFI_STATUS disc_status,connect_status,enable_status;
  BOOLEAN stop_clears_decode,stop_perturbs_status,stop_replaces_pciio,connect_creates_children,
    start_sets_bme,start_rewrites_devctl,enable_noop,stop_leaves_record,stop_sets_bme,disable_noop,disable_keeps_cached_bm;
  int stop_moves_dword; /* config dword the driver Stop changes (0: none) */
  int disable_moves_dword; /* config dword Attributes(Disable) also changes (0: none) */
  EFI_STATUS disable_status;
  /* state */
  BOOLEAN driver_bound;int child_idx[4];
  /* counters / records */
  int disconnect_calls,connect_calls,exclusive_opens,close_calls;
  EFI_HANDLE disc_ctrl,disc_drv,disc_child,conn_ctrl,conn_drv[2];BOOLEAN conn_recursive;
  int tpl,section,last_pph_section,excl_section,excl_pph_section,tpl_violations;
  int trace[2048],ntrace;
} Fg;
#define H(i) ((EFI_HANDLE)&Fg.h[i])

static struct {
  EFI_PCI_IO_PROTOCOL io,io2,bridge_io,igpu_io;
  UINT8 cfg[256];UINT64 attrs;UINTN seg,bus,dev,fun;UINT64 bar_base,bar_len;
  int pci_writes,mem_writes,enable_n,disable_n,bm_enables;UINT64 enable_args[8],disable_args[8];
  struct {UINT32 off,val;} regs[16];int nregs;
} Pc;

static EFI_GUID G_PCI=EFI_PCI_IO_PROTOCOL_GUID,G_PATH=EFI_DEVICE_PATH_PROTOCOL_GUID,G_GOP=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
  G_BIND=EFI_DRIVER_BINDING_PROTOCOL_GUID,G_IMG=EFI_LOADED_IMAGE_PROTOCOL_GUID,
  G_TEXTOUT=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,G_TEXTIN=EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID,
  G_EDID={0x1c0c34f6,0xd380,0x41fa,{0xa0,0x49,0x8a,0xd0,0x6c,0x1a,0x66,0xaa}},
  G_VENDOR_A={0xfe47349a,0x7f0d,0x4641,{0x82,0x2b,0x34,0xba,0xa2,0x8e,0xcd,0xd0}},
  G_VENDOR_B={0x3bc1b285,0x8a15,0x4a82,{0xaa,0xbf,0x4d,0x7d,0x13,0xfb,0x32,0x65}},
  G_POINTER={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};

static const UINT32 SurveyConfig[64]={
  0x268410de,0x00100002,0x030000a1,0x00800010, 0xdd000000,0x0000000c,0x000000f0,0x0000000c,
  0x000000f8,0x0000f001,0x00000000,0x51041462, 0xde000000,0x00000060,0x00000000,0x000001ff,
  0x51041462,0,0,0, 0,0x00000001,0x0023d6ce,0, 0x48036801,0x00000008,0x00807805,0,
  0,0,0x0012b410,0x112c8de1, 0x00092930,0x00457904,0x10840042,0, 0,0,0,0x00070813,
  0x00001400,0x0180001e,0x001f0004,0, 0,0x01140009,0,0, 0,0,0x00050011,0x00b90000,
  0x00ba0000,0,0,0, 0,0,0,0, 0,0,0,0};

static void trace_ev(int e){if(Fg.ntrace<(int)(sizeof(Fg.trace)/sizeof(Fg.trace[0])))Fg.trace[Fg.ntrace++]=e;}

/* ---- device path construction (raw bytes as the firmware reports them) ---- */
static int dp_put(UINT8 *b,int at,int type,int sub,int len,const UINT8 *payload){
  b[at]=(UINT8)type;b[at+1]=(UINT8)sub;b[at+2]=(UINT8)len;b[at+3]=0;memcpy(b+at+4,payload,(size_t)(len-4));return at+len;}
static int dp_root(UINT8 *b){const UINT8 pl[8]={0xd0,0x41,0x03,0x0a,0,0,0,0};return dp_put(b,0,2,1,12,pl);}
static int dp_pci(UINT8 *b,int at,int dev,int fn){const UINT8 pl[2]={(UINT8)fn,(UINT8)dev};return dp_put(b,at,1,1,6,pl);}
static int dp_adr(UINT8 *b,int at,UINT32 adr){const UINT8 pl[4]={(UINT8)adr,(UINT8)(adr>>8),(UINT8)(adr>>16),(UINT8)(adr>>24)};return dp_put(b,at,2,3,8,pl);}
static int dp_end(UINT8 *b,int at){const UINT8 e[4]={0x7f,0xff,4,0};memcpy(b+at,e,4);return at+4;}
/* Bytes before the end node. */
static int dp_prefix(const UINT8 *b){int at=0;while(b[at]!=0x7f){at+=b[at+2]|(b[at+3]<<8);}return at;}

/* ---- handle database ---- */
static int hidx(EFI_HANDLE h){
  if(!h)return -1;
  long i=(long)((FG_HANDLE *)h-Fg.h);
  return (i>=0&&i<H_COUNT&&(FG_HANDLE *)h==&Fg.h[i]&&Fg.h[i].live)?(int)i:-1;
}
static FG_PROTO *fg_find(EFI_HANDLE h,const EFI_GUID *g){
  int i=hidx(h);if(i<0)return NULL;
  for(int p=0;p<Fg.h[i].nprotos;p++)if(!memcmp(&Fg.h[i].protos[p].guid,g,sizeof(EFI_GUID)))return &Fg.h[i].protos[p];
  return NULL;
}
static void fg_add_proto(int i,const EFI_GUID *g,VOID *iface){
  FG_HANDLE *x=&Fg.h[i];x->live=1;FG_PROTO *p=&x->protos[x->nprotos++];p->guid=*g;p->iface=iface;p->nopens=0;
}
static void fg_open(EFI_HANDLE h,const EFI_GUID *g,EFI_HANDLE agent,EFI_HANDLE ctrl,UINT32 attr,UINT32 count){
  FG_PROTO *p=fg_find(h,g);if(!p){fprintf(stderr,"fake: open on missing protocol\n");exit(2);}
  for(int i=0;i<p->nopens;i++)
    if(p->opens[i].agent==agent&&p->opens[i].controller==ctrl&&p->opens[i].attr==attr){p->opens[i].count+=count;return;}
  if(p->nopens>=FG_OPENS){fprintf(stderr,"fake: too many opens\n");exit(2);}
  p->opens[p->nopens++]=(FG_OPEN){agent,ctrl,attr,count};
}
static void fg_remove_opens(FG_PROTO *p,EFI_HANDLE agent,UINT32 attr){
  int w=0;
  for(int i=0;i<p->nopens;i++)if(!(p->opens[i].agent==agent&&p->opens[i].attr==attr))p->opens[w++]=p->opens[i];
  p->nopens=w;
}

/* ---- display children and their console consumers ---- */
static EFI_GRAPHICS_OUTPUT_PROTOCOL FgGop[8];
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE FgMode[8];
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION FgInfo[8];
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL FgText[8];
static int FgDummy[4];
static const UINT32 ChildAdr[4]={0x80010320,0x80010300,0x80010340,0x80010360};
static void fg_gop_data(int k,UINT64 fb,UINT64 size){
  FgInfo[k]=(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION){.Version=0,.HorizontalResolution=1920,.VerticalResolution=1080,
    .PixelFormat=PixelBlueGreenRedReserved8BitPerColor,.PixelsPerScanLine=2048};
  FgMode[k]=(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE){.MaxMode=14,.Mode=12,.Info=&FgInfo[k],.SizeOfInfo=sizeof(FgInfo[k]),
    .FrameBufferBase=fb,.FrameBufferSize=size};
  FgGop[k]=(EFI_GRAPHICS_OUTPUT_PROTOCOL){.Mode=&FgMode[k]};
}
static int fg_free_slot(void){int i;for(i=H_FIRST_CHILD;i<H_COUNT&&Fg.h[i].live;i++);if(i==H_COUNT){fprintf(stderr,"fake: no free handle\n");exit(2);}return i;}
/* One GOP child of the GPU and the three consumers seen on the real machine. */
static int fg_child(int k,UINT32 adr){
  int i=fg_free_slot();memset(&Fg.h[i],0,sizeof(Fg.h[i]));
  int len=dp_prefix(Fg.path[H_GPU]);memcpy(Fg.path[i],Fg.path[H_GPU],(size_t)len);dp_end(Fg.path[i],dp_adr(Fg.path[i],len,adr));
  fg_gop_data(k,0xf000000000ULL+(UINT64)k*0x1000000ULL,0x870000);
  int dark=(Fg.cfg.dark>>k)&1?Fg.cfg.dark_kind:0;
  if(dark==DARK_NO_GOP||dark==DARK_NO_PATH){
    if(dark==DARK_NO_GOP){fg_add_proto(i,&G_PATH,Fg.path[i]);fg_open(H(i),&G_PATH,H(H_CORE),NULL,EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,4);}
    fg_add_proto(i,&G_EDID,&FgDummy[0]);fg_open(H(i),&G_EDID,H(H_NV),NULL,EFI_OPEN_PROTOCOL_GET_PROTOCOL,1);
    fg_open(H(H_GPU),&G_PATH,H(H_NV),H(i),EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER,1);
    Fg.child_idx[k]=i;return i;
  }
  if(dark==DARK_BLT){FgMode[k].FrameBufferBase=0;FgMode[k].FrameBufferSize=0;FgInfo[k].PixelFormat=PixelBltOnly;}
  if(dark==DARK_BLT_FB)FgInfo[k].PixelFormat=PixelBltOnly;
  if(dark==DARK_NO_MODE)FgGop[k].Mode=NULL;
  fg_add_proto(i,&G_PATH,Fg.path[i]);fg_add_proto(i,&G_GOP,&FgGop[k]);
  fg_add_proto(i,&G_TEXTOUT,&FgText[k]);fg_add_proto(i,&G_EDID,&FgDummy[0]);
  fg_open(H(i),&G_PATH,H(H_CORE),NULL,EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,64);
  fg_open(H(i),&G_PATH,H(H_GFX),NULL,EFI_OPEN_PROTOCOL_TEST_PROTOCOL,2);
  fg_open(H(i),&G_PATH,H(H_EDID),NULL,EFI_OPEN_PROTOCOL_GET_PROTOCOL,2);
  fg_open(H(i),&G_GOP,H(H_CORE),NULL,EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,53);
  fg_open(H(i),&G_GOP,H(H_GFX),H(i),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  fg_open(H(i),&G_TEXTOUT,H(H_CORE),NULL,EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,3);
  fg_open(H(i),&G_TEXTOUT,H(H_SPLIT),H(i),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  fg_open(H(i),&G_TEXTOUT,H(H_SPLIT),H(H_CON),EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER,1);
  fg_open(H(i),&G_EDID,H(H_EDID),H(i),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  fg_open(H(i),&G_EDID,H(H_NV),H(i),EFI_OPEN_PROTOCOL_GET_PROTOCOL,1);
  fg_open(H(H_GPU),&G_PATH,H(H_NV),H(i),EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER,1);
  Fg.child_idx[k]=i;return i;
}

/* ---- the PciIo mock (the This pointer is ignored: one card) ---- */
static UINT32 cfg_get(int dw){UINT32 v;memcpy(&v,Pc.cfg+dw*4,4);return v;}
static void cfg_set(int dw,UINT32 v){memcpy(Pc.cfg+dw*4,&v,4);}
static UINT32 cmd(void){return cfg_get(1)&0xffffU;}
static void cmd_set(UINT32 c){cfg_set(1,(cfg_get(1)&0xffff0000U)|(c&0xffffU));}
static void reg_set(UINT32 off,UINT32 val){
  for(int i=0;i<Pc.nregs;i++)if(Pc.regs[i].off==off){Pc.regs[i].val=val;return;}
  Pc.regs[Pc.nregs].off=off;Pc.regs[Pc.nregs].val=val;Pc.nregs++;
}
static void regs_cold(void){
  Pc.nregs=0;reg_set(0,0x192000a1);reg_set(0x823804,0xffffff8f);reg_set(0x82381c,0x15732127);reg_set(0x823820,5);
  reg_set(0x823818,0x410080);reg_set(0x110100,0x10);reg_set(0x1fa828,0);
}
/* The state the transaction leaves (the Linux-verified readbacks). */
static void regs_unlocked(void){reg_set(0x823804,0xffffffff);reg_set(0x82381c,0x88888888);reg_set(0x823820,8);reg_set(0x823818,0);}

static EFI_STATUS EFIAPI pc_cfg_read(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *buf){
  (void)p;UINTN sz=(UINTN)1<<w;
  if(w>EfiPciIoWidthUint32||!n||off>256||sz*n>256-off)return EFI_INVALID_PARAMETER;
  memcpy(buf,Pc.cfg+off,sz*n);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI pc_cfg_write(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 off,UINTN n,VOID *buf){
  (void)p;UINTN sz=(UINTN)1<<w;
  if(w>EfiPciIoWidthUint32||!n||off>256||sz*n>256-off)return EFI_INVALID_PARAMETER;
  Pc.pci_writes++;memcpy(Pc.cfg+off,buf,sz*n);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI pc_mem_read(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 off,UINTN n,VOID *buf){
  (void)p;
  if(bar||w!=EfiPciIoWidthUint32||n!=1||(off&3)||off+4>0x1000000)return EFI_INVALID_PARAMETER;
  UINT32 v=0xffffffffU;
  if(cmd()&2){v=0;for(int i=0;i<Pc.nregs;i++)if(Pc.regs[i].off==off)v=Pc.regs[i].val;} /* memory decode off reads all-ones */
  memcpy(buf,&v,4);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI pc_mem_write(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 bar,UINT64 off,UINTN n,VOID *buf){
  (void)p;(void)w;(void)bar;(void)off;(void)n;(void)buf;Pc.mem_writes++;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI pc_location(EFI_PCI_IO_PROTOCOL *p,UINTN *s,UINTN *b,UINTN *d,UINTN *f){
  (void)p;*s=Pc.seg;*b=Pc.bus;*d=Pc.dev;*f=Pc.fun;return EFI_SUCCESS;
}
static void attrs_apply(UINT64 a,BOOLEAN on){
  if(a&EFI_PCI_IO_ATTRIBUTE_IO)cmd_set(on?cmd()|1:cmd()&~1U);
  if(a&EFI_PCI_IO_ATTRIBUTE_MEMORY)cmd_set(on?cmd()|2:cmd()&~2U);
  if(a&EFI_PCI_IO_ATTRIBUTE_BUS_MASTER){cmd_set(on?cmd()|4:cmd()&~4U);if(on)Pc.bm_enables++;}
  if(on)Pc.attrs|=a;else Pc.attrs&=~a;
}
static EFI_STATUS EFIAPI pc_attributes(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 a,UINT64 *res){
  (void)p;
  switch(op){
  case EfiPciIoAttributeOperationSupported:if(res)*res=0x1b77b;return EFI_SUCCESS;
  case EfiPciIoAttributeOperationGet:if(res)*res=Pc.attrs;return EFI_SUCCESS;
  case EfiPciIoAttributeOperationEnable:
    if(Pc.enable_n<8)Pc.enable_args[Pc.enable_n]=a;Pc.enable_n++;
    if(Fg.enable_status!=EFI_SUCCESS)return Fg.enable_status;
    if(Fg.enable_noop)return EFI_SUCCESS; /* claims success, changes nothing */
    attrs_apply(a,TRUE);return EFI_SUCCESS;
  case EfiPciIoAttributeOperationDisable:
    if(Pc.disable_n<8)Pc.disable_args[Pc.disable_n]=a;Pc.disable_n++;
    if(Fg.disable_status!=EFI_SUCCESS)return Fg.disable_status;
    if(Fg.disable_noop)return EFI_SUCCESS; /* claims success, changes nothing */
    attrs_apply(a,FALSE);
    if(Fg.disable_keeps_cached_bm)Pc.attrs|=a&EFI_PCI_IO_ATTRIBUTE_BUS_MASTER; /* hardware cleared, cache not */
    if(Fg.disable_moves_dword)cfg_set(Fg.disable_moves_dword,cfg_get(Fg.disable_moves_dword)^1U);
    return EFI_SUCCESS;
  default:return EFI_UNSUPPORTED;
  }
}
static EFI_STATUS EFIAPI pc_bar_attributes(EFI_PCI_IO_PROTOCOL *p,UINT8 bar,UINT64 *sup,VOID **res){
  (void)p;if(bar!=0)return EFI_UNSUPPORTED;if(sup)*sup=0;
  UINT8 *buf=calloc(1,sizeof(EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR)+2);
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *d=(VOID *)buf;
  d->Desc=0x8a;d->Len=43;d->ResType=0;d->AddrSpaceGranularity=32;d->AddrRangeMin=Pc.bar_base;
  d->AddrRangeMax=Pc.bar_base+Pc.bar_len-1;d->AddrTranslationOffset=0;d->AddrLen=Pc.bar_len;
  buf[sizeof(*d)]=0x79;*res=buf;return EFI_SUCCESS;
}
static void pc_init(void){
  memset(&Pc,0,sizeof(Pc));
  for(int i=0;i<64;i++)cfg_set(i,SurveyConfig[i]);
  Pc.io.Pci.Read=pc_cfg_read;Pc.io.Pci.Write=pc_cfg_write;Pc.io.Mem.Read=pc_mem_read;Pc.io.Mem.Write=pc_mem_write;
  Pc.io.GetLocation=pc_location;Pc.io.Attributes=pc_attributes;Pc.io.GetBarAttributes=pc_bar_attributes;
  Pc.io2=Pc.io;
  Pc.attrs=0x300; /* surveyed: PciIo caches IO|MEM although the command register is 0x0002 */
  Pc.seg=0;Pc.bus=1;Pc.dev=0;Pc.fun=0;Pc.bar_base=0xdd000000ULL;Pc.bar_len=0x1000000ULL;
  regs_cold();
}

/* ---- driver Stop / Start side effects and the UEFI core cascade ---- */
static void fg_driver_stop(void){
  FG_PROTO *pci=fg_find(H(H_GPU),&G_PCI),*dp=fg_find(H(H_GPU),&G_PATH);
  fg_remove_opens(pci,H(H_NV),EFI_OPEN_PROTOCOL_BY_DRIVER);
  fg_remove_opens(dp,H(H_NV),EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  /* Uninstalling the child cascades through its consumers: every record on it disappears. */
  for(int k=0;k<4;k++)if(Fg.child_idx[k]){memset(&Fg.h[Fg.child_idx[k]],0,sizeof(FG_HANDLE));Fg.child_idx[k]=0;}
  Fg.driver_bound=FALSE;
  if(Fg.stop_clears_decode){cmd_set(cmd()&~3U);Pc.attrs=0;}
  if(Fg.stop_sets_bme)cmd_set(cmd()|4U);
  if(Fg.stop_perturbs_status){cfg_set(1,cfg_get(1)|0x80000000U);cfg_set(32,cfg_get(32)|0x00020000U);}
  if(Fg.stop_moves_dword)cfg_set(Fg.stop_moves_dword,cfg_get(Fg.stop_moves_dword)^1U);
  if(Fg.stop_replaces_pciio)pci->iface=&Pc.io2;
  if(Fg.stop_leaves_record)fg_open(H(H_GPU),&G_PATH,H(H_OTHER),H(H_GPU),EFI_OPEN_PROTOCOL_BY_DRIVER,1); /* not on PciIo: the core would not refuse an EXCLUSIVE open for it */
}
static void fg_driver_start(void){
  FG_PROTO *pci=fg_find(H(H_GPU),&G_PCI);
  fg_open(H(H_GPU),&G_PCI,H(H_NV),H(H_GPU),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  (void)pci;Pc.attrs|=EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY;cmd_set(cmd()|2); /* IO decode stays clear, as at boot */
  if(Fg.start_sets_bme)cmd_set(cmd()|4);
  if(Fg.start_rewrites_devctl)cfg_set(32,cfg_get(32)^0x20U);
  if(Fg.connect_creates_children)for(int k=0;k<Fg.cfg.nchild;k++)fg_child(k,ChildAdr[k]);
  Fg.driver_bound=TRUE;
}

/* ---- fake boot services ---- */
static EFI_TPL EFIAPI bs_raise(EFI_TPL n){EFI_TPL old=(EFI_TPL)Fg.tpl;if(n<old)Fg.tpl_violations++;Fg.tpl=(int)n;if(old<n)Fg.section++;return old;}
static VOID EFIAPI bs_restore(EFI_TPL o){Fg.tpl=(int)o;}
static EFI_STATUS EFIAPI bs_free(VOID *p){free(p);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI bs_alloc(EFI_MEMORY_TYPE t,UINTN n,VOID **p){(void)t;*p=calloc(1,n);return *p?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;}
static EFI_STATUS EFIAPI bs_stall(UINTN us){(void)us;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI bs_pph(EFI_HANDLE h,EFI_GUID ***buf,UINTN *count){
  trace_ev(EV_PPH);if(Fg.tpl<(int)TPL_NOTIFY)Fg.tpl_violations++;Fg.last_pph_section=Fg.section;
  int i=hidx(h);if(i<0)return EFI_INVALID_PARAMETER;
  EFI_GUID **a=calloc((size_t)Fg.h[i].nprotos+1,sizeof(*a));
  for(int p=0;p<Fg.h[i].nprotos;p++)a[p]=&Fg.h[i].protos[p].guid;
  *buf=a;*count=(UINTN)Fg.h[i].nprotos;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI bs_openinfo(EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **e,UINTN *n){
  trace_ev(EV_OPENINFO);if(Fg.tpl<(int)TPL_NOTIFY)Fg.tpl_violations++;
  FG_PROTO *p=fg_find(h,g);if(!p)return EFI_NOT_FOUND;
  *e=NULL;*n=(UINTN)p->nopens;
  if(p->nopens){
    *e=calloc((size_t)p->nopens,sizeof(**e));
    for(int i=0;i<p->nopens;i++)(*e)[i]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){p->opens[i].agent,p->opens[i].controller,p->opens[i].attr,p->opens[i].count};
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI bs_open(EFI_HANDLE h,EFI_GUID *g,VOID **iface,EFI_HANDLE agent,EFI_HANDLE ctrl,UINT32 attr){
  FG_PROTO *p=fg_find(h,g);if(!p)return EFI_UNSUPPORTED;
  if(attr==EFI_OPEN_PROTOCOL_EXCLUSIVE){
    trace_ev(EV_OPEN_EXCL);if(Fg.tpl<(int)TPL_NOTIFY)Fg.tpl_violations++;
    Fg.excl_section=Fg.section;Fg.excl_pph_section=Fg.last_pph_section;
    for(int i=0;i<p->nopens;i++)
      if(p->opens[i].attr&(EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_EXCLUSIVE))return EFI_ACCESS_DENIED;
    Fg.exclusive_opens++;
  }
  fg_open(h,g,agent,ctrl,attr,1);
  if(iface)*iface=p->iface;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI bs_close(EFI_HANDLE h,EFI_GUID *g,EFI_HANDLE agent,EFI_HANDLE ctrl){
  trace_ev(EV_CLOSE);Fg.close_calls++;
  FG_PROTO *p=fg_find(h,g);if(!p)return EFI_NOT_FOUND;
  for(int i=0;i<p->nopens;i++)if(p->opens[i].agent==agent&&p->opens[i].controller==ctrl){
    for(int j=i;j+1<p->nopens;j++)p->opens[j]=p->opens[j+1];p->nopens--;return EFI_SUCCESS;}
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI bs_handle(EFI_HANDLE h,EFI_GUID *g,VOID **iface){
  *iface=NULL;if(hidx(h)<0)return EFI_INVALID_PARAMETER;
  FG_PROTO *p=fg_find(h,g);if(!p)return EFI_UNSUPPORTED;*iface=p->iface;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI bs_locate_buffer(EFI_LOCATE_SEARCH_TYPE t,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **buf){
  (void)key;*n=0;*buf=NULL;if(t!=ByProtocol)return EFI_UNSUPPORTED;
  EFI_HANDLE *a=calloc(H_COUNT,sizeof(*a));int c=0;
  for(int i=0;i<H_COUNT;i++)if(fg_find(H(i),g))a[c++]=H(i);
  if(!c){free(a);return EFI_NOT_FOUND;}
  *n=(UINTN)c;*buf=a;return EFI_SUCCESS;
}
static int dp_total(const UINT8 *b){int at=0;for(;;){int l=b[at+2]|(b[at+3]<<8);at+=l;if(b[at-l]==0x7f)return at;}}
/* Longest handle device path that is a node-aligned prefix of *path, among handles that carry
   the requested protocol. */
static EFI_STATUS EFIAPI bs_locate_path(EFI_GUID *g,EFI_DEVICE_PATH_PROTOCOL **path,EFI_HANDLE *dev){
  const UINT8 *p=(const UINT8 *)*path;int total=dp_total(p),best=-1,bestlen=-1;
  for(int i=0;i<H_COUNT;i++){
    if(!fg_find(H(i),g)||!fg_find(H(i),&G_PATH))continue;
    int l=dp_prefix(Fg.path[i]);
    if(l<=total&&l>bestlen&&!memcmp(p,Fg.path[i],(size_t)l)){best=i;bestlen=l;}
  }
  if(best<0)return EFI_NOT_FOUND;
  *path=(EFI_DEVICE_PATH_PROTOCOL *)(p+bestlen);*dev=H(best);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI bs_disconnect(EFI_HANDLE ctrl,EFI_HANDLE drv,EFI_HANDLE child){
  trace_ev(EV_DISCONNECT);Fg.disconnect_calls++;if(Fg.tpl>(int)TPL_APPLICATION)Fg.tpl_violations++;
  Fg.disc_ctrl=ctrl;Fg.disc_drv=drv;Fg.disc_child=child;
  if(ctrl!=H(H_GPU)||drv!=H(H_NV)||child)return EFI_INVALID_PARAMETER;
  switch(Fg.disc_mode){
  case DISC_ERROR_NOCHANGE:return Fg.disc_status;
  case DISC_SUCCESS_NOOP:return EFI_SUCCESS;
  case DISC_ERROR_PARTIAL:if(Fg.driver_bound)fg_driver_stop();return Fg.disc_status;
  default:if(Fg.driver_bound)fg_driver_stop();return EFI_SUCCESS;
  }
}
static EFI_STATUS EFIAPI bs_connect(EFI_HANDLE ctrl,EFI_HANDLE *drivers,EFI_DEVICE_PATH_PROTOCOL *rest,BOOLEAN recursive){
  (void)rest;trace_ev(EV_CONNECT);Fg.connect_calls++;if(Fg.tpl>(int)TPL_APPLICATION)Fg.tpl_violations++;
  Fg.conn_ctrl=ctrl;Fg.conn_drv[0]=drivers?drivers[0]:NULL;Fg.conn_drv[1]=drivers&&drivers[0]?drivers[1]:NULL;Fg.conn_recursive=recursive;
  if(Fg.connect_status!=EFI_SUCCESS)return Fg.connect_status;
  if(ctrl!=H(H_GPU)||(drivers&&drivers[0]!=H(H_NV)))return EFI_NOT_FOUND;
  FG_PROTO *pci=fg_find(H(H_GPU),&G_PCI);
  for(int i=0;i<pci->nopens;i++)if(pci->opens[i].attr&EFI_OPEN_PROTOCOL_EXCLUSIVE)return EFI_NOT_FOUND; /* driver Start cannot open PciIo */
  if(Fg.driver_bound)return EFI_SUCCESS;
  fg_driver_start();return EFI_SUCCESS;
}

/* ---- system table and the build of the default topology ---- */
static EFI_BOOT_SERVICES FgBs;
static EFI_SYSTEM_TABLE FgSt;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL FgIn;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL FgOut,FgErr,FgOutOther;
static EFI_STATUS EFIAPI fg_key_read(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *t,EFI_INPUT_KEY *k){(void)t;(void)k;return EFI_NOT_READY;}
static EFI_STATUS EFIAPI fg_key_read2(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *t,EFI_INPUT_KEY *k){(void)t;(void)k;return EFI_NOT_READY;}
static EFI_STATUS EFIAPI fg_key_reset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *t,BOOLEAN e){(void)t;(void)e;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI fg_dsupp(EFI_DRIVER_BINDING_PROTOCOL *t,EFI_HANDLE c,EFI_DEVICE_PATH_PROTOCOL *r){(void)t;(void)c;(void)r;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI fg_dstop(EFI_DRIVER_BINDING_PROTOCOL *t,EFI_HANDLE c,UINTN n,EFI_HANDLE *b){(void)t;(void)c;(void)n;(void)b;return EFI_SUCCESS;}
static EFI_DRIVER_BINDING_PROTOCOL FgBind;
static EFI_LOADED_IMAGE_PROTOCOL FgImage;
static UINT8 FgNvCode[161696];

static void fg_build(void){
  memset(Fg.h,0,sizeof(Fg.h));memset(Fg.path,0,sizeof(Fg.path));memset(Fg.child_idx,0,sizeof(Fg.child_idx));
  Fg.driver_bound=FALSE;
  /* paths */
  int at=dp_root(Fg.path[H_BRIDGE]);at=dp_pci(Fg.path[H_BRIDGE],at,Fg.cfg.root_dev,Fg.cfg.root_fn);dp_end(Fg.path[H_BRIDGE],at);
  at=dp_root(Fg.path[H_GPU]);at=dp_pci(Fg.path[H_GPU],at,Fg.cfg.root_dev,Fg.cfg.root_fn);at=dp_pci(Fg.path[H_GPU],at,0,0);dp_end(Fg.path[H_GPU],at);
  at=dp_root(Fg.path[H_IGPU]);at=dp_pci(Fg.path[H_IGPU],at,8,1);at=dp_pci(Fg.path[H_IGPU],at,0,0);dp_end(Fg.path[H_IGPU],at);
  /* PCI hierarchy */
  fg_add_proto(H_BRIDGE,&G_PATH,Fg.path[H_BRIDGE]);fg_add_proto(H_BRIDGE,&G_PCI,&Pc.bridge_io);
  fg_add_proto(H_IGPU,&G_PATH,Fg.path[H_IGPU]);fg_add_proto(H_IGPU,&G_PCI,&Pc.igpu_io);
  Fg.h[H_CORE].live=1;Fg.h[H_IMG].live=1;Fg.h[H_OTHER].live=1;Fg.h[H_GFX].live=1;Fg.h[H_SPLIT].live=1;Fg.h[H_EDID].live=1;Fg.h[H_FW].live=1;
  /* the GPU handle, protocol order as surveyed */
  fg_add_proto(H_GPU,&G_VENDOR_A,&FgDummy[1]);
  fg_open(H(H_GPU),&G_VENDOR_A,H(H_NV),H(H_GPU),EFI_OPEN_PROTOCOL_GET_PROTOCOL,3);
  fg_open(H(H_GPU),&G_VENDOR_A,H(H_FW),H(H_GPU),EFI_OPEN_PROTOCOL_GET_PROTOCOL,1);
  fg_add_proto(H_GPU,&G_VENDOR_B,&FgDummy[2]);
  fg_open(H(H_GPU),&G_VENDOR_B,H(H_CORE),NULL,EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,2);
  fg_add_proto(H_GPU,&G_PATH,Fg.path[H_GPU]);
  fg_open(H(H_GPU),&G_PATH,H(H_CORE),NULL,EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,164);
  fg_open(H(H_GPU),&G_PATH,H(H_NV),H(H_GPU),EFI_OPEN_PROTOCOL_GET_PROTOCOL,1);
  fg_open(H(H_GPU),&G_PATH,H(H_EDID),H(H_GPU),EFI_OPEN_PROTOCOL_GET_PROTOCOL,2);
  fg_add_proto(H_GPU,&G_PCI,&Pc.io);
  fg_open(H(H_GPU),&G_PCI,H(H_CORE),NULL,EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,27);
  /* the driver: agent == image handle == binding handle */
  FgBind=(EFI_DRIVER_BINDING_PROTOCOL){.Supported=fg_dsupp,.Start=fg_dsupp,.Stop=fg_dstop,
    .Version=0x70009,.ImageHandle=H(H_NV),.DriverBindingHandle=H(H_NV)};
  memset(FgNvCode,0x5a,sizeof(FgNvCode));
  FgImage=(EFI_LOADED_IMAGE_PROTOCOL){.ImageBase=FgNvCode,.ImageSize=sizeof(FgNvCode),.ImageCodeType=EfiBootServicesCode,.DeviceHandle=H(H_GPU)};
  fg_add_proto(H_NV,&G_BIND,&FgBind);fg_add_proto(H_NV,&G_IMG,&FgImage);
  /* the virtual console */
  fg_add_proto(H_CON,&G_TEXTOUT,&FgOut);fg_add_proto(H_CON,&G_TEXTIN,&FgIn);fg_add_proto(H_CON,&G_POINTER,&FgDummy[3]);
  fg_open(H(H_CON),&G_TEXTOUT,H(H_CORE),NULL,EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,1);
  fg_open(H(H_CON),&G_POINTER,H(H_EDID),H(H_CON),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
  /* a GOP on the integrated GPU */
  if(Fg.cfg.igpu_gop){
    int len=dp_prefix(Fg.path[H_IGPU]);memcpy(Fg.path[H_IGOP],Fg.path[H_IGPU],(size_t)len);dp_end(Fg.path[H_IGOP],dp_adr(Fg.path[H_IGOP],len,0x80010300U));
    if(Fg.cfg.igpu_gop==1)fg_gop_data(7,0xe0000000ULL,0x1000000);else fg_gop_data(7,0,0); /* 2: blt-only, no framebuffer */
    fg_add_proto(H_IGOP,&G_PATH,Fg.path[H_IGOP]);fg_add_proto(H_IGOP,&G_GOP,&FgGop[7]);
  }
  /* driver bound with its display children */
  if(Fg.cfg.driver){
    fg_open(H(H_GPU),&G_PCI,H(H_NV),H(H_GPU),EFI_OPEN_PROTOCOL_BY_DRIVER,1);
    for(int k=0;k<Fg.cfg.nchild;k++)fg_child(k,ChildAdr[k]);
    Fg.driver_bound=TRUE;
  }
  /* system table: ConOut = ConIn = StdErr, one virtual handle */
  FgIn=(EFI_SIMPLE_TEXT_INPUT_PROTOCOL){.Reset=fg_key_reset,.ReadKeyStroke=fg_key_read};
  memset(&FgBs,0,sizeof(FgBs));
  FgBs.RaiseTPL=bs_raise;FgBs.RestoreTPL=bs_restore;FgBs.FreePool=bs_free;FgBs.AllocatePool=bs_alloc;FgBs.Stall=bs_stall;
  FgBs.ProtocolsPerHandle=bs_pph;FgBs.OpenProtocolInformation=bs_openinfo;FgBs.OpenProtocol=bs_open;FgBs.CloseProtocol=bs_close;
  FgBs.HandleProtocol=bs_handle;FgBs.LocateHandleBuffer=bs_locate_buffer;FgBs.LocateDevicePath=bs_locate_path;
  FgBs.DisconnectController=bs_disconnect;FgBs.ConnectController=bs_connect;
  memset(&FgSt,0,sizeof(FgSt));
  FgSt.BootServices=&FgBs;FgSt.ConsoleInHandle=H(H_CON);FgSt.ConIn=&FgIn;FgSt.ConsoleOutHandle=H(H_CON);FgSt.ConOut=&FgOut;
  FgSt.StandardErrorHandle=H(H_CON);FgSt.StdErr=&FgErr;
}
static void fg_defaults(void){
  memset(&Fg.cfg,0,sizeof(Fg.cfg));Fg.cfg.root_dev=1;Fg.cfg.root_fn=1;Fg.cfg.nchild=1;Fg.cfg.driver=1;
  Fg.disc_mode=DISC_OK;Fg.disc_status=EFI_DEVICE_ERROR;Fg.connect_status=EFI_SUCCESS;Fg.enable_status=EFI_SUCCESS;
  Fg.stop_clears_decode=Fg.stop_perturbs_status=Fg.stop_replaces_pciio=FALSE;Fg.connect_creates_children=TRUE;
  Fg.start_sets_bme=Fg.start_rewrites_devctl=Fg.enable_noop=Fg.stop_leaves_record=Fg.stop_sets_bme=FALSE;
  Fg.disable_noop=Fg.disable_keeps_cached_bm=FALSE;Fg.stop_moves_dword=Fg.disable_moves_dword=0;Fg.disable_status=EFI_SUCCESS;
  Fg.disconnect_calls=Fg.connect_calls=Fg.exclusive_opens=Fg.close_calls=0;Fg.disc_ctrl=Fg.disc_drv=Fg.disc_child=NULL;
  Fg.conn_ctrl=NULL;Fg.conn_drv[0]=Fg.conn_drv[1]=NULL;Fg.conn_recursive=FALSE;
  Fg.tpl=(int)TPL_APPLICATION;Fg.section=0;Fg.last_pph_section=Fg.excl_section=Fg.excl_pph_section=-1;Fg.tpl_violations=0;Fg.ntrace=0;
}
