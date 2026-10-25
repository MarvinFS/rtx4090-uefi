/* SPDX-License-Identifier: GPL-2.0-only */
/* Host test for boot_io.c against a fake EFI environment (tests/fake_efi_fs.h). The device paths and
 * the Windows Boot Manager boot entry below are the real bytes from the firmware survey of the
 * development machine (uefi-survey-run00/SURVEY.TXT): fs[0] USB stick, fs[1] Windows ESP on the
 * FireCuda, fs[2] C:, fs[3] WinRE, fs[4] a SATA NTFS disk. Nothing in boot_io.c may depend on them:
 * the cases below move the home volume and the boot manager around on purpose.
 * Usage: test-boot-io [ASSETS_DIR]   (the five AD102 assets, read only) */
#include "fake_efi_fs.h"
#include "../boot_io.c"
#include <setjmp.h>

static unsigned Checks;static const char *Case="";
static void dump_state(void);
#define CHECK(x) do{Checks++;if(!(x)){fprintf(stderr,"boot_io FAIL %s:%d case=%s: %s\n",__FILE__,__LINE__,Case,#x);dump_state();exit(1);}}while(0)

static const unsigned char RealFsPath0[88]={
  0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,
  0x01,0x02,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x00,0x0c,0x01,0x01,
  0x06,0x00,0x00,0x00,0x03,0x05,0x06,0x00,0x06,0x00,0x04,0x01,0x2a,0x00,0x01,0x00,
  0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,
  0x00,0x00,0x47,0x30,0x11,0x3a,0x0e,0x03,0xc6,0x4a,0xaf,0x97,0xde,0x88,0xd5,0x13,
  0x34,0xe1,0x02,0x02,0x7f,0xff,0x04,0x00,
};
static const unsigned char RealFsPath1[98]={
  0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,
  0x01,0x02,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,
  0x06,0x00,0x00,0x00,0x03,0x17,0x10,0x00,0x01,0x00,0x00,0x00,0x00,0x24,0xcf,0x01,
  0x54,0x00,0x8c,0xe2,0x04,0x01,0x2a,0x00,0x01,0x00,0x00,0x00,0x00,0x08,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x20,0x03,0x00,0x00,0x00,0x00,0x00,0x9c,0x1b,0x2c,0x1a,
  0xd9,0x5a,0x37,0x44,0x91,0xee,0xdb,0x78,0x1c,0xc2,0x9a,0x8f,0x02,0x02,0x7f,0xff,
  0x04,0x00,
};
static const unsigned char RealFsPath2[98]={
  0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,
  0x01,0x02,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,
  0x06,0x00,0x00,0x00,0x03,0x17,0x10,0x00,0x01,0x00,0x00,0x00,0x00,0x24,0xcf,0x01,
  0x54,0x00,0x8c,0xe2,0x04,0x01,0x2a,0x00,0x03,0x00,0x00,0x00,0x00,0xa8,0x03,0x00,
  0x00,0x00,0x00,0x00,0x00,0x28,0xaf,0xe8,0x00,0x00,0x00,0x00,0x01,0x8f,0x56,0x53,
  0x5f,0xac,0xff,0x47,0xaa,0xc3,0xa9,0xb5,0x03,0xd1,0x4e,0x65,0x02,0x02,0x7f,0xff,
  0x04,0x00,
};
static const unsigned char RealFsPath3[98]={
  0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,
  0x01,0x02,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,
  0x06,0x00,0x00,0x00,0x03,0x17,0x10,0x00,0x01,0x00,0x00,0x00,0x00,0x24,0xcf,0x01,
  0x54,0x00,0x8c,0xe2,0x04,0x01,0x2a,0x00,0x04,0x00,0x00,0x00,0x00,0xd0,0xc2,0xe8,
  0x00,0x00,0x00,0x00,0x00,0xb0,0x1d,0x00,0x00,0x00,0x00,0x00,0x19,0x57,0xde,0x96,
  0x4c,0x9f,0xd9,0x4c,0x8a,0x66,0xc5,0x58,0xd7,0x94,0x83,0x1f,0x02,0x02,0x7f,0xff,
  0x04,0x00,
};
static const unsigned char RealFsPath4[104]={
  0x02,0x01,0x0c,0x00,0xd0,0x41,0x03,0x0a,0x00,0x00,0x00,0x00,0x01,0x01,0x06,0x00,
  0x01,0x02,0x01,0x01,0x06,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x00,0x08,0x01,0x01,
  0x06,0x00,0x00,0x00,0x01,0x01,0x06,0x00,0x00,0x0d,0x01,0x01,0x06,0x00,0x00,0x00,
  0x03,0x12,0x0a,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x04,0x01,0x2a,0x00,0x02,0x00,
  0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x70,0x3f,0x8c,0x04,0x00,
  0x00,0x00,0x9d,0xb4,0x0a,0xd1,0x02,0xae,0x8a,0x49,0xa0,0x70,0x56,0xb5,0xe7,0x18,
  0xc6,0x08,0x02,0x02,0x7f,0xff,0x04,0x00,
};
static const unsigned char RealBoot0000[172]={
  0x01,0x00,0x00,0x00,0x74,0x00,0x57,0x00,0x69,0x00,0x6e,0x00,0x64,0x00,0x6f,0x00,
  0x77,0x00,0x73,0x00,0x20,0x00,0x42,0x00,0x6f,0x00,0x6f,0x00,0x74,0x00,0x20,0x00,
  0x4d,0x00,0x61,0x00,0x6e,0x00,0x61,0x00,0x67,0x00,0x65,0x00,0x72,0x00,0x00,0x00,
  0x04,0x01,0x2a,0x00,0x01,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x20,0x03,0x00,0x00,0x00,0x00,0x00,0x9c,0x1b,0x2c,0x1a,0xd9,0x5a,0x37,0x44,
  0x91,0xee,0xdb,0x78,0x1c,0xc2,0x9a,0x8f,0x02,0x02,0x04,0x04,0x46,0x00,0x5c,0x00,
  0x45,0x00,0x46,0x00,0x49,0x00,0x5c,0x00,0x4d,0x00,0x49,0x00,0x43,0x00,0x52,0x00,
  0x4f,0x00,0x53,0x00,0x4f,0x00,0x46,0x00,0x54,0x00,0x5c,0x00,0x42,0x00,0x4f,0x00,
  0x4f,0x00,0x54,0x00,0x5c,0x00,0x42,0x00,0x4f,0x00,0x4f,0x00,0x54,0x00,0x4d,0x00,
  0x47,0x00,0x46,0x00,0x57,0x00,0x2e,0x00,0x45,0x00,0x46,0x00,0x49,0x00,0x00,0x00,
  0x7f,0xff,0x04,0x00,0x57,0x49,0x4e,0x44,0x4f,0x57,0x53,0x00,
};

