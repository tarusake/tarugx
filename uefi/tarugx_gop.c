#include <efi.h>
#include <efilib.h>
#include <efipciio.h>
#include "blt.h"

static EFI_GUID pci_guid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID binding_guid = EFI_DRIVER_BINDING_PROTOCOL_GUID;
static EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID discovered_guid = EFI_EDID_DISCOVERED_PROTOCOL_GUID;
static EFI_GUID active_guid = EFI_EDID_ACTIVE_PROTOCOL_GUID;
static UINTN instances;

typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop; /* First: GOP pointer is the context. */
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
    EFI_EDID_DISCOVERED_PROTOCOL discovered;
    EFI_EDID_ACTIVE_PROTOCOL active;
    EFI_PCI_IO_PROTOCOL *pci;
    UINT64 attributes;
    struct tgx_io io;
} TGX;

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
static EFI_STATUS EFIAPI query(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, UINT32 mode,
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
static EFI_STATUS EFIAPI set_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *g, UINT32 mode)
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
static EFI_STATUS EFIAPI blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *g,
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *b, EFI_GRAPHICS_OUTPUT_BLT_OPERATION op,
    UINTN sx, UINTN sy, UINTN dx, UINTN dy, UINTN w, UINTN h, UINTN delta)
{
    int rc;
    if (!g) return EFI_INVALID_PARAMETER;
    if (g->Mode->Mode != 0) return EFI_NOT_STARTED;
    rc = tgx_blt(&((TGX *)g)->io, b, op, sx, sy, dx, dy, w, h, delta);
    return rc == 0 ? EFI_SUCCESS : rc == -1 ? EFI_INVALID_PARAMETER : EFI_DEVICE_ERROR;
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

static EFI_STATUS EFIAPI supported(EFI_DRIVER_BINDING_PROTOCOL *self,
                                   EFI_HANDLE controller, EFI_DEVICE_PATH *remaining)
{
    EFI_PCI_IO_PROTOCOL *pci;
    EFI_STATUS s;
    if (remaining && !IsDevicePathEnd(remaining)) return EFI_UNSUPPORTED;
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
    if (remaining && !IsDevicePathEnd(remaining)) return EFI_UNSUPPORTED;
    s = BS->OpenProtocol(controller, &pci_guid, (VOID **)&pci, self->DriverBindingHandle,
                         controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (EFI_ERROR(s)) return s;
    s = BS->AllocatePool(EfiBootServicesData, sizeof(*t), (VOID **)&t);
    if (EFI_ERROR(s)) goto close;
    SetMem(t, sizeof(*t), 0);
    t->pci = pci;
    t->io = (struct tgx_io){t, fb_read, fb_write};
    s = identify(pci);
    if (EFI_ERROR(s)) goto free;
    s = check_bar(pci, 0, 65536);
    if (EFI_ERROR(s)) goto free;
    s = check_bar(pci, 2, 4*1024*1024);
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
    /* Do not expose BAR2 as a directly writable linear framebuffer. */
    s = set_mode(&t->gop, 0);
    if (EFI_ERROR(s)) goto blank;
    s = BS->InstallMultipleProtocolInterfaces(&controller,
        &gop_guid, &t->gop, &discovered_guid, &t->discovered, &active_guid, &t->active, NULL);
    if (EFI_ERROR(s)) goto blank;
    instances++;
    return EFI_SUCCESS;
blank:
    reg_write(t, 0x10, 0);
free:
    if (saved) pci->Attributes(pci, EfiPciIoAttributeOperationSet, t->attributes, NULL);
    BS->FreePool(t);
close:
    BS->CloseProtocol(controller, &pci_guid, self->DriverBindingHandle, controller);
    return s;
}
static EFI_STATUS EFIAPI stop(EFI_DRIVER_BINDING_PROTOCOL *self, EFI_HANDLE controller,
                              UINTN children, EFI_HANDLE *child_buffer)
{
    TGX *t;
    EFI_STATUS s;
    (void)child_buffer;
    if (children) return EFI_UNSUPPORTED;
    s = BS->OpenProtocol(controller, &gop_guid, (VOID **)&t, self->DriverBindingHandle,
                         controller, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(s)) return s;
    if (t->gop.Blt != blt) return EFI_UNSUPPORTED;
    s = BS->UninstallMultipleProtocolInterfaces(controller,
        &gop_guid, &t->gop, &discovered_guid, &t->discovered, &active_guid, &t->active, NULL);
    if (EFI_ERROR(s)) return s;
    reg_write(t, 0x10, 0);
    t->pci->Attributes(t->pci, EfiPciIoAttributeOperationSet, t->attributes, NULL);
    BS->CloseProtocol(controller, &pci_guid, self->DriverBindingHandle, controller);
    BS->FreePool(t);
    instances--;
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
