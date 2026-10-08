/* Run real calculator transport/editor with simulated OS, UART and clock. */
#define main calculator_main
#include "../../apps/CasioWIFI/src/main.c"
#undef main
#include <assert.h>
static int now,port_open,tx_free=256,close_mode=-1,opens;
static unsigned char incoming[4096];
static int input_start,input_end;
static char last_payload[WIRE_CAP];
static wire_rx outgoing;
static color_t pixels[384*216];
static void (*menu_callback)(void);
int RTC_GetTicks(void) { return now; }
int RTC_Elapsed_ms(int start,int duration) { return now-start>=duration; }
int Serial_IsOpen(void) { return port_open; }
int Serial_Open(unsigned char *mode) { (void)mode; opens++; port_open=1; return 0; }
int Serial_Close(int mode) { close_mode=mode; port_open=0; return 0; }
int Serial_ClearRX(void) { input_start=input_end=0; return 0; }
int Serial_ClearTX(void) { return 0; }
int Serial_PollRX(void) { return input_end-input_start; }
int Serial_PollTX(void) { return tx_free; }
int Serial_ReadSingle(unsigned char *out) { *out=incoming[input_start++]; return 0; }
int Serial_Write(const unsigned char *data,int count) {
    for(int i=0;i<count;i++) if(wire_feed(&outgoing,data[i])==1) strcpy(last_payload,outgoing.data);
    return 0;
}
void *GetVRAMAddress(void) { return pixels; }
void Bdisp_Fill_VRAM(int c,int mode) { (void)mode; for(int i=0;i<384*216;i++) pixels[i]=c; }
void Bdisp_PutDisp_DD(void) {}
void CMT_Delay_100micros(int n) { now+=n/10; }
void EnableStatusArea(int value) { (void)value; }
void Bkey_SetAllFlags(short flags) { (void)flags; }
int PRGM_GetKey(void) { return 0; }
int Timer_Install(int id,void (*cb)(void),int interval) { (void)id;(void)interval; menu_callback=cb;return 1; }
int Timer_Start(int id) { return id; }
int Timer_Stop(int id) { return id; }
int Timer_Deinstall(int id) { return id; }
int Keyboard_PutKeycode(int x,int y,int key) { assert(x==4 && y==9 && key==0); return 0; }
int GetKey(int *key) { assert(!busy && !port_open && !password[0]); menu_callback(); *key=KEY_CTRL_EXE; return 0; }

static void tick(int ms) { now+=ms; poll_transport(); }
static void send_all(void) {
    int limit=200;
    while(queued && --limit) tick(12);
    assert(limit);
}
static void bytes(const char *s) {
    while(*s) { incoming[input_end++]=(unsigned char)*s++; poll_transport(); }
    input_start=input_end=0;
}
static void response(const char *kind,unsigned long id,const char *suffix) {
    char payload[210],frame[WIRE_CAP];
    sprintf(payload,"%s:%lu%s",kind,id,suffix); assert(wire_pack(frame,payload));
    frame[strlen(frame)-1]=0; /* Deliberately lose the final newline. */
    bytes(frame+3); /* Also lose the first three preamble bytes. */
}
int main(void) {
    for(int pass=0;pass<100;pass++) {
        begin_scan(); send_all(); unsigned long old=request_id-1;
        response("READY",old,":2"); assert(busy && waiting && !expected);
        char original[WIRE_CAP]; strcpy(original,last_payload);
        for(int j=0;j<3;j++) { tick(1601); send_all(); assert(!strcmp(original,last_payload)); }
        assert(close_mode==1 && opens>1);
        response("WAIT",request_id,""); assert(busy && !waiting);
        tick(300); send_all(); response("READY",request_id,":2"); send_all();
        assert(busy && request_index==0 && waiting);
        response("ITEM",old,":0:-20:3:0:426164"); assert(received==0);
        bytes("junk@ITEM:0:0:-20:3:0:426164*00000000"); assert(received==0);
        response("ITEM",request_id,":0:-42:3:2:486F6D65"); send_all();
        assert(received==1 && request_index==1);
        response("ITEM",request_id,":0:-42:3:2:486F6D65"); assert(received==1);
        response("ITEM",request_id,":1:-70:0:0:413A422A43");
        assert(!busy && view==VIEW_LIST && network_count==2);
        assert(!strcmp(networks[0].ssid,"Home") && networks[0].flags==2);
        assert(!strcmp(networks[1].ssid,"A:B*C") && networks[1].auth==0);
    }
    selected=0; view=VIEW_PASSWORD; password_len=password_cursor=0;
    password_key(KEY_PRGM_SHIFT); password_key(KEY_PRGM_ALPHA); assert(alpha_lock);
    password_key(65); password_key(56); assert(!strcmp(password,"hc"));
    password_key(KEY_PRGM_F2); password_key(64); assert(!strcmp(password,"hcN"));
    password_key(KEY_PRGM_LEFT); password_key(44); assert(!strcmp(password,"hN"));
    password_key(KEY_PRGM_ACON); assert(!password[0]);
    password_key(KEY_PRGM_F3); symbol_cursor=2; password_key(KEY_PRGM_RETURN);
    assert(password[0]=='"' && !symbol_mode);
    password_key(KEY_PRGM_ACON);
    str_copy(password,sizeof(password),"password1");password_len=password_cursor=9;
    password_key(KEY_PRGM_RETURN); send_all(); assert(busy==2 && !password[0]);
    assert(!strncmp(last_payload,"JOIN:",5));
    response("LINK",request_id,":1::OK"); tick(300); send_all();
    assert(request_kind==REQ_JSTATE);
    response("LINK",request_id,":2:486F6D65:OK");
    assert(!busy && wifi_connected && view==VIEW_LIST && (networks[0].flags&1));
    begin_status(); send_all(); response("LINK",request_id,":0::OK");
    assert(!busy && !wifi_connected && !(networks[0].flags&1));
    begin_scan(); send_all();
    for(int i=0;i<8 && busy;i++) { tick(1601); send_all(); }
    assert(!busy && strstr(last_error,"no llegan bytes"));
    char preserved_error[sizeof(last_error)]; strcpy(preserved_error,last_error);
    begin_status(); send_all(); response("LINK",request_id,":0::OK");
    assert(view==VIEW_ERROR && !strcmp(last_error,preserved_error));
    begin_status(); send_all();
    for(int i=0;i<8 && busy;i++) { tick(1601); send_all(); }
    assert(view==VIEW_ERROR && !strcmp(last_error,preserved_error));
    begin_scan(); send_all(); unsigned long old=request_id;
    cancel_activity(); response("READY",old,":1"); assert(!busy);
    begin_scan();send_all();response("READY",old,":1");assert(busy && !expected);
    response("READY",request_id,":0");assert(!busy && network_count==0);
    begin_join();send_all();response("ERROR",request_id,":AUTH_TYPE");
    assert(view==VIEW_ERROR && !busy);
    for(int i=0;i<10;i++) {
        begin_scan(); send_all(); strcpy(password,"test-only");
        leave_to_menu(0);
        assert(close_mode==1 && !port_open && !busy && !password[0] && !menu_timer);
    }
    puts("PASS calculator: 100 scans; paced IO; missing newline; UART recovery; corrupt/stale frames; cancel; auth/connection UI state; password case/symbols/editing; 10 cleanup-to-menu cycles.");
}
