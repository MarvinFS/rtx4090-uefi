/* SPDX-License-Identifier: GPL-2.0-only */
/* Home volume (wherever this image was started from): log and assets only. Windows volume:
 * found at run time, read-only. Boot variables are only read, never written. */
#include "boot_io.h"
#include "compat/compat.h"
#include "format.h"
#include "profile.h"
#include "sha256.h"
#include <stdarg.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleTextIn.h>
#include <Guid/FileInfo.h>

#define LOG_SLOTS 40U
#define WINDOWS_MIN 1048576U
#define WINDOWS_MAX 16777216U
static EFI_SYSTEM_TABLE *Table;
static EFI_HANDLE Self,Windows,OwnVolume;
static EFI_FILE_PROTOCOL *Root,*Log;
static BOOLEAN Silent;
static EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,InfoGuid=EFI_FILE_INFO_ID;
static EFI_GUID GlobalGuid={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};
static const CHAR16 WindowsName[]=L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";

/* Length of a complete, bounded device path including its end node (0 if malformed). */
static UINTN PathBytes(const EFI_DEVICE_PATH_PROTOCOL *p) {
  UINTN at=0;
  if(!p)return 0;
  for(UINTN nodes=0;nodes<64&&at+4<=4096;nodes++) {
    UINTN n=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(n<4||n>4096-at)return 0;
    if(p->Type==END_DEVICE_PATH_TYPE)return (n==4&&p->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE)?at+4:0;
    at+=n;p=(const VOID *)((const UINT8 *)p+n);
  }
  return 0;
}
static VOID Out(const char *text) {
  CHAR16 buffer[256];UINTN n=0;
  for(;*text;text++) {
    if(n>=250){buffer[n]=0;Table->ConOut->OutputString(Table->ConOut,buffer);n=0;}
    if(*text=='\n')buffer[n++]=L'\r';
    buffer[n++]=(CHAR16)(unsigned char)*text;
  }
  if(n){buffer[n]=0;Table->ConOut->OutputString(Table->ConOut,buffer);}
}
VOID GuBootSay(const char *text) {
  if(!Silent&&Table&&Table->ConOut&&Table->ConOut->OutputString&&text)Out(text);
}
VOID GuBootSilence(VOID){Silent=TRUE;}
VOID GuBootUnsilence(VOID){Silent=FALSE;}
VOID GuLog(const char *format,...) {
  CHAR8 buffer[2048];va_list args;va_start(args,format);
  size_t n=GuFormatV(buffer,sizeof(buffer),format,args);va_end(args);
  if(n==(size_t)-1||n>=sizeof(buffer)||!Log){Gu.fault=TRUE;return;}
  UINTN bytes=n;EFI_STATUS s=Log->Write(Log,&bytes,buffer);
  if(s!=EFI_SUCCESS||bytes!=n||Log->Flush(Log)!=EFI_SUCCESS)Gu.fault=TRUE;
}
VOID GuLogText(const char *text){GuLog("%s",text);}
BOOLEAN GuBootLogOpen(VOID){return Log!=NULL;}
VOID GuBootNote(const char *format,...) {
  char buffer[1024];va_list args;va_start(args,format);
  size_t n=GuFormatV(buffer,sizeof(buffer),format,args);va_end(args);
  if(n==(size_t)-1||n>=sizeof(buffer))return;
  if(Log)GuLog("%s",buffer);
  GuBootSay(buffer);
}
static VOID SayF(const char *format,...) {
  char buffer[256];va_list args;va_start(args,format);
  size_t n=GuFormatV(buffer,sizeof(buffer),format,args);va_end(args);
  if(n!=(size_t)-1&&n<sizeof(buffer))GuBootSay(buffer);
}
/* Log-only, never faults when the log is not open (pre-open and fallback paths). */
static VOID Trace(const char *format,...) {
  char buffer[512];va_list args;va_start(args,format);
  size_t n=GuFormatV(buffer,sizeof(buffer),format,args);va_end(args);
  if(n!=(size_t)-1&&n<sizeof(buffer)&&Log)GuLog("%s",buffer);
}
static VOID SlotName(CHAR16 name[12],UINTN slot) {
  static const CHAR16 pattern[]=L"RUN0000.TXT";
  for(UINTN i=0;i<12;i++)name[i]=pattern[i];
  for(UINTN at=7;at>3;){at--;name[at]=(CHAR16)('0'+slot%10);slot/=10;}
}
static BOOLEAN Older(const EFI_TIME *a,const EFI_TIME *b) {
  if(a->Year!=b->Year)return a->Year<b->Year;if(a->Month!=b->Month)return a->Month<b->Month;
  if(a->Day!=b->Day)return a->Day<b->Day;if(a->Hour!=b->Hour)return a->Hour<b->Hour;
  if(a->Minute!=b->Minute)return a->Minute<b->Minute;return a->Second<b->Second;
}
/* Ring of LOG_SLOTS files: first free slot, else replace the oldest by modification time,
   so an every-boot automatic mode cannot fill the partition. */
