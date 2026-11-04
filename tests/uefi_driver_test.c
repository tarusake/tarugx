// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
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
static UINT32 registers[16], pixels[1920*1080/2];
static UINT64 attrs;
static int allocations, opened, reject_install, fail_read, child_open, reject_child, reject_remove;
static EFI_HANDLE output_handle=(EFI_HANDLE)(UINTN)123;
static UINT32 pci_id=0x110022c2;
#ifdef TARUGX_BOOT_SPLASH
static unsigned splash_stalls;
static EFI_STATUS EFIAPI splash_stall(UINTN microseconds)
{
    assert(microseconds==1000000);
    unsigned colored=0;
    unsigned count=registers[1]>=0x00010003 ? 1920*1080/2 : 640*480;
    for (unsigned i=0;i<count;i++) colored+=pixels[i]!=0;
    assert(colored>10000 && !installed); /* Hold after drawing, before publishing GOP. */
    splash_stalls++;
    return EFI_SUCCESS;
}
#endif
VOID InitializeLib(EFI_HANDLE h, EFI_SYSTEM_TABLE *s) { (void)h; (void)s; }
VOID SetMem(VOID *p, UINTN n, UINT8 v) { memset(p,v,n); }

static UINT64 cache_attrs=EFI_MEMORY_WT;
static int reject_uc;
static EFI_STATUS EFIAPI gcd_get(EFI_PHYSICAL_ADDRESS address, TGX_GCD_DESCRIPTOR *d)
{
    assert(address==0x80400000);
    *d=(TGX_GCD_DESCRIPTOR){.base=address,.length=0x400000,
        .capabilities=EFI_MEMORY_UC|EFI_MEMORY_WT,.attributes=cache_attrs,.type=3};
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI gcd_set(EFI_PHYSICAL_ADDRESS address,UINT64 length,UINT64 value)
{
    assert(address==0x80400000 && length==0x400000);
    if(reject_uc && value==EFI_MEMORY_UC)return EFI_UNSUPPORTED;
    cache_attrs=value;return EFI_SUCCESS;
}
static TGX_DXE_PREFIX dxe={.header={.Signature=0x565245535f455844ULL,.HeaderSize=sizeof(TGX_DXE_PREFIX)},
    .get=gcd_get,.set=gcd_set};
EFI_STATUS LibGetSystemConfigurationTable(EFI_GUID *guid,VOID **table)
{ EFI_GUID expected=TGX_DXE_GUID;assert(!memcmp(guid,&expected,sizeof(expected)));*table=&dxe;return EFI_SUCCESS; }
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
        if(attr==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) {
            assert(opened && c==output_handle);
            if(reject_child)return EFI_ACCESS_DENIED;
            assert(!child_open);child_open=1;*p=&mock_pci;return EFI_SUCCESS;
        }
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
{ (void)h;(void)g;(void)a; assert(opened);
  if(c==output_handle){assert(child_open);child_open=0;}else {assert(!child_open);opened=0;}
  return EFI_SUCCESS; }
static EFI_DEVICE_PATH parent_path={0x7f,0xff,{4,0}};
static EFI_STATUS EFIAPI handle_proto(EFI_HANDLE h, EFI_GUID *g, VOID **p)
{ (void)h;assert(!memcmp(g,&path_guid,sizeof(*g)));*p=&parent_path;return EFI_SUCCESS; }
EFI_DEVICE_PATH *AppendDevicePathNode(EFI_DEVICE_PATH *p, EFI_DEVICE_PATH *node)
{
    assert(p==&parent_path && DevicePathNodeLength(node)==8);
    EFI_DEVICE_PATH *result=NULL;
    if(EFI_ERROR(allocate(EfiBootServicesData,12,(VOID **)&result)))return NULL;
    memcpy(result,node,8);memcpy((char *)result+8,p,4);return result;
}
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
    if(bar==2)assert(cache_attrs==EFI_MEMORY_UC);
    if(fail_read) return EFI_DEVICE_ERROR;
    assert((bar==0 && off<sizeof(registers)) || (bar==2 && off<sizeof(pixels)));
    *(UINT32 *)v=bar==0?registers[off/4]:pixels[off/4];return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI mem_write(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_WIDTH w,
                                   UINT8 bar, UINT64 off, UINTN n, VOID *v)
{
    (void)p;assert(w==EfiPciIoWidthUint32 && n==1 && !(off%4));
    assert((bar==0 && off<sizeof(registers)) || (bar==2 && off<sizeof(pixels)));
    if(bar==2)assert(cache_attrs==EFI_MEMORY_UC);
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
    UINT64 base=bar==0?0xa1300000:0x80400000;
    for(unsigned i=0;i<8;i++)d[14+i]=base>>(8*i);
    UINT64 len=bar==0?65536:4*1024*1024;
    for(unsigned i=0;i<8;i++)d[38+i]=len>>(8*i);
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI install(EFI_HANDLE *h, ...)
{
    __builtin_ms_va_list args;
    assert(!*h);
    if(reject_install)return EFI_ACCESS_DENIED;
    __builtin_ms_va_start(args,h);
    assert(__builtin_va_arg(args,EFI_GUID *)==&gop_guid);
    installed=__builtin_va_arg(args,TGX *);
    assert(__builtin_va_arg(args,EFI_GUID *)==&discovered_guid);
    assert(__builtin_va_arg(args,VOID *)==&installed->discovered);
    assert(__builtin_va_arg(args,EFI_GUID *)==&active_guid);
    assert(__builtin_va_arg(args,VOID *)==&installed->active);
    assert(__builtin_va_arg(args,EFI_GUID *)==&trace_guid);
    assert(__builtin_va_arg(args,VOID *)==&installed->trace);
    assert(__builtin_va_arg(args,EFI_GUID *)==&path_guid);
    assert(__builtin_va_arg(args,VOID *)==installed->path);
    assert(__builtin_va_arg(args,VOID *)==NULL);
    *h=output_handle;
    __builtin_ms_va_end(args);
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI uninstall(EFI_HANDLE h, ...)
{ assert(h==output_handle && installed);if(reject_remove)return EFI_ACCESS_DENIED;installed=NULL;return EFI_SUCCESS; }
#ifdef TARUGX_EARLY_TEXT_INIT
static unsigned live_events;
static EFI_STATUS EFIAPI event_create(UINT32 type,EFI_TPL tpl,EFI_EVENT_NOTIFY fn,VOID *ctx,EFI_EVENT *event)
{ (void)fn;(void)ctx;assert(type==EVT_NOTIFY_SIGNAL && tpl==TPL_CALLBACK);live_events++;*event=(EFI_EVENT)1;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI event_register(EFI_GUID *guid,EFI_EVENT event,VOID **key)
{ (void)guid;assert(event==(EFI_EVENT)1);*key=(VOID *)2;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI event_close(EFI_EVENT event)
{ assert(event==(EFI_EVENT)1 && live_events);live_events--;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI event_signal(EFI_EVENT event)
{ assert(event==(EFI_EVENT)1);return EFI_SUCCESS; }
#endif
int main(void)
{
    BS=&services;
#ifdef TARUGX_BOOT_SPLASH
    services.Stall=splash_stall;
#endif
#ifdef TARUGX_EARLY_TEXT_INIT
    services.CreateEvent=event_create;services.RegisterProtocolNotify=event_register;
    services.CloseEvent=event_close;services.SignalEvent=event_signal;
#endif
    services.HandleProtocol=handle_proto;
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
    assert(cache_attrs==EFI_MEMORY_UC);
    assert(installed->trace.version==2 && installed->trace.count==1);
    assert(installed->trace.entries[0].operation==TGX_TRACE_SET);
    assert(installed->trace.entries[0].status==EFI_SUCCESS);
    assert(installed && instances==1 && opened && allocations==2 && child_open);
    assert(attrs==EFI_PCI_IO_ATTRIBUTE_MEMORY && registers[4]==1);
#ifdef TARUGX_BOOT_SPLASH
    unsigned colored=0;
    for (unsigned i=0;i<640*480;i++) colored+=pixels[i]!=0;
    assert(colored>10000); /* Logo was drawn during child creation. */
    assert(splash_stalls==1);
    UINT32 saved_pixels[640*480];memcpy(saved_pixels,pixels,sizeof(saved_pixels));
    assert(start(&binding,NULL,NULL)==EFI_SUCCESS);
    assert(!memcmp(saved_pixels,pixels,sizeof(saved_pixels))); /* No repeated draw. */
    assert(splash_stalls==1); /* No repeated wait. */
#endif
    assert(installed->mode.FrameBufferBase==0 && installed->mode.FrameBufferSize==0);
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;UINTN size;
    assert(query(&installed->gop,0,&size,&info)==EFI_SUCCESS);
    assert(info->PixelFormat==PixelBltOnly && info->HorizontalResolution==640);
    release(info);
    assert(set_mode(&installed->gop,1)==EFI_UNSUPPORTED);
    assert(installed->trace.entries[2].operation==TGX_TRACE_SET);
    assert(installed->trace.entries[2].mode==1);
    assert(installed->trace.entries[2].status==EFI_UNSUPPORTED);
    assert(installed->mode.Mode==0); /* Unsupported modes preserve working display. */
    for(unsigned i=0;i<TGX_TRACE_CAPACITY+2;i++)
        assert(query(&installed->gop,7,&size,&info)==EFI_INVALID_PARAMETER);
    assert(installed->trace.count==TGX_TRACE_CAPACITY && installed->trace.dropped==5);
    assert(installed->trace.entries[0].status==EFI_SUCCESS);
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL white={255,255,255,0};
    for(unsigned i=0;i<TGX_BLT_TRACE_CAPACITY+2;i++)
        assert(blt(&installed->gop,&white,EfiBltVideoFill,0,0,0,0,1,1,0)==EFI_SUCCESS);
    assert(installed->trace.blt_count==TGX_BLT_TRACE_CAPACITY && installed->trace.blt_dropped==2);
    assert(blt(&installed->gop,&white,EfiBltVideoFill,0,0,640,480,1,1,0)==EFI_INVALID_PARAMETER);
    assert(installed->trace.blt_errors==1 && installed->trace.failure_valid);
    assert(installed->trace.first_failure.dx==640 && installed->trace.first_failure.dy==480);
    assert(blt(&installed->gop,&white,EfiBltVideoFill,0,0,999,999,1,1,0)==EFI_INVALID_PARAMETER);
    assert(installed->trace.first_failure.dx==640 && installed->trace.blt_errors==2);
#ifdef TARUGX_BOOT_SPLASH
    assert(set_mode(&installed->gop,0)==EFI_SUCCESS);
    for (unsigned i=0;i<640*480;i++) assert(pixels[i]==0); /* Public SetMode remains black. */
    assert(splash_stalls==1); /* Public SetMode and BLT must not pause. */
#endif
    assert(unload(NULL)==EFI_ACCESS_DENIED);
    assert(stop(&binding,NULL,0,NULL)==EFI_DEVICE_ERROR);
    reject_remove=1;
    assert(stop(&binding,NULL,1,&output_handle)==EFI_ACCESS_DENIED && child_open);
    reject_remove=0;
    assert(stop(&binding,NULL,1,&output_handle)==EFI_SUCCESS && !child_open);
    assert(stop(&binding,NULL,0,NULL)==EFI_SUCCESS);
    assert(!allocations && !instances && !opened && !attrs && !registers[4]);
    assert(cache_attrs==EFI_MEMORY_WT);
    reject_install=1;
    assert(start(&binding,NULL,NULL)==EFI_ACCESS_DENIED);
    assert(!allocations && !instances && !opened && !attrs && !registers[4]);
    assert(cache_attrs==EFI_MEMORY_WT);
    reject_install=0;fail_read=1;
    assert(start(&binding,NULL,NULL)==EFI_DEVICE_ERROR);
    assert(!allocations && !instances && !opened && !attrs);
    assert(cache_attrs==EFI_MEMORY_WT);
    fail_read=0;reject_uc=1;
    assert(start(&binding,NULL,NULL)==EFI_UNSUPPORTED);
    assert(!allocations && !instances && !opened && !attrs && cache_attrs==EFI_MEMORY_WT);
    reject_uc=0;cache_attrs=EFI_MEMORY_UC;
    assert(start(&binding,NULL,NULL)==EFI_SUCCESS);
    assert(stop(&binding,NULL,1,&output_handle)==EFI_SUCCESS);
    assert(stop(&binding,NULL,0,NULL)==EFI_SUCCESS && cache_attrs==EFI_MEMORY_UC);
    reject_child=1;
    assert(start(&binding,NULL,NULL)==EFI_ACCESS_DENIED);
    assert(!allocations && !instances && !opened && !child_open);
    reject_remove=1;
    assert(start(&binding,NULL,NULL)==EFI_ACCESS_DENIED);
    assert(installed && instances==1 && allocations==2 && !child_open);
    reject_remove=0;
    assert(stop(&binding,NULL,1,&output_handle)==EFI_SUCCESS);
    assert(stop(&binding,NULL,0,NULL)==EFI_SUCCESS);
    reject_child=0;
    assert(start(&binding,NULL,&parent_path)==EFI_SUCCESS);
    assert(!installed && opened && allocations==1);
    assert(start(&binding,NULL,NULL)==EFI_SUCCESS && installed);
    assert(stop(&binding,NULL,1,&output_handle)==EFI_SUCCESS);
    assert(start(&binding,NULL,NULL)==EFI_SUCCESS && installed);
    assert(stop(&binding,NULL,1,&output_handle)==EFI_SUCCESS);
    assert(stop(&binding,NULL,0,NULL)==EFI_SUCCESS);
    struct { ACPI_ADR_DEVICE_PATH adr; EFI_DEVICE_PATH end; } rem={output_adr,parent_path};
    assert(supported(&binding,NULL,&rem.adr.Header)==EFI_SUCCESS);
    rem.adr.ADR++;
    assert(supported(&binding,NULL,&rem.adr.Header)==EFI_UNSUPPORTED);
    assert(start(&binding,NULL,&rem.adr.Header)==EFI_UNSUPPORTED);
    assert(!allocations && !instances && !opened && !contexts);
    registers[1]=0x00010003;
    assert(start(&binding,NULL,NULL)==EFI_SUCCESS && installed);
    assert(installed->info.HorizontalResolution==1920);
    assert(installed->info.VerticalResolution==1080);
    assert(installed->info.PixelsPerScanLine==1920);
    assert(installed->io.rgb565 && registers[0x20/4]==3840);
    assert(blt(&installed->gop,&white,EfiBltVideoFill,
               0,0,1919,1079,1,1,0)==EFI_SUCCESS);
    assert((pixels[1920*1080/2-1]>>16)==0xffff);
    assert(stop(&binding,NULL,1,&output_handle)==EFI_SUCCESS);
    assert(stop(&binding,NULL,0,NULL)==EFI_SUCCESS);
    assert(!allocations && !instances && !opened && !contexts);
#ifdef TARUGX_EARLY_TEXT_INIT
    assert(!live_events);
#endif
    puts("GOP driver: binding, mode, memory-only access, cleanup and failure rollback passed");
}
