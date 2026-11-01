// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#ifndef TARUGX_GOP_TRACE_H
#define TARUGX_GOP_TRACE_H
/* Private, read-only boot-services diagnostic protocol, version 2 (version-1 prefix retained).
 * Records the first calls: preserving startup matters more than latest calls.
 * No callbacks, physical addresses or persistent writes. */
#define TGX_TRACE_GUID {0xb335a4af,0x7ade,0x41d7,{0x8b,0x7b,0xe8,0xf9,0x06,0x31,0xe6,0x57}}
#define TGX_TRACE_CAPACITY 64
#define TGX_TRACE_QUERY 1
#define TGX_TRACE_SET 2
typedef struct {
    UINT32 operation, mode;
    UINT64 status;
} TGX_TRACE_ENTRY;
#define TGX_BLT_TRACE_CAPACITY 32
typedef struct {
    UINT64 operation, status, sx, sy, dx, dy, width, height, delta;
} TGX_BLT_TRACE_ENTRY;
typedef struct {
    UINT32 version, count, dropped, reserved;
    TGX_TRACE_ENTRY entries[TGX_TRACE_CAPACITY];
    UINT32 blt_count, blt_dropped, blt_errors, failure_valid;
    TGX_BLT_TRACE_ENTRY first_failure;
    TGX_BLT_TRACE_ENTRY blt_entries[TGX_BLT_TRACE_CAPACITY];
} TGX_TRACE;
#endif
