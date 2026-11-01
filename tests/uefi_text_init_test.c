#include <efi.h>
#include <efilib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
EFI_BOOT_SERVICES *BS;
#include "../uefi/text_init.h"
static EFI_BOOT_SERVICES services;
static EFI_SIMPLE_TEXT_OUT_PROTOCOL text;
static SIMPLE_TEXT_OUTPUT_MODE mode;
static EFI_HANDLE queue[8];
static unsigned head,tail,sets,closed;
static EFI_EVENT_NOTIFY notify;
static VOID *context;
static EFI_STATUS create_status,register_status,set_status,signal_status;
static UINTN columns=80;
static EFI_STATUS EFIAPI create(UINT32 type,EFI_TPL tpl,EFI_EVENT_NOTIFY fn,VOID *ctx,EFI_EVENT *event)
{ assert(type==EVT_NOTIFY_SIGNAL && tpl==TPL_CALLBACK);if(EFI_ERROR(create_status))return create_status;
  notify=fn;context=ctx;*event=(EFI_EVENT)1;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI reg(EFI_GUID *guid,EFI_EVENT event,VOID **key)
{ assert(!memcmp(guid,&tgx_text_guid,sizeof(*guid)) && event==(EFI_EVENT)1);*key=(VOID *)2;return register_status; }
static EFI_STATUS EFIAPI close_event(EFI_EVENT event)
{ assert(event==(EFI_EVENT)1);closed++;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI signal_event(EFI_EVENT event)
{ if(EFI_ERROR(signal_status))return signal_status;notify(event,context);return EFI_SUCCESS; }
static EFI_STATUS EFIAPI locate(EFI_LOCATE_SEARCH_TYPE type,EFI_GUID *guid,VOID *key,UINTN *size,EFI_HANDLE *h)
{ (void)guid;assert(type==ByRegisterNotify && key==(VOID *)2 && *size==sizeof(*h));
  if(head==tail)return EFI_NOT_FOUND;
  *h=queue[head++];return EFI_SUCCESS; }
static EFI_STATUS EFIAPI handle(EFI_HANDLE h,EFI_GUID *guid,VOID **p)
{ (void)guid;assert(h==(EFI_HANDLE)3);*p=&text;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI query_text(EFI_SIMPLE_TEXT_OUT_PROTOCOL *t,UINTN n,UINTN *c,UINTN *r)
{ assert(t==&text && n==0);*c=columns;*r=25;return EFI_SUCCESS; }
static EFI_STATUS EFIAPI set_text(EFI_SIMPLE_TEXT_OUT_PROTOCOL *t,UINTN n)
{ assert(t==&text && n==0);sets++;signal_event((EFI_EVENT)1);
  if(!EFI_ERROR(set_status))mode.Mode=0;
  return set_status; }
int main(void)
{
 BS=&services;services.CreateEvent=create;services.RegisterProtocolNotify=reg;
 services.CloseEvent=close_event;services.SignalEvent=signal_event;services.LocateHandle=locate;
 services.HandleProtocol=handle;text.Mode=&mode;text.QueryMode=query_text;text.SetMode=set_text;
 TGX_TEXT_INIT state={0};mode.Mode=-1;
 queue[tail++]=(EFI_HANDLE)9;queue[tail++]=(EFI_HANDLE)3;queue[tail++]=(EFI_HANDLE)3;
 assert(tgx_text_arm(&state,(EFI_HANDLE)3)==EFI_SUCCESS);
 assert(sets==1 && state.before==-1 && state.result==EFI_SUCCESS && mode.Mode==0);
 tgx_text_disarm(&state);assert(closed==1 && !state.event);
 queue[tail++]=(EFI_HANDLE)3;mode.Mode=0;
 assert(tgx_text_arm(&state,(EFI_HANDLE)3)==EFI_SUCCESS && sets==1);tgx_text_disarm(&state);
 queue[tail++]=(EFI_HANDLE)3;mode.Mode=-1;set_status=EFI_DEVICE_ERROR;
 assert(tgx_text_arm(&state,(EFI_HANDLE)3)==EFI_SUCCESS && sets==2 && state.result==EFI_DEVICE_ERROR);
 tgx_text_disarm(&state);columns=100;queue[tail++]=(EFI_HANDLE)3;
 assert(tgx_text_arm(&state,(EFI_HANDLE)3)==EFI_SUCCESS && sets==2 && state.result==EFI_UNSUPPORTED);
 tgx_text_disarm(&state);register_status=EFI_OUT_OF_RESOURCES;
 assert(tgx_text_arm(&state,(EFI_HANDLE)3)==EFI_OUT_OF_RESOURCES && !state.event);
 register_status=EFI_SUCCESS;signal_status=EFI_DEVICE_ERROR;
 assert(tgx_text_arm(&state,(EFI_HANDLE)3)==EFI_DEVICE_ERROR && !state.event && !state.registration);
 create_status=EFI_OUT_OF_RESOURCES;
 assert(tgx_text_arm(&state,(EFI_HANDLE)3)==EFI_OUT_OF_RESOURCES && !state.event);
 puts("text init: target filtering, valid mode, reentry, failures and cleanup passed");
}
