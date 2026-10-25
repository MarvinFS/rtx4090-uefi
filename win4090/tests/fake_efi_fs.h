/* SPDX-License-Identifier: GPL-2.0-only */
/* Fake UEFI environment for the host tests of boot_io.c and app.c (hosted gcc only, no hardware):
 * a handle/protocol table, in-memory FAT-like volumes behind SimpleFileSystem / EFI_FILE_PROTOCOL,
 * a Boot/Runtime Services subset, and a scripted console. Everything it hands out is tracked so a
 * test can assert that nothing leaks (pool, open file handles) and that nothing was written where
 * it must not be (per-volume create/write/delete counters, no variable writes). */
#pragma once
/* Each test uses a different subset of this shared fake. */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "../port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>
#include <Guid/FileInfo.h>

#define FAKE_UNUSED __attribute__((unused))
#define FCHECK(x) do{if(!(x)){fprintf(stderr,"fake env FAIL %s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)

static EFI_GUID FG_Fs=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,FG_Path=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID FG_Loaded=EFI_LOADED_IMAGE_PROTOCOL_GUID,FG_Info=EFI_FILE_INFO_ID;
FAKE_UNUSED static EFI_GUID FG_Pci=EFI_PCI_IO_PROTOCOL_GUID;

/* ---- hex helper ---- */
FAKE_UNUSED static UINTN fhex(const char *h,UINT8 *out) {
  UINTN n=0;
  for(;h[0]&&h[1];h+=2,n++){
    unsigned v;char b[3]={h[0],h[1],0};sscanf(b,"%x",&v);out[n]=(UINT8)v;
  }
  return n;
}