/* ---------------------------------------------------------------- helpers */
enum {V_USB,V_WIN,V_C,V_RE,V_SATA,V_N};
static FVol *V[V_N];
static void w16(UINT8 *p,UINT16 v){p[0]=(UINT8)v;p[1]=(UINT8)(v>>8);}
static void w32(UINT8 *p,UINT32 v){w16(p,(UINT16)v);w16(p+2,(UINT16)(v>>16));}
static void w64(UINT8 *p,UINT64 v){w32(p,(UINT32)v);w32(p+4,(UINT32)(v>>32));}
static void reset(void) {
  fake_env_reset();Table=NULL;Self=NULL;Windows=NULL;OwnVolume=NULL;Root=NULL;Log=NULL;Silent=FALSE;
  GuInit(&FBs,NULL);memset(GuAssets,0,sizeof GuAssets);memset(V,0,sizeof V);
}
static void world(void) {
  V[V_USB]=fv_new(RealFsPath0,sizeof RealFsPath0);V[V_WIN]=fv_new(RealFsPath1,sizeof RealFsPath1);
  V[V_C]=fv_new(RealFsPath2,sizeof RealFsPath2);V[V_RE]=fv_new(RealFsPath3,sizeof RealFsPath3);
  V[V_SATA]=fv_new(RealFsPath4,sizeof RealFsPath4);
}
/* A partition path derived from a real one: same disk, other partition identity. */
static void patched_path(const UINT8 *src,UINTN len,UINT32 part,UINT64 start,UINT64 size,const UINT8 guid[16],UINT8 *out) {
  memcpy(out,src,len);UINTN hd=len-4-42;FCHECK(out[hd]==4&&out[hd+1]==1);
  w32(out+hd+4,part);w64(out+hd+8,start);w64(out+hd+16,size);memcpy(out+hd+24,guid,16);
}
static const UINT8 GuidOther[16]={0x91,0x0a,0x1c,0x54,0x5f,0x13,0xcc,0x4d,0xaf,0x11,0xc0,0x10,0xa3,0xcf,0x4e,0xef};
static const UINT8 GuidDisk2[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};

/* Synthetic bootmgfw images: valid PE32+ x64 EFI applications (and broken ones). */
enum {PE_A,PE_B,PE_NOMZ,PE_MACHINE,PE_SUBSYS,PE_MAGIC,PE_PEOFF,PE_NOSIG,PE_SMALL,PE_BIG,PE_N};
static UINT8 *PeBuf[PE_N];static UINTN PeLen[PE_N];
static UINT8 *pe(int k) {
  if(PeBuf[k])return PeBuf[k];
  UINTN size=3086848;UINT8 fill=0x5a;
  if(k==PE_A)fill=0xa1;if(k==PE_B)fill=0xb2;if(k==PE_SMALL)size=1000;if(k==PE_BIG)size=17u<<20;
  UINT8 *b=malloc(size);memset(b,fill,size);
  b[0]='M';b[1]='Z';w32(b+0x3c,0x80);memcpy(b+0x80,"PE\0\0",4);w16(b+0x84,0x8664);w16(b+0x98,0x20b);w16(b+0x98+68,10);
  switch(k){
    case PE_NOMZ:b[0]='X';break;case PE_MACHINE:w16(b+0x84,0x14c);break;case PE_SUBSYS:w16(b+0x98+68,3);break;
    case PE_MAGIC:w16(b+0x98,0x10b);break;case PE_PEOFF:w32(b+0x3c,(UINT32)size);break;case PE_NOSIG:b[0x80]='X';break;default:break;
  }
  PeBuf[k]=b;PeLen[k]=size;return b;
}
static const char *WinFile="\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
static void put_windows(FVol *v,int kind) {UINT8 *b=pe(kind);fv_put(v,WinFile,b,PeLen[kind],0);}

/* Boot options as the firmware stores them: Attributes u32, FilePathListLength u16, UTF-16 description, path list. */
static UINTN file_node(UINT8 *out,const char *path) {
  UINTN n=strlen(path)+1,len=4+2*n;out[0]=4;out[1]=4;w16(out+2,(UINT16)len);
  for(UINTN i=0;i<n;i++)w16(out+4+2*i,(UINT16)(unsigned char)path[i]);return len;
}
static UINTN hd_node(const FVol *v,UINT8 *out) {memcpy(out,v->path+v->pathlen-4-42,42);return 42;}
static void boot_entry(unsigned num,const char *desc,const UINT8 *prefix,UINTN plen,const char *file) {
  UINT8 b[1024];UINTN at=6;w32(b,1);
  for(const char *d=desc;;d++){w16(b+at,(UINT16)(unsigned char)*d);at+=2;if(!*d)break;}
  UINTN list=at;memcpy(b+at,prefix,plen);at+=plen;at+=file_node(b+at,file);
  b[at++]=0x7f;b[at++]=0xff;b[at++]=4;b[at++]=0;w16(b+4,(UINT16)(at-list));
  char name[16];snprintf(name,sizeof name,"Boot%04X",num);fvar_set(name,b,at);
}
static void boot_order(const UINT16 *o,int n) {fvar_set("BootOrder",o,(UINTN)n*2);}
/* The surveyed machine: BootOrder {0001 stick, 0000 Windows}, Boot0000 verbatim from the firmware. */
static void survey_boot_vars(void) {
  static const UINT16 order[2]={1,0};UINT8 prefix[128];UINTN n=V[V_USB]->pathlen-4;
  memcpy(prefix,V[V_USB]->path,n);boot_order(order,2);
  boot_entry(1,"UEFI: KingstonDataTraveler 2.0PMAP, Partition 1",prefix,n,"\\EFI\\BOOT\\BOOTX64.EFI");
  fvar_set("Boot0000",RealBoot0000,sizeof RealBoot0000);
}
static EFI_STATUS open_on(FVol *home) {return GuBootOpen(fake_self(home->handle),&FSt);}
static void free_assets(void) {
  for(int i=0;i<5;i++){GuFree((VOID *)GuAssets[i].firmware.data);GuAssets[i].firmware.data=NULL;GuAssets[i].firmware.size=0;GuAssets[i].name=NULL;}
}
/* Release whatever the code under test may still hold, then insist nothing leaked. */
static void tidy(void) {
  free_assets();GuBootUnloadWindows();(void)GuBootClose();
  CHECK(FOpenLive==0);CHECK(FPoolLive==0);
}
static char LogBuf[1<<16];
static const char *logtxt(FVol *home,const char *slot);
static void dump_state(void) {
  if(FVn&&FVols[0]){fprintf(stderr,"--- log on the first volume:\n%s\n--- console:\n%s\n",logtxt(FVols[0],"RUN0001.TXT"),FCon);}
}
static const char *logtxt(FVol *home,const char *slot) {
  char p[64];snprintf(p,sizeof p,"\\GPU4090-UNLOCK\\%s",slot);FNode *n=fv_get(home,p);
  if(!n){LogBuf[0]=0;return LogBuf;}
  size_t c=n->size<sizeof LogBuf-1?(size_t)n->size:sizeof LogBuf-1;memcpy(LogBuf,n->data,c);LogBuf[c]=0;return LogBuf;
}
static void others_untouched(const FVol *except) {
  for(int i=0;i<FVn;i++)if(FVols[i]!=except)CHECK(fv_untouched(FVols[i]));
}

