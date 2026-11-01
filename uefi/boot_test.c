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
static EFI_STATUS load(EFI_HANDLE image, EFI_HANDLE device, CHAR16 *name, EFI_HANDLE *loaded)
{
    EFI_DEVICE_PATH *path=FileDevicePath(device,name);
    EFI_STATUS s;
    if (!path) return EFI_OUT_OF_RESOURCES;
    s=BS->LoadImage(FALSE,image,path,NULL,0,loaded);
    BS->FreePool(path);
    return s;
}
static EFI_STATUS pattern(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL colors[]={{0,0,255,0},{0,255,0,0},{255,0,0,0}};
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL white={255,255,255,0}, readback;
    EFI_STATUS s;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=NULL;
    UINTN size;
    s=gop->QueryMode(gop,0,&size,&info);
    if (EFI_ERROR(s)) return s;
    BOOLEAN valid=info->HorizontalResolution==640 && info->VerticalResolution==480 &&
                  info->PixelFormat==PixelBltOnly;
    BS->FreePool(info);
    if (!valid) return EFI_UNSUPPORTED;
    s=gop->SetMode(gop,0);
    if (EFI_ERROR(s)) return s;
    for (UINTN i=0;i<3;i++) {
        s=gop->Blt(gop,&colors[i],EfiBltVideoFill,0,0,0,i*160,640,160,0);
        if (EFI_ERROR(s)) return s;
        s=gop->Blt(gop,&readback,EfiBltVideoToBltBuffer,320,i*160+80,0,0,1,1,0);
        if (EFI_ERROR(s)) return s;
        if (readback.Red!=colors[i].Red || readback.Green!=colors[i].Green ||
            readback.Blue!=colors[i].Blue) return EFI_DEVICE_ERROR;
    }
    s=gop->Blt(gop,&white,EfiBltBufferToVideo,0,0,0,0,1,1,0);
    if (EFI_ERROR(s)) return s;
    s=gop->Blt(gop,NULL,EfiBltVideoToVideo,0,0,639,479,1,1,0);
    if (EFI_ERROR(s)) return s;
    s=gop->Blt(gop,&readback,EfiBltVideoToBltBuffer,639,479,0,0,1,1,0);
    if (EFI_ERROR(s)) return s;
    if (readback.Red!=255 || readback.Green!=255 || readback.Blue!=255) return EFI_DEVICE_ERROR;
    return EFI_SUCCESS;
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    EFI_LOADED_IMAGE *self;
    EFI_FILE_IO_INTERFACE *fs;
    EFI_FILE_HANDLE root=NULL;
    EFI_HANDLE driver=NULL, os=NULL, *handles=NULL, controller=NULL;
    EFI_HANDLE driver_list[2];
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
        root->Open(root,&log_file,L"\\EFI\\TaruGX\\gop-test.log",
                   EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
    }
    record((CHAR8 *)"TEST_START",EFI_SUCCESS);
    BS->SetWatchdogTimer(120,0,0,NULL);
    s=load(image,self->DeviceHandle,L"\\EFI\\TaruGX\\TaruGxGop.efi",&driver);
    record((CHAR8 *)"LOAD_DRIVER",s);
    if (EFI_ERROR(s)) goto boot;
    s=BS->StartImage(driver,NULL,NULL);
    record((CHAR8 *)"START_DRIVER",s);
    if (EFI_ERROR(s)) { driver=NULL; goto boot; }
    s=BS->LocateHandleBuffer(ByProtocol,&pci_guid,NULL,&count,&handles);
    if (EFI_ERROR(s)) { record((CHAR8 *)"ENUMERATE_PCI",s); goto boot; }
    for (UINTN i=0;i<count;i++) {
        UINT32 id;
        if (EFI_ERROR(BS->HandleProtocol(handles[i],&pci_guid,(VOID **)&pci))) continue;
        if (EFI_ERROR(pci->Pci.Read(pci,EfiPciIoWidthUint32,0,1,&id))) continue;
        if (id==0x110022c2) { controller=handles[i]; break; }
    }
    BS->FreePool(handles);
    record((CHAR8 *)"FIND_TARUGX",controller ? EFI_SUCCESS : EFI_NOT_FOUND);
    if (!controller) goto boot;
    driver_list[0]=driver; driver_list[1]=NULL;
    s=BS->ConnectController(controller,driver_list,NULL,FALSE);
    record((CHAR8 *)"CONNECT_DRIVER",s);
    if (EFI_ERROR(s)) goto boot;
    s=BS->HandleProtocol(controller,&gop_guid,(VOID **)&gop);
    record((CHAR8 *)"GET_GOP",s);
    if (EFI_ERROR(s)) goto boot;
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
            Print(L"TaruGX GOP: RGB bands will remain enabled after this test.\r\n");
            BS->Stall(20000000);
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
    s=load(image,self->DeviceHandle,L"\\EFI\\ubuntu\\shimx64.efi",&os);
    record((CHAR8 *)"LOAD_UBUNTU",s);
    if (log_file) log_file->Close(log_file);
    if (root) root->Close(root);
    BS->SetWatchdogTimer(300,0,0,NULL);
    if (EFI_ERROR(s)) return s;
    return BS->StartImage(os,NULL,NULL);
}
