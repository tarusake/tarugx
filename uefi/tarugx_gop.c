// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>
#include "blt.h"
#include "dxe_memory.h"
#include "gop_trace.h"
#ifdef TARUGX_BOOT_SPLASH
#include "splash.h"
#endif
#ifdef TARUGX_EARLY_TEXT_INIT
#include "text_init.h"
#endif

static EFI_GUID pci_guid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID binding_guid = EFI_DRIVER_BINDING_PROTOCOL_GUID;
static EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID discovered_guid = EFI_EDID_DISCOVERED_PROTOCOL_GUID;
static EFI_GUID active_guid = EFI_EDID_ACTIVE_PROTOCOL_GUID;
static UINTN instances;
static EFI_GUID trace_guid=TGX_TRACE_GUID;

typedef struct tgx {
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop; /* First: GOP pointer is the context. */
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
    EFI_EDID_DISCOVERED_PROTOCOL discovered;
    EFI_EDID_ACTIVE_PROTOCOL active;
    EFI_PCI_IO_PROTOCOL *pci;
    UINT64 attributes;
    struct tgx_io io;
    TGX_DXE_PREFIX *dxe;
    UINT64 fb_address, fb_attributes;
    BOOLEAN cache_changed;
    TGX_TRACE trace;
#ifdef TARUGX_EARLY_TEXT_INIT
    TGX_TEXT_INIT text_init;
#endif
    EFI_HANDLE controller, child;
    BOOLEAN child_open;
    EFI_DEVICE_PATH *path;
    struct tgx *next;
} TGX;

static EFI_GUID path_guid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static TGX *contexts;
/* ACPI display ID scheme, non-VGA, external digital, port/index 0.
 * No hotplug/EDID detection is advertised (BiosCanDetect=0). */