/* ----------------------------------------------------------- GuBootOpen */
static void t_open_homes(void) {
  for(int h=0;h<3;h++) {
    Case=h==0?"open: USB stick":h==1?"open: internal NVMe partition":"open: the Windows ESP itself";
    reset();world();FVol *home;
    if(h==0)home=V[V_USB];
    else if(h==1){UINT8 p[98];patched_path(RealFsPath3,sizeof RealFsPath3,5,0xe8b2d000ULL,0x100000,GuidOther,p);home=fv_new(p,sizeof p);}
    else home=V[V_WIN];
    FNode *boot=NULL;UINT8 *bdata=NULL;
    if(h==2){put_windows(home,PE_A);boot=fv_get(home,WinFile);bdata=boot->data;}
    EFI_STATUS s=open_on(home);
    CHECK(s==EFI_SUCCESS);CHECK(GuBootLogOpen());CHECK(FWatchdogCalls==1&&FWatchdogTimeout==0);
    GuLog("line %u\n",1);
    CHECK(!Gu.fault);CHECK(!strcmp(logtxt(home,"RUN0001.TXT"),"line 1\n"));CHECK(home->flushes>=1);
    CHECK(home->creates==2&&home->deletes==0&&home->bytes_written==7);
    others_untouched(home);
    if(h==2){CHECK(boot->data==bdata&&boot->size==PeLen[PE_A]&&!boot->deleted);}
    tidy();
  }
}
static void t_open_failures(void) {
  Case="open: write-protected home";
  reset();world();V[V_USB]->write_protected=1;
  CHECK(open_on(V[V_USB])==EFI_WRITE_PROTECTED);CHECK(!GuBootLogOpen());CHECK(V[V_USB]->creates==0);
  CHECK(GuBootClose()==EFI_SUCCESS);CHECK(FOpenLive==0);tidy();
  Case="open: no room for the log file";
  reset();world();V[V_USB]->full_files=1;
  CHECK(open_on(V[V_USB])==EFI_VOLUME_FULL);CHECK(!GuBootLogOpen());tidy();
  Case="open: no room even for the directory";
  reset();world();V[V_USB]->full=1;
  CHECK(open_on(V[V_USB])==EFI_VOLUME_FULL);CHECK(!GuBootLogOpen());tidy();
  Case="open: watchdog cannot be disabled";
  reset();world();FWatchdogStatus=EFI_DEVICE_ERROR;
  CHECK(open_on(V[V_USB])==EFI_UNSUPPORTED);CHECK(!Root);tidy();
  Case="open: image without LoadedImage";
  reset();world();CHECK(GuBootOpen(fh_new(),&FSt)==EFI_UNSUPPORTED);tidy();
  Case="open: LoadedImage without a device handle";
  reset();world();CHECK(GuBootOpen(fake_self(NULL),&FSt)==EFI_UNSUPPORTED);tidy();
  Case="open: device without a device path";
  reset();world();{EFI_HANDLE d=fh_new();fh_add(d,&FG_Fs,&V[V_USB]->fs);CHECK(GuBootOpen(fake_self(d),&FSt)==EFI_UNSUPPORTED);}tidy();
  Case="open: malformed device path";
  reset();world();{static UINT8 junk[16]={2,1,12,0,0xd0,0x41,3,10,0,0,0,0};EFI_HANDLE d=fh_new();
    fh_add(d,&FG_Fs,&V[V_USB]->fs);fh_add(d,&FG_Path,junk);CHECK(GuBootOpen(fake_self(d),&FSt)==EFI_UNSUPPORTED);}tidy();
  Case="open: device without a file system";
  reset();world();{EFI_HANDLE d=fh_new();fh_add(d,&FG_Path,V[V_USB]->path);CHECK(GuBootOpen(fake_self(d),&FSt)==EFI_UNSUPPORTED);}tidy();
  Case="open: volume cannot be opened";
  reset();world();V[V_USB]->no_media=1;CHECK(open_on(V[V_USB])==EFI_DEVICE_ERROR);tidy();
  Case="open: second call";
  reset();world();CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(open_on(V[V_USB])==EFI_ALREADY_STARTED);tidy();
  Case="open: bad parameters";
  reset();world();CHECK(GuBootOpen(NULL,&FSt)==EFI_INVALID_PARAMETER);CHECK(GuBootOpen(fake_self(V[V_USB]->handle),NULL)==EFI_INVALID_PARAMETER);
  {EFI_SYSTEM_TABLE bare;memset(&bare,0,sizeof bare);CHECK(GuBootOpen(fake_self(V[V_USB]->handle),&bare)==EFI_INVALID_PARAMETER);}tidy();
}
static void fill_ring(FVol *v,int from,int to,int y,int mo,int d,int h,int mi,int s) {
  for(int k=from;k<=to;k++){
    char p[64],c[16];snprintf(p,sizeof p,"\\GPU4090-UNLOCK\\RUN%04d.TXT",k);snprintf(c,sizeof c,"old%02d",k);
    FNode *n=fv_put(v,p,c,strlen(c),1);fn_time(n,y,mo,d,h,mi,s);
  }
}
static void t_ring(void) {
  Case="ring: first free slot, gaps first";
  reset();world();fv_put(V[V_USB],"\\GPU4090-UNLOCK\\RUN0001.TXT","a",1,1);fv_put(V[V_USB],"\\GPU4090-UNLOCK\\RUN0003.TXT","c",1,1);
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0002.TXT")->size==0);
  GuLog("x");CHECK(!strcmp(logtxt(V[V_USB],"RUN0002.TXT"),"x"));CHECK(!strcmp(logtxt(V[V_USB],"RUN0001.TXT"),"a"));tidy();
  Case="ring: slot 40 is the last free one";
  reset();world();fill_ring(V[V_USB],1,39,2026,10,1,0,0,0);
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0040.TXT")&&V[V_USB]->deletes==0);tidy();
  Case="ring: full ring replaces the oldest by modification time";
  reset();world();fill_ring(V[V_USB],1,40,2026,10,1,0,0,0);
  for(int k=1;k<=40;k++){char p[64];snprintf(p,sizeof p,"\\GPU4090-UNLOCK\\RUN%04d.TXT",k);fn_time(fv_get(V[V_USB],p),2026,10,1,0,0,(k*7)%50+1);}
  fn_time(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0017.TXT"),2026,9,30,23,59,59);
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(V[V_USB]->deletes==1&&V[V_USB]->creates==1);
  CHECK(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0017.TXT")->size==0);GuLog("new");CHECK(!strcmp(logtxt(V[V_USB],"RUN0017.TXT"),"new"));
  CHECK(!strcmp(logtxt(V[V_USB],"RUN0016.TXT"),"old16")&&!strcmp(logtxt(V[V_USB],"RUN0018.TXT"),"old18"));tidy();
  Case="ring: equal timestamps replace the first slot";
  reset();world();fill_ring(V[V_USB],1,40,2026,10,1,0,0,0);
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0001.TXT")->size==0&&V[V_USB]->deletes==1);tidy();
  for(int field=0;field<6;field++) {
    static const char *names[]={"year","month","day","hour","minute","second"};
    char cn[64];snprintf(cn,sizeof cn,"ring: oldest decided by %s",names[field]);Case=cn;
    reset();world();fill_ring(V[V_USB],1,40,2026,10,15,12,30,30);
    FNode *n=fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0007.TXT");
    switch(field){case 0:n->mtime.Year=2025;break;case 1:n->mtime.Month=9;break;case 2:n->mtime.Day=14;break;
      case 3:n->mtime.Hour=11;break;case 4:n->mtime.Minute=29;break;default:n->mtime.Second=29;}
    CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0007.TXT")->size==0&&V[V_USB]->deletes==1);tidy();
  }
  Case="ring: a smaller year beats a smaller month";
  reset();world();fill_ring(V[V_USB],1,40,2026,10,15,12,30,30);
  fn_time(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0030.TXT"),2026,9,30,23,59,59);fn_time(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0009.TXT"),2025,12,31,23,59,59);
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(fv_get(V[V_USB],"\\GPU4090-UNLOCK\\RUN0009.TXT")->size==0);
  CHECK(!strcmp(logtxt(V[V_USB],"RUN0030.TXT"),"old30"));tidy();
  Case="ring: delete of the oldest fails";
  reset();world();fill_ring(V[V_USB],1,40,2026,10,1,0,0,0);V[V_USB]->fail_delete=1;
  CHECK(open_on(V[V_USB])==EFI_WARN_DELETE_FAILURE);CHECK(!GuBootLogOpen());tidy();
  Case="ring: open error on a slot is reported";
  reset();world();strcpy(V[V_USB]->fail_name,"RUN0001.TXT");V[V_USB]->fail_status=EFI_DEVICE_ERROR;
  fv_put(V[V_USB],"\\GPU4090-UNLOCK\\RUN0001.TXT","a",1,1);CHECK(open_on(V[V_USB])==EFI_DEVICE_ERROR);CHECK(!GuBootLogOpen());tidy();
}

/* ------------------------------------------------- console and logging */
static void t_console(void) {
  Case="console: say, note, silence";
  reset();world();CHECK(open_on(V[V_USB])==EFI_SUCCESS);
  GuBootSay("a\nb");CHECK(fcon_has("a\r\nb"));GuBootSay(NULL);
  GuBootNote("n=%u\n",5);CHECK(fcon_has("n=5\r\n"));CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"n=5\n"));
  FConN=0;FCon[0]=0;GuBootSilence();GuBootSay("hidden");GuBootNote("quiet %u\n",1);
  CHECK(FConN==0);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"quiet 1\n"));
  GuBootUnsilence();GuBootSay("shown");CHECK(fcon_has("shown"));
  tidy();
  Case="console: long text is chunked without loss";
  reset();world();Table=&FSt;
  {char t[700];int n=0;for(int i=0;i<600;i++){t[n++]=(i==248||i==249||i==250||i==300)?'\n':'x';}t[n]=0;
   GuBootSay(t);size_t nl=0;for(int i=0;i<n;i++)if(t[i]=='\n')nl++;
   CHECK(FConN==(size_t)n+nl);size_t crs=0;for(size_t i=0;i<FConN;i++)if(FCon[i]=='\r')crs++;CHECK(crs==nl);
   for(size_t i=1;i<FConN;i++)if(FCon[i]=='\n')CHECK(FCon[i-1]=='\r');}
  tidy();
  Case="console: oversized note is dropped, not truncated into the log";
  reset();world();CHECK(open_on(V[V_USB])==EFI_SUCCESS);
  {char t[1500];memset(t,'y',sizeof t);t[sizeof t-1]=0;GuBootNote("%s",t);CHECK(FConN==0);CHECK(!Gu.fault);}tidy();
  Case="console: note without a log is console-only and does not fault";
  reset();world();V[V_USB]->write_protected=1;(void)open_on(V[V_USB]);GuBootNote("only %s\n","console");
  CHECK(fcon_has("only console"));CHECK(!Gu.fault);tidy();
  Case="console: logging without an open log faults the platform (fail closed)";
  reset();world();V[V_USB]->write_protected=1;(void)open_on(V[V_USB]);GuLog("no log");CHECK(Gu.fault);tidy();
  Case="console: log write failure faults the platform";
  reset();world();CHECK(open_on(V[V_USB])==EFI_SUCCESS);V[V_USB]->fail_flush=1;GuLog("x");CHECK(Gu.fault);tidy();
}

