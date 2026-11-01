/* Exercise the actual driver entry points with a mock EFI PCI I/O device. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../uefi/tarugx_gop.c"

EFI_BOOT_SERVICES *BS;
static EFI_BOOT_SERVICES services;
static EFI_PCI_IO_PROTOCOL mock_pci;
static TGX *installed;
static UINT32 registers[16], pixels[640*480];
static UINT64 attrs;
static int allocations, opened, reject_install, fail_read;
static UINT32 pci_id=0x110022c2;
VOID InitializeLib(EFI_HANDLE h, EFI_SYSTEM_TABLE *s) { (void)h; (void)s; }
VOID SetMem(VOID *p, UINTN n, UINT8 v) { memset(p,v,n); }

static EFI_STATUS EFIAPI allocate(EFI_MEMORY_TYPE type, UINTN n, VOID **p)
{
    assert(type==EfiBootServicesData); *p=calloc(1,n);
    if (!*p) return EFI_OUT_OF_RESOURCES;
    allocations++; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI release(VOID *p)
{ assert(p); free(p); allocations--; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI open_proto(EFI_HANDLE h, EFI_GUID *guid, VOID **p,
                                    EFI_HANDLE a, EFI_HANDLE c, UINT32 attr)
{
    (void)h;(void)a;(void)c;
    if (!memcmp(guid,&pci_guid,sizeof(*guid))) {
        assert(attr==EFI_OPEN_PROTOCOL_BY_DRIVER);
        if(opened) return EFI_ALREADY_STARTED;
        opened=1; *p=&mock_pci;
    } else {
        assert(attr==EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        assert(!memcmp(guid,&gop_guid,sizeof(*guid)));
        if(!installed) return EFI_UNSUPPORTED;
        *p=installed;
    }
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI close_proto(EFI_HANDLE h, EFI_GUID *g, EFI_HANDLE a, EFI_HANDLE c)
{ (void)h;(void)g;(void)a;(void)c; assert(opened); opened=0; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI config_read(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w,
                                     UINT32 off, UINTN n, VOID *v)
{
    (void)p;assert(w==EfiPciIoWidthUint32 && n==1);
    *(UINT32 *)v=off==0?pci_id:0x03800000;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI mem_read(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w,
                                  UINT8 bar, UINT64 off, UINTN n, VOID *v)
{
    (void)p; assert(w==EfiPciIoWidthUint32 && n==1 && !(off%4));
    if(fail_read) return EFI_DEVICE_ERROR;
    assert((bar==0 && off<sizeof(registers)) || (bar==2 && off<sizeof(pixels)));
    *(UINT32 *)v=bar==0?registers[off/4]:pixels[off/4];return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI mem_write(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w,
                                   UINT8 bar, UINT64 off, UINTN n, VOID *v)
{
    (void)p;assert(w==EfiPciIoWidthUint32 && n==1 && !(off%4));
    assert((bar==0 && off<sizeof(registers)) || (bar==2 && off<sizeof(pixels)));
    if(bar==0)registers[off/4]=*(UINT32 *)v;else pixels[off/4]=*(UINT32 *)v;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI attributes(EFI_PCI_IO_PROTOCOL *p,
                                    EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op, UINT64 a, UINT64 *v)
{
    (void)p;
    if(op==EfiPciIoAttributeOperationGet)*v=attrs;
    else if(op==EfiPciIoAttributeOperationEnable){assert(a==EFI_PCI_IO_ATTRIBUTE_MEMORY);attrs|=a;}
    else {assert(op==EfiPciIoAttributeOperationSet);attrs=a;}
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI bars(EFI_PCI_IO_PROTOCOL *p, UINT8 bar, UINT64 *supports, VOID **r)
{
    (void)p;(void)supports;assert(bar==0 || bar==2);
    EFI_STATUS s=allocate(EfiBootServicesData,48,r);
    if(EFI_ERROR(s))return s;
    unsigned char *d=*r;d[0]=0x8a;d[1]=0x2b;
    UINT64 len=bar==0?65536:4*1024*1024;
    for(unsigned i=0;i<8;i++)d[38+i]=len>>(8*i);
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI install(EFI_HANDLE *h, ...)
{
    __builtin_ms_va_list args;
    (void)h;
    if(reject_install)return EFI_ACCESS_DENIED;
    __builtin_ms_va_start(args,h);
    assert(__builtin_va_arg(args,EFI_GUID *)==&gop_guid);
    installed=__builtin_va_arg(args,TGX *);
    __builtin_ms_va_end(args);
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI uninstall(EFI_HANDLE h, ...)
{ (void)h;assert(installed);installed=NULL;return EFI_SUCCESS; }
int main(void)
{
    BS=&services;
    services.AllocatePool=allocate;services.FreePool=release;
    services.OpenProtocol=open_proto;services.CloseProtocol=close_proto;
    services.InstallMultipleProtocolInterfaces=install;
    services.UninstallMultipleProtocolInterfaces=uninstall;
    mock_pci.Pci.Read=config_read;mock_pci.Mem.Read=mem_read;mock_pci.Mem.Write=mem_write;
    mock_pci.Attributes=attributes;mock_pci.GetBarAttributes=bars;
    registers[0]=0x54475055;
    assert(supported(&binding,NULL,NULL)==EFI_SUCCESS && !opened);
    pci_id=0;assert(supported(&binding,NULL,NULL)==EFI_UNSUPPORTED && !opened);pci_id=0x110022c2;
    assert(start(&binding,NULL,NULL)==EFI_SUCCESS);
    assert(installed && instances==1 && opened && allocations==1);
    assert(attrs==EFI_PCI_IO_ATTRIBUTE_MEMORY && registers[4]==1);
    assert(installed->mode.FrameBufferBase==0 && installed->mode.FrameBufferSize==0);
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;UINTN size;
    assert(query(&installed->gop,0,&size,&info)==EFI_SUCCESS);
    assert(info->PixelFormat==PixelBltOnly && info->HorizontalResolution==640);
    release(info);
    assert(set_mode(&installed->gop,1)==EFI_UNSUPPORTED);
    assert(unload(NULL)==EFI_ACCESS_DENIED);
    assert(stop(&binding,NULL,0,NULL)==EFI_SUCCESS);
    assert(!allocations && !instances && !opened && !attrs && !registers[4]);
    reject_install=1;
    assert(start(&binding,NULL,NULL)==EFI_ACCESS_DENIED);
    assert(!allocations && !instances && !opened && !attrs && !registers[4]);
    reject_install=0;fail_read=1;
    assert(start(&binding,NULL,NULL)==EFI_DEVICE_ERROR);
    assert(!allocations && !instances && !opened && !attrs);
    puts("GOP driver: binding, mode, memory-only access, cleanup and failure rollback passed");
}
