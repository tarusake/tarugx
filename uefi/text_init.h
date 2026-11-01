#ifndef TARUGX_TEXT_INIT_H
#define TARUGX_TEXT_INIT_H
/* Optional firmware compatibility experiment. Only our output, once per child.
 * Call through SimpleTextOutput; never draw a substitute font or change NVRAM. */
typedef struct {
    EFI_EVENT event;
    VOID *registration;
    EFI_HANDLE output;
    BOOLEAN attempted;
    INT32 before;
    EFI_STATUS result;
} TGX_TEXT_INIT;
static EFI_GUID tgx_text_guid=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
static VOID EFIAPI tgx_text_notify(EFI_EVENT event, VOID *context)
{
    (void)event;
    TGX_TEXT_INIT *state=context;
    EFI_HANDLE handle;
    UINTN size;
    while (TRUE) {
        size=sizeof(handle);
        EFI_STATUS s=BS->LocateHandle(ByRegisterNotify,&tgx_text_guid,state->registration,&size,&handle);
        if (EFI_ERROR(s)) break;
        if (handle!=state->output || state->attempted) continue;
        EFI_SIMPLE_TEXT_OUT_PROTOCOL *text=NULL;
        s=BS->HandleProtocol(handle,&tgx_text_guid,(VOID **)&text);
        if (EFI_ERROR(s) || !text || !text->Mode) continue;
        state->attempted=TRUE; /* Guard reentrant notifications before firmware calls. */
        state->before=text->Mode->Mode;
        state->result=EFI_SUCCESS;
        if (state->before!=-1) continue;
        UINTN columns=0,rows=0;
        s=text->QueryMode(text,0,&columns,&rows);
        if (!EFI_ERROR(s) && (columns!=80 || rows!=25)) s=EFI_UNSUPPORTED;
        if (!EFI_ERROR(s)) s=text->SetMode(text,0);
        state->result=s;
    }
}
static EFI_STATUS tgx_text_arm(TGX_TEXT_INIT *state, EFI_HANDLE output)
{
    state->output=output;state->attempted=FALSE;state->result=EFI_NOT_READY;
    EFI_STATUS s=BS->CreateEvent(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,tgx_text_notify,state,&state->event);
    if (EFI_ERROR(s)) return s;
    s=BS->RegisterProtocolNotify(&tgx_text_guid,state->event,&state->registration);
    if (EFI_ERROR(s)) { BS->CloseEvent(state->event);state->event=NULL;state->registration=NULL;return s; }
    /* Drain any already installed instance as well as subsequent notifications. */
    s=BS->SignalEvent(state->event);
    if (EFI_ERROR(s)) { BS->CloseEvent(state->event);state->event=NULL;state->registration=NULL; }
    return s;
}
static VOID tgx_text_disarm(TGX_TEXT_INIT *state)
{
    if (state->event) BS->CloseEvent(state->event);
    state->event=NULL;state->registration=NULL;
}
#endif