/* ------------------------------------------------------------- assets */
static UINT8 *AssetData[5];static UINTN AssetSize[5];
static const char *AssetFile[5]={"booter_load_ad102.bin","v67-ad102-610.43.03-four-plm.bin","gsp_ga10x.bin","gsp_boot_ad102.bin","gsp_boot_desc_ad102.bin"};
static const UINTN AssetWant[5]={57672,64000,84277400,36864,84};
static void load_real_assets(const char *dir) {
  for(int i=0;i<5;i++){
    char p[512];snprintf(p,sizeof p,"%s/%s",dir,AssetFile[i]);FILE *f=fopen(p,"rb");
    if(!f){fprintf(stderr,"cannot read %s\n",p);exit(2);}
    AssetData[i]=malloc(AssetWant[i]);AssetSize[i]=fread(AssetData[i],1,AssetWant[i],f);
    if(AssetSize[i]!=AssetWant[i]||fgetc(f)!=EOF){fprintf(stderr,"unexpected size of %s\n",p);exit(2);}
    fclose(f);
  }
}
static void put_assets(FVol *v) {
  for(int i=0;i<5;i++){char p[128];snprintf(p,sizeof p,"\\GPU4090-ASSETS\\%s",AssetFile[i]);fv_put(v,p,AssetData[i],AssetSize[i],0);}
}
static void t_assets(void) {
  static const char *want[5]={"gpu-unlock/610.43.03/booter_load_ad102.bin","gpu-unlock/610.43.03/v67-ad102-610.43.03-four-plm.bin",
    "gpu-unlock/610.43.03/gsp_ga10x.bin","gpu-unlock/610.43.03/gsp_boot_ad102.bin","gpu-unlock/610.43.03/gsp_boot_desc_ad102.bin"};
  Case="assets: the five real files load and validate";
  reset();world();put_assets(V[V_USB]);CHECK(open_on(V[V_USB])==EFI_SUCCESS);
  CHECK(GuBootLoadAssets()==EFI_SUCCESS);
  for(int i=0;i<5;i++){CHECK(!strcmp(GuAssets[i].name,want[i]));CHECK(GuAssets[i].firmware.size==AssetWant[i]);
    CHECK(GuAssets[i].firmware.data!=AssetData[i]&&!memcmp(GuAssets[i].firmware.data,AssetData[i],AssetWant[i]));}
  CHECK(GuAd104AssetsValid());CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"asset 2 gpu-unlock/610.43.03/gsp_ga10x.bin: status=0\n"));
  CHECK(!Gu.fault);others_untouched(V[V_USB]);CHECK(V[V_USB]->bytes_written>0);tidy();
  Case="assets: 84 MB image one byte short";
  reset();world();put_assets(V[V_USB]);fv_put(V[V_USB],"\\GPU4090-ASSETS\\gsp_ga10x.bin",AssetData[2],AssetSize[2]-1,0);
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(GuBootLoadAssets()==EFI_COMPROMISED_DATA);CHECK(GuAssets[2].firmware.data==NULL);tidy();
  Case="assets: one byte too long";
  reset();world();put_assets(V[V_USB]);{UINT8 *c=malloc(AssetWant[0]+1);memcpy(c,AssetData[0],AssetWant[0]);c[AssetWant[0]]=0;
    fv_put(V[V_USB],"\\GPU4090-ASSETS\\booter_load_ad102.bin",c,AssetWant[0]+1,0);}
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(GuBootLoadAssets()==EFI_COMPROMISED_DATA);tidy();
  Case="assets: missing file";
  reset();world();put_assets(V[V_USB]);fv_get(V[V_USB],"\\GPU4090-ASSETS\\booter_load_ad102.bin")->deleted=1;
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(GuBootLoadAssets()==EFI_NOT_FOUND);tidy();
  Case="assets: a directory instead of a file";
  reset();world();put_assets(V[V_USB]);fv_get(V[V_USB],"\\GPU4090-ASSETS\\gsp_boot_ad102.bin")->deleted=1;fv_mkdir(V[V_USB],"\\GPU4090-ASSETS\\gsp_boot_ad102.bin");
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(GuBootLoadAssets()==EFI_COMPROMISED_DATA);tidy();
  Case="assets: right size, one flipped byte is loaded (content is authenticated by the hardware, not by size checks)";
  reset();world();put_assets(V[V_USB]);{UINT8 *c=malloc(AssetWant[0]);memcpy(c,AssetData[0],AssetWant[0]);c[100]^=1;
    fv_put(V[V_USB],"\\GPU4090-ASSETS\\booter_load_ad102.bin",c,AssetWant[0],0);}
  CHECK(open_on(V[V_USB])==EFI_SUCCESS);CHECK(GuBootLoadAssets()==EFI_SUCCESS);CHECK(GuAd104AssetsValid());CHECK(!Gu.fault);
  CHECK(memcmp(GuAssets[0].firmware.data,AssetData[0],AssetWant[0])!=0&&GuAssets[0].firmware.size==AssetWant[0]);tidy();
  Case="assets: read error part-way frees what it allocated";
  reset();world();put_assets(V[V_USB]);CHECK(open_on(V[V_USB])==EFI_SUCCESS);
  V[V_USB]->read_budget=57672+64000+10*1024*1024;CHECK(GuBootLoadAssets()==EFI_DEVICE_ERROR);
  CHECK(FPoolLive==2&&GuAssets[0].firmware.data&&GuAssets[1].firmware.data&&!GuAssets[2].firmware.data);tidy();
  Case="assets: short read";
  reset();world();put_assets(V[V_USB]);CHECK(open_on(V[V_USB])==EFI_SUCCESS);V[V_USB]->short_read=1;
  CHECK(GuBootLoadAssets()==EFI_DEVICE_ERROR);tidy();
  Case="assets: refused once the GPU was touched, or without a home volume";
  reset();world();put_assets(V[V_USB]);CHECK(open_on(V[V_USB])==EFI_SUCCESS);Gu.started=TRUE;CHECK(GuBootLoadAssets()==EFI_NOT_READY);
  Gu.started=FALSE;GuBootClose();CHECK(GuBootLoadAssets()==EFI_NOT_READY);tidy();
}