static ACPI_ADR_DEVICE_PATH output_adr={{ACPI_DEVICE_PATH,ACPI_ADR_DP,{8,0}},0x80020300};
static TGX *find_context(EFI_HANDLE controller)
{
    for (TGX *t=contexts;t;t=t->next) if (t->controller==controller) return t;
    return NULL;
}
static BOOLEAN valid_remaining(EFI_DEVICE_PATH *p)
{
    if (!p || IsDevicePathEnd(p)) return TRUE;
    if (p->Type!=ACPI_DEVICE_PATH || p->SubType!=ACPI_ADR_DP || DevicePathNodeLength(p)!=8)
        return FALSE;
    UINT8 *bytes=(UINT8 *)p;
    UINT32 adr=(UINT32)bytes[4]|(UINT32)bytes[5]<<8|(UINT32)bytes[6]<<16|(UINT32)bytes[7]<<24;
    return adr==output_adr.ADR && IsDevicePathEnd(NextDevicePathNode(p));
}
static EFI_STATUS reg_write(TGX *t, UINT64 offset, UINT32 value)
{
    return t->pci->Mem.Write(t->pci, EfiPciIoWidthUint32, 0, offset, 1, &value);
}
static int fb_read(void *ctx, size_t offset, uint32_t *value)
{
    TGX *t = ctx;
    return EFI_ERROR(t->pci->Mem.Read(t->pci, EfiPciIoWidthUint32, 2, offset, 1, value));
}
static int fb_write(void *ctx, size_t offset, uint32_t value)
{
    TGX *t = ctx;
    return EFI_ERROR(t->pci->Mem.Write(t->pci, EfiPciIoWidthUint32, 2, offset, 1, &value));
}
static EFI_STATUS query_impl(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, UINT32 mode,
                               UINTN *size, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info)
{
    EFI_STATUS status;
    if (!g || !size || !info) return EFI_INVALID_PARAMETER;
    if (mode) return EFI_INVALID_PARAMETER;
    status = BS->AllocatePool(EfiBootServicesData, sizeof(**info), (VOID **)info);
    if (EFI_ERROR(status)) return status;
    **info = ((TGX *)g)->info;
    *size = sizeof(**info);
    return EFI_SUCCESS;
}
static EFI_STATUS set_mode_impl(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, UINT32 mode)
{
    TGX *t = (TGX *)g;
    UINT32 black = 0;
    EFI_STATUS status;
    if (!g) return EFI_INVALID_PARAMETER;
    if (mode) return EFI_UNSUPPORTED;
    t->mode.Mode = 0xffffffff;
    status = reg_write(t, 0x10, 0);
    if (EFI_ERROR(status)) return status;
    if (tgx_blt(&t->io, &black, 0, 0, 0, 0, 0, TGX_WIDTH, TGX_HEIGHT, 0))
        return EFI_DEVICE_ERROR;
    if (EFI_ERROR(reg_write(t, 0x18, 0)) || EFI_ERROR(reg_write(t, 0x1c, 0)) ||
        EFI_ERROR(reg_write(t, 0x20, TGX_WIDTH*4)) || EFI_ERROR(reg_write(t, 0x24, 1)))
        return EFI_DEVICE_ERROR;
    status = reg_write(t, 0x10, 1);
    if (!EFI_ERROR(status)) t->mode.Mode = 0;
    return status;
}
static VOID trace_call(TGX *t, UINT32 operation, UINT32 mode, EFI_STATUS status)
{
    if (t->trace.count<TGX_TRACE_CAPACITY)
        t->trace.entries[t->trace.count++]=(TGX_TRACE_ENTRY){operation,mode,status};
    else if (t->trace.dropped!=0xffffffff) t->trace.dropped++;
}
static EFI_STATUS EFIAPI query(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, UINT32 mode,
                               UINTN *size, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info)
{
    EFI_STATUS s=query_impl(g,mode,size,info);
    if (g) trace_call((TGX *)g,TGX_TRACE_QUERY,mode,s);
    return s;
}
static EFI_STATUS EFIAPI set_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, UINT32 mode)
{
    EFI_STATUS s=set_mode_impl(g,mode);
    if (g) trace_call((TGX *)g,TGX_TRACE_SET,mode,s);
    return s;
}
static EFI_STATUS EFIAPI blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *g,
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *b, EFI_GRAPHICS_OUTPUT_BLT_OPERATION op,
    UINTN sx, UINTN sy, UINTN dx, UINTN dy, UINTN w, UINTN h, UINTN delta)
{
    if (!g) return EFI_INVALID_PARAMETER;
    TGX *t=(TGX *)g;
    EFI_STATUS status;
    if (g->Mode->Mode!=0) status=EFI_NOT_STARTED;
    else {
        int rc=tgx_blt(&t->io,b,op,sx,sy,dx,dy,w,h,delta);
        status=rc==0 ? EFI_SUCCESS : rc==-1 ? EFI_INVALID_PARAMETER : EFI_DEVICE_ERROR;
    }
    TGX_BLT_TRACE_ENTRY entry={op,status,sx,sy,dx,dy,w,h,delta};
    if (t->trace.blt_count<TGX_BLT_TRACE_CAPACITY)
        t->trace.blt_entries[t->trace.blt_count++]=entry;
    else if (t->trace.blt_dropped!=0xffffffff) t->trace.blt_dropped++;
    if (EFI_ERROR(status)) {
        if (t->trace.blt_errors!=0xffffffff) t->trace.blt_errors++;
        if (!t->trace.failure_valid) {
            t->trace.first_failure=entry;t->trace.failure_valid=1;
        }
    }
    return status;
}

