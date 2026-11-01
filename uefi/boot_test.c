/* One-shot GOP test, then chainload the existing Ubuntu shim on this ESP.
 * No NVRAM writes, no PCI configuration writes, no FPGA reconfiguration. */
#include <efi.h>
#include <efilib.h>
#include <efipciio.h>

static EFI_GUID pci_guid = EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_FILE_HANDLE log_file;

static VOID record(CHAR8 *stage, EFI_STATUS status)
{
    CHAR8 line[128];
    UINTN n=0, len;
    while (*stage && n<90) line[n++]=*stage++;
    line[n++]=' '; line[n++]='0'; line[n++]='x';
    for (INTN i=15;i>=0;i--) line[n++]="0123456789abcdef"[(status>>(i*4))&15];
    line[n++]='\r'; line[n++]='\n'; len=n;
    if (log_file) {
        log_file->Write(log_file, &len, line);
        log_file->Flush(log_file);
    }
}
/* Read-only configuration snapshot: do not guess or rewrite bridge windows. */
static VOID record_pci(EFI_PCI_IO_PROTOCOL *pci)
{
    UINT32 v;
    UINTN seg,bus,dev,fn;
    EFI_STATUS s=pci->GetLocation(pci,&seg,&bus,&dev,&fn);
    if (!EFI_ERROR(s)) record((CHAR8 *)"PCI_LOCATION",(seg<<24)|(bus<<16)|(dev<<8)|fn);
    const UINT32 offsets[]={0,4,8,0x10,0x14,0x18,0x1c,0x20,0x24,0x28,0x2c};
    CHAR8 *names[]={(CHAR8 *)"PCI_ID",(CHAR8 *)"PCI_COMMAND_STATUS",(CHAR8 *)"PCI_CLASS_REV",
        (CHAR8 *)"PCI_CFG_10",(CHAR8 *)"PCI_CFG_14",(CHAR8 *)"PCI_CFG_18",
        (CHAR8 *)"PCI_CFG_1C",(CHAR8 *)"PCI_CFG_20",(CHAR8 *)"PCI_CFG_24",
        (CHAR8 *)"PCI_CFG_28",(CHAR8 *)"PCI_CFG_2C"};
    for (UINTN i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        s=pci->Pci.Read(pci,EfiPciIoWidthUint32,offsets[i],1,&v);
        record(names[i],EFI_ERROR(s)?s:v);
    }
}
static VOID record_bar(EFI_PCI_IO_PROTOCOL *pci, UINT8 bar)
{
    VOID *resources=NULL;
    UINT64 supports=0;
    EFI_STATUS st=pci->GetBarAttributes(pci,bar,&supports,&resources);
    record((CHAR8 *)"BAR_INDEX",bar);
    record((CHAR8 *)"BAR_RESOURCES_STATUS",st);
    record((CHAR8 *)"BAR_SUPPORTED_ATTRIBUTES",supports);
    if (!EFI_ERROR(st) && resources) {
        UINT8 *p=resources;
        if (p[0]==0x8a && p[1]==0x2b && p[2]==0) {
            const UINTN offsets[]={14,22,30,38};
            CHAR8 *names[]={(CHAR8 *)"BAR_RESOURCE_MIN",(CHAR8 *)"BAR_RESOURCE_MAX",
                (CHAR8 *)"BAR_RESOURCE_TRANSLATION",(CHAR8 *)"BAR_RESOURCE_LENGTH"};
            for (UINTN i=0;i<4;i++) {
                UINT64 value=0;
                for (UINTN j=0;j<8;j++) value|=(UINT64)p[offsets[i]+j]<<(j*8);
                record(names[i],value);
            }
        }
        BS->FreePool(resources);
    }
}
#include "dxe_memory.h"
static VOID record_gcd(EFI_PCI_IO_PROTOCOL *pci)
{
    EFI_GUID guid={0x05ad34ba,0x6f02,0x4214,{0x95,0x2e,0x4d,0xa0,0x39,0x8e,0x2b,0xb9}};
    TGX_DXE_PREFIX *dxe=NULL;
    EFI_STATUS status=LibGetSystemConfigurationTable(&guid,(VOID **)&dxe);
    record((CHAR8 *)"DXE_SERVICES",status);
    if (EFI_ERROR(status) || !dxe || dxe->header.Signature!=0x565245535f455844ULL ||
        dxe->header.HeaderSize<sizeof(*dxe) || !dxe->get) return;
    for (UINTN bar=0;bar<=2;bar+=2) {
        UINT32 value=0;
        status=pci->Pci.Read(pci,EfiPciIoWidthUint32,0x10+bar*4,1,&value);
        if (EFI_ERROR(status)) continue;
        TGX_GCD_DESCRIPTOR d;
        record((CHAR8 *)"GCD_BAR_INDEX",bar);
        status=dxe->get(value&0xfffffff0,&d);
        record((CHAR8 *)"GCD_STATUS",status);
        if (!EFI_ERROR(status)) {
            record((CHAR8 *)"GCD_BASE",d.base);
            record((CHAR8 *)"GCD_LENGTH",d.length);
            record((CHAR8 *)"GCD_TYPE",d.type);
            record((CHAR8 *)"GCD_CAPABILITIES",d.capabilities);
            record((CHAR8 *)"GCD_ATTRIBUTES",d.attributes);
        }
    }
}
#ifdef TARUGX_AUTO_ROM_TEST
/* Observe firmware-discovered ROM and the driver managing this controller.
 * This variant never loads TaruGxGop.efi from the ESP. */