/* ---------------------------------------------------------------- Windows */
static UINTN expect_path(const FVol *v,UINT8 *out) {
  UINTN body=v->pathlen-4;memcpy(out,v->path,body);UINTN fl=file_node(out+body,WinFile);
  out[body+fl]=0x7f;out[body+fl+1]=0xff;out[body+fl+2]=4;out[body+fl+3]=0;return body+fl+4;
}
static void check_staged(const FVol *vol,int kind) {
  CHECK(FLoads==1&&FImgN==1);FImage *im=&FImgs[0];UINT8 ex[256];UINTN el=expect_path(vol,ex);
  CHECK(im->pathlen==el&&!memcmp(im->path,ex,el));CHECK(im->device==vol->handle);
  CHECK(im->srclen==PeLen[kind]&&!memcmp(im->src,pe(kind),PeLen[kind]));CHECK(Windows==im->handle);CHECK(im->parent==Self);
}
/* A surveyed machine with the app on the USB stick: Windows on the ESP only, firmware boot options as found. */
static void wsetup(void) {
  reset();world();put_windows(V[V_WIN],PE_A);survey_boot_vars();CHECK(open_on(V[V_USB])==EFI_SUCCESS);
}
static FVol *second_disk(void) {
  UINT8 p[98];patched_path(RealFsPath1,sizeof RealFsPath1,1,0x800,0x32000,GuidDisk2,p);return fv_new(p,sizeof p);
}
static void t_windows(void) {
  Case="windows: real Boot0000 (short-form HD entry) selects the ESP";
  wsetup();CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);
  {const char *l=logtxt(V[V_USB],"RUN0001.TXT");CHECK(strstr(l,"Windows Boot Manager entry in BootOrder: found"));
   CHECK(strstr(l,"found=1 matching-boot-entry=1"));CHECK(strstr(l,"(logged, not pinned)"));
   UINT8 d[32];char hex[65];GuSha256(pe(PE_A),PeLen[PE_A],d);for(int i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",d[i]);CHECK(strstr(l,hex));}
  CHECK(fv_untouched(V[V_WIN])&&fv_untouched(V[V_C])&&fv_untouched(V[V_RE])&&fv_untouched(V[V_SATA]));CHECK(FVarReads>=2);tidy();

  Case="windows: same result when the app itself runs from the ESP";
  reset();world();put_windows(V[V_WIN],PE_A);survey_boot_vars();CHECK(open_on(V[V_WIN])==EFI_SUCCESS);
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);tidy();

  Case="windows: case-insensitive file name in the entry";
  wsetup();{UINT8 hd[42];hd_node(V[V_WIN],hd);boot_entry(0,"Windows Boot Manager",hd,42,"\\efi\\microsoft\\boot\\BootMgfw.EFI");}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"entry in BootOrder: found"));tidy();

  Case="windows: full hardware path in the entry also matches";
  wsetup();{UINT8 pre[128];UINTN n=V[V_WIN]->pathlen-4;memcpy(pre,V[V_WIN]->path,n);boot_entry(0,"Windows Boot Manager",pre,n,WinFile);}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"entry in BootOrder: found"));tidy();

  Case="windows: two Windows disks, the BootOrder entry decides";
  for(int pick=0;pick<2;pick++) {
    wsetup();FVol *w2=second_disk();put_windows(w2,PE_B);
    UINT8 hd[42];hd_node(pick?w2:V[V_WIN],hd);boot_entry(0,"Windows Boot Manager",hd,42,WinFile);
    CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(pick?w2:V[V_WIN],pick?PE_B:PE_A);
    CHECK(fv_untouched(V[V_WIN])&&fv_untouched(w2));tidy();
  }
  Case="windows: the first matching BootOrder entry wins";
  wsetup();{FVol *w2=second_disk();put_windows(w2,PE_B);UINT8 hd[42];static const UINT16 order[3]={7,1,0};
    hd_node(w2,hd);boot_entry(7,"Windows Boot Manager",hd,42,WinFile);boot_order(order,3);
    CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(w2,PE_B);}tidy();

  Case="windows: no BootOrder variable, exactly one candidate";
  reset();world();put_windows(V[V_WIN],PE_A);CHECK(open_on(V[V_USB])==EFI_SUCCESS);
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"not found; scanning volumes"));tidy();

  Case="windows: BootOrder without a Windows entry, exactly one candidate";
  wsetup();{static const UINT16 order[1]={1};boot_order(order,1);}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);tidy();

  Case="windows: no entry and two candidates is ambiguous";
  reset();world();put_windows(V[V_WIN],PE_A);{FVol *w2=second_disk();put_windows(w2,PE_B);}CHECK(open_on(V[V_USB])==EFI_SUCCESS);
  CHECK(GuBootLoadWindows()==EFI_NOT_FOUND);CHECK(FLoads==0&&!Windows);tidy();

  Case="windows: stale entry (its volume is gone) while another volume has a boot manager";
  wsetup();{UINT8 hd[42];hd_node(V[V_WIN],hd);hd[24]^=0xff;boot_entry(0,"Windows Boot Manager",hd,42,WinFile);}
  CHECK(GuBootLoadWindows()==EFI_NOT_FOUND);CHECK(FLoads==0);tidy();

  Case="windows: entry volume present but without a boot manager";
  wsetup();{FVol *w2=second_disk();UINT8 hd[42];hd_node(w2,hd);boot_entry(0,"Windows Boot Manager",hd,42,WinFile);}
  CHECK(GuBootLoadWindows()==EFI_NOT_FOUND);CHECK(FLoads==0);tidy();

  Case="windows: a decoy bootmgfw.efi on the USB stick is not chosen when the entry points elsewhere";
  wsetup();put_windows(V[V_USB],PE_B);CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);
  CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"found=2 matching-boot-entry=1"));tidy();
  Case="windows: the same decoy without any entry makes it ambiguous";
  reset();world();put_windows(V[V_WIN],PE_A);put_windows(V[V_USB],PE_B);CHECK(open_on(V[V_USB])==EFI_SUCCESS);
  CHECK(GuBootLoadWindows()==EFI_NOT_FOUND);CHECK(FLoads==0);tidy();

  Case="windows: wrong description is not a Windows entry";
  for(int v=0;v<3;v++) {
    wsetup();{UINT8 hd[42];hd_node(V[V_WIN],hd);static const UINT16 order[1]={0};
      boot_entry(0,v==0?"Windows Boot Manager ":v==1?"windows boot manager":"Windows Boot Manage",hd,42,WinFile);boot_order(order,1);}
    CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"not found; scanning volumes"));tidy();
  }
  Case="windows: description that merely starts with the right words (no terminator) is not an entry";
  wsetup();{UINT8 b[256],hd[42];UINTN at=6;w32(b,1);hd_node(V[V_WIN],hd);
    for(const char *d="Windows Boot ManagerX";*d;d++){w16(b+at,(UINT16)(unsigned char)*d);at+=2;}   /* no NUL: the path list follows 'X' */
    UINTN list=at;memcpy(b+at,hd,42);at+=42;at+=file_node(b+at,WinFile);b[at++]=0x7f;b[at++]=0xff;b[at++]=4;b[at++]=0;
    w16(b+4,(UINT16)(at-list));fvar_set("Boot0000",b,at);}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"not found; scanning volumes"));tidy();
  Case="windows: an entry that matches inside another volume's node, not on a node boundary, does not select it";
  wsetup();{UINT8 p[200],hd[42];UINTN n=12;hd_node(V[V_WIN],hd);memcpy(p,V[V_WIN]->path,12);          /* PciRoot(0x0) */
    p[n]=4;p[n+1]=3;w16(p+n+2,4+16+42);memset(p+n+4,0xee,16);memcpy(p+n+20,hd,42);n+=62;               /* vendor media node hiding the HD bytes */
    p[n++]=0x7f;p[n++]=0xff;p[n++]=4;p[n++]=0;{FVol *evil=fv_new(p,n);put_windows(evil,PE_B);}
    boot_entry(0,"Windows Boot Manager",hd,42,WinFile);}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"found=2 matching-boot-entry=1"));tidy();
  Case="windows: right description but not bootmgfw.efi";
  wsetup();{UINT8 hd[42];hd_node(V[V_WIN],hd);boot_entry(0,"Windows Boot Manager",hd,42,"\\EFI\\BOOT\\BOOTX64.EFI");}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"not found; scanning volumes"));tidy();

  Case="windows: entry without a volume part is ignored";
  wsetup();boot_entry(0,"Windows Boot Manager",(const UINT8 *)"",0,WinFile);
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"not found; scanning volumes"));tidy();

  Case="windows: damaged firmware variables are skipped";
  wsetup();{static const UINT16 o3[3]={5,6,0};UINT8 hd[42];hd_node(V[V_WIN],hd);boot_order(o3,3);
    fvar_set("Boot0006",(const UINT8 *)"\1\0\0\0\0\0",6);                /* too short */
    boot_entry(0,"Windows Boot Manager",hd,42,WinFile);}                  /* Boot0005 missing altogether */
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"entry in BootOrder: found"));tidy();
  Case="windows: path list longer than the variable";
  wsetup();{UINT8 b[256];UINT8 hd[42];hd_node(V[V_WIN],hd);boot_entry(0,"Windows Boot Manager",hd,42,WinFile);
    FVar *v0=NULL;for(int i=0;i<FVarN;i++)if(!memcmp(FVars[i].name,L"Boot0000",16))v0=&FVars[i];
    memcpy(b,v0->data,v0->size);w16(b+4,2000);fvar_set("Boot0000",b,v0->size);}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"not found; scanning volumes"));tidy();
  Case="windows: odd-sized BootOrder is ignored";
  wsetup();fvar_set("BootOrder","\1\0\0",3);CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"not found; scanning volumes"));tidy();
  Case="windows: boot option names use upper-case hex digits";
  wsetup();{static const UINT16 o[1]={0x0A1F};UINT8 hd[42];hd_node(V[V_WIN],hd);boot_order(o,1);boot_entry(0x0A1F,"Windows Boot Manager",hd,42,WinFile);}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(strstr(logtxt(V[V_USB],"RUN0001.TXT"),"entry in BootOrder: found"));tidy();

  static const struct {int kind;const char *name;} bad[]={{PE_NOMZ,"no MZ"},{PE_MACHINE,"wrong machine"},{PE_SUBSYS,"console subsystem"},
    {PE_MAGIC,"PE32 not PE32+"},{PE_PEOFF,"PE offset beyond the file"},{PE_NOSIG,"no PE signature"},{PE_SMALL,"smaller than 1 MiB"},{PE_BIG,"larger than 16 MiB"}};
  for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++) {
    char cn[96];snprintf(cn,sizeof cn,"windows: rejected boot manager (%s)",bad[i].name);Case=cn;
    reset();world();put_windows(V[V_WIN],bad[i].kind);survey_boot_vars();CHECK(open_on(V[V_USB])==EFI_SUCCESS);
    CHECK(GuBootLoadWindows()==EFI_COMPROMISED_DATA);CHECK(FLoads==0&&!Windows);tidy();
  }
  Case="windows: LoadImage fails";
  wsetup();FLoadStatus=EFI_SECURITY_VIOLATION;CHECK(GuBootLoadWindows()==EFI_SECURITY_VIOLATION);CHECK(!Windows&&FImgN==0);tidy();
  Case="windows: image loaded from the wrong device is unloaded again";
  wsetup();FLoadWrongDevice=1;CHECK(GuBootLoadWindows()==EFI_UNSUPPORTED);CHECK(!Windows&&FUnloads==1);tidy();
  Case="windows: staging twice, after the GPU was touched, before open";
  wsetup();CHECK(GuBootLoadWindows()==EFI_SUCCESS);CHECK(GuBootLoadWindows()==EFI_NOT_READY&&FLoads==1);tidy();
  wsetup();Gu.started=TRUE;CHECK(GuBootLoadWindows()==EFI_NOT_READY&&FLoads==0);Gu.started=FALSE;tidy();
  reset();world();CHECK(GuBootLoadWindows()==EFI_NOT_READY);tidy();

  Case="windows: unreadable or malformed volumes are skipped";
  wsetup();V[V_C]->no_media=1;V[V_RE]->no_media=1;{static UINT8 junk[16]={2,1,12,0,0xd0,0x41,3,10};FVol *j=fv_new(junk,16);put_windows(j,PE_B);}
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);tidy();
  Case="windows: the entry's own volume cannot be opened";
  wsetup();V[V_WIN]->no_media=1;CHECK(GuBootLoadWindows()==EFI_NOT_FOUND);tidy();
  Case="windows: handle enumeration fails";
  wsetup();FLocateFail=EFI_DEVICE_ERROR;CHECK(GuBootLoadWindows()==EFI_DEVICE_ERROR);tidy();
  Case="windows: staging works with no log at all, and does not fault";
  reset();world();put_windows(V[V_WIN],PE_A);survey_boot_vars();V[V_USB]->write_protected=1;CHECK(open_on(V[V_USB])!=EFI_SUCCESS);
  CHECK(GuBootLoadWindows()==EFI_SUCCESS);check_staged(V[V_WIN],PE_A);CHECK(!Gu.fault);tidy();
  Case="windows: volumes holding the OS were never written";
  wsetup();CHECK(GuBootLoadWindows()==EFI_SUCCESS);for(int i=1;i<V_N;i++)CHECK(fv_untouched(V[i]));tidy();
}
static void t_start(void) {
  Case="start: StartImage once, then never again";
  wsetup();FStartStatus=EFI_ABORTED;CHECK(GuBootLoadWindows()==EFI_SUCCESS);EFI_HANDLE h=Windows;
  CHECK(GuBootWindows()==EFI_ABORTED);CHECK(FStarts==1&&fimg_of(h)->started&&!Windows);
  CHECK(GuBootWindows()==EFI_NOT_READY&&FStarts==1);GuBootUnloadWindows();CHECK(FUnloads==0);tidy();
  Case="start: unload without start";
  wsetup();CHECK(GuBootLoadWindows()==EFI_SUCCESS);GuBootUnloadWindows();CHECK(FUnloads==1&&!Windows);
  GuBootUnloadWindows();CHECK(FUnloads==1);CHECK(GuBootWindows()==EFI_NOT_READY&&FStarts==0);tidy();
  Case="start: nothing staged";
  wsetup();CHECK(GuBootWindows()==EFI_NOT_READY&&FStarts==0);tidy();
}
static jmp_buf HaltJmp;
static void halt_hook(void) {if(FStalls==3)longjmp(HaltJmp,1);}
static void t_halt(void) {
  Case="halt: message, then waits forever in 1 s steps";
  reset();world();Table=&FSt;FStallHook=halt_hook;
  if(!setjmp(HaltJmp))GuBootHalt();
  CHECK(FStalls==3&&FStallUs==3000000);CHECK(fcon_has("STOPPED. Power OFF"));
  Case="halt: silent console stays silent";
  reset();world();Table=&FSt;FStallHook=halt_hook;GuBootSilence();
  if(!setjmp(HaltJmp))GuBootHalt();
  CHECK(FStalls==3&&FConN==0);
}