static EFI_STATUS identify(EFI_PCI_IO_PROTOCOL *pci)
{
    UINT32 id, class_revision;
    EFI_STATUS s = pci->Pci.Read(pci, EfiPciIoWidthUint32, 0, 1, &id);
    if (EFI_ERROR(s)) return s;
    s = pci->Pci.Read(pci, EfiPciIoWidthUint32, 8, 1, &class_revision);
    if (EFI_ERROR(s)) return s;
    return id == 0x110022c2 && (class_revision >> 8) == 0x038000 ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

/* GetBarAttributes returns packed ACPI QWORD address-space descriptors.
 * Read fields bytewise: resource type at 3, address length at 38. */
static EFI_STATUS check_bar(EFI_PCI_IO_PROTOCOL *pci, UINT8 bar, UINT64 minimum)
{
    VOID *resources = NULL;
    UINT8 *p;
    UINT64 length = 0;
    EFI_STATUS s = pci->GetBarAttributes(pci, bar, NULL, &resources);
    if (EFI_ERROR(s)) return s;
    if (!resources) return EFI_UNSUPPORTED;
    p = resources;
    if (p[0] != 0x8a || p[1] != 0x2b || p[2] || p[3] != 0) s = EFI_UNSUPPORTED;
    else {
        for (UINTN i = 0; i < 8; i++) length |= (UINT64)p[38+i] << (i*8);
        if (length < minimum) s = EFI_UNSUPPORTED;
    }
    BS->FreePool(resources);
    return s;
}

/* BAR2 is a transaction aperture, not cacheable RAM. A DWORD API call
 * must not become a CPU cache-line read that the current bridge rejects. */
static EFI_STATUS uncached_framebuffer(TGX *t)
{
    EFI_GUID guid=TGX_DXE_GUID;
    EFI_STATUS s;
    VOID *resources=NULL;
    TGX_GCD_DESCRIPTOR d;
    s=LibGetSystemConfigurationTable(&guid,(VOID **)&t->dxe);
    if (EFI_ERROR(s)) return s;
    if (!t->dxe || t->dxe->header.Signature!=0x565245535f455844ULL ||
        t->dxe->header.HeaderSize<sizeof(*t->dxe) || !t->dxe->get || !t->dxe->set)
        return EFI_UNSUPPORTED;
    s=t->pci->GetBarAttributes(t->pci,2,NULL,&resources);
    if (EFI_ERROR(s)) return s;
    if (!resources) return EFI_UNSUPPORTED;
    UINT8 *p=resources;
    if (p[0]!=0x8a || p[1]!=0x2b || p[2] || p[3]) s=EFI_UNSUPPORTED;
    else for (UINTN i=0;i<8;i++) t->fb_address|=(UINT64)p[14+i]<<(8*i);
    BS->FreePool(resources);
    if (EFI_ERROR(s)) return s;
    if (!t->fb_address || (t->fb_address&0xfff) || t->fb_address>~(UINT64)0-0x400000)
        return EFI_UNSUPPORTED;
    s=t->dxe->get(t->fb_address,&d);
    if (EFI_ERROR(s)) return s;
    if (d.type!=3 || !(d.capabilities&EFI_MEMORY_UC) || d.base>t->fb_address ||
        d.length<0x400000 || t->fb_address-d.base>d.length-0x400000)
        return EFI_UNSUPPORTED;
    t->fb_attributes=d.attributes;
    if ((d.attributes&0x1f)==EFI_MEMORY_UC) return EFI_SUCCESS;
    s=t->dxe->set(t->fb_address,0x400000,(d.attributes&~(UINT64)0x1f)|EFI_MEMORY_UC);
    if (EFI_ERROR(s)) return s;
    t->cache_changed=TRUE;
    s=t->dxe->get(t->fb_address,&d);
    if (!EFI_ERROR(s) && (d.attributes&0x1f)!=EFI_MEMORY_UC) s=EFI_DEVICE_ERROR;
    return s;
}
static EFI_STATUS restore_framebuffer_cache(TGX *t)
{
    if (!t->cache_changed) return EFI_SUCCESS;
    EFI_STATUS s=t->dxe->set(t->fb_address,0x400000,t->fb_attributes);
    if (!EFI_ERROR(s)) t->cache_changed=FALSE;
    return s;
}

static EFI_STATUS child_protocols(TGX *t, BOOLEAN install)
{
    if (install) return BS->InstallMultipleProtocolInterfaces(&t->child,
        &gop_guid,&t->gop,&discovered_guid,&t->discovered,&active_guid,&t->active,
        &trace_guid,&t->trace,&path_guid,t->path,NULL);
    return BS->UninstallMultipleProtocolInterfaces(t->child,
        &gop_guid,&t->gop,&discovered_guid,&t->discovered,&active_guid,&t->active,
        &trace_guid,&t->trace,&path_guid,t->path,NULL);
}
static EFI_STATUS create_child(EFI_DRIVER_BINDING_PROTOCOL *self,TGX *t)
{
    if (t->child) {
        if (t->child_open) {
#ifdef TARUGX_EARLY_TEXT_INIT
            if (!t->text_init.event) return tgx_text_arm(&t->text_init,t->child);
#endif
            return EFI_SUCCESS;
        }
        EFI_PCI_IO_PROTOCOL *pci=NULL;
        EFI_STATUS retry=BS->OpenProtocol(t->controller,&pci_guid,(VOID **)&pci,
            self->DriverBindingHandle,t->child,EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
        if (!EFI_ERROR(retry)) t->child_open=TRUE;
#ifdef TARUGX_EARLY_TEXT_INIT
        if (!EFI_ERROR(retry) && !t->text_init.event) retry=tgx_text_arm(&t->text_init,t->child);
#endif
        return retry;
    }
    EFI_DEVICE_PATH *parent=NULL;
    EFI_STATUS s=BS->HandleProtocol(t->controller,&path_guid,(VOID **)&parent);
    if (EFI_ERROR(s)) return s;
    t->path=AppendDevicePathNode(parent,&output_adr.Header);
    if (!t->path) return EFI_OUT_OF_RESOURCES;
    s=set_mode(&t->gop,0);
#ifdef TARUGX_BOOT_SPLASH
    /* Initial child creation only. GOP SetMode must still clear to black.
     * Hold the completed logo before publishing GOP, never during public BLT
     * or SetMode calls. No timer or repaint after firmware takes ownership. */
    if (!EFI_ERROR(s) && tgx_boot_splash(&t->io)) s=EFI_DEVICE_ERROR;
    if (!EFI_ERROR(s)) s=BS->Stall(1000000); /* One second, in microseconds. */
#endif
    if (!EFI_ERROR(s)) s=child_protocols(t,TRUE);
    if (EFI_ERROR(s)) { BS->FreePool(t->path);t->path=NULL;t->child=NULL;return s; }
    EFI_PCI_IO_PROTOCOL *pci=NULL;
    s=BS->OpenProtocol(t->controller,&pci_guid,(VOID **)&pci,self->DriverBindingHandle,
                       t->child,EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
    if (EFI_ERROR(s)) {
        EFI_STATUS removed=child_protocols(t,FALSE);
        if (EFI_ERROR(removed)) return removed; /* Keep live interfaces allocated. */
        BS->FreePool(t->path);t->path=NULL;t->child=NULL;
    }
    if (!EFI_ERROR(s)) t->child_open=TRUE;
#ifdef TARUGX_EARLY_TEXT_INIT
    if (!EFI_ERROR(s)) s=tgx_text_arm(&t->text_init,t->child);
#endif
    return s;
}
static EFI_STATUS EFIAPI supported(EFI_DRIVER_BINDING_PROTOCOL *self,
                                   EFI_HANDLE controller, EFI_DEVICE_PATH *remaining)
{
    EFI_PCI_IO_PROTOCOL *pci;
    EFI_STATUS s;
    if (!valid_remaining(remaining)) return EFI_UNSUPPORTED;
    if (find_context(controller)) return EFI_SUCCESS;
    s = BS->OpenProtocol(controller, &pci_guid, (VOID **)&pci, self->DriverBindingHandle,
                         controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (EFI_ERROR(s)) return s;
    s = identify(pci);
    BS->CloseProtocol(controller, &pci_guid, self->DriverBindingHandle, controller);
    return s;
}
static EFI_STATUS EFIAPI start(EFI_DRIVER_BINDING_PROTOCOL *self,
                               EFI_HANDLE controller, EFI_DEVICE_PATH *remaining)
{
    TGX *t;
    EFI_PCI_IO_PROTOCOL *pci;
    EFI_STATUS s;
    UINT32 id;
    BOOLEAN saved = FALSE;
    if (!valid_remaining(remaining)) return EFI_UNSUPPORTED;
    t=find_context(controller);
    if (t) return remaining && IsDevicePathEnd(remaining) ? EFI_SUCCESS : create_child(self,t);
    s = BS->OpenProtocol(controller, &pci_guid, (VOID **)&pci, self->DriverBindingHandle,
                         controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (EFI_ERROR(s)) return s;
    s = BS->AllocatePool(EfiBootServicesData, sizeof(*t), (VOID **)&t);
    if (EFI_ERROR(s)) goto close;
    SetMem(t, sizeof(*t), 0);
    t->pci = pci;
    t->controller=controller;
    t->trace.version=2;
    t->io = (struct tgx_io){t, fb_read, fb_write};
    s = identify(pci);
    if (EFI_ERROR(s)) goto free;
    s = check_bar(pci, 0, 65536);
    if (EFI_ERROR(s)) goto free;
    s = check_bar(pci, 2, 4*1024*1024);
    if (EFI_ERROR(s)) goto free;
    s = uncached_framebuffer(t);
    if (EFI_ERROR(s)) goto free;
    s = pci->Attributes(pci, EfiPciIoAttributeOperationGet, 0, &t->attributes);
    if (EFI_ERROR(s)) goto free;
    saved = TRUE;
    s = pci->Attributes(pci, EfiPciIoAttributeOperationEnable, EFI_PCI_IO_ATTRIBUTE_MEMORY, NULL);
    if (EFI_ERROR(s)) goto free;
    s = pci->Mem.Read(pci, EfiPciIoWidthUint32, 0, 0, 1, &id);
    if (EFI_ERROR(s)) goto free;
    if (id != 0x54475055) { s = EFI_UNSUPPORTED; goto free; }
    t->gop.QueryMode = query; t->gop.SetMode = set_mode; t->gop.Blt = blt;
    t->gop.Mode = &t->mode;
    t->info.HorizontalResolution = TGX_WIDTH;
    t->info.VerticalResolution = TGX_HEIGHT;
    t->info.PixelFormat = PixelBltOnly;
    t->info.PixelsPerScanLine = TGX_WIDTH;
    t->mode.MaxMode = 1; t->mode.Mode = 0xffffffff;
    t->mode.Info = &t->info; t->mode.SizeOfInfo = sizeof(t->info);
    /* Parent-only requests must not create or initialize an output. */
    if (!remaining || !IsDevicePathEnd(remaining)) {
        s=create_child(self,t);
        if (EFI_ERROR(s) && !t->child) goto blank;
    }
    t->next=contexts;contexts=t;
    instances++;
    return s;
blank:
    reg_write(t, 0x10, 0);
free:
    { EFI_STATUS restored=restore_framebuffer_cache(t);
      if (EFI_ERROR(restored)) s=restored; }
    if (saved) pci->Attributes(pci, EfiPciIoAttributeOperationSet, t->attributes, NULL);
    BS->FreePool(t);
close:
    BS->CloseProtocol(controller, &pci_guid, self->DriverBindingHandle, controller);
    return s;
}
static EFI_STATUS EFIAPI stop(EFI_DRIVER_BINDING_PROTOCOL *self, EFI_HANDLE controller,
                              UINTN children, EFI_HANDLE *child_buffer)
{
    TGX *t=find_context(controller);
    if (!t) return EFI_NOT_STARTED;
    EFI_STATUS s;
    if (children) {
        if (children!=1 || !child_buffer || !t->child || child_buffer[0]!=t->child)
            return EFI_INVALID_PARAMETER;
        BOOLEAN was_open=t->child_open;
        if (was_open) {
            s=BS->CloseProtocol(controller,&pci_guid,self->DriverBindingHandle,t->child);
            if (EFI_ERROR(s)) return s;
            t->child_open=FALSE;
        }
        s=child_protocols(t,FALSE);
        if (EFI_ERROR(s)) {
            EFI_PCI_IO_PROTOCOL *pci=NULL;
            if (was_open) t->child_open=!EFI_ERROR(BS->OpenProtocol(controller,&pci_guid,
                (VOID **)&pci,self->DriverBindingHandle,t->child,EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER));
            return s;
        }
#ifdef TARUGX_EARLY_TEXT_INIT
        tgx_text_disarm(&t->text_init);
#endif
        BS->FreePool(t->path);t->path=NULL;t->child=NULL;
        return reg_write(t,0x10,0);
    }
    if (t->child) return EFI_DEVICE_ERROR;
    s=restore_framebuffer_cache(t);
    if (EFI_ERROR(s)) return s;
    s=t->pci->Attributes(t->pci,EfiPciIoAttributeOperationSet,t->attributes,NULL);
    if (EFI_ERROR(s)) return s;
    s=BS->CloseProtocol(controller,&pci_guid,self->DriverBindingHandle,controller);
    if (EFI_ERROR(s)) return s;
    TGX **link=&contexts;
    while (*link!=t) link=&(*link)->next;
    *link=t->next;
    BS->FreePool(t);instances--;
    return EFI_SUCCESS;
}

static EFI_DRIVER_BINDING_PROTOCOL binding = {supported, start, stop, 0x10, NULL, NULL};
static EFI_STATUS EFIAPI unload(EFI_HANDLE image)
{
    if (instances) return EFI_ACCESS_DENIED; /* Disconnect controllers before unload. */
    return BS->UninstallProtocolInterface(image, &binding_guid, &binding);
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    EFI_LOADED_IMAGE *loaded;
    EFI_STATUS s;
    InitializeLib(image, system);
    s = BS->HandleProtocol(image, &loaded_guid, (VOID **)&loaded);
    if (EFI_ERROR(s)) return s;
    binding.ImageHandle = image; binding.DriverBindingHandle = image;
    s = BS->InstallProtocolInterface(&image, &binding_guid, EFI_NATIVE_INTERFACE, &binding);
    if (!EFI_ERROR(s)) loaded->Unload = unload;
    return s;
}