static EFI_STATUS OpenLog(EFI_FILE_PROTOCOL *dir) {
  EFI_TIME oldest;UINTN pick=0;CHAR16 name[12];memset(&oldest,0,sizeof(oldest));
  for(UINTN slot=1;slot<=LOG_SLOTS;slot++) {
    EFI_FILE_PROTOCOL *f=NULL;UINT64 words[64];UINTN bytes=sizeof(words);SlotName(name,slot);
    EFI_STATUS s=dir->Open(dir,&f,name,EFI_FILE_MODE_READ,0);
    if(s==EFI_NOT_FOUND)return dir->Open(dir,&Log,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    if(s!=EFI_SUCCESS||!f)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
    s=f->GetInfo(f,&InfoGuid,&bytes,words);
    EFI_FILE_INFO *info=(VOID *)words;
    if(s==EFI_SUCCESS&&(!pick||Older(&info->ModificationTime,&oldest))){oldest=info->ModificationTime;pick=slot;}
    EFI_STATUS closed=f->Close(f);if(s==EFI_SUCCESS)s=closed;
    if(s!=EFI_SUCCESS)return s;
  }
  EFI_FILE_PROTOCOL *f=NULL;SlotName(name,pick);
  EFI_STATUS s=dir->Open(dir,&f,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE,0);
  if(s!=EFI_SUCCESS||!f)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
  s=f->Delete(f);if(s!=EFI_SUCCESS)return s;
  return dir->Open(dir,&Log,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
}
EFI_STATUS GuBootOpen(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
  if(Table)return EFI_ALREADY_STARTED;
  if(!image||!t||!t->BootServices)return EFI_INVALID_PARAMETER;
  Table=t;Self=image;
  EFI_BOOT_SERVICES *b=t->BootServices;EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
  /* The default 5 minute watchdog would reset a halted or slow session. */
  if(!b->SetWatchdogTimer||b->SetWatchdogTimer(0,0,0,NULL)!=EFI_SUCCESS)return EFI_UNSUPPORTED;
  EFI_STATUS s=b->HandleProtocol(image,&LoadedGuid,(VOID **)&loaded);
  if(s!=EFI_SUCCESS||!loaded||!loaded->DeviceHandle)return EFI_UNSUPPORTED;
  OwnVolume=loaded->DeviceHandle;s=b->HandleProtocol(OwnVolume,&PathGuid,(VOID **)&path);
  if(s!=EFI_SUCCESS||!PathBytes(path))return EFI_UNSUPPORTED;
  s=b->HandleProtocol(OwnVolume,&FsGuid,(VOID **)&fs);if(s!=EFI_SUCCESS||!fs)return EFI_UNSUPPORTED;
  s=fs->OpenVolume(fs,&Root);if(s!=EFI_SUCCESS||!Root)return EFI_DEVICE_ERROR;
  EFI_FILE_PROTOCOL *dir=NULL;
  s=Root->Open(Root,&dir,L"\\GPU4090-UNLOCK",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
  if(s!=EFI_SUCCESS||!dir)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
  s=OpenLog(dir);
  EFI_STATUS closed=dir->Close(dir);if(s!=EFI_SUCCESS||!Log)return s==EFI_SUCCESS?EFI_OUT_OF_RESOURCES:s;
  return closed;
}
/* min==max demands an exact size. The file must be a regular file, read completely, with no tail. */
static EFI_STATUS ReadFile(EFI_FILE_PROTOCOL *root,const CHAR16 *name,UINTN min,UINTN max,VOID **data,UINTN *size) {
  *data=NULL;*size=0;EFI_FILE_PROTOCOL *f=NULL;UINT64 info_words[128];UINTN info_bytes=sizeof(info_words);
  EFI_STATUS s=root->Open(root,&f,(CHAR16 *)name,EFI_FILE_MODE_READ,0);
  if(s!=EFI_SUCCESS||!f)return s==EFI_SUCCESS?EFI_DEVICE_ERROR:s;
  s=f->GetInfo(f,&InfoGuid,&info_bytes,info_words);
  EFI_FILE_INFO *info=(VOID *)info_words;UINTN expected=0;
  if(s==EFI_SUCCESS&&(info_bytes<sizeof(EFI_FILE_INFO)||info_bytes>sizeof(info_words)||
     info->FileSize<min||info->FileSize>max||(info->Attribute&EFI_FILE_DIRECTORY)))s=EFI_COMPROMISED_DATA;
  VOID *p=NULL;
  if(s==EFI_SUCCESS){expected=(UINTN)info->FileSize;p=GuAlloc(expected);if(!p)s=EFI_OUT_OF_RESOURCES;}
  for(UINTN at=0;s==EFI_SUCCESS&&at<expected;) {
    UINTN n=expected-at;if(n>1048576)n=1048576;UINTN wanted=n;
    s=f->Read(f,&n,(UINT8 *)p+at);if(s==EFI_SUCCESS&&n!=wanted)s=EFI_DEVICE_ERROR;at+=n;
  }
  if(s==EFI_SUCCESS){UINT8 tail;UINTN n=1;s=f->Read(f,&n,&tail);if(s==EFI_SUCCESS&&n)s=EFI_COMPROMISED_DATA;}
  EFI_STATUS closed=f->Close(f);if(s==EFI_SUCCESS)s=closed;
  if(s!=EFI_SUCCESS){GuFree(p);return s;}*data=p;*size=expected;return EFI_SUCCESS;
}
EFI_STATUS GuBootLoadAssets(VOID) {
  static const CHAR16 *names[]={L"\\GPU4090-ASSETS\\booter_load_ad102.bin",L"\\GPU4090-ASSETS\\v67-ad102-610.43.03-four-plm.bin",
    L"\\GPU4090-ASSETS\\gsp_ga10x.bin",L"\\GPU4090-ASSETS\\gsp_boot_ad102.bin",L"\\GPU4090-ASSETS\\gsp_boot_desc_ad102.bin"};
  static const char *full[]={"gpu-unlock/610.43.03/booter_load_ad102.bin","gpu-unlock/610.43.03/v67-ad102-610.43.03-four-plm.bin",
    "gpu-unlock/610.43.03/gsp_ga10x.bin","gpu-unlock/610.43.03/gsp_boot_ad102.bin","gpu-unlock/610.43.03/gsp_boot_desc_ad102.bin"};
  static const UINTN sizes[]={57672,64000,84277400,36864,84};
  if(!Root||Gu.started)return EFI_NOT_READY;
  for(UINTN i=0;i<5;i++) {
    VOID *p=NULL;UINTN got=0;EFI_STATUS s=ReadFile(Root,names[i],sizes[i],sizes[i],&p,&got);
    Trace("asset %u %s: status=%#llx\n",(unsigned)i,full[i],(unsigned long long)s);
    if(s!=EFI_SUCCESS)return s;
    GuAssets[i].name=full[i];GuAssets[i].firmware=(struct firmware){sizes[i],p};
  }
  return GuAd104AssetsValid()?EFI_SUCCESS:EFI_CRC_ERROR;
}
static BOOLEAN PeSane(const UINT8 *p,UINTN size) {
  if(size<0x200||p[0]!='M'||p[1]!='Z')return FALSE;
  UINTN pe=p[0x3c]|((UINTN)p[0x3d]<<8)|((UINTN)p[0x3e]<<16)|((UINTN)p[0x3f]<<24);
  if(pe<0x40||pe>size-0x60||memcmp(p+pe,"PE\0\0",4))return FALSE;
  UINTN machine=p[pe+4]|((UINTN)p[pe+5]<<8),magic=p[pe+24]|((UINTN)p[pe+25]<<8),subsystem=p[pe+24+68]|((UINTN)p[pe+24+69]<<8);
  return machine==0x8664&&magic==0x20b&&subsystem==10;
}
/* Volume part of the device path of the first BootOrder entry described as "Windows
   Boot Manager" whose file is bootmgfw.efi (everything before the file-path node). */
static UINTN BootManagerPrefix(UINT8 *out,UINTN cap) {
  static const char want[]="Windows Boot Manager";
  EFI_RUNTIME_SERVICES *rs=Table->RuntimeServices;UINT16 order[128];UINTN bytes=sizeof(order);
  if(!rs||!rs->GetVariable||rs->GetVariable(L"BootOrder",&GlobalGuid,NULL,&bytes,order)!=EFI_SUCCESS||(bytes&1))return 0;
  for(UINTN i=0;i<bytes/2;i++) {
    static const char hex[]="0123456789ABCDEF";
    CHAR16 name[]=L"Boot0000";UINT8 buf[1024];UINTN n=sizeof(buf);
    for(UINTN d=0;d<4;d++)name[4+d]=(CHAR16)hex[(order[i]>>(12-4*d))&15];
    if(rs->GetVariable(name,&GlobalGuid,NULL,&n,buf)!=EFI_SUCCESS||n<8)continue;
    UINTN list=buf[4]|((UINTN)buf[5]<<8),at=6,k=0;
    while(at+2<=n&&(buf[at]|buf[at+1])&&k<sizeof(want)-1) {
      if(buf[at+1]||buf[at]!=(UINT8)want[k])break;
      at+=2;k++;
    }
    if(k!=sizeof(want)-1||at+2>n||buf[at]||buf[at+1])continue;
    at+=2;
    if(list>n-at)continue;
    UINTN end=at+list,prefix=0;BOOLEAN file=FALSE;
    for(UINTN nodes=0;nodes<64&&at+4<=end;nodes++) {
      UINTN len=buf[at+2]|((UINTN)buf[at+3]<<8);
      if(len<4||len>end-at)break;
      if(buf[at]==END_DEVICE_PATH_TYPE)break;
      if(buf[at]==MEDIA_DEVICE_PATH&&buf[at+1]==MEDIA_FILEPATH_DP) {
        static const char leaf[]="bootmgfw.efi";UINTN m=(len-4)/2;
        /* Case-insensitive match of the last path component, before the terminator. */
        if(m>=sizeof(leaf)) {
          const UINT8 *w=buf+at+4+2*(m-sizeof(leaf));BOOLEAN same=TRUE;
          for(UINTN q=0;q<sizeof(leaf)-1;q++){
            UINT8 c=w[2*q];if(c>='A'&&c<='Z')c=(UINT8)(c+32);
            if(c!=(UINT8)leaf[q]||w[2*q+1])same=FALSE;
          }
          file=same;
        }
        break;
      }
      prefix+=len;at+=len;
    }
    if(file&&prefix&&prefix<=cap){memcpy(out,buf+(end-list),prefix);return prefix;}
  }
  return 0;
}
/* Windows writes its boot option in the short form HD(n,GPT,guid,start,size)/\EFI\...: the firmware resolves it
   by scanning partitions. So the entry's volume part must match the END of the volume's path, on a node
   boundary; a full hardware path (as some firmware stores) is the case where it is the whole path. */
static BOOLEAN PathEndsWith(const UINT8 *path,UINTN bytes,const UINT8 *tail,UINTN n) {
  UINTN body=bytes-4,at=0;
  if(!n||n>body)return FALSE;
  while(at<body-n){UINTN len=path[at+2]|((UINTN)path[at+3]<<8);if(len<4)return FALSE;at+=len;}
  return at==body-n&&!memcmp(path+at,tail,n);
}
EFI_STATUS GuBootLoadWindows(VOID) {
  if(Windows||!Table||Gu.started)return EFI_NOT_READY;
  EFI_BOOT_SERVICES *b=Table->BootServices;EFI_HANDLE *h=NULL,volume=NULL;UINTN count=0,found=0,preferred=0;
  EFI_DEVICE_PATH_PROTOCOL *base=NULL;UINTN path_bytes=0;UINT8 prefix[512];
  EFI_STATUS s=b->LocateHandleBuffer(ByProtocol,&FsGuid,NULL,&count,&h);
  if(s==EFI_SUCCESS&&(!h||!count||count>128))s=EFI_UNSUPPORTED;
  UINTN want=s==EFI_SUCCESS?BootManagerPrefix(prefix,sizeof(prefix)):0;
  Trace("Windows Boot Manager entry in BootOrder: %s\n",want?"found":"not found; scanning volumes");
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL,*file=NULL;
    EFI_DEVICE_PATH_PROTOCOL *p=NULL;
    if(b->HandleProtocol(h[i],&FsGuid,(VOID **)&fs)!=EFI_SUCCESS||!fs||fs->OpenVolume(fs,&root)!=EFI_SUCCESS||!root)continue;
    BOOLEAN present=root->Open(root,&file,(CHAR16 *)WindowsName,EFI_FILE_MODE_READ,0)==EFI_SUCCESS&&file;
    if(present)file->Close(file);
    root->Close(root);
    UINTN bytes=0;
    if(!present||b->HandleProtocol(h[i],&PathGuid,(VOID **)&p)!=EFI_SUCCESS||!(bytes=PathBytes(p)))continue;
    BOOLEAN match=want&&PathEndsWith((const UINT8 *)p,bytes,prefix,want);
    Trace("bootmgfw.efi on volume %u (boot-entry match=%u)\n",(unsigned)i,(unsigned)match);
    found++;
    if(match){preferred++;volume=h[i];base=p;path_bytes=bytes;}
    else if(!want){volume=h[i];base=p;path_bytes=bytes;}
  }
  if(h)b->FreePool(h);
  if(s==EFI_SUCCESS)s=(want?preferred==1:found==1)?EFI_SUCCESS:EFI_NOT_FOUND;
  Trace("Windows boot manager volumes: found=%zu matching-boot-entry=%zu\n",(size_t)found,(size_t)preferred);
  if(s!=EFI_SUCCESS)return s;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_PROTOCOL *root=NULL;
  s=b->HandleProtocol(volume,&FsGuid,(VOID **)&fs);
  if(s==EFI_SUCCESS&&fs)s=fs->OpenVolume(fs,&root);else s=EFI_UNSUPPORTED;
  if(s!=EFI_SUCCESS||!root)return EFI_DEVICE_ERROR;
  VOID *bytes=NULL;UINTN size=0;s=ReadFile(root,WindowsName,WINDOWS_MIN,WINDOWS_MAX,&bytes,&size);
  EFI_STATUS closed=root->Close(root);if(s==EFI_SUCCESS)s=closed;
  if(s==EFI_SUCCESS&&!PeSane(bytes,size))s=EFI_COMPROMISED_DATA;
  if(s==EFI_SUCCESS) {
    static const char digits[]="0123456789abcdef";UINT8 digest[32];char hex[65];GuSha256(bytes,size,digest);
    for(UINTN i=0;i<32;i++){hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15];}hex[64]=0;
    Trace("bootmgfw.efi bytes=%zu sha256=%s (logged, not pinned)\n",size,hex);
  }
  UINTN node_bytes=4+sizeof(WindowsName),total=path_bytes-4+node_bytes+4;
  UINT8 *path=NULL;
  if(s==EFI_SUCCESS){path=GuAlloc(total);if(!path)s=EFI_OUT_OF_RESOURCES;}
  if(s==EFI_SUCCESS) {
    memcpy(path,base,path_bytes-4);UINT8 *node=path+path_bytes-4;
    node[0]=MEDIA_DEVICE_PATH;node[1]=MEDIA_FILEPATH_DP;node[2]=(UINT8)node_bytes;node[3]=(UINT8)(node_bytes>>8);
    memcpy(node+4,WindowsName,sizeof(WindowsName));node+=node_bytes;
    node[0]=END_DEVICE_PATH_TYPE;node[1]=END_ENTIRE_DEVICE_PATH_SUBTYPE;node[2]=4;node[3]=0;
    s=b->LoadImage(FALSE,Self,(VOID *)path,bytes,size,&Windows);
  }
  GuFree(bytes);GuFree(path);
  if(s==EFI_SUCCESS) {
    EFI_LOADED_IMAGE_PROTOCOL *im=NULL;s=b->HandleProtocol(Windows,&LoadedGuid,(VOID **)&im);
    if(s==EFI_SUCCESS&&(!im||im->DeviceHandle!=volume))s=EFI_UNSUPPORTED;
    if(s!=EFI_SUCCESS){b->UnloadImage(Windows);Windows=NULL;}
  }
  Trace("Windows image staged: status=%#llx\n",(unsigned long long)s);
  return s;
}
VOID GuBootUnloadWindows(VOID) {
  if(Windows&&Table){Table->BootServices->UnloadImage(Windows);Windows=NULL;}
}
EFI_STATUS GuBootMenu(BOOLEAN def,UINTN seconds,GU_CHOICE *choice) {
  if(!choice||!Table||!Table->ConIn||!Table->ConIn->ReadKeyStroke||!seconds)return EFI_UNSUPPORTED;
  EFI_BOOT_SERVICES *b=Table->BootServices;*choice=def?GU_CHOICE_UNLOCK:GU_CHOICE_WINDOWS;
  BOOLEAN menu=FALSE;
  for(UINTN tenths=seconds*10;tenths&&!menu;tenths--) {
    if(tenths%10==0)SayF(def?"\rRTX 4090 unlock starts in %u s - Esc: menu, S: skip, U: now   ":
      "\rWindows starts in %u s - U: unlock now, Esc: menu   ",(unsigned)(tenths/10));
    EFI_INPUT_KEY key;EFI_STATUS s=Table->ConIn->ReadKeyStroke(Table->ConIn,&key);
    if(s==EFI_SUCCESS) {
      if(key.ScanCode==0x17||key.UnicodeChar==27)menu=TRUE;
      else if(key.UnicodeChar=='u'||key.UnicodeChar=='U'){*choice=GU_CHOICE_UNLOCK;return EFI_SUCCESS;}
      else if(key.UnicodeChar=='s'||key.UnicodeChar=='S'||key.UnicodeChar=='w'||key.UnicodeChar=='W'){*choice=GU_CHOICE_WINDOWS;return EFI_SUCCESS;}
      else if(key.UnicodeChar=='\r')return EFI_SUCCESS;
    } else if(s!=EFI_NOT_READY)return s;
    if(!menu&&(s=b->Stall(100000))!=EFI_SUCCESS)return s;
  }
  if(!menu){GuBootSay("\n");return EFI_SUCCESS;}
  GuBootSay("\n\nRTX 4090 unlock menu\n  1  Unlock, then start Windows\n  2  Start Windows without the mod\nChoice (2 after 2 minutes): ");
  *choice=GU_CHOICE_WINDOWS;
  for(UINTN tenths=1200;tenths;tenths--) {
    EFI_INPUT_KEY key;EFI_STATUS s=Table->ConIn->ReadKeyStroke(Table->ConIn,&key);
    if(s==EFI_SUCCESS) {
      CHAR16 c=key.UnicodeChar;
      if(c=='1'||c=='u'||c=='U'){*choice=GU_CHOICE_UNLOCK;break;}
      if(c=='2'||c=='w'||c=='W'||c=='s'||c=='S'){*choice=GU_CHOICE_WINDOWS;break;}
      if(key.ScanCode==0x17||c==27){*choice=GU_CHOICE_WINDOWS;break;}
    } else if(s!=EFI_NOT_READY)return s;
    s=b->Stall(100000);if(s!=EFI_SUCCESS)return s;
  }
  GuBootSay("\n");return EFI_SUCCESS;
}
EFI_STATUS GuBootClose(VOID) {
  EFI_STATUS s=EFI_SUCCESS;
  if(Log){s=Log->Flush(Log);EFI_STATUS closed=Log->Close(Log);Log=NULL;if(s==EFI_SUCCESS)s=closed;}
  if(Root){EFI_STATUS closed=Root->Close(Root);Root=NULL;if(s==EFI_SUCCESS)s=closed;}
  return s;
}
EFI_STATUS GuBootWindows(VOID) {
  if(!Windows||!Table)return EFI_NOT_READY;
  EFI_HANDLE image=Windows;Windows=NULL; /* A returned image is gone; never start or unload it twice. */
  return Table->BootServices->StartImage(image,NULL,NULL);
}
VOID GuBootHalt(VOID) {
  GuBootSay("\nSTOPPED. Power OFF with the power button. Next boot: Esc menu, Windows without the mod.\n");
  for(;;) {
    if(Table&&Table->BootServices)Table->BootServices->Stall(1000000);else __asm__ volatile("pause");
  }
}