/* ------------------------------------------------------------------ menu */
static void script(const char *s) {
  fkey_clear();
  for(;*s;s++) switch(*s){
    case '.':fkey_nr(1);break;case 'E':fkey_char(27);break;case '~':fkey_scan(0x17);break;
    case '!':fkey_err(EFI_DEVICE_ERROR);break;case '^':fkey_char('\r');break;default:fkey_char((CHAR16)(unsigned char)*s);
  }
}
static EFI_STATUS run_menu(const char *keys,BOOLEAN def,GU_CHOICE *c) {
  reset();Table=&FSt;script(keys);return GuBootMenu(def,3,c);
}
static void t_menu(void) {
  GU_CHOICE c;
  Case="menu: countdown expires into the default";
  CHECK(run_menu("",TRUE,&c)==EFI_SUCCESS&&c==GU_CHOICE_UNLOCK);
  CHECK(FStalls==30&&FStallUs==3000000&&FKeyReads==30);CHECK(fcon_has("unlock starts in 3 s")&&fcon_has("in 2 s")&&fcon_has("in 1 s")&&!fcon_has("in 0 s"));
  CHECK(fcon_has("Esc: menu"));CHECK(FCon[FConN-1]=='\n');
  CHECK(run_menu("",FALSE,&c)==EFI_SUCCESS&&c==GU_CHOICE_WINDOWS);CHECK(FStalls==30&&fcon_has("Windows starts in 3 s")&&fcon_has("U: unlock now"));
  Case="menu: single keys in the countdown";
  CHECK(run_menu("u",FALSE,&c)==EFI_SUCCESS&&c==GU_CHOICE_UNLOCK&&FStalls==0&&FKeyReads==1);
  CHECK(run_menu("U",FALSE,&c)==EFI_SUCCESS&&c==GU_CHOICE_UNLOCK);
  { const char *w="sSwW";for(int i=0;i<4;i++){char k[2]={w[i],0};CHECK(run_menu(k,TRUE,&c)==EFI_SUCCESS&&c==GU_CHOICE_WINDOWS&&FStalls==0);} }
  CHECK(run_menu("^",TRUE,&c)==EFI_SUCCESS&&c==GU_CHOICE_UNLOCK&&FStalls==0);
  CHECK(run_menu("^",FALSE,&c)==EFI_SUCCESS&&c==GU_CHOICE_WINDOWS);
  CHECK(run_menu("x.y",TRUE,&c)==EFI_SUCCESS&&c==GU_CHOICE_UNLOCK&&FKeyReads==30&&FStalls==30);
  CHECK(run_menu("..u",FALSE,&c)==EFI_SUCCESS&&c==GU_CHOICE_UNLOCK&&FStalls==2);
  Case="menu: Esc opens a menu with exactly two entries";
  CHECK(run_menu("E",TRUE,&c)==EFI_SUCCESS&&c==GU_CHOICE_WINDOWS);
  CHECK(fcon_has("1  Unlock, then start Windows")&&fcon_has("2  Start Windows without the mod")&&!fcon_has("3  ")&&!fcon_has("irmware"));
  CHECK(FKeyReads==1201&&FStalls==1200);
  CHECK(run_menu("~",TRUE,&c)==EFI_SUCCESS&&c==GU_CHOICE_WINDOWS&&fcon_has("RTX 4090 unlock menu")&&FStalls==1200);
  { static const struct {const char *k;GU_CHOICE c;} m[]={{"E1",GU_CHOICE_UNLOCK},{"Eu",GU_CHOICE_UNLOCK},{"EU",GU_CHOICE_UNLOCK},
      {"E2",GU_CHOICE_WINDOWS},{"Ew",GU_CHOICE_WINDOWS},{"EW",GU_CHOICE_WINDOWS},{"Es",GU_CHOICE_WINDOWS},{"E~",GU_CHOICE_WINDOWS},
      {"EE",GU_CHOICE_WINDOWS},{"E~~",GU_CHOICE_WINDOWS},{"~1",GU_CHOICE_UNLOCK},{"E3x1",GU_CHOICE_UNLOCK},{"E3",GU_CHOICE_WINDOWS}};
    for(unsigned i=0;i<sizeof m/sizeof m[0];i++){
      CHECK(run_menu(m[i].k,FALSE,&c)==EFI_SUCCESS&&c==m[i].c);
      CHECK(run_menu(m[i].k,TRUE,&c)==EFI_SUCCESS&&c==m[i].c);
    } }
  CHECK(run_menu("E..1",FALSE,&c)==EFI_SUCCESS&&c==GU_CHOICE_UNLOCK&&FStalls==2);
  Case="menu: key errors and stall errors";
  CHECK(run_menu("!",TRUE,&c)==EFI_DEVICE_ERROR&&c==GU_CHOICE_UNLOCK);
  CHECK(run_menu("!",FALSE,&c)==EFI_DEVICE_ERROR&&c==GU_CHOICE_WINDOWS);
  CHECK(run_menu("E!",TRUE,&c)==EFI_DEVICE_ERROR);
  reset();Table=&FSt;script("");FStallFailAt=5;FStallFailStatus=EFI_DEVICE_ERROR;CHECK(GuBootMenu(TRUE,3,&c)==EFI_DEVICE_ERROR&&FStalls==5);
  reset();Table=&FSt;script("E");FStallFailAt=3;FStallFailStatus=EFI_DEVICE_ERROR;CHECK(GuBootMenu(TRUE,3,&c)==EFI_DEVICE_ERROR&&FStalls==3);
  Case="menu: bad parameters";
  reset();Table=&FSt;CHECK(GuBootMenu(TRUE,0,&c)==EFI_UNSUPPORTED);CHECK(GuBootMenu(TRUE,3,NULL)==EFI_UNSUPPORTED);
  FSt.ConIn=NULL;CHECK(GuBootMenu(TRUE,3,&c)==EFI_UNSUPPORTED);FSt.ConIn=&FIn;FIn.ReadKeyStroke=NULL;CHECK(GuBootMenu(TRUE,3,&c)==EFI_UNSUPPORTED);
  reset();CHECK(GuBootMenu(TRUE,3,&c)==EFI_UNSUPPORTED);
  Case="menu: silenced console still works and prints nothing";
  reset();Table=&FSt;script("E1");GuBootSilence();CHECK(GuBootMenu(FALSE,3,&c)==EFI_SUCCESS&&c==GU_CHOICE_UNLOCK&&FConN==0);
}

int main(int argc,char **argv) {
  load_real_assets(argc>1?argv[1]:"/usr/lib/firmware/gpu-unlock/610.43.03");
  t_open_homes();t_open_failures();t_ring();t_console();t_assets();t_windows();t_start();t_halt();t_menu();
  printf("boot_io: %u checks, all scenarios PASS (fake EFI environment, real survey device paths and Boot0000)\n",Checks);
  return 0;
}
