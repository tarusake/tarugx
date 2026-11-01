#ifndef TARUGX_DXE_MEMORY_H
#define TARUGX_DXE_MEMORY_H
#include <efi.h>
/* Minimal PI DXE read-only ABI, based on MdePkg/Include/Pi/PiDxeCis.h
 * and Guid/DxeServices.h in upstream EDK II. GNU-EFI lacks these definitions. */
typedef struct {
    EFI_PHYSICAL_ADDRESS base;
    UINT64 length, capabilities, attributes;
    UINT32 type;
    EFI_HANDLE image, device;
} TGX_GCD_DESCRIPTOR;
typedef EFI_STATUS (EFIAPI *TGX_GET_GCD)(EFI_PHYSICAL_ADDRESS, TGX_GCD_DESCRIPTOR *);
typedef struct {
    EFI_TABLE_HEADER header;
    VOID *add, *allocate, *free_space, *remove;
    TGX_GET_GCD get;
    EFI_STATUS (EFIAPI *set)(EFI_PHYSICAL_ADDRESS, UINT64, UINT64);
} TGX_DXE_PREFIX;

#define TGX_DXE_GUID {0x05ad34ba,0x6f02,0x4214,{0x95,0x2e,0x4d,0xa0,0x39,0x8e,0x2b,0xb9}}
#endif