static VOID record_auto_rom(EFI_HANDLE controller, EFI_PCI_IO_PROTOCOL *pci)
{
    record((CHAR8 *)"AUTO_ROM_SIZE",(EFI_STATUS)pci->RomSize);
    record((CHAR8 *)"AUTO_ROM_PRESENT",pci->RomImage ? EFI_SUCCESS : EFI_NOT_FOUND);
    if (pci->RomImage && pci->RomSize && pci->RomSize<=65536) {
        UINT32 crc=0;
        EFI_STATUS st=BS->CalculateCrc32(pci->RomImage,(UINTN)pci->RomSize,&crc);
        record((CHAR8 *)"AUTO_ROM_CRC_STATUS",st);
        if (!EFI_ERROR(st)) record((CHAR8 *)"AUTO_ROM_CRC32",crc);
    }
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;
    UINTN n=0;
    EFI_STATUS st=BS->OpenProtocolInformation(controller,&pci_guid,&entries,&n);
    record((CHAR8 *)"AUTO_OPEN_PROTOCOL_INFO",st);
    if (EFI_ERROR(st)) return;
    for (UINTN i=0;i<n;i++) {
        if (!(entries[i].Attributes&EFI_OPEN_PROTOCOL_BY_DRIVER)) continue;
        EFI_LOADED_IMAGE *loaded=NULL;
        st=BS->HandleProtocol(entries[i].AgentHandle,&loaded_guid,(VOID **)&loaded);
        record((CHAR8 *)"AUTO_MANAGING_IMAGE",st);
        if (EFI_ERROR(st) || !loaded) continue;
        EFI_DEVICE_PATH *path=loaded->FilePath;
        for (UINTN node=0;path && node<32;node++) {
            UINTN length=DevicePathNodeLength(path);
            record((CHAR8 *)"AUTO_IMAGE_PATH_NODE",
                   ((UINTN)path->Type<<24)|((UINTN)path->SubType<<16)|length);
            if (IsDevicePathEnd(path) || length<4 || length>1024) break;
            path=NextDevicePathNode(path);
        }
    }
    BS->FreePool(entries);
}
#endif
static EFI_STATUS load(EFI_HANDLE image, EFI_HANDLE device, CHAR16 *name, EFI_HANDLE *loaded)
{
    EFI_DEVICE_PATH *path=FileDevicePath(device,name);
    EFI_STATUS s;
    if (!path) return EFI_OUT_OF_RESOURCES;
    s=BS->LoadImage(FALSE,image,path,NULL,0,loaded);
    BS->FreePool(path);
    return s;
}
static UINT32 pixel_value(EFI_GRAPHICS_OUTPUT_BLT_PIXEL p)
{
    return (UINT32)p.Blue | ((UINT32)p.Green << 8) | ((UINT32)p.Red << 16);
}
static EFI_STATUS pattern(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL colors[]={{0,0,255,0},{0,255,0,0},{255,0,0,0}};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL white={255,255,255,0}, readback;
    EFI_STATUS s;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=NULL;
    UINTN size;
    record((CHAR8 *)"QUERY_MODE_BEGIN",EFI_SUCCESS);
    s=gop->QueryMode(gop,0,&size,&info);
    record((CHAR8 *)"QUERY_MODE_END",s);
    if (EFI_ERROR(s)) return s;
    BOOLEAN valid=info->HorizontalResolution==640 && info->VerticalResolution==480 &&
                  info->PixelFormat==PixelBltOnly;
    BS->FreePool(info);
    if (!valid) return EFI_UNSUPPORTED;
    record((CHAR8 *)"SET_MODE_BEGIN",EFI_SUCCESS);
    s=gop->SetMode(gop,0);
    record((CHAR8 *)"SET_MODE_END",s);
    if (EFI_ERROR(s)) return s;
    CHAR8 *fill_stages[]={ (CHAR8 *)"FILL_RED", (CHAR8 *)"FILL_GREEN", (CHAR8 *)"FILL_BLUE" };
    CHAR8 *read_stages[]={ (CHAR8 *)"READ_RED", (CHAR8 *)"READ_GREEN", (CHAR8 *)"READ_BLUE" };
    for (UINTN i=0;i<3;i++) {
        record(fill_stages[i],EFI_NOT_READY);
        s=gop->Blt(gop,&colors[i],EfiBltVideoFill,0,0,0,i*160,640,160,0);
        record(fill_stages[i],s);
        if (EFI_ERROR(s)) return s;
        record(read_stages[i],EFI_NOT_READY);
        s=gop->Blt(gop,&readback,EfiBltVideoToBltBuffer,320,i*160+80,0,0,1,1,0);
        record(read_stages[i],s);
        if (EFI_ERROR(s)) return s;
        record((CHAR8 *)"PIXEL_EXPECTED",pixel_value(colors[i]));
        record((CHAR8 *)"PIXEL_ACTUAL",pixel_value(readback));
        if (readback.Red!=colors[i].Red || readback.Green!=colors[i].Green ||
            readback.Blue!=colors[i].Blue) return EFI_DEVICE_ERROR;
    }
    s=gop->Blt(gop,&white,EfiBltBufferToVideo,0,0,0,0,1,1,0);
    if (EFI_ERROR(s)) return s;
    s=gop->Blt(gop,NULL,EfiBltVideoToVideo,0,0,639,479,1,1,0);
    if (EFI_ERROR(s)) return s;
    s=gop->Blt(gop,&readback,EfiBltVideoToBltBuffer,639,479,0,0,1,1,0);
    if (EFI_ERROR(s)) return s;
    record((CHAR8 *)"COPY_PIXEL_EXPECTED",0x00ffffff);
    record((CHAR8 *)"COPY_PIXEL_ACTUAL",pixel_value(readback));
    if (readback.Red!=255 || readback.Green!=255 || readback.Blue!=255) return EFI_DEVICE_ERROR;
    return EFI_SUCCESS;
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    EFI_LOADED_IMAGE *self;
    EFI_FILE_IO_INTERFACE *fs;
    EFI_FILE_HANDLE root=NULL;
    EFI_HANDLE driver=NULL, os=NULL, *handles=NULL, controller=NULL;
#ifndef TARUGX_AUTO_ROM_TEST
    EFI_HANDLE driver_list[2];
#endif
    EFI_PCI_IO_PROTOCOL *pci;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_STATUS s;
    UINTN count=0;
    BOOLEAN keep_display=FALSE;
    InitializeLib(image,system);
    s=BS->HandleProtocol(image,&loaded_guid,(VOID **)&self);
    if (EFI_ERROR(s)) return s;
    if (!EFI_ERROR(BS->HandleProtocol(self->DeviceHandle,&fs_guid,(VOID **)&fs)) &&
        !EFI_ERROR(fs->OpenVolume(fs,&root))) {
        /* Installer removes the previous log before each test. */
#ifdef TARUGX_AUTO_ROM_TEST
        root->Open(root,&log_file,L"\\EFI\\TaruGX\\auto-rom-test.log",
#else
        root->Open(root,&log_file,L"\\EFI\\TaruGX\\gop-test.log",
#endif
                   EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    }
    record((CHAR8 *)"TEST_START",EFI_SUCCESS);
    BS->SetWatchdogTimer(120,0,0,NULL);
#ifndef TARUGX_AUTO_ROM_TEST
    s=load(image,self->DeviceHandle,L"\\EFI\\TaruGX\\TaruGxGop.efi",&driver);
    record((CHAR8 *)"LOAD_DRIVER",s);
    if (EFI_ERROR(s)) goto boot;
    s=BS->StartImage(driver,NULL,NULL);
    record((CHAR8 *)"START_DRIVER",s);
    if (EFI_ERROR(s)) { driver=NULL; goto boot; }
#else
    record((CHAR8 *)"AUTO_ROM_TEST_NO_ESP_DRIVER_LOAD",EFI_SUCCESS);
#endif
    s=BS->LocateHandleBuffer(ByProtocol,&pci_guid,NULL,&count,&handles);
    if (EFI_ERROR(s)) { record((CHAR8 *)"ENUMERATE_PCI",s); goto boot; }
    for (UINTN i=0;i<count;i++) {
        UINT32 id;
        if (EFI_ERROR(BS->HandleProtocol(handles[i],&pci_guid,(VOID **)&pci))) continue;
        if (EFI_ERROR(pci->Pci.Read(pci,EfiPciIoWidthUint32,0,1,&id))) continue;
        UINT32 classrev=0;
        pci->Pci.Read(pci,EfiPciIoWidthUint32,8,1,&classrev);
        if (id==0x110022c2 || (classrev>>16)==0x0604) record_pci(pci);
        if (id==0x110022c2) controller=handles[i];
    }
    BS->FreePool(handles);
    record((CHAR8 *)"FIND_TARUGX",controller ? EFI_SUCCESS : EFI_NOT_FOUND);
    if (!controller) goto boot;
    s=BS->HandleProtocol(controller,&pci_guid,(VOID **)&pci);
    if (EFI_ERROR(s)) { record((CHAR8 *)"GET_PCI_IO",s); goto boot; }
#ifdef TARUGX_AUTO_ROM_TEST
    record_auto_rom(controller,pci);
    s=BS->HandleProtocol(controller,&gop_guid,(VOID **)&gop);
    record((CHAR8 *)"AUTO_GOP_BEFORE_CONNECT",s);
    if (EFI_ERROR(s)) {
        s=BS->ConnectController(controller,NULL,NULL,FALSE);
        record((CHAR8 *)"AUTO_CONNECT_EXISTING_DRIVERS",s);
        record_auto_rom(controller,pci);
    }
#else
    driver_list[0]=driver; driver_list[1]=NULL;
    record((CHAR8 *)"CONNECT_DRIVER_BEGIN",EFI_SUCCESS);
    s=BS->ConnectController(controller,driver_list,NULL,FALSE);
    record((CHAR8 *)"CONNECT_DRIVER",s);
    if (EFI_ERROR(s)) goto boot;
#endif
    s=BS->HandleProtocol(controller,&gop_guid,(VOID **)&gop);
    record((CHAR8 *)"GET_GOP",s);
    if (EFI_ERROR(s)) goto boot;
    record_bar(pci,0); record_bar(pci,2); record_gcd(pci);
    UINT64 attrs=0;
    s=pci->Attributes(pci,EfiPciIoAttributeOperationGet,0,&attrs);
    record((CHAR8 *)"PCI_CURRENT_ATTRIBUTES",EFI_ERROR(s)?s:attrs);
    UINT32 raw=0;
    s=pci->Mem.Read(pci,EfiPciIoWidthUint32,0,0,1,&raw);
    record((CHAR8 *)"RAW_GPU_ID",EFI_ERROR(s)?s:raw);
    s=pci->Mem.Read(pci,EfiPciIoWidthUint32,2,0,1,&raw);
    record((CHAR8 *)"RAW_BAR2_AFTER_CLEAR",EFI_ERROR(s)?s:raw);
    UINT32 bar2=0;
    s=pci->Pci.Read(pci,EfiPciIoWidthUint32,0x18,1,&bar2);
    if (!EFI_ERROR(s) && (bar2&0xfffffff0) && (bar2&7)==0) {
        s=pci->Mem.Read(pci,EfiPciIoWidthUint32,EFI_PCI_IO_PASS_THROUGH_BAR,
                        bar2&0xfffffff0,1,&raw);
        record((CHAR8 *)"RAW_BAR2_PASSTHROUGH",EFI_ERROR(s)?s:raw);
    }
    s=pattern(gop);
    record((CHAR8 *)"BLT_PATTERN_AND_READBACK",s);
    if (!EFI_ERROR(s)) {
        UINT32 ctrl, base, stride;
        s=pci->Mem.Read(pci,EfiPciIoWidthUint32,0,0x10,1,&ctrl);
        if (!EFI_ERROR(s)) s=pci->Mem.Read(pci,EfiPciIoWidthUint32,0,0x1c,1,&base);
        if (!EFI_ERROR(s)) s=pci->Mem.Read(pci,EfiPciIoWidthUint32,0,0x20,1,&stride);
        if (!EFI_ERROR(s) && ((ctrl&3)!=1 || base!=0 || stride!=2560)) s=EFI_DEVICE_ERROR;
        record((CHAR8 *)"DISPLAY_STATE_VALIDATED",s);
        if (!EFI_ERROR(s)) {
            keep_display=TRUE;
            record((CHAR8 *)"PATTERN_HOLD_BEGIN",EFI_SUCCESS);
            BS->Stall(20000000);
            record((CHAR8 *)"PATTERN_HOLD_END",EFI_SUCCESS);
        }
    }
boot:
    /* Normal GOPs remain bound through ExitBootServices. The FPGA continues
     * scanning DDR without firmware callbacks or DMA; Linux later takes over.
     * Stop intentionally blanks the display, so only run it on test failure. */
    if (keep_display) record((CHAR8 *)"KEEP_DISPLAY_ENABLED",EFI_SUCCESS);
    else {
        if (controller && driver) record((CHAR8 *)"DISCONNECT",BS->DisconnectController(controller,driver,NULL));
        if (driver) record((CHAR8 *)"UNLOAD",BS->UnloadImage(driver));
    }
#ifdef TARUGX_DIAGNOSTIC_RESET
    /* A fresh firmware boot avoids carrying diagnostic GOP state into shim.
     * BootNext must be one-shot and consumed before this application runs. */
    record((CHAR8 *)"RESET_TO_NORMAL_BOOT",EFI_SUCCESS);
    if (log_file) { log_file->Close(log_file); log_file=NULL; }
    if (root) { root->Close(root); root=NULL; }
    RT->ResetSystem(EfiResetCold,EFI_SUCCESS,0,NULL);
    return EFI_DEVICE_ERROR;
#endif
    s=load(image,self->DeviceHandle,L"\\EFI\\ubuntu\\shimx64.efi",&os);
    record((CHAR8 *)"LOAD_UBUNTU",s);
    if (log_file) log_file->Close(log_file);
    if (root) root->Close(root);
    BS->SetWatchdogTimer(300,0,0,NULL);
    if (EFI_ERROR(s)) return s;
    return BS->StartImage(os,NULL,NULL);
}