/* ---- pool tracking ---- */
static void *FPool[8192];static unsigned long FPoolLive;static unsigned FPoolAllocs;
static EFI_STATUS EFIAPI f_alloc(EFI_MEMORY_TYPE t,UINTN n,VOID **p) {
  (void)t;void *m=malloc(n?n:1);
  if(!m)return EFI_OUT_OF_RESOURCES;
  for(int i=0;i<8192;i++)if(!FPool[i]){FPool[i]=m;FPoolLive++;FPoolAllocs++;*p=m;return EFI_SUCCESS;}
  FCHECK(!"pool table full");return EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS EFIAPI f_free(VOID *p) {
  if(!p)return EFI_INVALID_PARAMETER;
  for(int i=0;i<8192;i++)if(FPool[i]==p){FPool[i]=NULL;FPoolLive--;free(p);return EFI_SUCCESS;}
  FCHECK(!"FreePool of an unknown or already freed pointer");return EFI_INVALID_PARAMETER;
}

/* ---- handle / protocol table ---- */
typedef struct {EFI_GUID *guid;VOID *iface;} FProto;
typedef struct {FProto p[8];int n;} FHandle;
static FHandle FH[128];static int FHn;
static EFI_HANDLE fh_new(void) {FCHECK(FHn<128);return (EFI_HANDLE)&FH[FHn++];}
static void fh_add(EFI_HANDLE h,EFI_GUID *g,VOID *iface) {FHandle *f=h;FCHECK(f->n<8);f->p[f->n].guid=g;f->p[f->n].iface=iface;f->n++;}
static void fh_drop(EFI_HANDLE h) {FHandle *f=h;f->n=0;}
static VOID *fh_get(EFI_HANDLE h,EFI_GUID *g) {
  FHandle *f=h;for(int i=0;f&&i<f->n;i++)if(!memcmp(f->p[i].guid,g,16))return f->p[i].iface;return NULL;
}
static EFI_STATUS EFIAPI f_handleprotocol(EFI_HANDLE h,EFI_GUID *g,VOID **iface) {
  FCHECK(h&&(FHandle *)h>=FH&&(FHandle *)h<FH+128);
  VOID *p=fh_get(h,g);*iface=p;return p?EFI_SUCCESS:EFI_UNSUPPORTED;
}
static EFI_STATUS FLocateFail;
static EFI_STATUS EFIAPI f_locatebuffer(EFI_LOCATE_SEARCH_TYPE t,EFI_GUID *g,VOID *key,UINTN *n,EFI_HANDLE **out) {
  (void)key;FCHECK(t==ByProtocol);
  if(FLocateFail)return FLocateFail;
  UINTN c=0;for(int i=0;i<FHn;i++)if(fh_get(&FH[i],g))c++;
  if(!c){*n=0;*out=NULL;return EFI_NOT_FOUND;}
  EFI_HANDLE *a;EFI_STATUS s=f_alloc(EfiBootServicesData,c*sizeof(EFI_HANDLE),(VOID **)&a);if(s)return s;
  c=0;for(int i=0;i<FHn;i++)if(fh_get(&FH[i],g))a[c++]=&FH[i];
  *n=c;*out=a;return EFI_SUCCESS;
}

/* ---- device path helpers ---- */
/* Length of a complete path including its end node (0 if malformed). */
FAKE_UNUSED static UINTN fp_len(const UINT8 *p) {
  UINTN at=0;
  for(int i=0;i<64;i++){
    UINTN n=p[at+2]|((UINTN)p[at+3]<<8);if(n<4)return 0;
    if(p[at]==0x7f)return p[at+1]==0xff&&n==4?at+4:0;
    at+=n;
  }
  return 0;
}

/* ---- in-memory volumes ---- */
typedef struct FNode FNode;
struct FNode {CHAR16 name[64];int dir,deleted;FNode *parent,**kid;int nkid,capkid;UINT8 *data;UINT64 size;int owned;EFI_TIME mtime;};
typedef struct {
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL fs;FNode *root;EFI_HANDLE handle;UINT8 *path;UINTN pathlen;
  int write_protected,full,full_files,no_media,fail_delete,fail_flush,short_read;
  UINT64 read_budget;                 /* bytes readable before EFI_DEVICE_ERROR (all ones: unlimited) */
  char fail_name[16];EFI_STATUS fail_status; /* Open of this component fails with this status */
  unsigned opens,creates,writes,deletes,flushes;UINT64 bytes_written;
} FVol;
typedef struct {EFI_FILE_PROTOCOL p;FNode *n;FVol *v;UINT64 pos;int writable;} FFile;
static int FOpenLive;
static FVol *FVols[32];static int FVn;

static int ci_eq(const CHAR16 *a,const CHAR16 *b) {
  for(;;a++,b++){
    CHAR16 x=*a,y=*b;if(x>='a'&&x<='z')x=(CHAR16)(x-32);if(y>='a'&&y<='z')y=(CHAR16)(y-32);
    if(x!=y)return 0;if(!x)return 1;
  }
}
static FNode *fn_new(FNode *parent,const CHAR16 *name,int dir) {
  FNode *n=calloc(1,sizeof *n);int i=0;n->parent=parent;n->dir=dir;
  for(;name[i]&&i<63;i++)n->name[i]=name[i];
  if(parent){
    if(parent->nkid==parent->capkid){parent->capkid=parent->capkid?parent->capkid*2:8;parent->kid=realloc(parent->kid,parent->capkid*sizeof *parent->kid);}
    parent->kid[parent->nkid++]=n;
  }
  return n;
}
static FNode *fn_find(FNode *d,const CHAR16 *c) {
  for(int i=0;d&&i<d->nkid;i++)if(!d->kid[i]->deleted&&ci_eq(d->kid[i]->name,c))return d->kid[i];
  return NULL;
}
static EFI_STATUS fn_walk(FNode *base,FNode *root,const CHAR16 *path,FNode **parent,FNode **node,CHAR16 *last) {
  FNode *cur=path[0]==L'\\'?root:base;*parent=NULL;*node=NULL;last[0]=0;
  while(*path==L'\\')path++;
  if(!*path){*node=cur;return EFI_SUCCESS;}
  for(;;){
    CHAR16 comp[64];int n=0;
    while(*path&&*path!=L'\\'){if(n>=63)return EFI_INVALID_PARAMETER;comp[n++]=*path++;}
    comp[n]=0;while(*path==L'\\')path++;
    if(!*path){*parent=cur;*node=fn_find(cur,comp);memcpy(last,comp,(size_t)(n+1)*2);return EFI_SUCCESS;}
    cur=fn_find(cur,comp);if(!cur||!cur->dir)return EFI_NOT_FOUND;
  }
}
static FFile *ff_new(FVol *v,FNode *n,int writable);

static EFI_STATUS EFIAPI ff_open(EFI_FILE_PROTOCOL *This,EFI_FILE_PROTOCOL **New,CHAR16 *Name,UINT64 Mode,UINT64 Attr) {
  FFile *f=(FFile *)This;FVol *v=f->v;FNode *parent,*node;CHAR16 last[64];v->opens++;
  EFI_STATUS s=fn_walk(f->n,v->root,Name,&parent,&node,&last[0]);
  if(s)return s;
  if(v->fail_name[0]&&parent) {
    CHAR16 w[16];int i=0;for(;v->fail_name[i];i++)w[i]=(CHAR16)(unsigned char)v->fail_name[i];w[i]=0;
    if(ci_eq(last,w))return v->fail_status;
  }
  int create=(Mode&EFI_FILE_MODE_CREATE)!=0,write=(Mode&EFI_FILE_MODE_WRITE)!=0;
  if(write&&v->write_protected)return EFI_WRITE_PROTECTED;
  if(!node) {
    if(!create)return EFI_NOT_FOUND;
    if(v->write_protected)return EFI_WRITE_PROTECTED;
    if(v->full||(v->full_files&&!(Attr&EFI_FILE_DIRECTORY)))return EFI_VOLUME_FULL;
    node=fn_new(parent,last,(Attr&EFI_FILE_DIRECTORY)!=0);v->creates++;
  }
  *New=&ff_new(v,node,write)->p;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ff_close(EFI_FILE_PROTOCOL *This) {FOpenLive--;free(This);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI ff_delete(EFI_FILE_PROTOCOL *This) {
  FFile *f=(FFile *)This;FVol *v=f->v;EFI_STATUS s=v->fail_delete?EFI_WARN_DELETE_FAILURE:EFI_SUCCESS;
  if(!v->fail_delete){f->n->deleted=1;v->deletes++;}
  FOpenLive--;free(f);return s;
}
static EFI_STATUS EFIAPI ff_read(EFI_FILE_PROTOCOL *This,UINTN *Size,VOID *Buf) {
  FFile *f=(FFile *)This;if(f->n->dir)return EFI_UNSUPPORTED;
  UINT64 left=f->pos>=f->n->size?0:f->n->size-f->pos;UINTN n=*Size<left?*Size:(UINTN)left;
  if(f->v->short_read&&n>1)n/=2;
  if(f->v->read_budget!=~0ULL){if(n>f->v->read_budget)return EFI_DEVICE_ERROR;f->v->read_budget-=n;}
  if(n)memcpy(Buf,f->n->data+f->pos,n);
  f->pos+=n;*Size=n;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ff_write(EFI_FILE_PROTOCOL *This,UINTN *Size,VOID *Buf) {
  FFile *f=(FFile *)This;FVol *v=f->v;FNode *n=f->n;
  if(!f->writable)return EFI_ACCESS_DENIED;
  if(v->write_protected)return EFI_WRITE_PROTECTED;
  if(n->dir)return EFI_UNSUPPORTED;
  UINT64 end=f->pos+*Size;
  if(v->full&&end>n->size)return EFI_VOLUME_FULL;
  if(!n->owned){UINT8 *c=malloc(n->size?n->size:1);if(n->size)memcpy(c,n->data,n->size);n->data=c;n->owned=1;}
  if(end>n->size){n->data=realloc(n->data,end);memset(n->data+n->size,0,end-n->size);n->size=end;}
  memcpy(n->data+f->pos,Buf,*Size);f->pos=end;v->writes++;v->bytes_written+=*Size;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ff_getpos(EFI_FILE_PROTOCOL *This,UINT64 *P) {*P=((FFile *)This)->pos;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI ff_setpos(EFI_FILE_PROTOCOL *This,UINT64 P) {((FFile *)This)->pos=P==~0ULL?((FFile *)This)->n->size:P;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI ff_getinfo(EFI_FILE_PROTOCOL *This,EFI_GUID *Type,UINTN *Size,VOID *Buf) {
  FFile *f=(FFile *)This;UINTN nl=0;
  if(memcmp(Type,&FG_Info,16))return EFI_UNSUPPORTED;
  while(f->n->name[nl])nl++;
  UINTN need=sizeof(EFI_FILE_INFO)+nl*2;
  if(*Size<need){*Size=need;return EFI_BUFFER_TOO_SMALL;}
  EFI_FILE_INFO *i=Buf;memset(i,0,need);i->Size=need;i->FileSize=f->n->size;i->PhysicalSize=(f->n->size+4095)&~4095ULL;
  i->Attribute=f->n->dir?EFI_FILE_DIRECTORY:EFI_FILE_ARCHIVE;i->ModificationTime=f->n->mtime;
  memcpy(i->FileName,f->n->name,(nl+1)*2);*Size=need;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ff_setinfo(EFI_FILE_PROTOCOL *This,EFI_GUID *T,UINTN S,VOID *B) {(void)This;(void)T;(void)S;(void)B;return EFI_UNSUPPORTED;}
static EFI_STATUS EFIAPI ff_flush(EFI_FILE_PROTOCOL *This) {
  FVol *v=((FFile *)This)->v;v->flushes++;return v->fail_flush?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static FFile *ff_new(FVol *v,FNode *n,int writable) {
  FFile *f=calloc(1,sizeof *f);f->n=n;f->v=v;f->writable=writable;FOpenLive++;
  f->p.Revision=EFI_FILE_PROTOCOL_REVISION;f->p.Open=ff_open;f->p.Close=ff_close;f->p.Delete=ff_delete;
  f->p.Read=ff_read;f->p.Write=ff_write;f->p.GetPosition=ff_getpos;f->p.SetPosition=ff_setpos;
  f->p.GetInfo=ff_getinfo;f->p.SetInfo=ff_setinfo;f->p.Flush=ff_flush;return f;
}
static EFI_STATUS EFIAPI fv_openvolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,EFI_FILE_PROTOCOL **Root) {
  FVol *v=(FVol *)This;if(v->no_media)return EFI_NO_MEDIA;
  *Root=&ff_new(v,v->root,1)->p;return EFI_SUCCESS;
}
/* A volume with a device path (copied) and its SimpleFileSystem + DevicePath protocols on a new handle. */
static FVol *fv_new(const UINT8 *path,UINTN pathlen) {
  FVol *v=calloc(1,sizeof *v);v->fs.Revision=0x10000;v->fs.OpenVolume=fv_openvolume;v->read_budget=~0ULL;
  v->root=fn_new(NULL,L"",1);v->path=malloc(pathlen);memcpy(v->path,path,pathlen);v->pathlen=pathlen;
  v->handle=fh_new();fh_add(v->handle,&FG_Fs,&v->fs);fh_add(v->handle,&FG_Path,v->path);
  FCHECK(FVn<32);FVols[FVn++]=v;return v;
}
FAKE_UNUSED static void fn_time(FNode *n,int y,int mo,int d,int h,int mi,int s) {
  memset(&n->mtime,0,sizeof n->mtime);n->mtime.Year=(UINT16)y;n->mtime.Month=(UINT8)mo;n->mtime.Day=(UINT8)d;
  n->mtime.Hour=(UINT8)h;n->mtime.Minute=(UINT8)mi;n->mtime.Second=(UINT8)s;
}
FAKE_UNUSED static void asc2w(CHAR16 *d,const char *s,int cap) {int i=0;for(;s[i]&&i<cap-1;i++)d[i]=(CHAR16)(unsigned char)s[i];d[i]=0;}
/* Create (or replace) a file at an ASCII path with backslashes; data is borrowed unless copy != 0. */
FAKE_UNUSED static FNode *fv_put(FVol *v,const char *path,const void *data,UINT64 size,int copy) {
  FNode *cur=v->root;CHAR16 comp[64];
  while(*path){
    int n=0;while(*path=='\\')path++;
    while(*path&&*path!='\\')comp[n++]=(CHAR16)(unsigned char)*path++;
    comp[n]=0;if(!n)break;
    FNode *k=fn_find(cur,comp);
    if(*path){if(!k)k=fn_new(cur,comp,1);cur=k;}
    else {
      if(!k)k=fn_new(cur,comp,0);
      if(copy){k->data=malloc(size?size:1);if(size)memcpy(k->data,data,size);k->owned=1;}else{k->data=(UINT8 *)data;k->owned=0;}
      k->size=size;return k;
    }
  }
  return cur;
}
FAKE_UNUSED static FNode *fv_mkdir(FVol *v,const char *path) {
  FNode *cur=v->root;CHAR16 comp[64];
  while(*path){
    int n=0;while(*path=='\\')path++;
    while(*path&&*path!='\\')comp[n++]=(CHAR16)(unsigned char)*path++;
    comp[n]=0;if(!n)break;
    FNode *k=fn_find(cur,comp);if(!k)k=fn_new(cur,comp,1);cur=k;
  }
  return cur;
}
FAKE_UNUSED static FNode *fv_get(FVol *v,const char *path) {
  FNode *cur=v->root;CHAR16 comp[64];
  while(*path){
    int n=0;while(*path=='\\')path++;
    while(*path&&*path!='\\')comp[n++]=(CHAR16)(unsigned char)*path++;
    comp[n]=0;if(!n)break;
    cur=fn_find(cur,comp);if(!cur)return NULL;
  }
  return cur;
}
FAKE_UNUSED static int fv_untouched(const FVol *v) {return !v->creates&&!v->writes&&!v->deletes&&!v->flushes&&!v->bytes_written;}

/* ---- runtime variables (read-only for the code under test) ---- */
typedef struct {CHAR16 name[32];UINT8 *data;UINTN size;} FVar;
static FVar FVars[64];static int FVarN;static unsigned FVarReads;
FAKE_UNUSED static void fvar_set(const char *name,const void *d,UINTN n) {
  FVar *v=NULL;CHAR16 w[32]={0};asc2w(w,name,32);
  for(int i=0;i<FVarN;i++)if(!memcmp(FVars[i].name,w,sizeof w))v=&FVars[i];
  if(!v){FCHECK(FVarN<64);v=&FVars[FVarN++];}
  memcpy(v->name,w,sizeof w);v->data=malloc(n?n:1);memcpy(v->data,d,n);v->size=n;
}
static EFI_STATUS EFIAPI f_getvar(CHAR16 *name,EFI_GUID *g,UINT32 *attr,UINTN *size,VOID *data) {
  (void)g;FVarReads++;CHAR16 w[32]={0};int i=0;for(;name[i]&&i<31;i++)w[i]=name[i];
  for(int k=0;k<FVarN;k++)if(!memcmp(FVars[k].name,w,sizeof w)) {
    if(*size<FVars[k].size){*size=FVars[k].size;return EFI_BUFFER_TOO_SMALL;}
    memcpy(data,FVars[k].data,FVars[k].size);*size=FVars[k].size;if(attr)*attr=7;return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI f_setvar_trap(CHAR16 *n,EFI_GUID *g,UINT32 a,UINTN s,VOID *d) {
  (void)n;(void)g;(void)a;(void)s;(void)d;FCHECK(!"SetVariable: boot variables must never be written");return EFI_DEVICE_ERROR;
}

/* ---- scripted console ---- */
typedef struct {int kind;UINT16 scan;CHAR16 ch;EFI_STATUS st;} FKey; /* kind 0: not ready, 1: key, 2: error status */
static FKey FKeys[4096];static int FKn,FKi;static unsigned FKeyReads;
FAKE_UNUSED static void fkey_clear(void) {FKn=FKi=0;FKeyReads=0;}
FAKE_UNUSED static void fkey_nr(int count) {for(;count--;){FCHECK(FKn<4096);FKeys[FKn++]=(FKey){0,0,0,0};}}
FAKE_UNUSED static void fkey_char(CHAR16 c) {FCHECK(FKn<4096);FKeys[FKn++]=(FKey){1,0,c,0};}
FAKE_UNUSED static void fkey_scan(UINT16 s) {FCHECK(FKn<4096);FKeys[FKn++]=(FKey){1,s,0,0};}
FAKE_UNUSED static void fkey_err(EFI_STATUS s) {FCHECK(FKn<4096);FKeys[FKn++]=(FKey){2,0,0,s};}
static EFI_STATUS EFIAPI f_readkey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *t,EFI_INPUT_KEY *k) {
  (void)t;FKeyReads++;
  if(FKi>=FKn)return EFI_NOT_READY;
  FKey *e=&FKeys[FKi++];
  if(e->kind==0)return EFI_NOT_READY;
  if(e->kind==2)return e->st;
  k->ScanCode=e->scan;k->UnicodeChar=e->ch;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI f_inreset(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *t,BOOLEAN x) {(void)t;(void)x;return EFI_SUCCESS;}
static char FCon[1<<16];static size_t FConN;
static EFI_STATUS EFIAPI f_outstr(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *t,CHAR16 *s) {
  (void)t;for(;*s;s++){if(FConN<sizeof FCon-1)FCon[FConN++]=(char)*s;}FCon[FConN]=0;return EFI_SUCCESS;
}
FAKE_UNUSED static int fcon_has(const char *s) {return strstr(FCon,s)!=NULL;}

/* ---- time, watchdog, image loading ---- */
static UINT64 FStallUs;static unsigned FStalls,FStallFailAt;static EFI_STATUS FStallFailStatus;
static void (*FStallHook)(void);
static EFI_STATUS EFIAPI f_stall(UINTN us) {
  FStalls++;FStallUs+=us;if(FStallHook)FStallHook();
  return FStallFailAt&&FStalls==FStallFailAt?FStallFailStatus:EFI_SUCCESS;
}
static unsigned FWatchdogCalls;static UINTN FWatchdogTimeout=99;static EFI_STATUS FWatchdogStatus;
static EFI_STATUS EFIAPI f_watchdog(UINTN timeout,UINT64 code,UINTN n,CHAR16 *d) {
  (void)code;(void)n;(void)d;FWatchdogCalls++;FWatchdogTimeout=timeout;return FWatchdogStatus;
}
typedef struct {EFI_LOADED_IMAGE_PROTOCOL li;UINT8 *path;UINTN pathlen;UINT8 *src;UINTN srclen;EFI_HANDLE handle,device,parent;int started,unloaded;} FImage;
static FImage FImgs[8];static int FImgN;static unsigned FLoads,FStarts,FUnloads;
static EFI_STATUS FLoadStatus,FStartStatus;static int FLoadWrongDevice;
static FImage *fimg_of(EFI_HANDLE h) {for(int i=0;i<FImgN;i++)if(FImgs[i].handle==h)return &FImgs[i];return NULL;}
static EFI_STATUS EFIAPI f_loadimage(BOOLEAN policy,EFI_HANDLE parent,EFI_DEVICE_PATH_PROTOCOL *dp,VOID *src,UINTN srclen,EFI_HANDLE *out) {
  (void)policy;FLoads++;if(FLoadStatus)return FLoadStatus;
  FCHECK(dp&&src&&srclen&&FImgN<8);
  FImage *im=&FImgs[FImgN++];memset(im,0,sizeof *im);
  im->pathlen=fp_len((UINT8 *)dp);FCHECK(im->pathlen);im->path=malloc(im->pathlen);memcpy(im->path,dp,im->pathlen);
  im->src=malloc(srclen);memcpy(im->src,src,srclen);im->srclen=srclen;im->parent=parent;
  /* The device handle is the volume whose path is everything before the first file-path node. */
  UINTN pre=0;while(!(im->path[pre]==0x04&&im->path[pre+1]==0x04)&&im->path[pre]!=0x7f)pre+=im->path[pre+2]|((UINTN)im->path[pre+3]<<8);
  for(int i=0;i<FVn;i++)if(FVols[i]->pathlen-4==pre&&!memcmp(FVols[i]->path,im->path,pre))im->device=FVols[i]->handle;
  im->handle=fh_new();im->li.DeviceHandle=FLoadWrongDevice?(FVn?FVols[FVn-1]->handle:NULL):im->device;im->li.ImageCodeType=EfiLoaderCode;
  fh_add(im->handle,&FG_Loaded,&im->li);*out=im->handle;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI f_startimage(EFI_HANDLE h,UINTN *n,CHAR16 **d) {
  (void)n;(void)d;FImage *im=fimg_of(h);FCHECK(im&&!im->started&&!im->unloaded);im->started=1;FStarts++;return FStartStatus;
}
static EFI_STATUS EFIAPI f_unloadimage(EFI_HANDLE h) {
  FImage *im=fimg_of(h);FCHECK(im&&!im->unloaded);im->unloaded=1;FUnloads++;fh_drop(h);return EFI_SUCCESS;
}

/* ---- the environment ---- */
static EFI_BOOT_SERVICES FBs;static EFI_RUNTIME_SERVICES FRt;static EFI_SYSTEM_TABLE FSt;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL FIn;static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL FOut;
static void fake_env_reset(void) {
  for(int i=0;i<8192;i++)FPool[i]=NULL;
  FPoolLive=0;FPoolAllocs=0;FHn=0;memset(FH,0,sizeof FH);FOpenLive=0;FVn=0;memset(FVols,0,sizeof FVols);
  FVarN=0;memset(FVars,0,sizeof FVars);FVarReads=0;fkey_clear();FConN=0;FCon[0]=0;
  FStallUs=0;FStalls=0;FStallFailAt=0;FStallHook=NULL;FStallFailStatus=0;FWatchdogCalls=0;FWatchdogTimeout=99;FWatchdogStatus=0;
  memset(FImgs,0,sizeof FImgs);FImgN=0;FLoads=FStarts=FUnloads=0;FLoadStatus=FStartStatus=0;FLoadWrongDevice=0;FLocateFail=0;
  memset(&FBs,0,sizeof FBs);memset(&FRt,0,sizeof FRt);memset(&FSt,0,sizeof FSt);memset(&FIn,0,sizeof FIn);memset(&FOut,0,sizeof FOut);
  FBs.AllocatePool=f_alloc;FBs.FreePool=f_free;FBs.HandleProtocol=f_handleprotocol;FBs.LocateHandleBuffer=f_locatebuffer;
  FBs.LoadImage=f_loadimage;FBs.StartImage=f_startimage;FBs.UnloadImage=f_unloadimage;FBs.Stall=f_stall;FBs.SetWatchdogTimer=f_watchdog;
  FRt.GetVariable=f_getvar;FRt.SetVariable=f_setvar_trap;
  FIn.Reset=f_inreset;FIn.ReadKeyStroke=f_readkey;FOut.OutputString=f_outstr;
  FSt.BootServices=&FBs;FSt.RuntimeServices=&FRt;FSt.ConIn=&FIn;FSt.ConOut=&FOut;FSt.StdErr=&FOut;
}
/* The running image: a handle with LoadedImage whose DeviceHandle is the volume it was started from. */
FAKE_UNUSED static EFI_HANDLE fake_self(EFI_HANDLE device) {
  EFI_LOADED_IMAGE_PROTOCOL *l=calloc(1,sizeof *l);l->DeviceHandle=device;
  EFI_HANDLE h=fh_new();fh_add(h,&FG_Loaded,l);return h;
}
